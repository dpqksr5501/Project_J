BEGIN;

ALTER TABLE pj_characters ADD COLUMN IF NOT EXISTS inventory_capacity INT NOT NULL DEFAULT 256
    CHECK (inventory_capacity BETWEEN 1 AND 1024);
ALTER TABLE pj_characters ADD COLUMN IF NOT EXISTS inventory_revision BIGINT NOT NULL DEFAULT 1 CHECK (inventory_revision > 0);
ALTER TABLE pj_inventory_items ADD COLUMN IF NOT EXISTS definition_version INT NOT NULL DEFAULT 1 CHECK (definition_version > 0);
ALTER TABLE pj_currency_balances DROP CONSTRAINT IF EXISTS pj_currency_balances_balance_check;
ALTER TABLE pj_currency_balances ADD CONSTRAINT pj_currency_balances_balance_check
    CHECK (balance >= 0 OR owner_id = '00000001-0000-0000-0000-000000000001'::UUID);

CREATE INDEX IF NOT EXISTS idx_pj_mail_pending_order ON pj_mail_messages(recipient_char_id, created_at DESC, mail_id DESC)
    WHERE claimed_at IS NULL;

CREATE OR REPLACE FUNCTION pj_mail_inbox(p_character UUID, p_epoch BIGINT, p_cursor UUID, p_limit INT)
RETURNS JSONB LANGUAGE plpgsql STABLE AS $$
DECLARE
    cursor_at TIMESTAMPTZ;
    records JSONB;
    next_cursor UUID;
BEGIN
    IF p_limit IS NULL OR p_limit NOT BETWEEN 1 AND 100 THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
    IF NOT EXISTS (SELECT 1 FROM pj_characters WHERE character_id = p_character AND session_epoch = p_epoch AND p_epoch > 0) THEN
        RETURN jsonb_build_object('status', 'CONFLICT');
    END IF;
    IF p_cursor IS NOT NULL THEN
        SELECT created_at INTO cursor_at FROM pj_mail_messages WHERE mail_id = p_cursor AND recipient_char_id = p_character;
        IF NOT FOUND THEN RETURN jsonb_build_object('status', 'INVALID_CURSOR'); END IF;
    END IF;
    WITH page AS MATERIALIZED (
        SELECT m.mail_id, m.subject_key, m.attached_currency, m.attached_amount::TEXT AS attached_amount,
            m.attached_item_id, m.expires_at, m.created_at, i.definition_id, i.definition_version, i.quantity
        FROM pj_mail_messages m LEFT JOIN pj_inventory_items i ON i.item_instance_id = m.attached_item_id
        WHERE m.recipient_char_id = p_character AND m.claimed_at IS NULL AND m.expires_at > CURRENT_TIMESTAMP
            AND (p_cursor IS NULL OR (m.created_at, m.mail_id) < (cursor_at, p_cursor))
        ORDER BY m.created_at DESC, m.mail_id DESC LIMIT p_limit + 1
    ), visible AS (
        SELECT * FROM page ORDER BY created_at DESC, mail_id DESC LIMIT p_limit
    )
    SELECT COALESCE(jsonb_agg(to_jsonb(visible) ORDER BY created_at DESC, mail_id DESC), '[]'::JSONB),
        CASE WHEN (SELECT count(*) FROM page) > p_limit
            THEN (SELECT mail_id FROM visible ORDER BY created_at, mail_id LIMIT 1) END
        INTO records, next_cursor FROM visible;
    RETURN jsonb_build_object('status', 'OK', 'mail', records, 'next_cursor', next_cursor);
END;
$$;

