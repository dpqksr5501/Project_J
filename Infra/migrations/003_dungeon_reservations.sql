BEGIN;

CREATE TABLE IF NOT EXISTS pj_dungeon_workers (
    endpoint VARCHAR(256) PRIMARY KEY,
    incarnation UUID NOT NULL UNIQUE,
    dungeon_id VARCHAR(64) NOT NULL,
    leased_until TIMESTAMPTZ NOT NULL
);
ALTER TABLE pj_dungeon_instances ADD COLUMN IF NOT EXISTS worker_incarnation UUID;
ALTER TABLE pj_dungeon_instances ADD COLUMN IF NOT EXISTS allocation_intent JSONB;
ALTER TABLE pj_dungeon_instances ADD COLUMN IF NOT EXISTS ready_at TIMESTAMPTZ;
ALTER TABLE pj_dungeon_instances ADD COLUMN IF NOT EXISTS join_until TIMESTAMPTZ;
ALTER TABLE pj_dungeon_instances ADD COLUMN IF NOT EXISTS started_at TIMESTAMPTZ;
ALTER TABLE pj_dungeon_instances ADD COLUMN IF NOT EXISTS cleared_at TIMESTAMPTZ;
CREATE INDEX IF NOT EXISTS idx_pj_dungeon_worker_instances ON pj_dungeon_instances(worker_incarnation, expires_at);

CREATE TABLE IF NOT EXISTS pj_dungeon_members (
    instance_id UUID NOT NULL REFERENCES pj_dungeon_instances(instance_id) ON DELETE CASCADE,
    character_id UUID NOT NULL REFERENCES pj_characters(character_id),
    return_anchor JSONB NOT NULL,
    returned_at TIMESTAMPTZ,
    PRIMARY KEY (instance_id, character_id)
);
ALTER TABLE pj_dungeon_members ADD COLUMN IF NOT EXISTS returned_at TIMESTAMPTZ;
ALTER TABLE pj_dungeon_members ADD COLUMN IF NOT EXISTS joined_at TIMESTAMPTZ;
CREATE INDEX IF NOT EXISTS idx_pj_dungeon_member_character ON pj_dungeon_members(character_id, instance_id);

CREATE OR REPLACE FUNCTION pj_dungeon_heartbeat(p_worker UUID, p_endpoint TEXT, p_dungeon TEXT)
RETURNS BOOLEAN LANGUAGE plpgsql AS $$
BEGIN
    IF p_worker IS NULL OR p_endpoint IS NULL OR p_dungeon IS NULL OR length(p_dungeon) NOT BETWEEN 1 AND 64
        OR length(p_endpoint) > 128 OR p_endpoint !~ '^[a-zA-Z0-9.-]+:[0-9]{1,5}$' THEN RETURN FALSE; END IF;
    IF split_part(p_endpoint, ':', 2)::INT NOT BETWEEN 1 AND 65535 THEN RETURN FALSE; END IF;
    INSERT INTO pj_dungeon_workers(endpoint, incarnation, dungeon_id, leased_until)
        VALUES (p_endpoint, p_worker, p_dungeon, clock_timestamp() + INTERVAL '15 seconds')
    ON CONFLICT (endpoint) DO UPDATE SET incarnation = EXCLUDED.incarnation,
        dungeon_id = EXCLUDED.dungeon_id, leased_until = EXCLUDED.leased_until
    WHERE (pj_dungeon_workers.incarnation = p_worker AND pj_dungeon_workers.dungeon_id = p_dungeon)
        OR (pj_dungeon_workers.incarnation <> p_worker AND pj_dungeon_workers.leased_until <= clock_timestamp());
    RETURN FOUND;
END;
$$;

CREATE OR REPLACE FUNCTION pj_dungeon_status(p_instance UUID)
RETURNS JSONB LANGUAGE sql AS $$
    SELECT COALESCE((
        SELECT jsonb_build_object('status', i.state, 'instance_id', i.instance_id,
            'server_endpoint', CASE WHEN i.state = 'READY' THEN i.server_endpoint END,
            'expires_at', extract(epoch FROM i.expires_at)::DOUBLE PRECISION)
        FROM pj_dungeon_instances i JOIN pj_dungeon_workers w
            ON w.incarnation = i.worker_incarnation AND w.endpoint = i.server_endpoint
        WHERE i.instance_id = p_instance AND i.expires_at > clock_timestamp() AND i.cleared_at IS NULL
            AND (i.started_at IS NOT NULL OR i.join_until IS NULL OR i.join_until > clock_timestamp())
            AND w.leased_until > clock_timestamp() AND i.state IN ('RESERVED', 'READY')
    ), jsonb_build_object('status', 'UNAVAILABLE'));
