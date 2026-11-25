BEGIN;

-- The caller already owns the character row lock. Authored targets are frozen when the quest is accepted.
CREATE OR REPLACE FUNCTION pj_credit_online_quests(p_character UUID, p_event TEXT, p_recipe TEXT, p_version INT, p_count INT, p_gather TEXT DEFAULT NULL)
RETURNS VOID LANGUAGE plpgsql AS $$
DECLARE
    row pj_quest_states%ROWTYPE;
    updated JSONB;
    changed BOOLEAN := FALSE;
BEGIN
    IF p_count IS NULL OR p_count <= 0 OR p_count > 1000000 THEN RETURN; END IF;
    FOR row IN SELECT * FROM pj_quest_states WHERE character_id = p_character AND state = 'ACTIVE'
        AND jsonb_typeof(progress_json->'objectives') = 'array'
    LOOP
        updated := row.progress_json;
        IF p_event IS NOT NULL AND lower(updated->>'event') = lower(p_event) THEN
            updated := jsonb_set(updated, '{count}', to_jsonb(LEAST(
                (updated->>'required')::BIGINT, (updated->>'count')::BIGINT + p_count)));
        END IF;
        updated := jsonb_set(updated, '{objectives}', COALESCE((
            SELECT jsonb_agg(CASE WHEN
                (p_event IS NOT NULL AND lower(value->>'event') = lower(p_event))
                OR (p_recipe IS NOT NULL AND lower(value->>'recipe') = lower(p_recipe) AND (value->>'version')::INT = p_version)
                OR (p_gather IS NOT NULL AND lower(value->>'gather') = lower(p_gather) AND (value->>'version')::INT = p_version)
                THEN jsonb_set(value, '{count}', to_jsonb(LEAST((value->>'required')::BIGINT,
                    (value->>'count')::BIGINT + p_count))) ELSE value END ORDER BY ord)
            FROM jsonb_array_elements(updated->'objectives') WITH ORDINALITY AS values(value, ord)
        ), '[]'::JSONB));
        IF updated IS DISTINCT FROM row.progress_json THEN
            UPDATE pj_quest_states SET progress_json = updated, revision = revision + 1, updated_at = NOW()
                WHERE character_id = p_character AND quest_id = row.quest_id;
            changed := TRUE;
        END IF;
    END LOOP;
    IF changed THEN UPDATE pj_characters SET quest_revision = quest_revision + 1 WHERE character_id = p_character; END IF;
END;
$$;

CREATE OR REPLACE FUNCTION pj_online_quest_edit(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    actor UUID := (p_req->>'character_id')::UUID;
    epoch BIGINT := (p_req->>'session_epoch')::BIGINT;
    kind TEXT := p_req->>'kind';
    quest TEXT := p_req->>'quest';
    row JSONB := p_req->'row';
    progress_row JSONB;
    group_row JSONB;
    patch JSONB;
    goals JSONB;
    amount INT;
    prior JSONB;
    outcome CONSTANT JSONB := '{"status":"COMMITTED"}';
BEGIN
    IF p_key IS NULL OR kind IS NULL OR kind NOT IN ('accept','track','progress')
        OR (kind <> 'progress' AND COALESCE(length(quest), 0) NOT BETWEEN 1 AND 64) THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    prior := pj_replay_command(p_key, 'online_quest_edit', p_req);
    PERFORM 1 FROM pj_characters WHERE character_id = actor AND session_epoch = epoch AND epoch > 0 FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    IF prior IS NOT NULL THEN RETURN pj_online_inventory(actor, epoch) || prior; END IF;
    IF kind = 'progress' THEN
        IF jsonb_typeof(p_req->'changes') IS DISTINCT FROM 'array' OR jsonb_array_length(p_req->'changes') NOT BETWEEN 1 AND 128
            THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
        FOR group_row IN SELECT value FROM jsonb_array_elements(p_req->'changes')
        LOOP
            row := group_row->'row';
            quest := row->>'id';
            IF COALESCE(length(quest), 0) NOT BETWEEN 1 AND 64 OR jsonb_typeof(row->'objectives') IS DISTINCT FROM 'array'
                OR jsonb_typeof(group_row->'deltas') IS DISTINCT FROM 'array' OR jsonb_array_length(group_row->'deltas') NOT BETWEEN 1 AND 8
                THEN RAISE check_violation; END IF;
            SELECT progress_json INTO progress_row FROM pj_quest_states WHERE character_id = actor AND quest_id = quest AND state = 'ACTIVE';
            IF NOT FOUND THEN
                IF (SELECT count(*) FROM pj_quest_states WHERE character_id = actor) >= 128 THEN RAISE check_violation; END IF;
                INSERT INTO pj_quest_states(character_id, quest_id, state, progress_json) VALUES (actor, quest, 'ACTIVE', row);
                progress_row := row;
            ELSIF progress_row->'definition_version' IS DISTINCT FROM row->'definition_version' THEN RAISE check_violation;
            END IF;
            goals := progress_row->'objectives';
            FOR patch IN SELECT value FROM jsonb_array_elements(group_row->'deltas')
            LOOP
                amount := (patch->>'amount')::INT;
                IF amount IS NULL OR amount NOT BETWEEN 1 AND 1000000 THEN RAISE check_violation; END IF;
                IF patch->>'id' = '' THEN
                    IF jsonb_array_length(goals) <> 0 OR NOT (progress_row ? 'required') THEN RAISE check_violation; END IF;
                    progress_row := jsonb_set(progress_row, '{count}', to_jsonb(LEAST(
                        (progress_row->>'required')::BIGINT, (progress_row->>'count')::BIGINT + amount)));
                ELSE
                    IF NOT EXISTS (SELECT 1 FROM jsonb_array_elements(goals) WHERE value->>'id' = patch->>'id')
                        THEN RAISE check_violation; END IF;
                    SELECT jsonb_agg(CASE WHEN value->>'id' = patch->>'id' THEN jsonb_set(value, '{count}',
                        to_jsonb(LEAST((value->>'required')::BIGINT, (value->>'count')::BIGINT + amount))) ELSE value END ORDER BY idx)
                        INTO goals FROM jsonb_array_elements(goals) WITH ORDINALITY AS deltas(value, idx);
                END IF;
            END LOOP;
            UPDATE pj_quest_states SET progress_json = jsonb_set(progress_row, '{objectives}', goals), revision = revision + 1
                WHERE character_id = actor AND quest_id = quest AND state = 'ACTIVE';
        END LOOP;
    ELSIF kind = 'accept' THEN
        IF jsonb_typeof(row) IS DISTINCT FROM 'object' OR row->>'id' IS DISTINCT FROM quest
            OR jsonb_typeof(row->'objectives') IS DISTINCT FROM 'array' OR octet_length(row::TEXT) > 8192
            OR (SELECT count(*) FROM pj_quest_states WHERE character_id = actor) >= 128 THEN
            RETURN jsonb_build_object('status', 'INVALID');
        END IF;
        INSERT INTO pj_quest_states(character_id, quest_id, state, progress_json) VALUES (actor, quest, 'ACTIVE', row)
            ON CONFLICT DO NOTHING;
    ELSE
        IF jsonb_typeof(p_req->'tracked') IS DISTINCT FROM 'boolean' THEN RETURN jsonb_build_object('status','INVALID'); END IF;
        UPDATE pj_quest_states SET progress_json = jsonb_set(progress_json, '{tracked}', p_req->'tracked'), revision = revision + 1
            WHERE character_id = actor AND quest_id = quest AND state = 'ACTIVE';
    END IF;
    IF NOT FOUND THEN RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','STALE'); END IF;
    UPDATE pj_characters SET quest_revision = quest_revision + 1 WHERE character_id = actor;
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, 'online_quest_edit', 200, outcome, p_req);
    RETURN pj_online_inventory(actor, epoch) || outcome;
