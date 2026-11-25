BEGIN;

CREATE OR REPLACE FUNCTION pj_online_bag_edit(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    actor UUID := (p_req->>'character_id')::UUID;
    epoch BIGINT := (p_req->>'session_epoch')::BIGINT;
    operation TEXT := p_req->>'edit';
    expected_source INT := (p_req->>'source_count')::INT;
    expected_target INT := (p_req->>'target_count')::INT;
    amount INT := (p_req->>'amount')::INT;
    max_stack INT := (p_req->>'max_stack')::INT;
    capacity INT;
    free_slot INT;
    source pj_inventory_items%ROWTYPE;
    target pj_inventory_items%ROWTYPE;
    prior JSONB;
    outcome JSONB := jsonb_build_object('status', 'COMMITTED');
BEGIN
    IF p_key IS NULL OR operation IS NULL OR operation NOT IN ('use', 'split', 'merge', 'swap') THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    prior := pj_replay_command(p_key, 'online_bag_edit', p_req);
    SELECT inventory_capacity INTO capacity FROM pj_characters WHERE character_id = actor
        AND session_epoch = epoch AND epoch > 0 FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    IF prior IS NOT NULL THEN RETURN pj_online_inventory(actor, epoch) || prior; END IF;
    SELECT * INTO source FROM pj_inventory_items WHERE item_instance_id = (p_req->>'source')::UUID
        AND owner_character_id = actor AND owner_scope = 'CHAR_INV' AND equipped_slot IS NULL FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    IF operation = 'use' THEN
        IF source.quantity = 1 THEN
            UPDATE pj_inventory_items SET owner_scope = 'DESTROYED', slot_index = -1, revision = revision + 1, updated_at = NOW()
                WHERE item_instance_id = source.item_instance_id;
        ELSE
            UPDATE pj_inventory_items SET quantity = quantity - 1, revision = revision + 1, updated_at = NOW()
                WHERE item_instance_id = source.item_instance_id;
        END IF;
    ELSE
        IF source.quantity IS DISTINCT FROM expected_source OR source.slot_index IS DISTINCT FROM (p_req->>'source_order')::INT THEN
            RETURN jsonb_build_object('status', 'CONFLICT');
        END IF;
        IF operation = 'split' THEN
            IF amount IS NULL OR amount <= 0 OR amount >= source.quantity OR max_stack IS NULL OR amount > max_stack THEN
                RETURN jsonb_build_object('status', 'INVALID');
            END IF;
            IF (SELECT count(*) FROM pj_inventory_items WHERE owner_character_id = actor AND owner_scope = 'CHAR_INV') >= capacity THEN
                RETURN jsonb_build_object('status', 'INVENTORY_FULL');
            END IF;
            SELECT candidate INTO free_slot FROM generate_series(0, capacity - 1) AS candidate
                WHERE NOT EXISTS (SELECT 1 FROM pj_inventory_items WHERE owner_character_id = actor AND owner_scope = 'CHAR_INV' AND slot_index = candidate)
                ORDER BY candidate LIMIT 1;
            IF NOT FOUND THEN RETURN jsonb_build_object('status', 'INVENTORY_FULL'); END IF;
            UPDATE pj_inventory_items SET quantity = quantity - amount, revision = revision + 1, updated_at = NOW()
                WHERE item_instance_id = source.item_instance_id;
            INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id, definition_version,
                quantity, slot_index, enhance_level, pity_counter, rolls_json)
                VALUES (gen_random_uuid(), actor, 'CHAR_INV', source.definition_id, source.definition_version, amount, free_slot,
                    source.enhance_level, source.pity_counter, source.rolls_json);
        ELSE
            SELECT * INTO target FROM pj_inventory_items WHERE item_instance_id = (p_req->>'target')::UUID
                AND item_instance_id <> source.item_instance_id AND owner_character_id = actor AND owner_scope = 'CHAR_INV'
                AND equipped_slot IS NULL FOR UPDATE;
            IF NOT FOUND OR target.quantity IS DISTINCT FROM expected_target OR target.slot_index IS DISTINCT FROM (p_req->>'target_order')::INT THEN
                RETURN jsonb_build_object('status', 'CONFLICT');
            END IF;
            IF operation = 'swap' THEN
                UPDATE pj_inventory_items SET slot_index = CASE WHEN item_instance_id = source.item_instance_id THEN target.slot_index ELSE source.slot_index END,
                    revision = revision + 1, updated_at = NOW() WHERE item_instance_id IN (source.item_instance_id, target.item_instance_id);
            ELSE
                IF amount IS NULL OR amount <= 0 OR amount > source.quantity OR max_stack IS NULL OR amount > max_stack - target.quantity
                    OR (source.definition_id, source.definition_version, source.enhance_level, source.pity_counter, source.rolls_json)
                    IS DISTINCT FROM (target.definition_id, target.definition_version, target.enhance_level, target.pity_counter, target.rolls_json) THEN
                    RETURN jsonb_build_object('status', 'CONFLICT');
                END IF;
                UPDATE pj_inventory_items SET quantity = quantity + amount, revision = revision + 1, updated_at = NOW()
                    WHERE item_instance_id = target.item_instance_id;
                UPDATE pj_inventory_items SET quantity = GREATEST(1, quantity - amount),
                    owner_scope = CASE WHEN quantity = amount THEN 'DESTROYED' ELSE owner_scope END,
                    slot_index = CASE WHEN quantity = amount THEN -1 ELSE slot_index END,
                    revision = revision + 1, updated_at = NOW() WHERE item_instance_id = source.item_instance_id;
            END IF;
        END IF;
    END IF;
    UPDATE pj_characters SET inventory_revision = inventory_revision + 1 WHERE character_id = actor;
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, 'online_bag_edit', 200, outcome, p_req);
    RETURN pj_online_inventory(actor, epoch) || outcome;
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (7, 'PostgreSQL owner bag edits and revisioned inventory projection') ON CONFLICT (version) DO NOTHING;
COMMIT;
