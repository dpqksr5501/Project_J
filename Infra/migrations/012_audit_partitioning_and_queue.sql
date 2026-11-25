BEGIN;

-- 1. Append-only Economy Audit Stream (dupe detection & rollback evidence)
CREATE TABLE IF NOT EXISTS pj_economy_audit_log (
    audit_id BIGSERIAL PRIMARY KEY,
    occurred_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    character_id UUID NOT NULL,
    operation TEXT NOT NULL,
    idempotency_key UUID,
    gold_delta BIGINT NOT NULL DEFAULT 0,
    details_json JSONB NOT NULL DEFAULT '{}'::JSONB
);
CREATE INDEX IF NOT EXISTS idx_pj_economy_audit_char_time ON pj_economy_audit_log(character_id, occurred_at DESC);
CREATE INDEX IF NOT EXISTS idx_pj_economy_audit_time ON pj_economy_audit_log(occurred_at DESC);

CREATE OR REPLACE FUNCTION pj_record_economy_audit(
    p_character_id UUID, p_operation TEXT, p_key UUID, p_gold_delta BIGINT, p_details JSONB)
RETURNS BIGINT LANGUAGE plpgsql AS $$
DECLARE
    new_id BIGINT;
BEGIN
    IF p_character_id IS NULL OR COALESCE(length(p_operation), 0) NOT BETWEEN 1 AND 64 THEN
        RAISE EXCEPTION 'Invalid economy audit payload';
    END IF;
    INSERT INTO pj_economy_audit_log(character_id, operation, idempotency_key, gold_delta, details_json)
    VALUES (p_character_id, p_operation, p_key, COALESCE(p_gold_delta, 0), COALESCE(p_details, '{}'::JSONB))
    RETURNING audit_id INTO new_id;
    RETURN new_id;
END;
$$;

-- 2. Server-Authoritative Anti-Cheat / Security Violation Audit Table
CREATE TABLE IF NOT EXISTS pj_security_audit_events (
    event_id BIGSERIAL PRIMARY KEY,
    occurred_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    character_id UUID NOT NULL,
    violation_code TEXT NOT NULL,
    severity INT NOT NULL CHECK (severity BETWEEN 1 AND 3),
    zone_id TEXT NOT NULL DEFAULT '',
    details_json JSONB NOT NULL DEFAULT '{}'::JSONB
);
CREATE INDEX IF NOT EXISTS idx_pj_security_audit_char_time ON pj_security_audit_events(character_id, occurred_at DESC);

CREATE OR REPLACE FUNCTION pj_record_security_violation(
    p_character_id UUID, p_code TEXT, p_severity INT, p_zone TEXT, p_details JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    new_id BIGINT;
BEGIN
    IF p_character_id IS NULL OR COALESCE(length(p_code), 0) NOT BETWEEN 1 AND 64
        OR p_severity IS NULL OR p_severity NOT BETWEEN 1 AND 3 THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    INSERT INTO pj_security_audit_events(character_id, violation_code, severity, zone_id, details_json)
    VALUES (p_character_id, p_code, p_severity, COALESCE(p_zone, ''), COALESCE(p_details, '{}'::JSONB))
    RETURNING event_id INTO new_id;
    RETURN jsonb_build_object('status', 'OK', 'event_id', new_id::TEXT);
END;
$$;

-- This migration uses indexed open tables, with no declarative partitioning.
CREATE TABLE IF NOT EXISTS pj_world_login_queue (
    queue_id BIGSERIAL PRIMARY KEY,
    account_id UUID NOT NULL REFERENCES pj_accounts(account_id) ON DELETE CASCADE,
    character_id UUID NOT NULL REFERENCES pj_characters(character_id) ON DELETE CASCADE,
    zone_id TEXT NOT NULL,
    enqueued_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '5 minutes'),
    UNIQUE (zone_id, character_id)
);
CREATE INDEX IF NOT EXISTS idx_pj_login_queue_zone_seq ON pj_world_login_queue(zone_id, queue_id);
CREATE INDEX IF NOT EXISTS idx_pj_login_queue_expiry ON pj_world_login_queue(expires_at);
CREATE INDEX IF NOT EXISTS idx_pj_login_tickets_channel_live
    ON pj_world_login_tickets(zone_id, channel_id, incarnation, expires_at);
