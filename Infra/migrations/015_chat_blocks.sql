BEGIN;

ALTER TABLE pj_characters ADD COLUMN IF NOT EXISTS block_revision BIGINT NOT NULL DEFAULT 1 CHECK (block_revision > 0);

CREATE OR REPLACE FUNCTION pj_chat_block_snapshot(p_owner UUID) RETURNS JSONB
LANGUAGE SQL STABLE AS $$
    SELECT jsonb_build_object('block_revision', c.block_revision::text, 'blocked_characters',
        COALESCE((SELECT jsonb_agg(r.target_char_id ORDER BY r.target_char_id) FROM pj_social_relations r
            WHERE r.owner_char_id=c.character_id AND r.relation_type='BLOCKED'), '[]'::jsonb))
    FROM pj_characters c WHERE c.character_id=p_owner
$$;

-- Ticket redemption already holds the character row lock. Publish one consistent
-- lease and private chat projection, including on return travel to a new server.
CREATE OR REPLACE FUNCTION pj_redeem_world_login_with_social(p_req JSONB) RETURNS JSONB
LANGUAGE plpgsql AS $$
DECLARE reply JSONB;
BEGIN
    reply := pj_redeem_world_login(p_req);
    IF reply->>'status'='OK' THEN
        reply := reply || pj_chat_block_snapshot((reply->>'character')::uuid);
    END IF;
    RETURN reply;
END;
$$;

CREATE OR REPLACE FUNCTION pj_set_chat_block(p_owner UUID, p_epoch BIGINT, p_expected BIGINT, p_target UUID, p_block BOOLEAN)
RETURNS JSONB LANGUAGE plpgsql AS $$
DECLARE
    player pj_characters%ROWTYPE;
    already BOOLEAN;
    outcome TEXT := 'OK';
BEGIN
    IF p_owner IS NULL OR p_target IS NULL OR p_owner=p_target OR p_block IS NULL
        OR p_expected IS NULL OR p_expected<1 THEN RETURN jsonb_build_object('status','INVALID'); END IF;
    -- Reciprocal friend cleanup and simultaneous A->B / B->A changes use the same lock order.
    PERFORM 1 FROM pj_characters WHERE character_id IN (p_owner,p_target) ORDER BY character_id FOR UPDATE;
    SELECT * INTO player FROM pj_characters WHERE character_id=p_owner;
    IF NOT FOUND OR p_epoch IS NULL OR p_epoch<=0 OR player.session_epoch<>p_epoch THEN
        RETURN jsonb_build_object('status','STALE_SESSION');
    END IF;
    SELECT EXISTS(SELECT 1 FROM pj_social_relations WHERE owner_char_id=p_owner AND target_char_id=p_target
        AND relation_type='BLOCKED') INTO already;
    IF player.block_revision<>p_expected THEN
        outcome := 'CONFLICT';
    ELSIF NOT EXISTS (SELECT 1 FROM pj_characters WHERE character_id=p_target) THEN
        outcome := 'INVALID';
    ELSIF p_block AND NOT already AND (SELECT count(*) FROM pj_social_relations
        WHERE owner_char_id=p_owner AND relation_type='BLOCKED') >= 256 THEN
        outcome := 'FULL';
    ELSE
        IF p_block THEN
            INSERT INTO pj_social_relations(owner_char_id,target_char_id,relation_type)
                VALUES(p_owner,p_target,'BLOCKED') ON CONFLICT(owner_char_id,target_char_id) DO UPDATE
                SET relation_type='BLOCKED', updated_at=clock_timestamp() WHERE pj_social_relations.relation_type<>'BLOCKED';
            DELETE FROM pj_social_relations WHERE owner_char_id=p_target AND target_char_id=p_owner AND relation_type<>'BLOCKED';
        ELSE
            DELETE FROM pj_social_relations WHERE owner_char_id=p_owner AND target_char_id=p_target AND relation_type='BLOCKED';
        END IF;
        -- A successful no-op also fences an older timed-out command on the same lease.
        UPDATE pj_characters SET block_revision=block_revision+1 WHERE character_id=p_owner;
    END IF;
    RETURN jsonb_build_object('status',outcome) || pj_chat_block_snapshot(p_owner);
END;
$$;

INSERT INTO pj_schema_migrations(version,description) VALUES(15,'Lease-fenced private character chat block lists')
    ON CONFLICT(version) DO NOTHING;
COMMIT;
