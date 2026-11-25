// Internal Project J commands. Nakama authenticates the Runtime HTTP Key
// before dispatch and these handlers explicitly reject user sessions.
function InitModule(ctx, logger, nk, initializer) {
    initializer.registerRpc("j_health", rpcHealth);
    initializer.registerRpc("j_profile_acquire_session", rpcAcquireSession);
    initializer.registerRpc("j_profile_save", rpcSaveProfile);
    initializer.registerRpc("j_auction_buyout", rpcAuctionBuyout);
    initializer.registerRpc("j_reward_boss", rpcBossReward);
    initializer.registerRpc("j_mail_inbox", rpcMailInbox);
    initializer.registerRpc("j_mail_claim", rpcClaimMail);
    initializer.registerRpc("j_world_login_ticket", rpcWorldLoginTicket);
    initializer.registerRpc("j_world_redeem_login", rpcWorldRedeemLogin);
    initializer.registerRpc("j_world_channel_heartbeat", rpcWorldChannelHeartbeat);
    initializer.registerRpc("j_world_channel_list", rpcWorldChannelList);
    initializer.registerRpc("j_world_inventory", rpcWorldInventory);
    initializer.registerRpc("j_world_bag_edit", rpcWorldBagEdit);
    initializer.registerRpc("j_world_equipment_edit", rpcWorldEquipmentEdit);
    initializer.registerRpc("j_world_craft_commit", rpcWorldCraftCommit);
    initializer.registerRpc("j_world_quest_edit", rpcWorldQuestEdit);
    initializer.registerRpc("j_world_quest_progress", rpcWorldQuestProgress);
    initializer.registerRpc("j_dungeon_clear", rpcDungeonClear);
    initializer.registerRpc("j_dungeon_allocate", rpcDungeonAllocate);
    initializer.registerRpc("j_dungeon_status", rpcDungeonStatus);
    initializer.registerRpc("j_dungeon_heartbeat", rpcDungeonHeartbeat);
    initializer.registerRpc("j_dungeon_worker_admission", rpcDungeonWorkerAdmission);
    initializer.registerRpc("j_dungeon_acknowledge", rpcDungeonAcknowledge);
    logger.info("Project J Nakama MMO module initialized.");
}

function rpcHealth(ctx, logger, nk, payload) {
    return JSON.stringify({ ok: true, service: "project_j_nakama", ts: Date.now() });
}

function readServerRequest(ctx, nk, payload) {
    if (ctx.userId) throw Error("Server RPC requires runtime authentication");
    if (!payload || payload.length > 65536) throw Error("Invalid command payload");
    var input = JSON.parse(payload);
    if (!input || typeof input !== "object" || Array.isArray(input)) throw Error("Invalid command payload");
    if (Object.prototype.hasOwnProperty.call(input, "payload_b64")) {
        if (input.schema_version !== 1 || typeof input.payload_b64 !== "string") throw Error("Unsupported command schema");
        var intent = JSON.parse(nk.binaryToString(nk.base64Decode(input.payload_b64)));
        if (!intent || typeof intent !== "object" || Array.isArray(intent)
            || Object.prototype.hasOwnProperty.call(intent, "idempotency_key")) throw Error("Invalid command intent");
        intent.idempotency_key = input.idempotency_key;
        return intent;
    }
    return input;
}

function readId(input) {
    if (typeof input !== "string" || !/^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(input)
        || /^0{8}(-0{4}){3}-0{12}$/.test(input)) throw Error("Invalid identifier");
    return input.toLowerCase();
}

function readRevision(input) {
    if (typeof input === "number" && Number.isSafeInteger(input)) input = String(input);
    if (typeof input !== "string" || !/^[1-9][0-9]{0,18}$/.test(input)
        || (input.length === 19 && input > "9223372036854775807")) throw Error("Invalid revision or epoch");
    return input;
}

