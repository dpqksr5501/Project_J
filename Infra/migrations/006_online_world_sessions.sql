BEGIN;

CREATE TABLE IF NOT EXISTS pj_world_channels (
    zone_id VARCHAR(64) NOT NULL,
    channel_id INT NOT NULL CHECK (channel_id > 0),
    incarnation UUID NOT NULL,
    endpoint VARCHAR(256) NOT NULL,
    population INT NOT NULL CHECK (population >= 0),
    capacity INT NOT NULL CHECK (capacity > 0),
    draining BOOLEAN NOT NULL,
    is_dungeon BOOLEAN NOT NULL DEFAULT FALSE,
    leased_until TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (zone_id, channel_id)
);
ALTER TABLE pj_world_channels ADD COLUMN IF NOT EXISTS is_dungeon BOOLEAN NOT NULL DEFAULT FALSE;

CREATE TABLE IF NOT EXISTS pj_world_login_tickets (
    ticket UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    character_id UUID NOT NULL REFERENCES pj_characters(character_id) ON DELETE CASCADE,
    zone_id VARCHAR(64) NOT NULL,
    channel_id INT NOT NULL,
    incarnation UUID NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    UNIQUE (character_id, zone_id, channel_id)
);

CREATE TABLE IF NOT EXISTS pj_return_ticket_consumptions (
    nonce UUID PRIMARY KEY,
    expires_at TIMESTAMPTZ NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_pj_return_ticket_expiry ON pj_return_ticket_consumptions(expires_at);

CREATE OR REPLACE FUNCTION pj_world_channel_heartbeat(p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    lease pj_world_channels%ROWTYPE;
BEGIN
    INSERT INTO pj_world_channels AS c(zone_id, channel_id, incarnation, endpoint, population, capacity, draining, is_dungeon, leased_until)
        VALUES (p_req->>'zone', (p_req->>'channel')::INT, (p_req->>'worker')::UUID, p_req->>'endpoint',
            (p_req->>'population')::INT, (p_req->>'capacity')::INT, (p_req->>'draining')::BOOLEAN,
            COALESCE((p_req->>'is_dungeon')::BOOLEAN, FALSE), clock_timestamp() + INTERVAL '30 seconds')
        ON CONFLICT (zone_id, channel_id) DO UPDATE SET incarnation = EXCLUDED.incarnation, endpoint = EXCLUDED.endpoint,
            population = EXCLUDED.population, capacity = EXCLUDED.capacity, draining = EXCLUDED.draining,
            is_dungeon = EXCLUDED.is_dungeon, leased_until = EXCLUDED.leased_until
        WHERE c.incarnation = EXCLUDED.incarnation OR c.leased_until <= clock_timestamp()
        RETURNING * INTO lease;
    RETURN jsonb_build_object('status', CASE WHEN FOUND THEN 'OK' ELSE 'CONFLICT' END);
END;
$$;

CREATE OR REPLACE FUNCTION pj_world_channel_list(p_zone TEXT)
RETURNS JSONB LANGUAGE sql STABLE AS $$
    SELECT jsonb_build_object('status', 'OK', 'channels', COALESCE(jsonb_agg(to_jsonb(c)), '[]'::JSONB)) FROM (
        SELECT channel_id, endpoint, population, capacity, draining,
            EXTRACT(EPOCH FROM (leased_until - CURRENT_TIMESTAMP))::DOUBLE PRECISION AS seconds_remaining
        FROM pj_world_channels WHERE zone_id = p_zone AND leased_until > CURRENT_TIMESTAMP AND NOT is_dungeon
        ORDER BY channel_id LIMIT 128
    ) c;
$$;

-- The caller is the authenticated Nakama player. The external principal is never taken from the body.
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
        AND leased_until > clock_timestamp() AND NOT draining AND population < capacity;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'UNAVAILABLE'); END IF;
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

CREATE OR REPLACE FUNCTION pj_online_inventory(p_character UUID, p_epoch BIGINT)
RETURNS JSONB LANGUAGE sql STABLE AS $$
    SELECT CASE WHEN NOT EXISTS (SELECT 1 FROM pj_characters WHERE character_id = p_character AND session_epoch = p_epoch AND p_epoch > 0)
        THEN jsonb_build_object('status', 'CONFLICT')
        ELSE jsonb_build_object('status', 'OK', 'inventory_revision', (SELECT inventory_revision::TEXT FROM pj_characters WHERE character_id = p_character), 'items', COALESCE((
            SELECT jsonb_agg(jsonb_build_object('id', item_instance_id, 'definition', definition_id, 'version', definition_version,
                'count', quantity, 'level', GREATEST(1, enhance_level), 'order', slot_index, 'scope', owner_scope)
                ORDER BY slot_index, item_instance_id)
            FROM pj_inventory_items WHERE owner_character_id = p_character AND owner_scope = 'CHAR_INV'
        ), '[]'::JSONB)) END;
$$;

CREATE OR REPLACE FUNCTION pj_redeem_world_login(p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    login pj_world_login_tickets%ROWTYPE;
    player pj_characters%ROWTYPE;
    return_nonce UUID := (p_req->>'return_nonce')::UUID;
    return_expires TIMESTAMPTZ := to_timestamp((p_req->>'return_expires')::DOUBLE PRECISION);
    dungeon_instance UUID := (p_req->>'dungeon_instance')::UUID;
    return_instance UUID := (p_req->>'return_instance')::UUID;
    dungeon_channel BOOLEAN;
BEGIN
    SELECT * INTO login FROM pj_world_login_tickets WHERE ticket = (p_req->>'ticket')::UUID FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT', 'reason', 'TICKET_NOT_FOUND'); END IF;
    IF login.zone_id IS DISTINCT FROM p_req->>'zone' OR login.channel_id IS DISTINCT FROM (p_req->>'channel')::INT
        OR login.incarnation IS DISTINCT FROM (p_req->>'worker')::UUID OR login.expires_at <= clock_timestamp() THEN
        RETURN jsonb_build_object('status', 'CONFLICT', 'reason', 'TICKET_OUTSIDE_LEASE');
    END IF;
    IF p_req->>'return_character' IS NOT NULL AND (p_req->>'return_character')::UUID <> login.character_id THEN
        RETURN jsonb_build_object('status', 'CONFLICT', 'reason', 'RETURN_CHARACTER_MISMATCH');
    END IF;
    SELECT c.is_dungeon INTO dungeon_channel FROM pj_world_channels c WHERE c.zone_id = login.zone_id AND c.channel_id = login.channel_id
        AND c.incarnation = login.incarnation AND c.leased_until > clock_timestamp() AND NOT c.draining;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT', 'reason', 'CHANNEL_INACTIVE'); END IF;
    SELECT c.* INTO player FROM pj_characters c JOIN pj_accounts a USING (account_id)
        WHERE c.character_id = login.character_id AND a.status = 'ACTIVE' FOR UPDATE OF c;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT', 'reason', 'ACCOUNT_INACTIVE'); END IF;
    IF dungeon_channel OR dungeon_instance IS NOT NULL THEN
        IF NOT dungeon_channel OR return_nonce IS NOT NULL THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
        UPDATE pj_dungeon_instances i SET started_at = COALESCE(started_at, clock_timestamp())
            FROM pj_dungeon_members m WHERE i.instance_id = dungeon_instance AND m.instance_id = i.instance_id
                AND m.character_id = player.character_id AND m.returned_at IS NULL AND i.worker_incarnation = login.incarnation
                AND i.state = 'READY' AND i.cleared_at IS NULL AND i.expires_at > clock_timestamp()
                AND (i.started_at IS NOT NULL OR i.join_until > clock_timestamp());
        IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT', 'reason', 'DUNGEON_ADMISSION_INACTIVE'); END IF;
        UPDATE pj_dungeon_members SET joined_at = COALESCE(joined_at, clock_timestamp())
            WHERE instance_id = dungeon_instance AND character_id = player.character_id;
    END IF;
    IF return_nonce IS NOT NULL THEN
        IF return_expires IS NULL OR return_expires <= clock_timestamp() OR return_expires > clock_timestamp() + INTERVAL '61 seconds' THEN
            RETURN jsonb_build_object('status', 'CONFLICT');
        END IF;
        IF return_instance IS NOT NULL THEN
            PERFORM 1 FROM pj_dungeon_instances i JOIN pj_dungeon_members m USING (instance_id)
                WHERE i.instance_id = return_instance AND i.cleared_at IS NOT NULL AND m.joined_at IS NOT NULL
                AND m.returned_at IS NULL AND m.character_id = player.character_id FOR UPDATE OF i;
            IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT', 'reason', 'DUNGEON_NOT_CLEARED'); END IF;
        END IF;
        INSERT INTO pj_return_ticket_consumptions(nonce, expires_at) VALUES (return_nonce, return_expires) ON CONFLICT DO NOTHING;
        IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
        IF return_instance IS NOT NULL THEN
            UPDATE pj_dungeon_members SET returned_at = COALESCE(returned_at, clock_timestamp())
                WHERE instance_id = return_instance AND character_id = player.character_id;
            UPDATE pj_dungeon_instances SET state = 'COMPLETE'
                WHERE instance_id = return_instance AND state = 'READY'
                AND NOT EXISTS (SELECT 1 FROM pj_dungeon_members WHERE instance_id = return_instance AND joined_at IS NOT NULL AND returned_at IS NULL);
        END IF;
        DELETE FROM pj_return_ticket_consumptions WHERE nonce IN (
            SELECT nonce FROM pj_return_ticket_consumptions WHERE expires_at <= clock_timestamp() ORDER BY expires_at LIMIT 64
        );
    END IF;
    DELETE FROM pj_world_login_tickets WHERE ticket = login.ticket;
    UPDATE pj_characters SET session_epoch = session_epoch + 1, updated_at = NOW()
        WHERE character_id = player.character_id RETURNING * INTO player;
    RETURN pj_online_inventory(player.character_id, player.session_epoch) || jsonb_build_object('status', 'OK',
        'character', player.character_id, 'account', player.account_id, 'epoch', player.session_epoch::TEXT,
        'class', player.class_id, 'level', player.level, 'xp', player.xp::TEXT);
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (6, 'Shared world channel leases, authenticated logins and durable return consumption') ON CONFLICT (version) DO NOTHING;
COMMIT;
