BEGIN;

-- A heartbeat sent before drain can arrive after the drain announcement.
-- Once this incarnation starts draining only a new owner may reopen the channel.
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
            population = EXCLUDED.population, capacity = EXCLUDED.capacity,
            draining = EXCLUDED.draining OR (c.incarnation = EXCLUDED.incarnation AND c.draining),
            is_dungeon = EXCLUDED.is_dungeon, leased_until = EXCLUDED.leased_until
        WHERE c.incarnation = EXCLUDED.incarnation OR c.leased_until <= clock_timestamp()
        RETURNING * INTO lease;
    RETURN jsonb_build_object('status', CASE WHEN FOUND THEN 'OK' ELSE 'CONFLICT' END);
END;
$$;

-- Player RPCs derive the principal from Nakama. A poll or cancellation can only
-- operate on the specific FIFO slot returned to that player's earlier request.
CREATE OR REPLACE FUNCTION pj_player_channel_route(
    p_external text, p_character uuid, p_zone text, p_channel int, p_slot bigint, p_cancel boolean)
RETURNS jsonb LANGUAGE plpgsql AS $$
DECLARE
    owner_id uuid;
    reply jsonb;
    slot bigint;
BEGIN
    SELECT a.account_id INTO owner_id FROM pj_accounts a JOIN pj_characters c USING (account_id)
        WHERE a.external_sub = p_external AND a.status = 'ACTIVE' AND c.character_id = p_character;
    IF NOT FOUND THEN RETURN jsonb_build_object('status','CONFLICT'); END IF;
    IF p_zone IS NULL OR length(p_zone) NOT BETWEEN 1 AND 64 OR p_channel IS NULL OR p_channel < 1 THEN
        RETURN jsonb_build_object('status','INVALID');
    END IF;
    PERFORM pg_advisory_xact_lock(hashtextextended('pj_world_login_queue:' || p_zone, 0));
    IF p_slot IS NOT NULL OR p_cancel THEN
        PERFORM 1 FROM pj_world_login_queue WHERE zone_id = p_zone AND character_id = p_character
            AND queue_id = p_slot AND expires_at > clock_timestamp() FOR UPDATE;
        IF NOT FOUND THEN RETURN jsonb_build_object('status','EXPIRED'); END IF;
        IF p_cancel THEN
            DELETE FROM pj_world_login_queue WHERE queue_id = p_slot;
            RETURN jsonb_build_object('status','CANCELLED');
        END IF;
    END IF;
    reply := pj_route_or_enqueue_channel(owner_id, p_character, p_zone, p_channel);
    IF reply->>'status' = 'QUEUED' THEN
        UPDATE pj_world_login_queue SET expires_at = clock_timestamp() + interval '30 seconds'
            WHERE zone_id = p_zone AND character_id = p_character RETURNING queue_id INTO slot;
        reply := reply || jsonb_build_object('slot', slot::text);
    END IF;
    RETURN reply;
END;
$$;
-- Exact-endpoint travel also shares the world queue. A verified dungeon member may
-- return to their saved anchor, with the usual capacity and redemption checks intact.
CREATE OR REPLACE FUNCTION pj_issue_player_exact_login(p_external text, p_character uuid, p_zone text, p_channel int)
RETURNS jsonb LANGUAGE plpgsql AS $$
DECLARE
    head_character uuid;
    issued jsonb;
BEGIN
    PERFORM 1 FROM pj_accounts a JOIN pj_characters c USING (account_id)
        WHERE a.external_sub=p_external AND a.status='ACTIVE' AND c.character_id=p_character;
    IF NOT FOUND THEN RETURN jsonb_build_object('status','CONFLICT'); END IF;
    PERFORM pg_advisory_xact_lock(hashtextextended('pj_world_login_queue:' || p_zone, 0));
    SELECT character_id INTO head_character FROM pj_world_login_queue
        WHERE zone_id=p_zone AND expires_at>clock_timestamp() ORDER BY queue_id LIMIT 1;
    IF FOUND AND head_character <> p_character AND NOT EXISTS (
        SELECT 1 FROM pj_world_channels WHERE zone_id=p_zone AND channel_id=p_channel AND is_dungeon
    ) AND NOT EXISTS (
        SELECT 1 FROM pj_dungeon_members m JOIN pj_dungeon_instances i USING (instance_id)
        WHERE m.character_id=p_character AND m.joined_at IS NOT NULL AND m.returned_at IS NULL
            AND i.cleared_at IS NOT NULL AND m.return_anchor->>'zone_id'=p_zone
            AND m.return_anchor->>'channel_id'=p_channel::text
    ) THEN RETURN jsonb_build_object('status','UNAVAILABLE'); END IF;
    issued := pj_issue_world_login(p_external,p_character,p_zone,p_channel);
    IF issued->>'status'='OK' THEN
        DELETE FROM pj_world_login_queue WHERE zone_id=p_zone AND character_id=p_character;
    END IF;
    RETURN issued;
END;
$$;
INSERT INTO pj_schema_migrations(version, description) VALUES (14, 'Authenticated renewable player channel queue')
    ON CONFLICT (version) DO NOTHING;
COMMIT;