function runCommand(nk, sql, args) {
    var rows = nk.sqlQuery(sql, args);
    if (!rows || rows.length !== 1 || typeof rows[0].result !== "string") throw Error("Missing command result");
    return rows[0].result;
}

function rpcAcquireSession(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var rows = nk.sqlQuery(
        "UPDATE pj_characters SET session_epoch = session_epoch + 1, updated_at = NOW() " +
        "WHERE character_id = $1::uuid AND account_id = $2::uuid " +
        "RETURNING character_id, class_id, level, xp::text AS xp, last_zone_id, last_channel_id, pos_x, pos_y, pos_z, yaw, " +
        "revision::text AS revision, session_epoch::text AS session_epoch",
        [readId(req.character_id), readId(req.account_id)]
    );
    return JSON.stringify(rows && rows.length === 1
        ? { status: "OK", character: rows[0] } : { status: "REJECTED" });
}

function rpcSaveProfile(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var key = readId(req.idempotency_key);
    if (!Number.isInteger(req.level) || req.level < 1 || req.level > 2147483647
        || typeof req.last_zone_id !== "string" || !req.last_zone_id || req.last_zone_id.length > 64
        || !Number.isInteger(req.last_channel_id) || req.last_channel_id < 1 || req.last_channel_id > 2147483647
        || ![req.pos_x, req.pos_y, req.pos_z, req.yaw].every(function (value) {
            return Number.isFinite(value) && Math.abs(value) <= 1000000000;
        })) throw Error("Invalid profile state");
    var intent = {
        character_id: readId(req.character_id),
        session_epoch: readRevision(req.session_epoch),
        expected_revision: readRevision(req.expected_revision),
        level: req.level,
        xp: req.xp === 0 || req.xp === "0" ? "0" : readRevision(req.xp),
        last_zone_id: req.last_zone_id,
        last_channel_id: req.last_channel_id,
        pos_x: req.pos_x, pos_y: req.pos_y, pos_z: req.pos_z, yaw: req.yaw
    };
    return runCommand(nk, "SELECT pj_save_profile($1::uuid, $2::jsonb)::text AS result", [key, JSON.stringify(intent)]);
}

function rpcAuctionBuyout(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var intent = {
        listing_id: readId(req.listing_id),
        buyer_char_id: readId(req.buyer_char_id),
        session_epoch: readRevision(req.session_epoch),
        expected_revision: readRevision(req.expected_revision)
    };
    return runCommand(nk, "SELECT pj_auction_buyout($1::uuid, $2::jsonb)::text AS result",
        [readId(req.idempotency_key), JSON.stringify(intent)]);
}

function rpcDungeonAllocate(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var instanceId = readId(req.instance_id);
    if (req.idempotency_key && readId(req.idempotency_key) !== instanceId) throw Error("Mismatched reservation identifier");
    if (typeof req.dungeon_id !== "string" || typeof req.lifetime_seconds !== "number"
        || !Number.isInteger(req.lifetime_seconds) || !req.members || typeof req.members !== "object"
        || Array.isArray(req.members)) throw Error("Invalid dungeon reservation");
    var members = {};
    var characterIds = Object.keys(req.members);
    if (characterIds.length === 0 || characterIds.length > 40) throw Error("Invalid party size");
    characterIds.forEach(function (characterId) {
        var normalized = readId(characterId);
        if (Object.prototype.hasOwnProperty.call(members, normalized)) throw Error("Duplicate party member");
        members[normalized] = req.members[characterId];
    });
    var intent = {
        dungeon_id: req.dungeon_id,
        lifetime_seconds: req.lifetime_seconds,
        requester_char_id: readId(req.requester_char_id),
        session_epoch: readRevision(req.session_epoch),
        members: members
    };
    return runCommand(nk, "SELECT pj_dungeon_allocate($1::uuid, $2::jsonb)::text AS result", [instanceId, JSON.stringify(intent)]);
}

