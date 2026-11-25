BEGIN;

ALTER TABLE pj_characters ADD COLUMN IF NOT EXISTS quest_revision BIGINT NOT NULL DEFAULT 0;

ALTER TABLE pj_inventory_items ADD COLUMN IF NOT EXISTS equipped_slot INT;
ALTER TABLE pj_inventory_items DROP CONSTRAINT IF EXISTS pj_online_equipped_item;
ALTER TABLE pj_inventory_items ADD CONSTRAINT pj_online_equipped_item CHECK (
    equipped_slot IS NULL OR (equipped_slot BETWEEN 1 AND 9 AND owner_scope = 'CHAR_INV' AND quantity = 1)
);
CREATE UNIQUE INDEX IF NOT EXISTS pj_online_equipped_slot
    ON pj_inventory_items(owner_character_id, equipped_slot) WHERE equipped_slot IS NOT NULL;

CREATE TABLE IF NOT EXISTS pj_online_craft_jobs (
    job_id UUID PRIMARY KEY,
    character_id UUID NOT NULL REFERENCES pj_characters(character_id),
    terms JSONB NOT NULL CHECK (jsonb_typeof(terms) = 'object'),
    due_ticks BIGINT NOT NULL
);
CREATE INDEX IF NOT EXISTS pj_online_craft_jobs_owner ON pj_online_craft_jobs(character_id);
ALTER TABLE pj_inventory_items ADD COLUMN IF NOT EXISTS craft_job_id UUID REFERENCES pj_online_craft_jobs(job_id);
ALTER TABLE pj_inventory_items DROP CONSTRAINT IF EXISTS pj_inventory_items_owner_scope_check;
ALTER TABLE pj_inventory_items ADD CONSTRAINT pj_inventory_items_owner_scope_check CHECK (
    owner_scope IN ('CHAR_INV','CHAR_EQUIP','AUCTION_ESCROW','MAIL_ESCROW','GUILD_BANK','DESTROYED','CRAFT_ESCROW')
    AND (owner_scope = 'CRAFT_ESCROW') = (craft_job_id IS NOT NULL)
);
CREATE INDEX IF NOT EXISTS pj_online_craft_escrow ON pj_inventory_items(craft_job_id) WHERE craft_job_id IS NOT NULL;

CREATE OR REPLACE FUNCTION pj_online_item_value(p_item pj_inventory_items)
RETURNS JSONB LANGUAGE sql IMMUTABLE AS $$
    SELECT jsonb_build_object('id', p_item.item_instance_id, 'definition', p_item.definition_id, 'version', p_item.definition_version,
        'count', p_item.quantity, 'level', GREATEST(1, p_item.enhance_level), 'order', p_item.slot_index, 'scope', p_item.owner_scope,
        'equipped_slot', COALESCE(p_item.equipped_slot, 0));
$$;