CREATE INDEX IF NOT EXISTS idx_pj_receipts_retention ON pj_idempotency_receipts(created_at, idempotency_key) INCLUDE (expires_at);

CREATE OR REPLACE FUNCTION pj_issue_world_login(p_external TEXT, p_character UUID, p_zone TEXT, p_channel INT)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    channel pj_world_channels%ROWTYPE;
    login pj_world_login_tickets%ROWTYPE;
BEGIN
    PERFORM 1 FROM pj_characters c JOIN pj_accounts a USING (account_id)
        WHERE c.character_id = p_character AND a.external_sub = p_external AND a.status = 'ACTIVE';
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    SELECT * INTO channel FROM pj_world_channels WHERE zone_id = p_zone AND channel_id = p_channel
        AND leased_until > clock_timestamp() AND NOT draining AND population < capacity FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'UNAVAILABLE'); END IF;
    -- Reserve in the same transaction as issuing the ticket. Count all other
    -- live reservations for this worker, while allowing same-character refresh.
    IF channel.population + (SELECT count(*) FROM pj_world_login_tickets t
        WHERE t.zone_id = p_zone AND t.channel_id = p_channel AND t.incarnation = channel.incarnation
            AND t.expires_at > clock_timestamp() AND t.character_id <> p_character) >= channel.capacity
    THEN RETURN jsonb_build_object('status', 'UNAVAILABLE'); END IF;
    IF channel.is_dungeon AND NOT EXISTS (
        SELECT 1 FROM pj_dungeon_instances i JOIN pj_dungeon_members m USING (instance_id)
        WHERE i.worker_incarnation = channel.incarnation AND i.state = 'READY' AND i.cleared_at IS NULL AND i.expires_at > clock_timestamp()
            AND (i.started_at IS NOT NULL OR i.join_until > clock_timestamp()) AND m.character_id = p_character AND m.returned_at IS NULL
    ) THEN RETURN jsonb_build_object('status', 'UNAVAILABLE'); END IF;
    INSERT INTO pj_world_login_tickets(character_id, zone_id, channel_id, incarnation, expires_at)
        VALUES (p_character, p_zone, p_channel, channel.incarnation, clock_timestamp() + INTERVAL '45 seconds')
        ON CONFLICT (character_id, zone_id, channel_id) DO UPDATE SET ticket = gen_random_uuid(),
            incarnation = EXCLUDED.incarnation, expires_at = EXCLUDED.expires_at
        RETURNING * INTO login;
    RETURN jsonb_build_object('status', 'OK', 'ticket', login.ticket, 'endpoint', channel.endpoint);
END;
$$;

-- Queue and channel selection are serialized per zone. The existing login
-- ticket contract also serializes direct and routed issuers per channel.
CREATE OR REPLACE FUNCTION pj_route_or_enqueue_channel(
    p_account_id UUID, p_character_id UUID, p_zone TEXT, p_preferred_channel INT)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    principal TEXT;
    candidate RECORD;
    issued JSONB;
    q_id BIGINT;
    q_pos BIGINT;