function rpcBossReward(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var defeatId = readId(req.defeat_id);
    var characterId = readId(req.character_id);
    if (!Array.isArray(req.items) || req.items.length > 40) throw Error("Invalid reward items");
    var items = req.items.map(function (item) {
        if (!item || typeof item.definition_id !== "string" || item.definition_id.length < 1 || item.definition_id.length > 64
            || !Number.isInteger(item.quantity) || item.quantity < 1 || item.quantity > 2147483647) throw Error("Invalid reward item");
        return { definition_id: item.definition_id, quantity: item.quantity };
    });
    var gold = req.gold === 0 || req.gold === "0" ? "0" : readRevision(req.gold);
    return runCommand(nk, "SELECT pj_grant_boss_reward($1::uuid, $2::uuid, $3::jsonb)::text AS result",
        [defeatId, characterId, JSON.stringify({ gold: gold, items: items })]);
}

function rpcMailInbox(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var limit = req.limit === undefined ? 50 : req.limit;
    if (!Number.isInteger(limit) || limit < 1 || limit > 100) throw Error("Invalid mail page size");
    return runCommand(nk, "SELECT pj_mail_inbox($1::uuid, $2::bigint, $3::uuid, $4::integer)::text AS result",
        [readId(req.character_id), readRevision(req.session_epoch), req.cursor == null ? null : readId(req.cursor), limit]);
}

function rpcClaimMail(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var intent = {
        character_id: readId(req.character_id),
        mail_id: readId(req.mail_id),
        session_epoch: readRevision(req.session_epoch)
    };
    return runCommand(nk, "SELECT pj_claim_mail($1::uuid, $2::jsonb)::text AS result",
        [readId(req.idempotency_key), JSON.stringify(intent)]);
}

function readWorldZone(value) {
    if (typeof value !== "string" || !/^[A-Za-z0-9_]{1,64}$/.test(value)) throw Error("Invalid world zone");
    return value;
}

function readWorldChannel(value) {
    if (!Number.isInteger(value) || value < 1 || value > 2147483647) throw Error("Invalid world channel");
    return value;
}

function rpcWorldLoginTicket(ctx, logger, nk, payload) {
    if (!ctx.userId || !payload || payload.length > 2048) throw Error("A Nakama player session is required");
    var req = JSON.parse(payload);
    return runCommand(nk, "SELECT pj_issue_world_login($1, $2::uuid, $3, $4)::text AS result",
        ["nakama:" + readId(ctx.userId), readId(req.character_id), readWorldZone(req.zone), readWorldChannel(req.channel)]);
}

function rpcWorldRedeemLogin(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    var intent = { ticket: readId(req.ticket), zone: readWorldZone(req.zone), channel: readWorldChannel(req.channel), worker: readId(req.worker) };
    if (req.return_character != null) intent.return_character = readId(req.return_character);
    if (req.dungeon_instance != null) intent.dungeon_instance = readId(req.dungeon_instance);
    if (req.return_nonce != null) {
        if (!Number.isFinite(req.return_expires) || req.return_expires <= 0 || !intent.return_character) throw Error("Invalid return expiry");
        intent.return_nonce = readId(req.return_nonce);
        intent.return_expires = req.return_expires;
        if (req.return_instance != null) intent.return_instance = readId(req.return_instance);
    }
    var result = runCommand(nk, "SELECT pj_redeem_world_login($1::jsonb)::text AS result", [JSON.stringify(intent)]);
    var reason = JSON.parse(result).reason;
    if (reason) logger.warn("World login rejected: %s", reason);
    return result;
}

function rpcWorldChannelHeartbeat(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (typeof req.endpoint !== "string" || !/^[A-Za-z0-9.\-:\[\]]{3,256}$/.test(req.endpoint)
        || !Number.isInteger(req.population) || req.population < 0 || req.population > 2147483647
        || !Number.isInteger(req.capacity) || req.capacity < 1 || req.capacity > 2147483647
        || typeof req.draining !== "boolean") throw Error("Invalid channel heartbeat");
    var intent = { zone: readWorldZone(req.zone), channel: readWorldChannel(req.channel), worker: readId(req.worker),
        endpoint: req.endpoint, population: req.population, capacity: req.capacity, draining: req.draining,
        is_dungeon: req.is_dungeon === true };
    return runCommand(nk, "SELECT pj_world_channel_heartbeat($1::jsonb)::text AS result", [JSON.stringify(intent)]);
}

