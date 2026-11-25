BEGIN;

ALTER TABLE pj_characters ADD COLUMN IF NOT EXISTS quest_completion_count INT NOT NULL DEFAULT 0
    CHECK (quest_completion_count BETWEEN 0 AND 65536);
ALTER TABLE pj_quest_states ADD COLUMN IF NOT EXISTS completion_no INT CHECK (completion_no BETWEEN 1 AND 65536);

-- Retain the original claim rows. Preserve any assigned ordinals when reapplying the migration.
WITH existing AS (
    SELECT character_id, max(completion_no) AS last_completion FROM pj_quest_states
    WHERE state = 'CLAIMED' AND completion_no IS NOT NULL GROUP BY character_id
), numbered AS (
    SELECT q.character_id, q.quest_id, COALESCE(e.last_completion, 0) + row_number() OVER (
        PARTITION BY q.character_id ORDER BY q.updated_at, q.quest_id) AS completion_no
    FROM pj_quest_states q LEFT JOIN existing e ON e.character_id = q.character_id
    WHERE q.state = 'CLAIMED' AND q.completion_no IS NULL
)
UPDATE pj_quest_states q SET completion_no = numbered.completion_no FROM numbered
WHERE q.character_id = numbered.character_id AND q.quest_id = numbered.quest_id;

UPDATE pj_characters c SET quest_completion_count = q.last_completion FROM (
    SELECT character_id, max(completion_no) AS last_completion FROM pj_quest_states
    WHERE state = 'CLAIMED' GROUP BY character_id
) q WHERE c.character_id = q.character_id AND c.quest_completion_count <> q.last_completion;

CREATE INDEX IF NOT EXISTS idx_pj_quest_active
    ON pj_quest_states(character_id, quest_id) WHERE state = 'ACTIVE';
CREATE UNIQUE INDEX IF NOT EXISTS idx_pj_quest_completion_page
    ON pj_quest_states(character_id, completion_no DESC) WHERE state = 'CLAIMED' AND completion_no IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_pj_quest_claimed_travel
    ON pj_quest_states(character_id, quest_id) WHERE state = 'CLAIMED'
        AND NOT (progress_json ? 'event') AND progress_json->'objectives' = '[]'::JSONB;

CREATE OR REPLACE FUNCTION pj_online_inventory(p_character UUID, p_epoch BIGINT)
RETURNS JSONB LANGUAGE sql STABLE AS $$
    SELECT COALESCE((
        SELECT jsonb_build_object('status', 'OK', 'inventory_revision', c.inventory_revision::TEXT,
            'profile_revision', c.revision::TEXT, 'level', c.level, 'xp', c.xp::TEXT,
            'quest_revision', c.quest_revision::TEXT, 'completed_quest_count', c.quest_completion_count, 'quests', COALESCE((
                SELECT jsonb_agg(q.progress_json ORDER BY q.quest_id) FROM pj_quest_states q
                WHERE q.character_id = c.character_id AND q.state = 'ACTIVE'
            ), '[]'::JSONB),
            'completed_travel_quests', COALESCE((SELECT jsonb_agg(travel.quest_id ORDER BY travel.quest_id) FROM (
                SELECT q.quest_id FROM pj_quest_states q WHERE q.character_id = c.character_id AND q.state = 'CLAIMED'
                    AND NOT (q.progress_json ? 'event') AND q.progress_json->'objectives' = '[]'::JSONB
                ORDER BY q.quest_id LIMIT 128
            ) travel), '[]'::JSONB),
            'items', COALESCE((SELECT jsonb_agg(pj_online_item_value(i) ORDER BY i.slot_index, i.item_instance_id)
                FROM pj_inventory_items i WHERE i.owner_character_id = c.character_id AND i.owner_scope = 'CHAR_INV'), '[]'::JSONB),
            'crafting_jobs', COALESCE((SELECT jsonb_agg(j.terms || jsonb_build_object(
                'id', j.job_id, 'due_ticks', j.due_ticks::TEXT, 'escrow', COALESCE((
                    SELECT jsonb_agg(pj_online_item_value(i) ORDER BY i.slot_index, i.item_instance_id)
                    FROM pj_inventory_items i WHERE i.craft_job_id = j.job_id AND i.owner_character_id = c.character_id
                ), '[]'::JSONB)) ORDER BY j.job_id) FROM pj_online_craft_jobs j WHERE j.character_id = c.character_id), '[]'::JSONB))
        FROM pj_characters c WHERE c.character_id = p_character AND c.session_epoch = p_epoch AND p_epoch > 0
    ), jsonb_build_object('status', 'CONFLICT'));
$$;

CREATE OR REPLACE FUNCTION pj_online_quest_history_page(p_character UUID, p_epoch BIGINT, p_count INT, p_ordinal INT)
RETURNS JSONB LANGUAGE plpgsql STABLE AS $$
DECLARE
    completion_count INT;
    newest INT;
    ids JSONB;
