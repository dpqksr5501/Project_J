BEGIN;

CREATE TABLE IF NOT EXISTS pj_boss_reward_receipts (
    defeat_id UUID NOT NULL,
    character_id UUID NOT NULL REFERENCES pj_characters(character_id),
    request_json JSONB NOT NULL,
    response_json JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (defeat_id, character_id)
);

CREATE OR REPLACE FUNCTION pj_grant_boss_reward(p_defeat UUID, p_recipient UUID, p_request JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    prior pj_boss_reward_receipts%ROWTYPE;
    grant_item JSONB;
    item_id UUID;
    mail_id UUID;
    mails UUID[] := '{}';
    gold BIGINT := (p_request->>'gold')::BIGINT;
    from_dungeon BOOLEAN := COALESCE(p_request->>'source' = 'dungeon', FALSE);
    response JSONB;
BEGIN
    IF p_defeat IS NULL OR p_recipient IS NULL OR gold IS NULL OR gold < 0
        OR jsonb_typeof(p_request->'items') IS DISTINCT FROM 'array' THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    IF jsonb_array_length(p_request->'items') > 40 THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
    FOR grant_item IN SELECT value FROM jsonb_array_elements(p_request->'items') LOOP
        IF COALESCE(length(grant_item->>'definition_id'), 0) NOT BETWEEN 1 AND 64
            OR COALESCE((grant_item->>'quantity')::INT, 0) <= 0 THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
    END LOOP;
    PERFORM pg_advisory_xact_lock(hashtextextended(p_defeat::TEXT || p_recipient::TEXT, 2));
    SELECT * INTO prior FROM pj_boss_reward_receipts WHERE defeat_id = p_defeat AND character_id = p_recipient;
    IF FOUND THEN
        IF prior.request_json IS DISTINCT FROM p_request THEN RETURN jsonb_build_object('status', 'KEY_REUSED'); END IF;
        RETURN jsonb_build_object('status', 'REPLAYED', 'receipt', prior.response_json);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pj_characters WHERE character_id = p_recipient) THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    FOR grant_item IN SELECT value FROM jsonb_array_elements(p_request->'items') LOOP
        item_id := gen_random_uuid();
        INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id, quantity)
            VALUES (item_id, p_recipient, 'MAIL_ESCROW', grant_item->>'definition_id', (grant_item->>'quantity')::INT);
        INSERT INTO pj_mail_messages(recipient_char_id, sender_label, subject_key, attached_item_id)
            VALUES (p_recipient, CASE WHEN from_dungeon THEN 'Dungeon' ELSE 'FieldBoss' END,
                CASE WHEN from_dungeon THEN 'MAIL_DUNGEON_CLEAR' ELSE 'MAIL_FIELDBOSS_REWARD' END, item_id)
            RETURNING pj_mail_messages.mail_id INTO mail_id;
        mails := array_append(mails, mail_id);
    END LOOP;
    IF gold > 0 THEN
        INSERT INTO pj_mail_messages(recipient_char_id, sender_label, subject_key, attached_currency, attached_amount)
            VALUES (p_recipient, CASE WHEN from_dungeon THEN 'Dungeon' ELSE 'FieldBoss' END,
                CASE WHEN from_dungeon THEN 'MAIL_DUNGEON_CLEAR' ELSE 'MAIL_FIELDBOSS_REWARD' END, 'GOLD', gold)
            RETURNING pj_mail_messages.mail_id INTO mail_id;
        mails := array_append(mails, mail_id);
    END IF;
    response := jsonb_build_object('status', 'COMMITTED', 'mail_ids', to_jsonb(mails));
    INSERT INTO pj_boss_reward_receipts(defeat_id, character_id, request_json, response_json)
        VALUES (p_defeat, p_recipient, p_request, response);
    RETURN response;
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (4, 'Durable field boss item and currency mail publication') ON CONFLICT (version) DO NOTHING;
COMMIT;