function rpcWorldChannelList(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    return runCommand(nk, "SELECT pj_world_channel_list($1)::text AS result", [readWorldZone(req.zone)]);
}

function rpcWorldInventory(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    return runCommand(nk, "SELECT pj_online_inventory($1::uuid, $2::bigint)::text AS result",
        [readId(req.character_id), readRevision(req.session_epoch)]);
}

function rpcWorldBagEdit(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (["use", "split", "merge", "swap"].indexOf(req.edit) < 0) throw Error("Invalid bag command");
    var intent = { character_id: readId(req.character_id), session_epoch: readRevision(req.session_epoch),
        edit: req.edit, source: readId(req.source) };
    if (req.target != null) intent.target = readId(req.target);
    ["source_count", "target_count", "source_order", "target_order", "amount", "max_stack"].forEach(function (field) {
        if (!Number.isInteger(req[field]) || req[field] < -1 || req[field] > 2147483647) throw Error("Invalid bag count or order");
        intent[field] = req[field];
    });
    return runCommand(nk, "SELECT pj_online_bag_edit($1::uuid, $2::jsonb)::text AS result",
        [readId(req.idempotency_key), JSON.stringify(intent)]);
}

function rpcDungeonStatus(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    return runCommand(nk, "SELECT (pj_dungeon_status($1::uuid) || COALESCE((" +
        "SELECT jsonb_build_object('zone', c.zone_id, 'channel', c.channel_id) " +
        "FROM pj_dungeon_instances i JOIN pj_world_channels c ON c.incarnation = i.worker_incarnation AND c.endpoint = i.server_endpoint " +
        "WHERE i.instance_id = $1::uuid AND c.is_dungeon AND NOT c.draining AND c.leased_until > clock_timestamp() LIMIT 1" +
        "), '{}'::jsonb))::text AS result", [readId(req.instance_id)]);
}

function rpcWorldEquipmentEdit(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (!Number.isInteger(req.slot) || req.slot < 1 || req.slot > 9 || typeof req.equip !== "boolean"
        || typeof req.definition !== "string" || req.definition.length < 1 || req.definition.length > 128
        || !Number.isInteger(req.version) || req.version < 1 || req.version > 2147483647) throw Error("Invalid equipment command");
    var intent = { character_id: readId(req.character_id), session_epoch: readRevision(req.session_epoch),
        item: readId(req.item), slot: req.slot, equip: req.equip, definition: req.definition, version: req.version };
    if (req.expected_slot_item != null) intent.expected_slot_item = readId(req.expected_slot_item);
    return runCommand(nk, "SELECT pj_online_equipment_edit($1::uuid, $2::jsonb)::text AS result",
        [readId(req.idempotency_key), JSON.stringify(intent)]);
}

