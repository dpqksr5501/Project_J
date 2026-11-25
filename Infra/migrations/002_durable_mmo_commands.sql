BEGIN;

ALTER TABLE pj_idempotency_receipts ADD COLUMN IF NOT EXISTS request_json JSONB;
ALTER TABLE pj_auction_listings ADD COLUMN IF NOT EXISTS revision BIGINT NOT NULL DEFAULT 1;

-- The lock is shared across command types because an idempotency key has one owner.
-- A lock hash collision only delays another key. Receipt identity remains the full UUID.
CREATE OR REPLACE FUNCTION pj_replay_command(p_key UUID, p_operation TEXT, p_request JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    receipt pj_idempotency_receipts%ROWTYPE;
BEGIN
    PERFORM pg_advisory_xact_lock(hashtextextended(p_key::TEXT, 0));
    SELECT * INTO receipt FROM pj_idempotency_receipts WHERE idempotency_key = p_key;
    IF NOT FOUND THEN RETURN NULL; END IF;
    IF receipt.operation <> p_operation OR receipt.request_json IS DISTINCT FROM p_request THEN
        RETURN jsonb_build_object('status', 'KEY_REUSED');
    END IF;
    RETURN jsonb_build_object('status', 'REPLAYED', 'receipt', receipt.response_json);
END;
$$;

CREATE OR REPLACE FUNCTION pj_save_profile(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    previous JSONB;
    committed_revision BIGINT;
    response JSONB;
BEGIN
    previous := pj_replay_command(p_key, 'profile_save', p_req);
    IF previous IS NOT NULL THEN RETURN previous; END IF;
    UPDATE pj_characters SET
        level = (p_req->>'level')::INT, xp = (p_req->>'xp')::BIGINT,
        last_zone_id = p_req->>'last_zone_id', last_channel_id = (p_req->>'last_channel_id')::INT,
        pos_x = (p_req->>'pos_x')::DOUBLE PRECISION, pos_y = (p_req->>'pos_y')::DOUBLE PRECISION,
        pos_z = (p_req->>'pos_z')::DOUBLE PRECISION, yaw = (p_req->>'yaw')::REAL,
        revision = revision + 1, updated_at = NOW()
    WHERE character_id = (p_req->>'character_id')::UUID
      AND session_epoch = (p_req->>'session_epoch')::BIGINT AND session_epoch > 0
      AND revision = (p_req->>'expected_revision')::BIGINT
    RETURNING revision INTO committed_revision;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    response := jsonb_build_object('status', 'COMMITTED', 'revision', committed_revision::TEXT);
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, 'profile_save', 200, response, p_req);
    RETURN response;
END;
$$;

CREATE OR REPLACE FUNCTION pj_auction_buyout(p_key UUID, p_req JSONB)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    buyer UUID := (p_req->>'buyer_char_id')::UUID;
    auction pj_auction_listings%ROWTYPE;
    sink CONSTANT UUID := '00000002-0000-0000-0000-000000000002';
    escrow CONSTANT UUID := '00000003-0000-0000-0000-000000000003';
    ledger_id UUID := gen_random_uuid();
    fee BIGINT;
    payout BIGINT;
    buyer_before BIGINT;
    buyer_after BIGINT;
    escrow_after BIGINT;
    sink_after BIGINT;
    previous JSONB;
    response JSONB;
BEGIN
    previous := pj_replay_command(p_key, 'auction_buyout', p_req);
    IF previous IS NOT NULL THEN RETURN previous; END IF;
    IF buyer IN (sink, escrow, '00000001-0000-0000-0000-000000000001'::UUID) THEN
        RETURN jsonb_build_object('status', 'CONFLICT');
    END IF;
    -- Lock the buyer's lease row so ownership cannot change during the write.
    PERFORM 1 FROM pj_characters WHERE character_id = buyer
        AND session_epoch = (p_req->>'session_epoch')::BIGINT AND session_epoch > 0 FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;
    SELECT * INTO auction FROM pj_auction_listings WHERE listing_id = (p_req->>'listing_id')::UUID
        AND status = 'ACTIVE' AND seller_char_id <> buyer
        AND revision = (p_req->>'expected_revision')::BIGINT FOR UPDATE;
    IF NOT FOUND OR auction.expires_at <= clock_timestamp() THEN
        RETURN jsonb_build_object('status', 'CONFLICT');
    END IF;
    PERFORM 1 FROM pj_inventory_items WHERE item_instance_id = auction.item_instance_id
        AND owner_character_id = auction.seller_char_id AND owner_scope = 'AUCTION_ESCROW' FOR UPDATE;
    IF NOT FOUND THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;

    fee := GREATEST(1, auction.buyout_price / 20);
    payout := auction.buyout_price - fee;
    -- Credit accounts are created and locked in UUID order across simultaneous purchases.
    INSERT INTO pj_currency_balances(owner_id, currency_code)
        VALUES (sink, auction.currency_code), (escrow, auction.currency_code) ON CONFLICT DO NOTHING;
    PERFORM 1 FROM pj_currency_balances WHERE currency_code = auction.currency_code
        AND owner_id IN (buyer, sink, escrow) ORDER BY owner_id FOR UPDATE;
    SELECT balance INTO buyer_before FROM pj_currency_balances
        WHERE owner_id = buyer AND currency_code = auction.currency_code;
    IF NOT FOUND OR buyer_before < auction.buyout_price THEN
        RETURN jsonb_build_object('status', 'INSUFFICIENT_FUNDS');
    END IF;
    IF auction.expires_at <= clock_timestamp() THEN RETURN jsonb_build_object('status', 'CONFLICT'); END IF;

    UPDATE pj_currency_balances SET balance = balance - auction.buyout_price, revision = revision + 1
        WHERE owner_id = buyer AND currency_code = auction.currency_code RETURNING balance INTO buyer_after;
    UPDATE pj_currency_balances SET balance = balance + fee, revision = revision + 1
        WHERE owner_id = sink AND currency_code = auction.currency_code RETURNING balance INTO sink_after;
    IF payout > 0 THEN
        UPDATE pj_currency_balances SET balance = balance + payout, revision = revision + 1
            WHERE owner_id = escrow AND currency_code = auction.currency_code RETURNING balance INTO escrow_after;
    END IF;
    UPDATE pj_auction_listings SET status = 'SOLD', revision = revision + 1 WHERE listing_id = auction.listing_id;
    UPDATE pj_inventory_items SET owner_character_id = buyer, owner_scope = 'MAIL_ESCROW',
        revision = revision + 1, updated_at = NOW() WHERE item_instance_id = auction.item_instance_id;

    INSERT INTO pj_ledger_transactions(tx_id, idempotency_key, operation, metadata_json)
        VALUES (ledger_id, p_key, 'auction_buyout', jsonb_build_object('listing_id', auction.listing_id));
    INSERT INTO pj_ledger_entries(tx_id, account_id, currency_code, delta_amount, balance_after) VALUES
        (ledger_id, buyer, auction.currency_code, -auction.buyout_price, buyer_after),
        (ledger_id, sink, auction.currency_code, fee, sink_after);
    IF payout > 0 THEN
        INSERT INTO pj_ledger_entries(tx_id, account_id, currency_code, delta_amount, balance_after)
            VALUES (ledger_id, escrow, auction.currency_code, payout, escrow_after);
    END IF;
    INSERT INTO pj_mail_messages(recipient_char_id, sender_label, subject_key, attached_item_id)
        VALUES (buyer, 'Auction', 'MAIL_AUCTION_WON', auction.item_instance_id);
    IF payout > 0 THEN
        INSERT INTO pj_mail_messages(recipient_char_id, sender_label, subject_key,
            body_params_json, attached_currency, attached_amount)
            VALUES (auction.seller_char_id, 'Auction', 'MAIL_AUCTION_SOLD',
                jsonb_build_object('escrow_account_id', escrow), auction.currency_code, payout);
    END IF;
    response := jsonb_build_object('status', 'COMMITTED', 'listing_id', auction.listing_id,
        'item_instance_id', auction.item_instance_id, 'price', auction.buyout_price::TEXT);
    INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, response_json, request_json)
        VALUES (p_key, 'auction_buyout', 200, response, p_req);
    RETURN response;
END;
$$;

INSERT INTO pj_schema_migrations(version, description)
    VALUES (2, 'Atomic profile save and auction buyout command receipts') ON CONFLICT (version) DO NOTHING;
COMMIT;
