BEGIN;

-- 1. Fix pj_grant_boss_reward: persist definition_version (default 1) instead of silently omitting it.
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
    def_version INT;
    response JSONB;
BEGIN
    IF p_defeat IS NULL OR p_recipient IS NULL OR gold IS NULL OR gold < 0
        OR jsonb_typeof(p_request->'items') IS DISTINCT FROM 'array' THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    IF jsonb_array_length(p_request->'items') > 40 THEN RETURN jsonb_build_object('status', 'INVALID'); END IF;
    FOR grant_item IN SELECT value FROM jsonb_array_elements(p_request->'items') LOOP
        IF COALESCE(length(grant_item->>'definition_id'), 0) NOT BETWEEN 1 AND 64
            OR COALESCE((grant_item->>'quantity')::INT, 0) <= 0
            OR COALESCE((grant_item->>'definition_version')::INT, 1) < 1 THEN
            RETURN jsonb_build_object('status', 'INVALID');
        END IF;
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
        def_version := COALESCE((grant_item->>'definition_version')::INT, 1);
        INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id, definition_version, quantity)
            VALUES (item_id, p_recipient, 'MAIL_ESCROW', grant_item->>'definition_id', def_version, (grant_item->>'quantity')::INT);
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

-- 2. Fix pj_claim_mail: permit Dungeon clear gold mail (sender_label='Dungeon', subject_key='MAIL_DUNGEON_CLEAR') funded from system mint.
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
        ELSIF (letter.sender_label = 'FieldBoss' AND letter.subject_key = 'MAIL_FIELDBOSS_REWARD')
            OR (letter.sender_label = 'Dungeon' AND letter.subject_key = 'MAIL_DUNGEON_CLEAR') THEN
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

-- 3. Fix pj_online_quest_edit: support 'claim' (ACTIVE -> CLAIMED) in addition to accept/track/progress.
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
    IF p_key IS NULL OR kind IS NULL OR kind NOT IN ('accept','track','progress','claim')
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
    IF NOT FOUND THEN RETURN pj_online_inventory(actor, epoch) || jsonb_build_object('status','STALE'); END IF;
    UPDATE pj_characters SET quest_revision = quest_revision + 1 WHERE character_id = actor;
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, 'online_quest_edit', 200, outcome, p_req);
    RETURN pj_online_inventory(actor, epoch) || outcome;
EXCEPTION WHEN check_violation OR unique_violation OR invalid_text_representation OR numeric_value_out_of_range THEN
    RETURN jsonb_build_object('status','CONFLICT');
END;
$$;

-- 4. Periodic maintenance sweep to bound receipt/ticket table growth.
CREATE OR REPLACE FUNCTION pj_maintenance_sweep(p_older_than INTERVAL DEFAULT INTERVAL '7 days')
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    pruned_receipts INT := 0;
    pruned_tickets INT := 0;
    pruned_returns INT := 0;
    sweep_at TIMESTAMPTZ := clock_timestamp();
BEGIN
    IF p_older_than IS NULL OR p_older_than < INTERVAL '0 seconds' THEN
        RETURN jsonb_build_object('status', 'INVALID');
    END IF;
    DELETE FROM pj_idempotency_receipts WHERE created_at < sweep_at - p_older_than AND expires_at < sweep_at;
    GET DIAGNOSTICS pruned_receipts = ROW_COUNT;
    DELETE FROM pj_world_login_tickets WHERE expires_at < sweep_at - INTERVAL '1 hour';
    GET DIAGNOSTICS pruned_tickets = ROW_COUNT;
    DELETE FROM pj_return_ticket_consumptions WHERE expires_at < sweep_at - INTERVAL '1 hour';
    GET DIAGNOSTICS pruned_returns = ROW_COUNT;
    RETURN jsonb_build_object(
        'status', 'OK',
        'pruned_receipts', pruned_receipts,
        'pruned_tickets', pruned_tickets,
        'pruned_returns', pruned_returns
    );
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (10, 'Dungeon gold mail claim, boss loot definition_version, quest claim, and maintenance sweep')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
