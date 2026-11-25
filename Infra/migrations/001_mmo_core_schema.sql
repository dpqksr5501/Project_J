-- Project J Core MMORPG Normalized Schema (Migration 001)
-- Enforces optimistic concurrency (revision), fencing tokens (session_epoch),
-- global single-owner item instances, double-entry currency ledger, and idempotency receipts.

CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS pj_schema_migrations (
    version         INT PRIMARY KEY,
    description     TEXT NOT NULL,
    applied_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 1. Accounts & Characters
CREATE TABLE IF NOT EXISTS pj_accounts (
    account_id      UUID PRIMARY KEY,
    external_sub    TEXT UNIQUE,
    status          TEXT NOT NULL DEFAULT 'ACTIVE',
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS pj_characters (
    character_id    UUID PRIMARY KEY,
    account_id      UUID NOT NULL REFERENCES pj_accounts(account_id),
    char_name       VARCHAR(32) NOT NULL UNIQUE,
    class_id        VARCHAR(64) NOT NULL,
    level           INT NOT NULL DEFAULT 1 CHECK (level >= 1),
    xp              BIGINT NOT NULL DEFAULT 0 CHECK (xp >= 0),
    last_zone_id    VARCHAR(64) NOT NULL DEFAULT 'MainWorld',
    last_channel_id INT NOT NULL DEFAULT 1,
    pos_x           DOUBLE PRECISION NOT NULL DEFAULT 0,
    pos_y           DOUBLE PRECISION NOT NULL DEFAULT 0,
    pos_z           DOUBLE PRECISION NOT NULL DEFAULT 100,
    yaw             REAL NOT NULL DEFAULT 0,
    revision        BIGINT NOT NULL DEFAULT 1,
    session_epoch   BIGINT NOT NULL DEFAULT 0,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_pj_characters_account ON pj_characters(account_id);

-- 2. Single-Owner Global Item Instances (Dupe-proof by primary key + owner_scope)
CREATE TABLE IF NOT EXISTS pj_inventory_items (
    item_instance_id    UUID PRIMARY KEY,
    owner_character_id  UUID REFERENCES pj_characters(character_id),
    owner_scope         VARCHAR(24) NOT NULL CHECK (
                            owner_scope IN ('CHAR_INV', 'CHAR_EQUIP', 'AUCTION_ESCROW', 'MAIL_ESCROW', 'GUILD_BANK', 'DESTROYED')
                        ),
    definition_id       VARCHAR(64) NOT NULL,
    quantity            INT NOT NULL DEFAULT 1 CHECK (quantity > 0),
    slot_index          INT NOT NULL DEFAULT -1,
    enhance_level       INT NOT NULL DEFAULT 0 CHECK (enhance_level >= 0),
    pity_counter        INT NOT NULL DEFAULT 0 CHECK (pity_counter >= 0),
    rolls_json          JSONB NOT NULL DEFAULT '{}'::jsonb,
    revision            BIGINT NOT NULL DEFAULT 1,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_pj_items_owner_scope ON pj_inventory_items(owner_character_id, owner_scope);

-- 3. Double-Entry Currency Ledger (SUM(delta_amount) == 0 per tx_id)
CREATE TABLE IF NOT EXISTS pj_currency_balances (
    owner_id        UUID NOT NULL,
    currency_code   VARCHAR(16) NOT NULL,
    balance         BIGINT NOT NULL DEFAULT 0 CHECK (balance >= 0),
    revision        BIGINT NOT NULL DEFAULT 1,
    PRIMARY KEY (owner_id, currency_code)
);

CREATE TABLE IF NOT EXISTS pj_ledger_transactions (
    tx_id           UUID PRIMARY KEY,
    idempotency_key UUID NOT NULL UNIQUE,
    operation       VARCHAR(64) NOT NULL,
    metadata_json   JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS pj_ledger_entries (
    entry_id        BIGSERIAL PRIMARY KEY,
    tx_id           UUID NOT NULL REFERENCES pj_ledger_transactions(tx_id),
    account_id      UUID NOT NULL,
    currency_code   VARCHAR(16) NOT NULL,
    delta_amount    BIGINT NOT NULL CHECK (delta_amount <> 0),
    balance_after   BIGINT NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_pj_ledger_entries_tx ON pj_ledger_entries(tx_id);

-- 4. Idempotency Receipts & Transactional Outbox
CREATE TABLE IF NOT EXISTS pj_idempotency_receipts (
    idempotency_key UUID PRIMARY KEY,
    operation       VARCHAR(64) NOT NULL,
    status_code     INT NOT NULL,
    response_json   JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at      TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '7 days')
);

CREATE TABLE IF NOT EXISTS pj_outbox_events (
    event_id        UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    aggregate_key   VARCHAR(128) NOT NULL,
    aggregate_rev   BIGINT NOT NULL,
    event_type      VARCHAR(64) NOT NULL,
    payload_json    JSONB NOT NULL,
    published_at    TIMESTAMPTZ,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_pj_outbox_unpublished ON pj_outbox_events(created_at) WHERE published_at IS NULL;

-- 5. Quests (Active States + Archived Pages + UTC Repeating Period Claims)
CREATE TABLE IF NOT EXISTS pj_quest_states (
    character_id    UUID NOT NULL REFERENCES pj_characters(character_id),
    quest_id        VARCHAR(64) NOT NULL,
    state           VARCHAR(16) NOT NULL,
    progress_json   JSONB NOT NULL DEFAULT '{}'::jsonb,
    revision        BIGINT NOT NULL DEFAULT 1,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, quest_id)
);

CREATE TABLE IF NOT EXISTS pj_quest_reset_claims (
    character_id    UUID NOT NULL REFERENCES pj_characters(character_id),
    quest_id        VARCHAR(64) NOT NULL,
    period_key      VARCHAR(24) NOT NULL, -- e.g. '2026-11-23' or '2026-W47'
    claimed_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (character_id, quest_id, period_key)
);

-- 6. Mail & Auction House
CREATE TABLE IF NOT EXISTS pj_mail_messages (
    mail_id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    recipient_char_id   UUID NOT NULL REFERENCES pj_characters(character_id),
    sender_char_id      UUID,
    sender_label        VARCHAR(64) NOT NULL,
    subject_key         VARCHAR(128) NOT NULL,
    body_params_json    JSONB NOT NULL DEFAULT '{}'::jsonb,
    attached_currency   VARCHAR(16),
    attached_amount     BIGINT NOT NULL DEFAULT 0 CHECK (attached_amount >= 0),
    attached_item_id    UUID REFERENCES pj_inventory_items(item_instance_id),
    claimed_at          TIMESTAMPTZ,
    expires_at          TIMESTAMPTZ NOT NULL DEFAULT (NOW() + INTERVAL '30 days'),
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_pj_mail_recipient ON pj_mail_messages(recipient_char_id, created_at DESC);

CREATE TABLE IF NOT EXISTS pj_auction_listings (
    listing_id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    seller_char_id      UUID NOT NULL REFERENCES pj_characters(character_id),
    item_instance_id    UUID NOT NULL UNIQUE REFERENCES pj_inventory_items(item_instance_id),
    definition_id       VARCHAR(64) NOT NULL,
    enhance_level       INT NOT NULL DEFAULT 0,
    currency_code       VARCHAR(16) NOT NULL DEFAULT 'GOLD',
    buyout_price        BIGINT NOT NULL CHECK (buyout_price > 0),
    status              VARCHAR(16) NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'SOLD', 'CANCELLED', 'EXPIRED')),
    expires_at          TIMESTAMPTZ NOT NULL,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_pj_auction_search ON pj_auction_listings(status, definition_id, enhance_level, buyout_price)
    WHERE status = 'ACTIVE';

-- 7. Persistent Guilds, Guild Bank & Social
CREATE TABLE IF NOT EXISTS pj_guilds (
    guild_id        UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    guild_name      VARCHAR(32) NOT NULL UNIQUE,
    master_char_id  UUID NOT NULL REFERENCES pj_characters(character_id),
    notice_text     TEXT NOT NULL DEFAULT '',
    bank_revision   BIGINT NOT NULL DEFAULT 1,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS pj_guild_members (
    guild_id        UUID NOT NULL REFERENCES pj_guilds(guild_id) ON DELETE CASCADE,
    character_id    UUID NOT NULL UNIQUE REFERENCES pj_characters(character_id),
    rank_tier       INT NOT NULL DEFAULT 1, -- 0=Initiate, 1=Member, 2=Officer, 3=Master
    daily_withdraws INT NOT NULL DEFAULT 0,
    withdraw_day    DATE NOT NULL DEFAULT CURRENT_DATE,
    joined_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (guild_id, character_id)
);

CREATE TABLE IF NOT EXISTS pj_guild_bank_items (
    guild_id            UUID NOT NULL REFERENCES pj_guilds(guild_id) ON DELETE CASCADE,
    slot_index          INT NOT NULL CHECK (slot_index >= 0 AND slot_index < 256),
    item_instance_id    UUID NOT NULL UNIQUE REFERENCES pj_inventory_items(item_instance_id),
    revision            BIGINT NOT NULL DEFAULT 1,
    PRIMARY KEY (guild_id, slot_index)
);

CREATE TABLE IF NOT EXISTS pj_guild_bank_logs (
    log_id          BIGSERIAL PRIMARY KEY,
    guild_id        UUID NOT NULL REFERENCES pj_guilds(guild_id) ON DELETE CASCADE,
    actor_char_id   UUID NOT NULL,
    action          VARCHAR(16) NOT NULL CHECK (action IN ('DEPOSIT', 'WITHDRAW')),
    definition_id   VARCHAR(64) NOT NULL,
    quantity        INT NOT NULL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS pj_social_relations (
    owner_char_id   UUID NOT NULL REFERENCES pj_characters(character_id),
    target_char_id  UUID NOT NULL REFERENCES pj_characters(character_id),
    relation_type   VARCHAR(16) NOT NULL CHECK (relation_type IN ('FRIEND_PENDING', 'FRIEND', 'BLOCKED')),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (owner_char_id, target_char_id)
);

-- 8. Dungeon Instances & Open-World Channel Registry
CREATE TABLE IF NOT EXISTS pj_dungeon_instances (
    instance_id     UUID PRIMARY KEY,
    dungeon_id      VARCHAR(64) NOT NULL,
    server_endpoint VARCHAR(128) NOT NULL,
    state           VARCHAR(16) NOT NULL DEFAULT 'RESERVED',
    allowed_chars   JSONB NOT NULL DEFAULT '[]'::jsonb,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at      TIMESTAMPTZ NOT NULL
);

INSERT INTO pj_schema_migrations(version, description)
VALUES (1, 'Initial normalized MMORPG core schema')
ON CONFLICT (version) DO NOTHING;