function rpcWorldCraftCommit(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (["craft", "start", "collect", "cancel", "gather"].indexOf(req.kind) < 0
        || !Array.isArray(req.changes) || req.changes.length > 256) throw Error("Invalid craft command");
    var intent = { character_id: readId(req.character_id), session_epoch: readRevision(req.session_epoch),
        expected_revision: readRevision(req.expected_revision), kind: req.kind, changes: req.changes };
    if (req.kind === "start" || req.kind === "craft") {
        if (typeof req.recipe !== "string" || req.recipe.length < 1 || req.recipe.length > 64
            || !Number.isInteger(req.recipe_version) || req.recipe_version < 1 || req.recipe_version > 2147483647
            || !Number.isInteger(req.batches) || req.batches < 1 || req.batches > 100
            || typeof req.rules !== "string" || !/^[a-f0-9]{40}$/i.test(req.rules)) throw Error("Invalid recipe");
        intent.recipe = req.recipe;
        intent.recipe_version = req.recipe_version;
        intent.batches = req.batches;
        intent.rules = req.rules;
    }
    if (req.kind === "gather") {
        if (typeof req.gather_item !== "string" || req.gather_item.length < 1 || req.gather_item.length > 64
            || !Number.isInteger(req.gather_version) || req.gather_version < 1 || req.gather_version > 2147483647
            || !Number.isInteger(req.gather_count) || req.gather_count < 1 || req.gather_count > 1000000
            || (req.gather_event != null && (typeof req.gather_event !== "string" || req.gather_event.length > 64)))
            throw Error("Invalid gather reward");
        intent.gather_item = req.gather_item;
        intent.gather_version = req.gather_version;
        intent.gather_count = req.gather_count;
        if (req.gather_event != null) intent.gather_event = req.gather_event;
    } else if (req.kind !== "craft") intent.job_id = readId(req.job_id);
    if (req.kind === "start") intent.job_terms = req.job_terms;
    return runCommand(nk, "SELECT pj_online_craft_commit($1::uuid, $2::jsonb)::text AS result",
        [readId(req.idempotency_key), JSON.stringify(intent)]);
}

function rpcWorldQuestEdit(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (["accept", "track", "progress", "claim"].indexOf(req.kind) < 0 || (req.kind !== "progress"
        && (typeof req.quest !== "string" || req.quest.length < 1 || req.quest.length > 64))) throw Error("Invalid quest edit");
    var intent = { character_id: readId(req.character_id), session_epoch: readRevision(req.session_epoch),
        kind: req.kind, quest: req.quest };
    if (req.kind === "accept") intent.row = req.row;
    else if (req.kind === "track") intent.tracked = req.tracked;
    else intent.changes = req.changes;
    return runCommand(nk, "SELECT pj_online_quest_edit($1::uuid, $2::jsonb)::text AS result",
        [readId(req.idempotency_key), JSON.stringify(intent)]);
}

function rpcWorldQuestProgress(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (req.kind !== "progress") throw Error("Invalid earned quest progress");
    var intent = { character_id: readId(req.character_id), session_epoch: readRevision(req.session_epoch),
        kind: req.kind, earned_at: readRevision(req.earned_at), changes: req.changes };
    return runCommand(nk, "SELECT pj_online_earned_quest_progress($1::uuid, $2::jsonb)::text AS result",
        [readId(req.idempotency_key), JSON.stringify(intent)]);
}

function rpcDungeonClear(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (req.quest_event != null && (typeof req.quest_event !== "string" || req.quest_event.length > 64))
        throw Error("Invalid dungeon quest event");
    return runCommand(nk, "SELECT pj_dungeon_clear($1::uuid, $2::uuid, $3::jsonb)::text AS result",
        [readId(req.worker_id), readId(req.instance_id), JSON.stringify({ rewards: req.rewards, quest_event: req.quest_event })]);
}

function rpcDungeonHeartbeat(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    if (typeof req.server_endpoint !== "string" || typeof req.dungeon_id !== "string") throw Error("Invalid worker registration");
    return runCommand(nk, "SELECT jsonb_build_object('ok', pj_dungeon_heartbeat($1::uuid, $2, $3))::text AS result",
        [readId(req.worker_id), req.server_endpoint, req.dungeon_id]);
}

function rpcDungeonWorkerAdmission(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    return runCommand(nk, "SELECT pj_dungeon_worker_admission($1::uuid)::text AS result", [readId(req.worker_id)]);
}

function rpcDungeonAcknowledge(ctx, logger, nk, payload) {
    var req = readServerRequest(ctx, nk, payload);
    return runCommand(nk, "SELECT pj_dungeon_acknowledge($1::uuid, $2::uuid)::text AS result",
        [readId(req.worker_id), readId(req.instance_id)]);
}