CREATE OR REPLACE FUNCTION pj_claim_mail(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    recipient UUID := (p_req->>'character_id')::UUID;
    requested_mail UUID := (p_req->>'mail_id')::UUID;
    mint CONSTANT UUID := '00000001-0000-0000-0000-000000000001';
    escrow CONSTANT UUID := '00000003-0000-0000-0000-000000000003';
    funding UUID;
    letter pj_mail_messages%ROWTYPE;
    attachment pj_inventory_items%ROWTYPE;
    capacity INT;
    free_slot INT;
    funding_balance BIGINT;
    recipient_balance BIGINT;
    ledger_id UUID;
    previous JSONB;
    response JSONB;
BEGIN
    IF p_key IS NULL OR recipient IS NULL OR requested_mail IS NULL OR recipient IN
        (mint, escrow, '00000002-0000-0000-0000-000000000002'::UUID) THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    previous := pj_replay_command(p_key, 'mail_claim', p_req);
    SELECT inventory_capacity INTO capacity FROM pj_characters WHERE character_id = recipient
        AND session_epoch = (p_req->>'session_epoch')::BIGINT AND session_epoch > 0 FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    IF previous IS NOT NULL THEN RETURN previous; END IF;
    SELECT * INTO letter FROM pj_mail_messages WHERE mail_id = requested_mail
        AND recipient_char_id = recipient AND claimed_at IS NULL FOR UPDATE;
    IF NOT FOUND OR letter.expires_at <= clock_timestamp() THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;

    IF letter.attached_item_id IS NOT NULL THEN
        SELECT * INTO attachment FROM pj_inventory_items WHERE item_instance_id = letter.attached_item_id
            AND owner_character_id = recipient AND owner_scope = 'MAIL_ESCROW' FOR UPDATE;
        IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
        -- Include old rows with unassigned slot indices in capacity accounting.
        IF (SELECT count(*) FROM pj_inventory_items WHERE owner_character_id = recipient AND owner_scope = 'CHAR_INV') >= capacity THEN
            RETURN jsonb_build_object('status', 'INVENTORY_FULL');
        END IF;
        SELECT candidate INTO free_slot FROM generate_series(0, capacity - 1) AS candidate
            WHERE NOT EXISTS (SELECT 1 FROM pj_inventory_items WHERE owner_character_id = recipient
                AND owner_scope = 'CHAR_INV' AND slot_index = candidate)
            ORDER BY candidate LIMIT 1;
        IF NOT FOUND THEN RETURN jsonb_build_object('status', 'INVENTORY_FULL'); END IF;
    END IF;

    IF letter.attached_amount > 0 THEN
        IF letter.attached_currency IS NULL THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
        IF letter.sender_label = 'Auction' AND letter.subject_key = 'MAIL_AUCTION_SOLD'
            AND letter.body_params_json->>'escrow_account_id' = escrow::TEXT THEN
            funding := escrow;
        ELSIF letter.sender_label = 'FieldBoss' AND letter.subject_key = 'MAIL_FIELDBOSS_REWARD' THEN
            funding := mint;
        ELSE
            RETURN jsonb_build_object('status', 'INVALID');
        END IF;
        INSERT INTO pj_currency_balances(owner_id, currency_code)
            SELECT owner, letter.attached_currency FROM unnest(ARRAY[recipient, funding]) AS owner ORDER BY owner
            ON CONFLICT DO NOTHING;
        PERFORM 1 FROM pj_currency_balances WHERE currency_code = letter.attached_currency
            AND owner_id IN (recipient, funding) ORDER BY owner_id FOR UPDATE;
        SELECT balance INTO funding_balance FROM pj_currency_balances
            WHERE owner_id = funding AND currency_code = letter.attached_currency;
        IF funding = escrow AND funding_balance < letter.attached_amount THEN
            RETURN jsonb_build_object('status', 'INSUFFICIENT_ESCROW');
        END IF;
        IF letter.expires_at <= clock_timestamp() THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
        UPDATE pj_currency_balances SET balance = balance - letter.attached_amount, revision = revision + 1
            WHERE owner_id = funding AND currency_code = letter.attached_currency RETURNING balance INTO funding_balance;
        UPDATE pj_currency_balances SET balance = balance + letter.attached_amount, revision = revision + 1
            WHERE owner_id = recipient AND currency_code = letter.attached_currency RETURNING balance INTO recipient_balance;
        ledger_id := gen_random_uuid();
        INSERT INTO pj_ledger_transactions(tx_id, idempotency_key, operation, metadata_json)
            VALUES (ledger_id, p_key, 'mail_claim', jsonb_build_object('mail_id', requested_mail));
        INSERT INTO pj_ledger_entries(tx_id, account_id, currency_code, delta_amount, balance_after) VALUES
            (ledger_id, funding, letter.attached_currency, -letter.attached_amount, funding_balance),
            (ledger_id, recipient, letter.attached_currency, letter.attached_amount, recipient_balance);
    END IF;
    IF letter.attached_item_id IS NOT NULL THEN
        UPDATE pj_inventory_items SET owner_scope = 'CHAR_INV', slot_index = free_slot,
            revision = revision + 1, updated_at = NOW() WHERE item_instance_id = attachment.item_instance_id;
        UPDATE pj_characters SET inventory_revision = inventory_revision + 1 WHERE character_id = recipient;
    END IF;
    UPDATE pj_mail_messages SET claimed_at = NOW() WHERE mail_id = requested_mail;
    response := jsonb_build_object('status', 'COMMITTED', 'mail_id', requested_mail,
        'item_instance_id', letter.attached_item_id, 'definition_id', attachment.definition_id,
        'quantity', attachment.quantity, 'slot_index', free_slot, 'enhance_level', attachment.enhance_level,
        'currency', letter.attached_currency, 'amount', letter.attached_amount::TEXT,
        'currency_balance', recipient_balance::TEXT);
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, 'mail_claim', 200, response, p_req);
    RETURN response;
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (5, 'Atomic mailbox claim, inventory ownership and double-entry funding') ON CONFLICT (version) DO NOTHING;
COMMIT;