$$;

CREATE OR REPLACE FUNCTION pj_dungeon_allocate(p_instance UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    party UUID[];
    recipient UUID;
    anchor JSONB;
    existing_intent JSONB;
    worker pj_dungeon_workers%ROWTYPE;
    dungeon TEXT := p_req->>'dungeon_id';
    lifetime INT := (p_req->>'lifetime_seconds')::INT;
    requester UUID := (p_req->>'requester_char_id')::UUID;
BEGIN
    IF p_instance IS NULL OR dungeon IS NULL OR length(dungeon) NOT BETWEEN 1 AND 64
        OR lifetime IS NULL OR lifetime NOT BETWEEN 60 AND 14400
        OR jsonb_typeof(p_req->'members') IS DISTINCT FROM 'object' THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    SELECT array_agg(key::UUID ORDER BY key::UUID) INTO party FROM jsonb_each(p_req->'members');
    IF party IS NULL OR cardinality(party) > 40 OR requester IS NULL OR NOT requester = ANY(party)
        OR cardinality(party) <> (SELECT count(DISTINCT id) FROM unnest(party) AS t(id)) THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    FOR recipient IN SELECT unnest(party) LOOP
        anchor := (p_req->'members')->recipient::TEXT;
        IF jsonb_typeof(anchor) IS DISTINCT FROM 'object'
            OR COALESCE(length(anchor->>'zone_id'), 0) NOT BETWEEN 1 AND 64
            OR (anchor->>'channel_id')::INT IS NULL OR (anchor->>'channel_id')::INT <= 0
            OR jsonb_typeof(anchor->'location') IS DISTINCT FROM 'array'
            OR jsonb_typeof(anchor->'rotation') IS DISTINCT FROM 'array' THEN
            RETURN jsonb_build_object('status', 'INVALID');
        END IF;
        IF jsonb_array_length(anchor->'location') <> 3 OR jsonb_array_length(anchor->'rotation') <> 3
            OR EXISTS (SELECT 1 FROM jsonb_array_elements((anchor->'location') || (anchor->'rotation')) AS v(value)
                WHERE jsonb_typeof(value) <> 'number' OR abs(value::NUMERIC) > 1000000000) THEN
            RETURN jsonb_build_object('status', 'INVALID');
        END IF;
    END LOOP;

    PERFORM pg_advisory_xact_lock(hashtextextended(p_instance::TEXT, 1));
    SELECT allocation_intent INTO existing_intent FROM pj_dungeon_instances WHERE instance_id = p_instance;
    IF FOUND THEN
        IF existing_intent IS DISTINCT FROM p_req THEN RETURN jsonb_build_object('status', 'KEY_REUSED'); END IF;
        RETURN pj_dungeon_status(p_instance);
    END IF;
    -- Every reservation claiming a character passes the same ordered row locks.
    PERFORM 1 FROM pj_characters WHERE character_id = ANY(party) ORDER BY character_id FOR UPDATE;
    IF (SELECT count(*) FROM pj_characters WHERE character_id = ANY(party)) <> cardinality(party)
        OR NOT EXISTS (SELECT 1 FROM pj_characters WHERE character_id = requester AND session_epoch > 0
            AND session_epoch = (p_req->>'session_epoch')::BIGINT)
        OR EXISTS (SELECT 1 FROM jsonb_each(p_req->'members') AS m(id, anchor_value)
            JOIN pj_characters c ON c.character_id = m.id::UUID
            WHERE m.anchor_value ? 'session_epoch' AND (c.session_epoch <= 0
                OR c.session_epoch IS DISTINCT FROM (m.anchor_value->>'session_epoch')::BIGINT))
        OR EXISTS (SELECT 1 FROM pj_dungeon_members m JOIN pj_dungeon_instances i USING (instance_id)
            JOIN pj_dungeon_workers w ON w.incarnation = i.worker_incarnation
            WHERE m.character_id = ANY(party) AND i.expires_at > clock_timestamp()
            AND (i.started_at IS NOT NULL OR i.join_until IS NULL OR i.join_until > clock_timestamp())
            AND m.returned_at IS NULL AND w.leased_until > clock_timestamp() AND i.state IN ('RESERVED', 'READY')) THEN
        RETURN jsonb_build_object('status', 'CONFLICT');
    END IF;
    SELECT * INTO worker FROM pj_dungeon_workers w
    WHERE w.dungeon_id = dungeon AND w.leased_until > clock_timestamp()
        AND NOT EXISTS (SELECT 1 FROM pj_dungeon_instances i WHERE i.worker_incarnation = w.incarnation
            AND (i.started_at IS NOT NULL OR (i.expires_at > clock_timestamp() AND i.state IN ('RESERVED', 'READY')
                AND (i.join_until IS NULL OR i.join_until > clock_timestamp()))))
    ORDER BY w.endpoint LIMIT 1 FOR UPDATE SKIP LOCKED;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'UNAVAILABLE'); END IF;
    -- A competing transaction may have committed after the selection snapshot.
    IF EXISTS (SELECT 1 FROM pj_dungeon_instances WHERE worker_incarnation = worker.incarnation
        AND (started_at IS NOT NULL OR (expires_at > clock_timestamp() AND state IN ('RESERVED', 'READY')
            AND (join_until IS NULL OR join_until > clock_timestamp())))) THEN
        RETURN jsonb_build_object('status', 'UNAVAILABLE');
    END IF;
    INSERT INTO pj_dungeon_instances(instance_id, dungeon_id, server_endpoint, state, allowed_chars,
        expires_at, worker_incarnation, allocation_intent, join_until)
    VALUES (p_instance, dungeon, worker.endpoint, 'RESERVED', to_jsonb(party),
        clock_timestamp() + make_interval(secs => lifetime), worker.incarnation, p_req, clock_timestamp() + INTERVAL '45 seconds');
    INSERT INTO pj_dungeon_members(instance_id, character_id, return_anchor)
        SELECT p_instance, key::UUID, value FROM jsonb_each(p_req->'members');
    RETURN pj_dungeon_status(p_instance);
