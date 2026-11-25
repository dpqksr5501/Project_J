BEGIN;

-- Only the trusted server event RPC may replay progress earned in an earlier session.
-- The receipt compares the immutable original event, including its original epoch.
CREATE OR REPLACE FUNCTION pj_apply_online_quest_edit(p_key UUID, p_req JSONB, p_earned BOOLEAN)
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
    submitted_epoch BIGINT := epoch;
    earned_at TIMESTAMPTZ;
    changed BOOLEAN := FALSE;
    operation TEXT := CASE WHEN p_earned THEN 'online_earned_quest_progress' ELSE 'online_quest_edit' END;
    prior JSONB;
    outcome CONSTANT JSONB := '{"status":"COMMITTED"}';
BEGIN
    IF p_key IS NULL OR kind IS NULL OR kind NOT IN ('accept','track','progress','claim')
        OR (p_earned AND kind <> 'progress')
        OR (kind <> 'progress' AND COALESCE(length(quest), 0) NOT BETWEEN 1 AND 64) THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    prior := pj_replay_command(p_key, operation, p_req);
    SELECT session_epoch INTO epoch FROM pj_characters WHERE character_id = actor
        AND submitted_epoch > 0 AND (session_epoch = submitted_epoch OR (p_earned AND session_epoch > submitted_epoch))
        FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    IF prior IS NOT NULL THEN RETURN pj_online_inventory(actor, epoch) || prior
        || CASE WHEN p_earned AND prior->>'status' = 'REPLAYED'
            THEN jsonb_build_object('quest_progress_epoch', epoch::TEXT) ELSE '{}'::JSONB END; END IF;
    IF p_earned THEN
        -- Receipt lifetime is seven days from the write. A newer receipt thus
        -- always outlives the allowed replay window of the originating event.
        earned_at := to_timestamp((p_req->>'earned_at')::BIGINT);
        IF earned_at IS NULL OR earned_at < clock_timestamp() - INTERVAL '6 days'
            OR earned_at > clock_timestamp() + INTERVAL '5 minutes' THEN
            RETURN jsonb_build_object('status', 'STALE');
        END IF;
    END IF;
    IF kind = 'progress' THEN
        IF jsonb_typeof(p_req->'changes') IS DISTINCT FROM 'array' OR jsonb_array_length(p_req->'changes') NOT BETWEEN 1 AND 128
            THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
        FOR group_row IN SELECT value FROM jsonb_array_elements(p_req->'changes')
        LOOP
            row := group_row->'row';
            quest := row->>'id';
            IF COALESCE(length(quest), 0) NOT BETWEEN 1 AND 64 OR jsonb_typeof(row->'objectives') IS DISTINCT FROM 'array'
                OR jsonb_typeof(group_row->'deltas') IS DISTINCT FROM 'array' OR jsonb_array_length(group_row->'deltas') NOT BETWEEN 1 AND (CASE WHEN p_earned THEN 64 ELSE 8 END)
                THEN RAISE check_violation; END IF;
            SELECT progress_json INTO progress_row FROM pj_quest_states WHERE character_id = actor AND quest_id = quest AND state = 'ACTIVE';
            IF NOT FOUND THEN
                -- A queued event must not reopen an already claimed quest.
                IF p_earned AND EXISTS (SELECT 1 FROM pj_quest_states
                    WHERE character_id = actor AND quest_id = quest AND state = 'CLAIMED') THEN CONTINUE; END IF;
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
            changed := TRUE;
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
    ELSIF kind = 'claim' THEN
        UPDATE pj_quest_states SET state = 'CLAIMED', revision = revision + 1
            WHERE character_id = actor AND quest_id = quest AND state = 'ACTIVE'
                AND CASE
                    WHEN jsonb_typeof(progress_json->'objectives') IS DISTINCT FROM 'array' THEN FALSE
                    WHEN jsonb_array_length(progress_json->'objectives') = 0 THEN
                        (progress_json->>'required')::BIGINT > 0
                        AND (progress_json->>'count')::BIGINT = (progress_json->>'required')::BIGINT
                    ELSE NOT EXISTS (SELECT 1 FROM jsonb_array_elements(progress_json->'objectives') AS goal
                        WHERE (goal->>'required') IS NULL OR (goal->>'count') IS NULL
                            OR (goal->>'required')::BIGINT <= 0
                            OR (goal->>'count')::BIGINT <> (goal->>'required')::BIGINT)
                END;
    ELSE
        IF jsonb_typeof(p_req->'tracked') IS DISTINCT FROM 'boolean' THEN RETURN jsonb_build_object('status','INVALID'); END IF;
        UPDATE pj_quest_states SET progress_json = jsonb_set(progress_json, '{tracked}', p_req->'tracked'), revision = revision + 1
            WHERE character_id = actor AND quest_id = quest AND state = 'ACTIVE';
    END IF;
    IF kind <> 'progress' AND NOT FOUND THEN RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','STALE'); END IF;
    IF kind <> 'progress' OR changed THEN
        UPDATE pj_characters SET quest_revision = quest_revision + 1 WHERE character_id = actor;
    END IF;
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, operation, 200, outcome, p_req);
    RETURN pj_online_inventory(actor, epoch) || outcome || CASE WHEN p_earned
        THEN jsonb_build_object('quest_progress_epoch', epoch::TEXT) ELSE '{}'::JSONB END;
EXCEPTION WHEN check_violation OR unique_violation OR invalid_text_representation OR numeric_value_out_of_range OR datetime_field_overflow THEN
    RETURN jsonb_build_object('status','CONFLICT');
END;
$$;

CREATE OR REPLACE FUNCTION pj_online_quest_edit(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE sql AS $$
    SELECT pj_apply_online_quest_edit(p_key, p_req, FALSE);
$$;

CREATE OR REPLACE FUNCTION pj_online_earned_quest_progress(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE sql AS $$
    SELECT pj_apply_online_quest_edit(p_key, p_req, TRUE);
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (11, 'Durable server-earned online quest progress') ON CONFLICT (version) DO NOTHING;

COMMIT;