BEGIN
    IF p_character IS NULL OR COALESCE(p_epoch, 0) <= 0 OR COALESCE(p_count, -1) NOT BETWEEN 0 AND 65536
        OR COALESCE(p_ordinal, -1) NOT BETWEEN 0 AND 2047 THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
    SELECT quest_completion_count INTO completion_count FROM pj_characters
        WHERE character_id = p_character AND session_epoch = p_epoch;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    IF completion_count <> p_count THEN RETURN jsonb_build_object('status', 'STALE', 'count', completion_count); END IF;
    newest := completion_count - p_ordinal * 32;
    IF newest < 1 AND (p_ordinal <> 0 OR completion_count <> 0) THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
    SELECT COALESCE(jsonb_agg(q.quest_id ORDER BY q.completion_no DESC), '[]'::JSONB) INTO ids FROM pj_quest_states q
        WHERE q.character_id = p_character AND q.state = 'CLAIMED' AND q.completion_no BETWEEN GREATEST(1, newest - 31) AND newest;
    RETURN jsonb_build_object('status', 'OK', 'count', completion_count, 'ids', ids);
END;
$$;

-- The caller holds the character lock. Item terms come from the trusted game server's
-- authored, versioned definitions, and allocation operates on the latest committed bag.
CREATE OR REPLACE FUNCTION pj_pack_online_quest_rewards(p_character UUID, p_capacity INT, p_items JSONB)
RETURNS VOID LANGUAGE plpgsql AS $$
DECLARE
    reward RECORD;
    stack RECORD;
    seen TEXT[] := ARRAY[]::TEXT[];
    available_slots INT[];
    occupied INT;
    owned INT;
    next_slot INT := 1;
    remaining INT;
    added INT;
BEGIN
    IF jsonb_typeof(p_items) IS DISTINCT FROM 'array' OR jsonb_array_length(p_items) > 8 THEN RAISE check_violation; END IF;
    IF jsonb_array_length(p_items) = 0 THEN RETURN; END IF;
    SELECT count(*) FILTER (WHERE owner_scope = 'CHAR_INV'), count(*) INTO occupied, owned
        FROM pj_inventory_items WHERE owner_character_id = p_character AND owner_scope IN ('CHAR_INV', 'CRAFT_ESCROW');
    SELECT array_agg(free.slot ORDER BY free.slot) INTO available_slots
        FROM generate_series(0, p_capacity - 1) AS free(slot)
        WHERE NOT EXISTS (SELECT 1 FROM pj_inventory_items i
            WHERE i.owner_character_id = p_character AND i.owner_scope = 'CHAR_INV' AND i.slot_index = free.slot);

    FOR reward IN SELECT * FROM jsonb_to_recordset(p_items)
        AS r(definition TEXT, version INT, level INT, quantity INT, max_stack INT)
    LOOP
        IF COALESCE(length(reward.definition), 0) NOT BETWEEN 1 AND 64 OR lower(reward.definition) = ANY(seen)
            OR COALESCE(reward.version, 0) < 1 OR COALESCE(reward.level, 0) NOT BETWEEN 1 AND 10000
            OR COALESCE(reward.quantity, 0) NOT BETWEEN 1 AND 1000000 OR COALESCE(reward.max_stack, 0) < 1
        THEN RAISE check_violation; END IF;
        seen := array_append(seen, lower(reward.definition));
        remaining := reward.quantity;
        FOR stack IN SELECT item_instance_id, quantity FROM pj_inventory_items
            WHERE owner_character_id = p_character AND owner_scope = 'CHAR_INV' AND equipped_slot IS NULL
                AND lower(definition_id) = lower(reward.definition) AND definition_version = reward.version
                AND GREATEST(1, enhance_level) = reward.level AND quantity < reward.max_stack
            ORDER BY slot_index, item_instance_id FOR UPDATE
        LOOP
            added := LEAST(remaining, reward.max_stack - stack.quantity);
            UPDATE pj_inventory_items SET quantity = quantity + added, revision = revision + 1, updated_at = NOW()
                WHERE item_instance_id = stack.item_instance_id;
            remaining := remaining - added;
            EXIT WHEN remaining = 0;
        END LOOP;
        WHILE remaining > 0 LOOP
            IF occupied >= p_capacity OR owned >= 4096 OR available_slots[next_slot] IS NULL THEN
                RAISE SQLSTATE 'PJ013' USING MESSAGE = 'Quest reward bag full';
            END IF;
            added := LEAST(remaining, reward.max_stack);
            INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id,
                definition_version, quantity, slot_index, enhance_level)
                VALUES (gen_random_uuid(), p_character, 'CHAR_INV', reward.definition, reward.version, added,
                    available_slots[next_slot], reward.level);
            remaining := remaining - added;
            next_slot := next_slot + 1;
            occupied := occupied + 1;
            owned := owned + 1;
        END LOOP;
    END LOOP;