BEGIN
    IF p_account_id IS NULL OR p_character_id IS NULL OR COALESCE(length(p_zone), 0) NOT BETWEEN 1 AND 64 THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    SELECT a.external_sub INTO principal FROM pj_characters c JOIN pj_accounts a USING (account_id)
        WHERE c.character_id = p_character_id AND c.account_id = p_account_id AND a.status = 'ACTIVE';
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;

    PERFORM pg_advisory_xact_lock(hashtextextended('pj_world_login_queue:' || p_zone, 0));
    SELECT queue_id INTO q_id FROM pj_world_login_queue
        WHERE zone_id = p_zone AND character_id = p_character_id AND expires_at > clock_timestamp();
    IF NOT EXISTS (SELECT 1 FROM pj_world_login_queue
        WHERE zone_id = p_zone AND expires_at > clock_timestamp() AND (q_id IS NULL OR queue_id < q_id))
    THEN
        FOR candidate IN
            SELECT c.channel_id, c.population, c.capacity FROM pj_world_channels c
            CROSS JOIN LATERAL (
                SELECT c.population + count(*) AS occupied FROM pj_world_login_tickets t
                WHERE t.zone_id = c.zone_id AND t.channel_id = c.channel_id AND t.incarnation = c.incarnation
                    AND t.expires_at > clock_timestamp() AND t.character_id <> p_character_id
            ) slots
            WHERE c.zone_id = p_zone AND NOT c.is_dungeon AND NOT c.draining
                AND c.leased_until > clock_timestamp() AND slots.occupied < c.capacity
            ORDER BY CASE WHEN c.channel_id = p_preferred_channel AND slots.occupied * 100 < c.capacity::BIGINT * 85 THEN 0 ELSE 1 END,
                slots.occupied::DOUBLE PRECISION / c.capacity, c.channel_id
            LIMIT 1
        LOOP
            issued := pj_issue_world_login(principal, p_character_id, p_zone, candidate.channel_id);
            IF issued->>'status' = 'OK' THEN
                DELETE FROM pj_world_login_queue WHERE zone_id = p_zone AND character_id = p_character_id;
                RETURN issued || jsonb_build_object('status','ROUTED','zone',p_zone,'channel',candidate.channel_id,
                    'population',candidate.population,'capacity',candidate.capacity);
            END IF;
            -- Avoid holding one channel lock while acquiring another. The
            -- following poll can re-rank the channels once reservations change.
            EXIT;
        END LOOP;
    END IF;

    WITH expired AS (
        SELECT queue_id FROM pj_world_login_queue WHERE expires_at <= clock_timestamp()
        ORDER BY expires_at LIMIT 64 FOR UPDATE SKIP LOCKED
    )
    DELETE FROM pj_world_login_queue q USING expired e WHERE q.queue_id = e.queue_id;
    IF q_id IS NULL THEN
        IF (SELECT count(*) FROM pj_world_login_queue WHERE zone_id = p_zone AND expires_at > clock_timestamp()) >= 4096
        THEN RETURN jsonb_build_object('status','UNAVAILABLE'); END IF;
        DELETE FROM pj_world_login_queue WHERE zone_id = p_zone AND character_id = p_character_id
            AND expires_at <= clock_timestamp();
        INSERT INTO pj_world_login_queue(account_id, character_id, zone_id)
            VALUES(p_account_id,p_character_id,p_zone) RETURNING queue_id INTO q_id;
    END IF;
    SELECT count(*) INTO q_pos FROM pj_world_login_queue
        WHERE zone_id = p_zone AND queue_id <= q_id AND expires_at > clock_timestamp();
    RETURN jsonb_build_object('status','QUEUED','zone',p_zone,'position',q_pos,'retry_after_sec',3);
END;
$$;

CREATE OR REPLACE FUNCTION pj_prune_expired_receipts(p_retention_days INT DEFAULT 7, p_batch_limit INT DEFAULT 10000)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    deleted_count INT := 0;
    safe_days INT := LEAST(3650, GREATEST(7, COALESCE(p_retention_days, 7)));
    safe_limit INT := LEAST(50000, GREATEST(100, COALESCE(p_batch_limit, 10000)));
BEGIN
    WITH doomed AS (
        SELECT idempotency_key FROM pj_idempotency_receipts
        WHERE created_at < clock_timestamp() - make_interval(days => safe_days) AND expires_at <= clock_timestamp()
        ORDER BY created_at, idempotency_key
        LIMIT safe_limit FOR UPDATE SKIP LOCKED
    )
    DELETE FROM pj_idempotency_receipts r USING doomed d WHERE r.idempotency_key = d.idempotency_key;
    GET DIAGNOSTICS deleted_count = ROW_COUNT;
    RETURN jsonb_build_object('status','OK','deleted',deleted_count,'retention_days',safe_days);
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (12, 'Economy & security audit trails, channel overflow routing/login queue, and bounded receipt pruning')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