EXCEPTION WHEN check_violation OR unique_violation THEN
    RETURN jsonb_build_object('status','CONFLICT');
END;
$$;

CREATE OR REPLACE FUNCTION pj_dungeon_clear(p_worker UUID, p_instance UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    instance pj_dungeon_instances%ROWTYPE;
    entry RECORD;
    event TEXT := p_req->>'quest_event';
BEGIN
    IF jsonb_typeof(p_req->'rewards') IS DISTINCT FROM 'object' OR COALESCE(length(event), 0) > 64
        OR (SELECT count(*) FROM jsonb_each(p_req->'rewards')) NOT BETWEEN 1 AND 40 THEN
        RETURN jsonb_build_object('status','INVALID');
    END IF;
    -- Same lock order as return admission: character rows, then the dungeon instance.
    PERFORM 1 FROM pj_characters WHERE character_id IN (SELECT key::UUID FROM jsonb_each(p_req->'rewards'))
        ORDER BY character_id FOR UPDATE;
    SELECT * INTO instance FROM pj_dungeon_instances WHERE instance_id = p_instance AND worker_incarnation = p_worker FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status','CONFLICT'); END IF;
    IF instance.cleared_at IS NOT NULL THEN RETURN jsonb_build_object('status','REPLAYED'); END IF;
    IF instance.state <> 'READY' OR instance.expires_at <= clock_timestamp() OR instance.started_at IS NULL
        OR NOT EXISTS (SELECT 1 FROM pj_dungeon_workers WHERE incarnation = p_worker AND leased_until > clock_timestamp())
        OR EXISTS (SELECT 1 FROM jsonb_each(p_req->'rewards') r
            WHERE NOT EXISTS (SELECT 1 FROM pj_dungeon_members m WHERE m.instance_id = p_instance AND m.character_id = r.key::UUID AND m.joined_at IS NOT NULL))
        THEN RETURN jsonb_build_object('status','CONFLICT'); END IF;
    BEGIN
        FOR entry IN SELECT key::UUID AS character_id, value FROM jsonb_each(p_req->'rewards') ORDER BY key
        LOOP
            IF COALESCE(pj_grant_boss_reward(p_instance, entry.character_id,
                entry.value || jsonb_build_object('source','dungeon'))->>'status','') NOT IN ('COMMITTED','REPLAYED')
                THEN RAISE check_violation; END IF;
            IF event IS NOT NULL THEN PERFORM pj_credit_online_quests(entry.character_id, event, NULL, NULL, 1); END IF;
        END LOOP;
        UPDATE pj_dungeon_instances SET cleared_at = clock_timestamp() WHERE instance_id = p_instance;
    EXCEPTION WHEN check_violation THEN
        RETURN jsonb_build_object('status','CONFLICT');
    END;
    RETURN jsonb_build_object('status','COMMITTED');
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (9, 'Online quest objectives and atomic dungeon completion') ON CONFLICT (version) DO NOTHING;
COMMIT;