END;
$$;

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
    character pj_characters%ROWTYPE;
    reward_level INT;
    reward_xp BIGINT;
    reward_definition_version INT;
    operation TEXT := CASE WHEN p_earned THEN 'online_earned_quest_progress' ELSE 'online_quest_edit' END;
    prior JSONB;
    outcome JSONB := '{"status":"COMMITTED"}';
BEGIN
    IF p_key IS NULL OR kind IS NULL OR kind NOT IN ('accept','track','progress','claim')
        OR (p_earned AND kind <> 'progress')
        OR (kind <> 'progress' AND COALESCE(length(quest), 0) NOT BETWEEN 1 AND 64) THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    prior := pj_replay_command(p_key, operation, p_req);
    SELECT * INTO character FROM pj_characters WHERE character_id = actor
        AND submitted_epoch > 0 AND (session_epoch = submitted_epoch OR (p_earned AND session_epoch > submitted_epoch))
        FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    epoch := character.session_epoch;
    IF prior IS NOT NULL THEN RETURN pj_online_inventory(actor, epoch) || prior
        || CASE WHEN p_earned AND prior->>'status' = 'REPLAYED'
            THEN jsonb_build_object('quest_progress_epoch', epoch::TEXT)
            WHEN kind = 'claim' AND prior->>'status' = 'REPLAYED'
            THEN COALESCE(prior->'receipt', '{}'::JSONB) - 'status' ELSE '{}'::JSONB END; END IF;
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
                IF EXISTS (SELECT 1 FROM pj_quest_states
                    WHERE character_id = actor AND quest_id = quest AND state = 'CLAIMED') THEN CONTINUE; END IF;
                IF (SELECT count(*) FROM pj_quest_states WHERE character_id = actor AND state = 'ACTIVE')
                    >= LEAST(128, 65536 - character.quest_completion_count) THEN RAISE check_violation; END IF;
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
            OR (SELECT count(*) FROM pj_quest_states WHERE character_id = actor AND state = 'ACTIVE')
                >= LEAST(128, 65536 - character.quest_completion_count) THEN
            RETURN jsonb_build_object('status', 'INVALID');
        END IF;
        INSERT INTO pj_quest_states(character_id, quest_id, state, progress_json) VALUES (actor, quest, 'ACTIVE', row)
            ON CONFLICT DO NOTHING;
    ELSIF kind = 'claim' THEN
        IF EXISTS (SELECT 1 FROM pj_quest_states WHERE character_id = actor AND quest_id = quest AND state = 'CLAIMED') THEN
            RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','STALE','completed_quest',quest);
        END IF;
        reward_definition_version := (p_req->>'definition_version')::INT;
        reward_level := (p_req->>'reward_level')::INT;
        reward_xp := (p_req->>'reward_xp')::BIGINT;
        IF COALESCE(reward_definition_version, 0) < 1 OR COALESCE(reward_level, 0) NOT BETWEEN 1 AND 10000
            OR reward_xp IS NULL OR reward_xp < 0 OR jsonb_typeof(p_req->'reward_items') IS DISTINCT FROM 'array'
            OR jsonb_array_length(p_req->'reward_items') > 8 OR COALESCE((p_req->>'expected_profile_revision')::BIGINT, 0) <= 0 THEN
            RETURN jsonb_build_object('status','INVALID');
        END IF;
        IF character.revision IS DISTINCT FROM (p_req->>'expected_profile_revision')::BIGINT THEN
            RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','STALE');
        END IF;
        IF reward_level < character.level OR (reward_level = character.level AND reward_xp < character.xp) THEN
            RETURN jsonb_build_object('status','INVALID');
        END IF;
        UPDATE pj_quest_states SET state = 'CLAIMED', revision = revision + 1, updated_at = NOW(),
            completion_no = character.quest_completion_count + 1
            WHERE character_id = actor AND quest_id = quest AND state = 'ACTIVE'
                AND (progress_json->>'definition_version')::INT = reward_definition_version
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
        IF NOT FOUND THEN RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','STALE'); END IF;
        PERFORM pj_pack_online_quest_rewards(actor, character.inventory_capacity, p_req->'reward_items');
        UPDATE pj_characters SET level = reward_level, xp = reward_xp, updated_at = NOW(),
            quest_completion_count = quest_completion_count + 1,
            revision = revision + CASE WHEN level <> reward_level OR xp <> reward_xp THEN 1 ELSE 0 END,
            inventory_revision = inventory_revision + CASE WHEN jsonb_array_length(p_req->'reward_items') > 0 THEN 1 ELSE 0 END
            WHERE character_id = actor;
        outcome := outcome || jsonb_build_object('completed_quest',quest);
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
EXCEPTION WHEN SQLSTATE 'PJ013' THEN
    RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','INVENTORY_FULL');
WHEN check_violation OR unique_violation OR invalid_text_representation OR numeric_value_out_of_range OR datetime_field_overflow THEN
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
    VALUES (13, 'Atomic online quest items and experience with an active-only quest limit') ON CONFLICT (version) DO NOTHING;

COMMIT;