END;
$$;

CREATE OR REPLACE FUNCTION pj_dungeon_worker_admission(p_worker UUID)
RETURNS JSONB LANGUAGE sql AS $$
    SELECT COALESCE((
        SELECT jsonb_build_object('status', i.state, 'instance_id', i.instance_id, 'dungeon_id', i.dungeon_id,
            'server_endpoint', i.server_endpoint, 'members', i.allocation_intent->'members',
            'expires_at', extract(epoch FROM i.expires_at)::DOUBLE PRECISION)
        FROM pj_dungeon_workers w JOIN pj_dungeon_instances i ON i.worker_incarnation = w.incarnation
        WHERE w.incarnation = p_worker AND w.leased_until > clock_timestamp()
            AND (i.started_at IS NOT NULL OR i.join_until IS NULL OR i.join_until > clock_timestamp())
            AND i.expires_at > clock_timestamp() AND i.state IN ('RESERVED', 'READY')
        ORDER BY i.created_at LIMIT 1
    ), jsonb_build_object('status', 'UNAVAILABLE'));
$$;

CREATE OR REPLACE FUNCTION pj_dungeon_acknowledge(p_worker UUID, p_instance UUID)
RETURNS JSONB LANGUAGE plpgsql AS $$
BEGIN
    PERFORM 1 FROM pj_dungeon_workers WHERE incarnation = p_worker AND leased_until > clock_timestamp() FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'UNAVAILABLE'); END IF;
    UPDATE pj_dungeon_instances SET state = 'READY', ready_at = COALESCE(ready_at, clock_timestamp())
        WHERE instance_id = p_instance AND worker_incarnation = p_worker
            AND (started_at IS NOT NULL OR join_until IS NULL OR join_until > clock_timestamp())
            AND expires_at > clock_timestamp() AND state IN ('RESERVED', 'READY');
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'UNAVAILABLE'); END IF;
    RETURN pj_dungeon_status(p_instance);
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (3, 'Dungeon worker reservation, destination acknowledgement and durable return anchors') ON CONFLICT (version) DO NOTHING;
COMMIT;