CREATE OR REPLACE FUNCTION pj_online_inventory(p_character UUID, p_epoch BIGINT)
RETURNS JSONB LANGUAGE sql STABLE AS $$
    SELECT COALESCE((
        SELECT jsonb_build_object('status', 'OK', 'inventory_revision', c.inventory_revision::TEXT,
            'quest_revision', c.quest_revision::TEXT, 'quests', COALESCE((
                SELECT jsonb_agg(q.progress_json ORDER BY q.quest_id) FROM pj_quest_states q
                WHERE q.character_id = c.character_id AND q.state = 'ACTIVE'
            ), '[]'::JSONB),
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

CREATE OR REPLACE FUNCTION pj_online_equipment_edit(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    actor UUID := (p_req->>'character_id')::UUID;
    epoch BIGINT := (p_req->>'session_epoch')::BIGINT;
    source_id UUID := (p_req->>'item')::UUID;
    slot INT := (p_req->>'slot')::INT;
    is_equip BOOLEAN := (p_req->>'equip')::BOOLEAN;
    prior JSONB;
    prior_slot INT;
    outcome CONSTANT JSONB := '{"status":"COMMITTED"}';
BEGIN
    IF p_key IS NULL OR source_id IS NULL OR slot IS NULL OR slot NOT BETWEEN 1 AND 9 OR is_equip IS NULL THEN
        RETURN jsonb_build_object('status','INVALID');
    END IF;
    prior := pj_replay_command(p_key, 'online_equipment_edit', p_req - 'expected_slot_item');
    PERFORM 1 FROM pj_characters WHERE character_id = actor AND session_epoch = epoch AND epoch > 0 FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status','CONFLICT'); END IF;
    IF prior IS NOT NULL THEN RETURN pj_online_inventory(actor, epoch) || prior; END IF;
    SELECT equipped_slot INTO prior_slot FROM pj_inventory_items WHERE item_instance_id = source_id
        AND owner_character_id = actor AND owner_scope = 'CHAR_INV' AND quantity = 1
        AND definition_id = p_req->>'definition' AND definition_version = (p_req->>'version')::INT;
    IF NOT FOUND OR (is_equip AND prior_slot IS NOT NULL) OR (NOT is_equip AND prior_slot IS DISTINCT FROM slot) THEN
        RETURN jsonb_build_object('status','CONFLICT');
    END IF;
    IF is_equip THEN
        IF (SELECT item_instance_id FROM pj_inventory_items WHERE owner_character_id = actor AND equipped_slot = slot)
            IS DISTINCT FROM (p_req->>'expected_slot_item')::UUID THEN RETURN jsonb_build_object('status','CONFLICT'); END IF;
        UPDATE pj_inventory_items SET equipped_slot = NULL, revision = revision + 1, updated_at = NOW()
            WHERE owner_character_id = actor AND equipped_slot = slot;
    END IF;
    UPDATE pj_inventory_items SET equipped_slot = CASE WHEN is_equip THEN slot END, revision = revision + 1, updated_at = NOW()
        WHERE item_instance_id = source_id;
    UPDATE pj_characters SET inventory_revision = inventory_revision + 1 WHERE character_id = actor;
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, 'online_equipment_edit', 200, outcome, p_req - 'expected_slot_item');
    RETURN pj_online_inventory(actor, epoch) || outcome;
END;
$$;

CREATE OR REPLACE FUNCTION pj_online_craft_commit(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    actor UUID := (p_req->>'character_id')::UUID;
    epoch BIGINT := (p_req->>'session_epoch')::BIGINT;
    kind TEXT := p_req->>'kind';
    job UUID := (p_req->>'job_id')::UUID;
    terms JSONB := p_req->'job_terms';
    intent JSONB := p_req - 'changes' - 'expected_revision' - 'job_terms';
    outcome CONSTANT JSONB := '{"status":"COMMITTED"}';
    prior JSONB;
    character pj_characters%ROWTYPE;
    item pj_inventory_items%ROWTYPE;
    edit RECORD;
    now_ticks BIGINT := (extract(epoch FROM clock_timestamp()) * 10000000)::BIGINT + 621355968000000000;
BEGIN
    IF p_key IS NULL OR kind IS NULL OR kind NOT IN ('craft','start','collect','cancel','gather')
        OR (kind IN ('craft','gather')) <> (job IS NULL) OR (kind = 'start' AND job <> p_key)
        OR jsonb_typeof(p_req->'changes') IS DISTINCT FROM 'array'
        OR jsonb_array_length(p_req->'changes') > 256 THEN RETURN jsonb_build_object('status','INVALID'); END IF;
    prior := pj_replay_command(p_key, 'online_craft_commit', intent);
    SELECT * INTO character FROM pj_characters WHERE character_id = actor AND session_epoch = epoch AND epoch > 0 FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status','CONFLICT'); END IF;
    IF prior IS NOT NULL THEN RETURN pj_online_inventory(actor, epoch) || prior; END IF;
    IF character.inventory_revision IS DISTINCT FROM (p_req->>'expected_revision')::BIGINT THEN
        RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','STALE');
    END IF;
    IF kind IN ('collect','cancel') AND NOT EXISTS (SELECT 1 FROM pj_online_craft_jobs
        WHERE job_id = job AND character_id = actor AND (kind = 'cancel' OR due_ticks <= now_ticks)) THEN
        RETURN jsonb_build_object('status','CONFLICT');
    END IF;
    IF kind = 'collect' THEN SELECT j.terms INTO terms FROM pj_online_craft_jobs j WHERE j.job_id = job; END IF;
    -- Validation exceptions roll back only this command's writes before returning a domain status.
    BEGIN
        IF kind = 'start' THEN
            IF jsonb_typeof(terms) IS DISTINCT FROM 'object' OR octet_length(terms::TEXT) > 8192
                OR terms->'recipe' IS DISTINCT FROM p_req->'recipe' OR terms->'recipe_version' IS DISTINCT FROM p_req->'recipe_version'
                OR terms->'batches' IS DISTINCT FROM p_req->'batches' OR terms->'rules' IS DISTINCT FROM p_req->'rules'
                OR jsonb_typeof(terms->'outputs') IS DISTINCT FROM 'array'
                OR (SELECT count(*) FROM pj_online_craft_jobs WHERE character_id = actor) >= 8
                OR (terms->>'due_ticks')::BIGINT NOT BETWEEN now_ticks - 10000000 AND now_ticks + 6048000000000
                OR (terms->>'due_ticks') IS NULL OR (terms->>'started_ticks') IS NULL THEN RAISE check_violation; END IF;
            IF jsonb_array_length(terms->'outputs') NOT BETWEEN 1 AND 8
                OR ((terms->>'due_ticks')::BIGINT - (terms->>'started_ticks')::BIGINT) NOT BETWEEN 10000000 AND 6048000000000
                OR ((terms->>'due_ticks')::BIGINT - (terms->>'started_ticks')::BIGINT) % 10000000 <> 0
                THEN RAISE check_violation; END IF;
            INSERT INTO pj_online_craft_jobs(job_id, character_id, terms, due_ticks)
                VALUES (job, actor, terms - 'due_ticks', (terms->>'due_ticks')::BIGINT);
        END IF;
        IF (SELECT count(DISTINCT value->>'id') FROM jsonb_array_elements(p_req->'changes')) <> jsonb_array_length(p_req->'changes')
            THEN RAISE check_violation; END IF;
        FOR edit IN SELECT * FROM jsonb_to_recordset(p_req->'changes') AS d(id UUID, definition TEXT, version INT,
            count INT, level INT, bag_order INT, escrow BOOLEAN)
        LOOP
            IF edit.count IS NULL OR edit.count < 0 OR edit.escrow IS NULL OR
                (edit.escrow AND (kind <> 'start' OR edit.count = 0)) THEN RAISE check_violation; END IF;
            IF edit.count > 0 AND (COALESCE(length(edit.definition), 0) NOT BETWEEN 1 AND 64 OR edit.version IS NULL OR edit.version < 1
                OR edit.level IS NULL OR edit.level NOT BETWEEN 1 AND 10000
                OR edit.bag_order IS NULL OR edit.bag_order NOT BETWEEN 0 AND 2147483646) THEN RAISE check_violation; END IF;
            SELECT * INTO item FROM pj_inventory_items WHERE item_instance_id = edit.id FOR UPDATE;
            IF FOUND THEN
                IF item.owner_character_id IS DISTINCT FROM actor OR item.owner_scope NOT IN ('CHAR_INV','CRAFT_ESCROW')
                    OR item.equipped_slot IS NOT NULL OR item.pity_counter <> 0 OR item.rolls_json <> '{}'::JSONB
                    OR (item.craft_job_id IS NOT NULL AND (kind NOT IN ('collect','cancel') OR item.craft_job_id IS DISTINCT FROM job))
                    OR (edit.count > 0 AND (item.definition_id, item.definition_version, GREATEST(1, item.enhance_level))
                        IS DISTINCT FROM (edit.definition, edit.version, edit.level))
                    OR (kind = 'cancel' AND (item.craft_job_id IS DISTINCT FROM job OR edit.count <> item.quantity))
                    THEN RAISE check_violation; END IF;
                UPDATE pj_inventory_items SET quantity = CASE WHEN edit.count = 0 THEN quantity ELSE edit.count END,
                    owner_scope = CASE WHEN edit.count = 0 THEN 'DESTROYED' WHEN edit.escrow THEN 'CRAFT_ESCROW' ELSE 'CHAR_INV' END,
                    slot_index = CASE WHEN edit.count = 0 THEN -1 ELSE edit.bag_order END,
                    craft_job_id = CASE WHEN edit.escrow THEN job END, revision = revision + 1, updated_at = NOW()
                    WHERE item_instance_id = edit.id;
            ELSE
                IF edit.count = 0 OR kind = 'cancel' THEN RAISE check_violation; END IF;
                INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id,
                    definition_version, quantity, slot_index, enhance_level, craft_job_id)
                    VALUES (edit.id, actor, CASE WHEN edit.escrow THEN 'CRAFT_ESCROW' ELSE 'CHAR_INV' END,
                        edit.definition, edit.version, edit.count, edit.bag_order, edit.level,
                        CASE WHEN edit.escrow THEN job END);
            END IF;
        END LOOP;
        IF kind = 'collect' THEN
            UPDATE pj_inventory_items SET owner_scope = 'DESTROYED', slot_index = -1, craft_job_id = NULL,
                revision = revision + 1, updated_at = NOW() WHERE craft_job_id = job AND owner_character_id = actor;
        END IF;
        IF kind IN ('collect','cancel') THEN DELETE FROM pj_online_craft_jobs WHERE job_id = job AND character_id = actor; END IF;
        IF kind = 'start' AND (SELECT count(*) FROM pj_inventory_items WHERE craft_job_id = job AND owner_character_id = actor)
            NOT BETWEEN 1 AND 64
            THEN RAISE check_violation; END IF;
        IF (SELECT count(*) > character.inventory_capacity OR count(*) <> count(DISTINCT slot_index)
            FROM pj_inventory_items WHERE owner_character_id = actor AND owner_scope = 'CHAR_INV' AND craft_job_id IS NULL)
            THEN RAISE check_violation; END IF;
        UPDATE pj_characters SET inventory_revision = inventory_revision + 1 WHERE character_id = actor;
        IF kind = 'craft' THEN
            PERFORM pj_credit_online_quests(actor, NULL, p_req->>'recipe', (p_req->>'recipe_version')::INT, (p_req->>'batches')::INT);
        ELSIF kind = 'collect' THEN
            PERFORM pj_credit_online_quests(actor, NULL, terms->>'recipe', (terms->>'recipe_version')::INT, (terms->>'batches')::INT);
        ELSIF kind = 'gather' THEN
            PERFORM pj_credit_online_quests(actor, p_req->>'gather_event', NULL,
                (p_req->>'gather_version')::INT, (p_req->>'gather_count')::INT, p_req->>'gather_item');
        END IF;
        INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
            VALUES (p_key, 'online_craft_commit', 200, outcome, intent);
    EXCEPTION WHEN check_violation OR unique_violation OR foreign_key_violation THEN
        RETURN jsonb_build_object('status','CONFLICT');
    END;
    RETURN pj_online_inventory(actor, epoch) || outcome;
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (8, 'Revisioned online equipment and crafting escrow') ON CONFLICT (version) DO NOTHING;
COMMIT;
