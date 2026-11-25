import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import vm from 'node:vm';

const [packageEntry, testDataPath] = process.argv.slice(2);
assert(packageEntry && testDataPath, 'Pass the PGlite module entry and a fresh data directory');
const { PGlite } = await import(pathToFileURL(packageEntry).href);
let db = new PGlite(testDataPath);
const migrations = new URL('../migrations/', import.meta.url);
for (const filename of (await readdir(migrations)).filter(name => /^\d+_.+\.sql$/.test(name)).sort()) {
    const sql = await readFile(new URL(filename, migrations), 'utf8');
    // gen_random_uuid is provided by PostgreSQL core. The WASM fixture does not ship pgcrypto.
    await db.exec(sql.replace(/^CREATE EXTENSION IF NOT EXISTS "pgcrypto";$/m, ''));
}

const id = (n) => '21a00000-0000-0000-0000-' + n.toString(16).padStart(12, '0');
const [account, buyer, seller, item, listing, worker, instance] = [1, 2, 3, 4, 5, 6, 7].map(id);
const exec = (query, values) => db.query(query, values);
const result = async (query, values) => (await exec(query, values)).rows[0].result;
await exec('INSERT INTO pj_accounts(account_id) VALUES ($1)', [account]);
await exec("INSERT INTO pj_characters(character_id, account_id, char_name, class_id, session_epoch) VALUES ($1, $3, 'Buyer', 'Test', 5), ($2, $3, 'Seller', 'Test', 2)", [buyer, seller, account]);
await exec("INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id) VALUES ($1, $2, 'AUCTION_ESCROW', 'TestItem')", [item, seller]);
await exec("INSERT INTO pj_auction_listings(listing_id, item_instance_id, seller_char_id, definition_id, buyout_price, expires_at) VALUES ($1, $2, $3, 'TestItem', 100, NOW() + INTERVAL '1 hour')", [listing, item, seller]);
await exec("INSERT INTO pj_currency_balances(owner_id, currency_code, balance) VALUES ($1, 'GOLD', 90)", [buyer]);

const buyout = (commandId, epoch = '5') => result('SELECT pj_auction_buyout($1, $2) AS result',
    [commandId, { listing_id: listing, buyer_char_id: buyer, expected_revision: '1', session_epoch: epoch }]);
assert.equal((await buyout(id(8), '4')).status, 'CONFLICT');
assert.equal((await buyout(id(8))).status, 'INSUFFICIENT_FUNDS');
assert.equal(await result('SELECT status AS result FROM pj_auction_listings WHERE listing_id = $1', [listing]), 'ACTIVE');
await exec("UPDATE pj_currency_balances SET balance = 130 WHERE owner_id = $1 AND currency_code = 'GOLD'", [buyer]);
// Inject a failure after the currency, ownership and listing update. The function statement must roll back everything.
await db.exec("ALTER TABLE pj_mail_messages ADD CONSTRAINT reject_test_auction_mail CHECK (subject_key <> 'MAIL_AUCTION_WON')");
await assert.rejects(buyout(id(8)), /reject_test_auction_mail/);
assert.equal(await result('SELECT balance::int AS result FROM pj_currency_balances WHERE owner_id = $1', [buyer]), 130);
assert.equal(await result('SELECT status AS result FROM pj_auction_listings WHERE listing_id = $1', [listing]), 'ACTIVE');
await db.exec('ALTER TABLE pj_mail_messages DROP CONSTRAINT reject_test_auction_mail');
assert.equal((await buyout(id(8))).status, 'COMMITTED');
assert.equal((await buyout(id(11))).status, 'CONFLICT');
assert.equal(await result('SELECT balance::int AS result FROM pj_currency_balances WHERE owner_id = $1', [buyer]), 30);
assert.equal(await result('SELECT sum(delta_amount)::int AS result FROM pj_ledger_entries', []), 0);
assert.equal(await result('SELECT count(*)::int AS result FROM pj_mail_messages', []), 2);
assert.equal(await result('SELECT owner_scope AS result FROM pj_inventory_items WHERE item_instance_id = $1', [item]), 'MAIL_ESCROW');

const profile = { character_id: buyer, session_epoch: '5', expected_revision: '1', level: 2, xp: '0',
    last_zone_id: 'TestZone', last_channel_id: 1, pos_x: 0, pos_y: 0, pos_z: 0, yaw: 0 };
const save = (intent, key = id(9)) => result('SELECT pj_save_profile($1, $2) AS result', [key, intent]);
assert.equal((await save(profile)).revision, '2');
assert.equal((await save({ ...profile, xp: '1' })).status, 'KEY_REUSED');
assert.equal((await save(profile, id(8))).status, 'KEY_REUSED');
assert.equal((await save(profile, id(10))).status, 'CONFLICT');

const dungeon = 'AuditMap';
const endpoint = 'dungeon.internal:7778';
const anchor = { zone_id: 'TestZone', channel_id: 1, location: [0, 100, 200], rotation: [0, 90, 0] };
const reservation = { dungeon_id: dungeon, requester_char_id: buyer, session_epoch: '5', lifetime_seconds: 600,
    members: { [buyer]: anchor, [seller]: anchor } };
const allocate = (instanceId, intent) => result('SELECT pj_dungeon_allocate($1, $2) AS result', [instanceId, intent]);
const status = (instanceId) => result('SELECT pj_dungeon_status($1) AS result', [instanceId]);
const acknowledge = (workerId, instanceId) => result('SELECT pj_dungeon_acknowledge($1, $2) AS result', [workerId, instanceId]);
const workerAdmission = (workerId) => result('SELECT pj_dungeon_worker_admission($1) AS result', [workerId]);
assert.equal((await allocate(instance, reservation)).status, 'UNAVAILABLE');
assert.equal(await result('SELECT pj_dungeon_heartbeat($1, $2, $3) AS result', [worker, endpoint, dungeon]), true);
const reserved = await allocate(instance, reservation);
assert.equal(reserved.status, 'RESERVED');
assert.equal(reserved.server_endpoint, null);
assert.equal((await allocate(id(12), reservation)).status, 'CONFLICT');
assert.equal((await allocate(instance, { ...reservation, lifetime_seconds: 300 })).status, 'KEY_REUSED');
assert.deepEqual((await result('SELECT pj_dungeon_worker_admission($1) AS result', [worker])).members[buyer], anchor);
assert.equal((await acknowledge(id(13), instance)).status, 'UNAVAILABLE');
assert.equal((await acknowledge(worker, instance)).server_endpoint, endpoint);
assert.equal((await acknowledge(worker, instance)).status, 'READY');

const reward = { gold: '500', items: [{ definition_id: 'TestReward', quantity: 2 }] };
const award = (intent) => result('SELECT pj_grant_boss_reward($1, $2, $3) AS result', [id(15), buyer, intent]);
await db.exec("ALTER TABLE pj_mail_messages ADD CONSTRAINT reject_test_gold_mail CHECK (attached_amount <> 500)");
await assert.rejects(award(reward), /reject_test_gold_mail/);
assert.equal(await result("SELECT count(*)::int AS result FROM pj_inventory_items WHERE definition_id = 'TestReward'", []), 0);
await db.exec('ALTER TABLE pj_mail_messages DROP CONSTRAINT reject_test_gold_mail');
const published = await award(reward);
assert.equal(published.status, 'COMMITTED');
assert.equal(published.mail_ids.length, 2);
assert.equal((await award({ ...reward, gold: '501' })).status, 'KEY_REUSED');

const claim = (key, mail, character = buyer, epoch = '5') => result('SELECT pj_claim_mail($1, $2) AS result',
    [key, { mail_id: mail, character_id: character, session_epoch: epoch }]);
const auctionWon = await result("SELECT mail_id AS result FROM pj_mail_messages WHERE subject_key = 'MAIL_AUCTION_WON'", []);
const auctionSold = await result("SELECT mail_id AS result FROM pj_mail_messages WHERE subject_key = 'MAIL_AUCTION_SOLD'", []);
await exec('UPDATE pj_characters SET inventory_capacity = 1 WHERE character_id = $1', [buyer]);
assert.equal((await claim(id(16), auctionWon, seller, '2')).status, 'CONFLICT');
assert.equal((await claim(id(16), auctionWon, buyer, '4')).status, 'CONFLICT');
const itemClaim = await claim(id(16), auctionWon);
assert.equal(itemClaim.status, 'COMMITTED');
assert.equal(itemClaim.item_instance_id, item);
assert.equal(itemClaim.slot_index, 0);
assert.equal((await claim(id(17), published.mail_ids[0])).status, 'INVENTORY_FULL');
assert.equal(await result('SELECT claimed_at AS result FROM pj_mail_messages WHERE mail_id = $1', [published.mail_ids[0]]), null);
await exec('UPDATE pj_characters SET inventory_capacity = 2 WHERE character_id = $1', [buyer]);
const rewardItem = await claim(id(17), published.mail_ids[0]);
assert.equal(rewardItem.quantity, 2);
assert.equal(rewardItem.slot_index, 1);
assert.equal((await claim(id(18), published.mail_ids[1])).currency_balance, '530');
assert.equal((await claim(id(19), auctionSold, seller, '2')).currency_balance, '95');
assert.equal(await result('SELECT sum(delta_amount)::int AS result FROM pj_ledger_entries', []), 0);
assert.equal(await result("SELECT balance::text AS result FROM pj_currency_balances WHERE owner_id = '00000003-0000-0000-0000-000000000003'", []), '0');

// Reopen the same backing files. The receipt, ownership and return anchor are all durable.
await db.close();
db = new PGlite(testDataPath);
assert.equal((await buyout(id(8))).status, 'REPLAYED');
assert.equal((await buyout(id(8), '4')).status, 'KEY_REUSED');
assert.equal((await save(profile)).receipt.revision, '2');
assert.equal(await result('SELECT count(*)::int AS result FROM pj_mail_messages', []), 4);
assert.deepEqual((await award(reward)).receipt.mail_ids, published.mail_ids);
assert.deepEqual((await claim(id(17), published.mail_ids[0])).receipt, rewardItem);
assert.equal((await claim(id(20), published.mail_ids[0])).status, 'CONFLICT');
assert.equal(await result('SELECT owner_scope AS result FROM pj_inventory_items WHERE item_instance_id = $1', [rewardItem.item_instance_id]), 'CHAR_INV');
assert.deepEqual(await result('SELECT return_anchor AS result FROM pj_dungeon_members WHERE instance_id = $1 AND character_id = $2',
    [instance, buyer]), anchor);
await exec("UPDATE pj_dungeon_workers SET leased_until = NOW() - INTERVAL '1 second' WHERE incarnation = $1", [worker]);
assert.equal((await status(instance)).status, 'UNAVAILABLE');
const replacementWorker = id(14);
assert.equal(await result('SELECT pj_dungeon_heartbeat($1, $2, $3) AS result', [replacementWorker, endpoint, dungeon]), true);
assert.equal((await acknowledge(worker, instance)).status, 'UNAVAILABLE');
assert.equal((await allocate(id(12), reservation)).status, 'RESERVED');
const principal = 'nakama:' + id(40);
const worldLease = { zone: 'TestZone', channel: 2, worker: id(41), endpoint: '127.0.0.1:7792', population: 0, capacity: 220, draining: false };
const pingWorld = (lease) => result('SELECT pj_world_channel_heartbeat($1) AS result', [lease]);
const worldLogin = (who) => result('SELECT pj_issue_world_login($1, $2, $3, $4) AS result', [who, buyer, worldLease.zone, worldLease.channel]);
const redeemWorld = (token, extra = {}) => result('SELECT pj_redeem_world_login_with_social($1) AS result', [{
    ticket: token, zone: worldLease.zone, channel: worldLease.channel, worker: worldLease.worker, ...extra }]);
await exec('UPDATE pj_accounts SET external_sub = $1 WHERE account_id = $2', [principal, account]);
assert.equal((await pingWorld(worldLease)).status, 'OK');
assert.equal((await pingWorld({ ...worldLease, worker: id(42) })).status, 'CONFLICT');
assert.equal((await worldLogin('nakama:' + id(43))).status, 'CONFLICT');
await exec("INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id) VALUES ($1, $2, 'CHAR_EQUIP', 'ExistingEquipment')", [id(49), buyer]);
let login = await worldLogin(principal);
assert.equal((await redeemWorld(login.ticket, { worker: id(42) })).status, 'CONFLICT');
await exec("UPDATE pj_world_channels SET leased_until = NOW() - INTERVAL '1 second' WHERE zone_id = $1 AND channel_id = $2",
    [worldLease.zone, worldLease.channel]);
assert.equal((await worldLogin(principal)).status, 'UNAVAILABLE');
assert.equal((await redeemWorld(login.ticket)).reason, 'CHANNEL_INACTIVE');
worldLease.worker = id(42);
assert.equal((await pingWorld(worldLease)).status, 'OK');
assert.equal((await redeemWorld(login.ticket)).reason, 'TICKET_OUTSIDE_LEASE');
login = await worldLogin(principal);
const loggedIn = await redeemWorld(login.ticket);
assert.equal(loggedIn.status, 'OK');
assert.equal(loggedIn.epoch, '6');
assert.deepEqual(loggedIn.blocked_characters, []);
const setBlock = (owner, epoch, rev, target, enabled) => result('SELECT pj_set_chat_block($1,$2,$3,$4,$5) AS result',
    [owner, epoch, rev, target, enabled]);
await exec("INSERT INTO pj_social_relations(owner_char_id,target_char_id,relation_type) VALUES($1,$2,'FRIEND_PENDING'),($2,$1,'FRIEND')", [buyer, seller]);
assert.equal((await setBlock(buyer, '5', '1', seller, true)).status, 'STALE_SESSION');
const mute = await setBlock(buyer, '6', '1', seller, true);
assert.deepEqual(mute.blocked_characters, [seller]);
assert.equal(mute.block_revision, '2');
assert.equal(await result('SELECT count(*)::int AS result FROM pj_social_relations WHERE owner_char_id=$1 AND target_char_id=$2', [seller,buyer]), 0);
assert.equal((await setBlock(buyer, '6', '1', seller, false)).status, 'CONFLICT');
assert.deepEqual((await setBlock(buyer, '6', '2', seller, false)).blocked_characters, []);
assert.equal((await setBlock(buyer, '6', '3', seller, true)).status, 'OK');
assert.deepEqual(loggedIn.items.map((row) => row.id).sort(), [item, rewardItem.item_instance_id].sort());
assert.equal((await redeemWorld(login.ticket)).status, 'CONFLICT');
assert.equal((await result('SELECT pj_online_inventory($1, $2) AS result', [buyer, '5'])).status, 'CONFLICT');
const returnProof = { return_nonce: id(44), return_expires: Date.now() / 1000 + 55, return_character: buyer };
login = await worldLogin(principal);
assert.equal((await redeemWorld(login.ticket, { ...returnProof, return_character: seller })).status, 'CONFLICT');
const returnedWithSocial = await redeemWorld(login.ticket, returnProof);
assert.equal(returnedWithSocial.status, 'OK');
assert.deepEqual(returnedWithSocial.blocked_characters, [seller]);
await db.close();
db = new PGlite(testDataPath);
assert.equal((await pingWorld(worldLease)).status, 'OK');
login = await worldLogin(principal);
assert.equal((await redeemWorld(login.ticket, returnProof)).status, 'CONFLICT');
assert.equal((await redeemWorld(login.ticket)).epoch, '8');
await exec('UPDATE pj_characters SET inventory_capacity = 4 WHERE character_id = $1', [buyer]);
const editBag = (key, edit) => result('SELECT pj_online_bag_edit($1, $2) AS result', [key,
    { character_id: buyer, session_epoch: '8', ...edit }]);
const split = { edit: 'split', source: rewardItem.item_instance_id, source_count: 2, source_order: 1, amount: 1, max_stack: 12 };
assert.equal((await editBag(id(45), { ...split, source_count: 6 })).status, 'CONFLICT');
const splitResult = await editBag(id(45), split);
assert.equal(splitResult.status, 'COMMITTED');
const newStack = splitResult.items.find((row) => row.order === 2);
assert.equal(newStack.count, 1);
assert.equal((await editBag(id(45), split)).status, 'REPLAYED');
assert.equal((await editBag(id(46), { edit: 'merge', source: newStack.id, source_count: 1, source_order: 2,
    target: rewardItem.item_instance_id, target_count: 1, target_order: 1, amount: 1, max_stack: 12 })).status, 'COMMITTED');
assert.equal((await editBag(id(47), { edit: 'swap', source: item, source_count: 1, source_order: 0,
    target: rewardItem.item_instance_id, target_count: 2, target_order: 1 })).items.find((row) => row.id === item).order, 1);
const consumed = await editBag(id(48), { edit: 'use', source: rewardItem.item_instance_id });
assert.equal(consumed.items.find((row) => row.id === rewardItem.item_instance_id).count, 1);
assert.equal((await editBag(id(48), { edit: 'use', source: rewardItem.item_instance_id })).items.find((row) => row.id === rewardItem.item_instance_id).count, 1);
const equipment = (key, intent) => result('SELECT pj_online_equipment_edit($1, $2) AS result', [key,
    { character_id: buyer, session_epoch: '8', definition: 'TestItem', version: 1, slot: 1, ...intent }]);
const equip = { item, equip: true };
assert.equal((await equipment(id(50), equip)).items.find(row => row.id === item).equipped_slot, 1);
assert.equal((await equipment(id(50), { ...equip, expected_slot_item: item })).status, 'REPLAYED');
assert.equal((await editBag(id(51), { edit: 'use', source: item })).status, 'CONFLICT');
assert.equal((await equipment(id(52), { item, equip: false, slot: 2 })).status, 'CONFLICT');
assert.equal((await equipment(id(52), { item, equip: false })).items.find(row => row.id === item).equipped_slot, 0);

const getBag = (epoch = '8') => result('SELECT pj_online_inventory($1, $2) AS result', [buyer, epoch]);
const objective = (reply, name) => reply.quests[0].objectives.find(goal => goal.id === name).count;
const questRow = { id: 'testcraft', count: 0, tracked: true, definition_version: 1, objectives: [
    { id: 'instant', count: 0, required: 3, recipe: 'InstantRecipe', version: 1 },
    { id: 'timed', count: 0, required: 3, recipe: 'TimedRecipe', version: 1 },
    { id: 'dungeon', count: 0, required: 1, event: 'Clear_Test_Dungeon' },
    { id: 'gather', count: 0, required: 3, gather: 'TestMaterial', version: 1 },
] };
const questIntent = { character_id: buyer, session_epoch: '8', kind: 'accept', quest: 'testcraft', row: questRow };
const editQuest = (key, intent) => result('SELECT pj_online_quest_edit($1, $2) AS result', [key, intent]);
assert.equal((await editQuest(id(68), questIntent)).status, 'COMMITTED');
assert.equal((await editQuest(id(68), questIntent)).status, 'REPLAYED');
const craft = (key, intent) => result('SELECT pj_online_craft_commit($1, $2) AS result', [key,
    { character_id: buyer, session_epoch: '8', ...intent }]);
const source = rewardItem.item_instance_id;
const sourceRow = (await getBag()).items.find(row => row.id === source);
const editItem = (row, change) => ({ id: row.id, definition: row.definition, version: row.version,
    count: row.count, level: row.level, bag_order: row.order, escrow: false, ...change });
const gather = { kind: 'gather', gather_item: 'TestMaterial', gather_version: 1, gather_count: 1,
    expected_revision: (await getBag()).inventory_revision,
    changes: [editItem({ id: id(69), definition: 'TestMaterial', version: 1, count: 1, level: 1, order: 3 }, {})] };
assert.equal(objective(await craft(id(70), gather), 'gather'), 1);
assert.equal(objective(await craft(id(70), gather), 'gather'), 1);
const genericProgress = { character_id: buyer, session_epoch: '8', kind: 'progress',
    changes: [{ row: questRow, deltas: [{ id: 'gather', amount: 1 }] }] };
assert.equal(objective(await editQuest(id(71), genericProgress), 'gather'), 2);
assert.equal(objective(await editQuest(id(71), genericProgress), 'gather'), 2);
const jobId = id(53);
const ticks = () => (BigInt(Date.now()) * 10000n + 621355968000000000n);
const startTicks = ticks();
const terms = { recipe: 'TimedRecipe', recipe_version: 1, batches: 1, rules: 'a'.repeat(40),
    started_ticks: startTicks.toString(), due_ticks: (startTicks + 300000000n).toString(),
    outputs: [{ definition: 'Result', version: 1, level: 1, count: 1, max_stack: 10 }] };
const start = { kind: 'start', job_id: jobId, expected_revision: (await getBag()).inventory_revision, job_terms: terms,
    recipe: terms.recipe, recipe_version: 1, batches: 1, rules: terms.rules, changes: [editItem(sourceRow, { escrow: true })] };
assert.equal((await craft(jobId, { ...start, expected_revision: '1' })).status, 'STALE');
const started = await craft(jobId, start);
assert.equal(started.status, 'COMMITTED');
assert.equal(objective(started, 'timed'), 0);
assert.equal(started.items.some(row => row.id === source), false);
assert.deepEqual(started.crafting_jobs[0].escrow.map(row => [row.id, row.scope]), [[source, 'CRAFT_ESCROW']]);
assert.equal((await craft(jobId, { ...start, job_terms: { ...terms, due_ticks: '42' } })).status, 'REPLAYED');
assert.equal((await editBag(id(54), { edit: 'use', source })).status, 'CONFLICT');
assert.equal((await craft(id(55), { kind: 'collect', job_id: jobId, expected_revision: started.inventory_revision, changes: [] })).status, 'CONFLICT');
assert.equal((await craft(id(56), { kind: 'cancel', job_id: jobId, expected_revision: started.inventory_revision,
    changes: [editItem(sourceRow, { count: 2 })] })).status, 'CONFLICT');
assert.equal((await getBag()).crafting_jobs.length, 1);
await db.close();
db = new PGlite(testDataPath);
const cancelled = await craft(id(56), { kind: 'cancel', job_id: jobId, expected_revision: started.inventory_revision,
    changes: [editItem(sourceRow, {})] });
assert.equal(cancelled.status, 'COMMITTED');
assert.equal(objective(cancelled, 'timed'), 0);
assert.equal(cancelled.crafting_jobs.length, 0);
assert.equal(cancelled.items.find(row => row.id === source).count, 1);
const nextJob = id(57);
const startedAgain = await craft(nextJob, { ...start, job_id: nextJob, expected_revision: cancelled.inventory_revision });
assert.equal(startedAgain.status, 'COMMITTED');
await exec('UPDATE pj_online_craft_jobs SET due_ticks = $1 WHERE job_id = $2', [(ticks() - 1n).toString(), nextJob]);
const product = editItem({ id: id(58), definition: 'Result', version: 1, count: 1, level: 1, order: sourceRow.order }, {});
const collected = await craft(id(59), { kind: 'collect', job_id: nextJob, expected_revision: startedAgain.inventory_revision, changes: [product] });
assert.equal(collected.status, 'COMMITTED');
assert.equal(objective(collected, 'timed'), 1);
assert.equal(collected.crafting_jobs.length, 0);
assert.equal(await result('SELECT owner_scope AS result FROM pj_inventory_items WHERE item_instance_id = $1', [source]), 'DESTROYED');
assert.equal(collected.items.find(row => row.id === product.id).count, 1);
const immediate = { kind: 'craft', recipe: 'InstantRecipe', recipe_version: 1, rules: terms.rules, batches: 1,
    expected_revision: collected.inventory_revision, changes: [{ ...product, count: 0 }, { ...product, id: id(60) }] };
assert.equal((await craft(id(61), immediate)).status, 'COMMITTED');
assert.equal((await craft(id(61), { ...immediate, changes: [{ ...product, id: id(62) }] })).status, 'REPLAYED');
assert.equal((await getBag()).items.some(row => row.id === id(62)), false);
assert.equal(objective(await getBag(), 'instant'), 1);

const dungeonWorker = id(63);
const instanceRoute = id(64);
const dungeonLease = { ...worldLease, channel: 3, worker: dungeonWorker, endpoint: '127.0.0.1:7793', is_dungeon: true };
assert.equal((await pingWorld(dungeonLease)).status, 'OK');
assert.equal((await result('SELECT pj_world_channel_list($1) AS result', [worldLease.zone])).channels.some(row => row.channel_id === 3), false);
assert.equal(await result('SELECT pj_dungeon_heartbeat($1, $2, $3) AS result', [dungeonWorker, dungeonLease.endpoint, 'GatewayDungeon']), true);
const dungeonLogin = () => result('SELECT pj_issue_world_login($1, $2, $3, $4) AS result', [principal, buyer, worldLease.zone, 3]);
await exec("UPDATE pj_dungeon_instances SET expires_at = NOW() - INTERVAL '1 second' WHERE instance_id = $1", [id(12)]);
const dungeonIntent = { ...reservation, dungeon_id: 'GatewayDungeon', session_epoch: '8',
    members: { [buyer]: { ...anchor, session_epoch: '8' } } };
assert.equal((await dungeonLogin()).status, 'UNAVAILABLE');
assert.equal((await allocate(instanceRoute, { ...dungeonIntent,
    members: { [buyer]: { ...anchor, session_epoch: '8' }, [seller]: { ...anchor, session_epoch: '1' } } })).status, 'CONFLICT');
const abandoned = id(67);
assert.equal((await allocate(abandoned, dungeonIntent)).status, 'RESERVED');
assert.equal((await acknowledge(dungeonWorker, abandoned)).status, 'READY');
const abandonedTicket = await dungeonLogin();
await exec("UPDATE pj_dungeon_instances SET join_until = NOW() - INTERVAL '1 second' WHERE instance_id = $1", [abandoned]);
assert.equal((await dungeonLogin()).status, 'UNAVAILABLE');
assert.equal((await result('SELECT pj_redeem_world_login($1) AS result', [{ ticket: abandonedTicket.ticket,
    zone: dungeonLease.zone, channel: 3, worker: dungeonWorker, return_character: buyer, dungeon_instance: abandoned }])).status, 'CONFLICT');
assert.equal((await allocate(instanceRoute, dungeonIntent)).status, 'RESERVED');
assert.equal((await dungeonLogin()).status, 'UNAVAILABLE');
assert.equal((await workerAdmission(dungeonWorker)).instance_id, instanceRoute);
assert.equal((await acknowledge(dungeonWorker, instanceRoute)).status, 'READY');
const dungeonTicket = await dungeonLogin();
const redeemDungeon = (proof) => result('SELECT pj_redeem_world_login($1) AS result', [{ ticket: dungeonTicket.ticket,
    zone: dungeonLease.zone, channel: 3, worker: dungeonWorker, return_character: buyer, ...proof }]);
assert.equal((await redeemDungeon({})).status, 'CONFLICT');
assert.equal((await redeemDungeon({ dungeon_instance: id(65) })).status, 'CONFLICT');
assert.equal((await redeemDungeon({ dungeon_instance: instanceRoute })).epoch, '9');
const finalReturn = await worldLogin(principal);
const finalProof = { return_nonce: id(66), return_expires: Date.now() / 1000 + 55, return_character: buyer, return_instance: instanceRoute };
assert.equal((await redeemWorld(finalReturn.ticket, finalProof)).reason, 'DUNGEON_NOT_CLEARED');
const clearDungeon = (workerId, details) => result('SELECT pj_dungeon_clear($1,$2,$3) AS result', [workerId, instanceRoute, details]);
const clear = { rewards: { [buyer]: { gold: '0', items: [{ definition_id: 'DungeonLoot', definition_version: 2, quantity: 1 }] } }, quest_event: 'Clear_Test_Dungeon' };
assert.equal((await clearDungeon(worldLease.worker, clear)).status, 'CONFLICT');
assert.equal((await clearDungeon(dungeonWorker, { ...clear, rewards: { [seller]: clear.rewards[buyer] } })).status, 'CONFLICT');
assert.equal((await clearDungeon(dungeonWorker, clear)).status, 'COMMITTED');
assert.equal((await clearDungeon(dungeonWorker, clear)).status, 'REPLAYED');
assert.equal(await result('SELECT count(*)::int AS result FROM pj_boss_reward_receipts WHERE defeat_id = $1', [instanceRoute]), 1);
assert.equal(await result("SELECT count(*)::int AS result FROM pj_mail_messages WHERE recipient_char_id = $1 AND subject_key = 'MAIL_DUNGEON_CLEAR'", [buyer]), 1);
assert.equal(await result("SELECT definition_version AS result FROM pj_inventory_items WHERE definition_id = 'DungeonLoot'", []), 2);
const returned = await redeemWorld(finalReturn.ticket, finalProof);
assert.equal(returned.epoch, '10');
assert.equal(objective(returned, 'dungeon'), 1);
assert.equal(objective(returned, 'instant'), 1);
assert.equal((await status(instanceRoute)).status, 'UNAVAILABLE');
assert.equal((await workerAdmission(dungeonWorker)).status, 'UNAVAILABLE');
// Exercise the 010 commands through their persisted state, including rejected requests and replay.
const dungeonGoldDefeat = id(80);
const grantDungeon = (intent) => result('SELECT pj_grant_boss_reward($1, $2, $3) AS result',
    [dungeonGoldDefeat, buyer, intent]);
const dungeonReward = { source: 'dungeon', gold: '250', items: [{ definition_id: 'DungeonLoot', quantity: 1, definition_version: 7 }] };
assert.equal((await grantDungeon({ ...dungeonReward, items: [{ ...dungeonReward.items[0], definition_version: 0 }] })).status, 'INVALID');
assert.equal(await result('SELECT count(*)::int AS result FROM pj_boss_reward_receipts WHERE defeat_id = $1', [dungeonGoldDefeat]), 0);
const dungeonGoldGrant = await grantDungeon(dungeonReward);
assert.equal(dungeonGoldGrant.status, 'COMMITTED');
assert.equal(dungeonGoldGrant.mail_ids.length, 2);
assert.equal(await result('SELECT i.definition_version AS result FROM pj_inventory_items i JOIN pj_mail_messages m ON m.attached_item_id = i.item_instance_id WHERE m.mail_id = $1', [dungeonGoldGrant.mail_ids[0]]), 7);
assert.equal((await grantDungeon(dungeonReward)).status, 'REPLAYED');
assert.equal((await grantDungeon({ ...dungeonReward, gold: '251' })).status, 'KEY_REUSED');
const goldClaim = { mail_id: dungeonGoldGrant.mail_ids[1], character_id: buyer, session_epoch: returned.epoch, bag_capacity: 120 };
const claimGold = (key, request = goldClaim) => result('SELECT pj_claim_mail($1, $2) AS result', [key, request]);
assert.equal((await claimGold(id(81), { ...goldClaim, mail_id: null })).status, 'INVALID');
assert.equal((await claimGold(id(81), { ...goldClaim, session_epoch: '9' })).status, 'CONFLICT');
const claimedDungeonGold = await claimGold(id(81));
assert.equal(claimedDungeonGold.status, 'COMMITTED');
assert.equal(Number(claimedDungeonGold.amount), 250);
assert.equal((await claimGold(id(81))).status, 'REPLAYED');
assert.equal((await claimGold(id(83))).status, 'CONFLICT');
assert.equal(await result("SELECT count(*)::int AS result FROM pj_ledger_entries e JOIN pj_ledger_transactions t USING (tx_id) WHERE t.idempotency_key = $1 AND e.account_id = '00000001-0000-0000-0000-000000000001' AND e.delta_amount = -250", [id(81)]), 1);
assert.equal(await result('SELECT sum(delta_amount)::int AS result FROM pj_ledger_entries', []), 0);
const questClaim = { character_id: buyer, session_epoch: returned.epoch, kind: 'claim', quest: questRow.id,
    definition_version: 1, expected_profile_revision: returned.profile_revision, reward_level: returned.level, reward_xp: returned.xp, reward_items: [] };
assert.equal((await editQuest(id(82), { ...questClaim, quest: 'Q_Missing' })).status, 'STALE');
assert.equal((await editQuest(id(82), questClaim)).status, 'STALE');
const completeQuest = { character_id: buyer, session_epoch: returned.epoch, kind: 'progress',
    changes: [{ row: questRow, deltas: [{ id: 'instant', amount: 3 }, { id: 'timed', amount: 3 }, { id: 'gather', amount: 3 }] }] };
assert.equal((await editQuest(id(84), completeQuest)).status, 'COMMITTED');
assert.equal((await editQuest(id(82), questClaim)).status, 'COMMITTED');
assert.equal((await editQuest(id(82), questClaim)).status, 'REPLAYED');
assert.equal((await editQuest(id(85), questClaim)).status, 'STALE');
assert.equal(await result('SELECT state AS result FROM pj_quest_states WHERE character_id = $1 AND quest_id = $2', [buyer, questRow.id]), 'CLAIMED');
await exec("INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, created_at, expires_at) VALUES ($1,'maintenance_fixture',200,NOW()-INTERVAL '8 days',NOW()-INTERVAL '1 day'),($2,'maintenance_fixture',200,NOW()-INTERVAL '8 days',NOW()+INTERVAL '1 day')", [id(86), id(87)]);
await exec("INSERT INTO pj_return_ticket_consumptions(nonce, expires_at) VALUES ($1,NOW()-INTERVAL '2 hours'),($2,NOW()+INTERVAL '1 hour')", [id(88), id(89)]);
await exec("INSERT INTO pj_world_login_tickets(ticket, character_id, zone_id, channel_id, incarnation, expires_at) VALUES ($1,$3,'sweep_old',1,$4,NOW()-INTERVAL '2 hours'),($2,$3,'sweep_current',1,$4,NOW()+INTERVAL '1 hour')", [id(90), id(91), buyer, id(92)]);
assert.equal((await result("SELECT pj_maintenance_sweep(INTERVAL '-1 seconds') AS result", [])).status, 'INVALID');
assert.equal(await result('SELECT count(*)::int AS result FROM pj_idempotency_receipts WHERE idempotency_key = $1', [id(86)]), 1);
const sweepRes = await result('SELECT pj_maintenance_sweep() AS result', []);
assert.equal(sweepRes.status, 'OK');
assert.ok(sweepRes.pruned_receipts >= 1 && sweepRes.pruned_tickets >= 1 && sweepRes.pruned_returns >= 1);
assert.equal(await result('SELECT count(*)::int AS result FROM pj_idempotency_receipts WHERE idempotency_key = $1', [id(87)]), 1);
assert.equal(await result('SELECT count(*)::int AS result FROM pj_world_login_tickets WHERE ticket = $1', [id(91)]), 1);
assert.equal(await result('SELECT count(*)::int AS result FROM pj_return_ticket_consumptions WHERE nonce = $1', [id(89)]), 1);
assert.deepEqual(await result('SELECT pj_maintenance_sweep() AS result', []),
    { status: 'OK', pruned_receipts: 0, pruned_tickets: 0, pruned_returns: 0 });
const sandbox = vm.createContext({});
vm.runInContext(await readFile(new URL('../nakama/modules/project_j_mmo.js', import.meta.url), 'utf8'), sandbox);
assert.throws(() => sandbox.rpcAuctionBuyout({ userId: buyer }, null, {}, '{}'), /requires runtime authentication/);
assert.throws(() => sandbox.rpcWorldEquipmentEdit({ userId: buyer }, null, {}, '{}'), /requires runtime authentication/);
assert.throws(() => sandbox.rpcWorldCraftCommit({ userId: buyer }, null, {}, '{}'), /requires runtime authentication/);
assert.throws(() => sandbox.rpcWorldQuestEdit({ userId: buyer }, null, {}, JSON.stringify(questClaim)), /requires runtime authentication/);
const wirePayload = Buffer.from(JSON.stringify(profile), 'utf8').toString('base64');
const wrappedResult = sandbox.rpcSaveProfile({}, null, {
    binaryToString: (bytes) => bytes.toString('utf8'),
    base64Decode: (data) => Buffer.from(data, 'base64'),
    sqlQuery: (sql, values) => {
        assert(sql.includes('pj_save_profile'));
        assert.equal(values[0], id(9));
        assert.deepEqual(JSON.parse(values[1]), profile);
        return [{ result: '{"status":"COMMITTED"}' }];
    }
}, JSON.stringify({ idempotency_key: id(9), schema_version: 1, payload_b64: wirePayload }));
assert.deepEqual(JSON.parse(wrappedResult), { status: 'COMMITTED' });

// Run the actual Nakama reward RPC's conversion, then execute exactly the
// statement and bound parameters it supplied, against the migrated SQL store.
const bossIntent = {
    defeat_id: id(8192), character_id: buyer, gold: '25',
    items: [{ definition_id: 'Wave3Loot', definition_version: 2, quantity: 2 }],
};
const rewardKey = id(8193);
function decodeRewardCommand(requestId, intent = bossIntent) {
    let command;
    const response = sandbox.rpcBossReward({}, null, {
        binaryToString: (bytes) => bytes.toString('utf8'),
        base64Decode: (data) => Buffer.from(data, 'base64'),
        sqlQuery: (sql, values) => {
            command = { sql, values: [...values] };
            return [{ result: '{}' }];
        },
    }, JSON.stringify({
        request_id: requestId, idempotency_key: rewardKey, schema_version: 1,
        payload_b64: Buffer.from(JSON.stringify(intent), 'utf8').toString('base64'),
    }));
    assert.equal(response, '{}');
    return command;
}
const rewardCommand = decodeRewardCommand(id(8194));
assert.deepEqual(rewardCommand.values.slice(0, 2), [bossIntent.defeat_id, buyer]);
assert.deepEqual(JSON.parse(rewardCommand.values[2]).items, bossIntent.items);
const legacyRewardCommand = decodeRewardCommand(id(8194), { ...bossIntent, items: [{ definition_id: 'Wave3Loot', quantity: 2 }] });
assert.deepEqual(JSON.parse(legacyRewardCommand.values[2]).items, [{ definition_id: 'Wave3Loot', quantity: 2 }]);
assert.throws(() => decodeRewardCommand(id(8194), { ...bossIntent, items: [{ ...bossIntent.items[0], definition_version: 0 }] }), /Invalid reward definition version/);
assert.deepEqual(decodeRewardCommand(id(8195)), rewardCommand);
const committedReward = JSON.parse(await result(rewardCommand.sql, rewardCommand.values));
assert.equal(committedReward.status, 'COMMITTED');
assert.equal(committedReward.mail_ids.length, 2);
assert.equal(await result('SELECT i.definition_version AS result FROM pj_inventory_items i JOIN pj_mail_messages m ON m.attached_item_id = i.item_instance_id WHERE m.mail_id = $1', [committedReward.mail_ids[0]]), 2);
await db.close();
db = new PGlite(testDataPath);
const replayedReward = JSON.parse(await result(rewardCommand.sql, rewardCommand.values));
assert.equal(replayedReward.status, 'REPLAYED');
assert.deepEqual(replayedReward.receipt.mail_ids, committedReward.mail_ids);
const changedReward = decodeRewardCommand(id(8196), { ...bossIntent, gold: '27' });
assert.equal(JSON.parse(await result(changedReward.sql, changedReward.values)).status, 'KEY_REUSED');
// A trusted earned event can arrive after the original character lease ends.
assert.throws(() => sandbox.rpcWorldQuestProgress({ userId: buyer }, null, {}, '{}'), /requires runtime authentication/);
const delayedQuest = { ...questRow, id: 'delayed_earned_progress' };
const earnedEvent = { character_id: buyer, session_epoch: returned.epoch, kind: 'progress',
    earned_at: String(Math.floor(Date.now() / 1000)),
    changes: [{ row: delayedQuest, deltas: [{ id: 'gather', amount: 1 }] }] };
function decodeEarnedCommand(key, intent = earnedEvent) {
    let command;
    sandbox.rpcWorldQuestProgress({}, null, {
        binaryToString: (bytes) => bytes.toString('utf8'),
        base64Decode: (data) => Buffer.from(data, 'base64'),
        sqlQuery: (sql, values) => {
            command = { sql, values: [...values] };
            return [{ result: '{}' }];
        },
    }, JSON.stringify({ idempotency_key: key, schema_version: 1,
        payload_b64: Buffer.from(JSON.stringify(intent), 'utf8').toString('base64') }));
    return command;
}
const earnedKey = id(8200);
const runEarned = async (key, intent = earnedEvent) => {
    const command = decodeEarnedCommand(key, intent);
    return JSON.parse(await result(command.sql, command.values));
};
await exec('UPDATE pj_characters SET session_epoch = session_epoch + 1 WHERE character_id = $1', [buyer]);
const liveEpoch = (BigInt(returned.epoch) + 1n).toString();
assert.equal((await editQuest(id(8201), earnedEvent)).status, 'CONFLICT');
const committedProgress = await runEarned(earnedKey);
assert.equal(committedProgress.status, 'COMMITTED');
assert.equal(committedProgress.quest_progress_epoch, liveEpoch);
const gatherCount = (reply) => reply.quests.find(row => row.id === delayedQuest.id).objectives.find(goal => goal.id === 'gather').count;
assert.equal(gatherCount(committedProgress), 1);
await db.close();
db = new PGlite(testDataPath);
assert.equal((await runEarned(earnedKey)).status, 'REPLAYED');
assert.equal(gatherCount(await runEarned(earnedKey)), 1);
assert.equal((await runEarned(earnedKey, { ...earnedEvent, session_epoch: liveEpoch })).status, 'KEY_REUSED');
assert.equal((await runEarned(id(8202), { ...earnedEvent, session_epoch: (BigInt(liveEpoch) + 1n).toString() })).status, 'CONFLICT');
assert.equal((await runEarned(id(8203), { ...earnedEvent, earned_at: String(Math.floor(Date.now() / 1000) - 7 * 86400) })).status, 'STALE');
assert.equal((await runEarned(id(8204), { ...earnedEvent,
    changes: [{ row: questRow, deltas: [{ id: 'gather', amount: 1 }] }] })).status, 'COMMITTED');
assert.equal(await result('SELECT state AS result FROM pj_quest_states WHERE character_id = $1 AND quest_id = $2', [buyer, questRow.id]), 'CLAIMED');
assert.equal(gatherCount(await runEarned(id(8205))), 2);

// Run the trusted Nakama command with an authored quest reward, including a
// partial stack merge that must be rolled back when there is no second slot.
const questOwner = id(13000);
const rewardStack = id(13001);
const occupiedSlot = id(13002);
await exec(`INSERT INTO pj_characters(character_id, account_id, char_name, class_id, session_epoch, level, xp, inventory_capacity)
    VALUES ($1, $2, 'QuestOwner', 'Test', 1, 5, 450, 2)`, [questOwner, account]);
await exec(`INSERT INTO pj_inventory_items(item_instance_id, owner_character_id, owner_scope, definition_id,
    definition_version, quantity, enhance_level, slot_index, equipped_slot)
    VALUES ($1,$3,'CHAR_INV','QuestReward',2,4,1,0,NULL), ($2,$3,'CHAR_INV','QuestReward',2,1,1,1,1)`,
    [rewardStack, occupiedSlot, questOwner]);
const travelQuest = { id: 'travel.migration013', count: 0, tracked: true, required: 1, definition_version: 2, objectives: [] };
const questOwnerIntent = { character_id: questOwner, session_epoch: '1' };
assert.equal((await editQuest(id(13003), { ...questOwnerIntent, kind: 'accept', quest: travelQuest.id, row: travelQuest })).status, 'COMMITTED');
assert.equal((await editQuest(id(13004), { ...questOwnerIntent, kind: 'progress',
    changes: [{ row: travelQuest, deltas: [{ id: '', amount: 1 }] }] })).status, 'COMMITTED');
const grantQuest = { ...questOwnerIntent, kind: 'claim', quest: travelQuest.id, definition_version: 2,
    expected_profile_revision: '1', reward_level: 6, reward_xp: '72',
    reward_items: [{ definition: 'QuestReward', version: 2, level: 1, quantity: 3, max_stack: 5 }] };
const questGrantKey = id(13005);
async function runQuestGrant(key, intent = grantQuest) {
    let command;
    sandbox.rpcWorldQuestEdit({}, null, {
        binaryToString: bytes => bytes.toString('utf8'),
        base64Decode: data => Buffer.from(data, 'base64'),
        sqlQuery: (sql, values) => {
            command = { sql, values: [...values] };
            return [{ result: '{}' }];
        },
    }, JSON.stringify({ idempotency_key: key, schema_version: 1,
        payload_b64: Buffer.from(JSON.stringify(intent), 'utf8').toString('base64') }));
    return JSON.parse(await result(command.sql, command.values));
}
const readQuestOwner = () => result('SELECT pj_online_inventory($1,1) AS result', [questOwner]);
const bagFull = await runQuestGrant(questGrantKey);
assert.equal(bagFull.status, 'INVENTORY_FULL');
assert.equal(bagFull.items.find(row => row.id === rewardStack).count, 4);
assert.equal(bagFull.items.find(row => row.id === occupiedSlot).count, 1);
assert.equal(bagFull.level, 5);
assert.equal(bagFull.xp, '450');
assert.equal(bagFull.quests[0].id, travelQuest.id);
assert.equal(await result('SELECT count(*)::int AS result FROM pj_idempotency_receipts WHERE idempotency_key = $1', [questGrantKey]), 0);
assert.equal((await runQuestGrant(questGrantKey, { ...grantQuest, definition_version: 1 })).status, 'STALE');
assert.equal((await runQuestGrant(questGrantKey, { ...grantQuest, expected_profile_revision: '2' })).status, 'STALE');
assert.equal((await runQuestGrant(questGrantKey, { ...grantQuest, session_epoch: '2' })).status, 'CONFLICT');
await exec("UPDATE pj_inventory_items SET owner_scope = 'DESTROYED', equipped_slot = NULL WHERE item_instance_id = $1", [occupiedSlot]);
const paidQuest = await runQuestGrant(questGrantKey);
assert.equal(paidQuest.status, 'COMMITTED');
assert.equal(paidQuest.level, 6);
assert.equal(paidQuest.xp, '72');
assert.equal(paidQuest.profile_revision, '2');
assert.equal(paidQuest.completed_quest_count, 1);
assert.equal(paidQuest.completed_quest, travelQuest.id);
assert.deepEqual(paidQuest.completed_travel_quests, [travelQuest.id]);
assert.deepEqual(paidQuest.quests, []);
assert.deepEqual(paidQuest.items.map(row => [row.count, row.version, row.order]), [[5, 2, 0], [2, 2, 1]]);
assert.equal(paidQuest.items[0].id, rewardStack);
await db.close();
db = new PGlite(testDataPath);
const replayedQuest = await runQuestGrant(questGrantKey);
assert.equal(replayedQuest.status, 'REPLAYED');
assert.equal(replayedQuest.completed_quest, travelQuest.id);
assert.deepEqual(replayedQuest.items, paidQuest.items);
assert.equal((await runQuestGrant(questGrantKey, { ...grantQuest, reward_xp: '73' })).status, 'KEY_REUSED');
assert.equal((await runQuestGrant(id(13006))).completed_quest, travelQuest.id);
assert.equal((await readQuestOwner()).xp, '72');

// Existing completions remain durable and do not consume active capacity.
await exec(`INSERT INTO pj_quest_states(character_id, quest_id, state, progress_json)
    SELECT $1, 'past.' || n, 'CLAIMED', '{}'::jsonb FROM generate_series(1,130) n`, [questOwner]);
await db.exec(await readFile(new URL('013_online_quest_rewards.sql', migrations), 'utf8'));
const persistedHistory = await readQuestOwner();
assert.equal(persistedHistory.completed_quest_count, 131);
assert.equal(persistedHistory.ids, undefined);
async function questHistory(count, ordinal, epoch = '1') {
    let command;
    sandbox.rpcWorldQuestHistory({}, null, {
        sqlQuery: (sql, values) => {
            command = { sql, values: [...values] };
            return [{ result: '{}' }];
        },
    }, JSON.stringify({ character_id: questOwner, session_epoch: epoch, count, ordinal }));
    return JSON.parse(await result(command.sql, command.values));
}
assert.throws(() => sandbox.rpcWorldQuestHistory({ userId: questOwner }, null, {}, '{}'), /requires runtime authentication/);
assert.deepEqual(await questHistory(1, 0), { count: 131, status: 'STALE' });
assert.equal((await questHistory(131, 0, '2')).status, 'CONFLICT');
assert.equal((await questHistory(131, 5)).status, 'INVALID');
const historyPages = await Promise.all([0, 1, 2, 3, 4].map(ordinal => questHistory(131, ordinal)));
assert.ok(historyPages.every(page => page.status === 'OK' && page.count === 131));
assert.deepEqual(historyPages.map(page => page.ids.length), [32, 32, 32, 32, 3]);
const completedIds = historyPages.flatMap(page => page.ids);
assert.equal(new Set(completedIds).size, 131);
assert.equal(completedIds.at(-1), travelQuest.id);
await exec(`INSERT INTO pj_quest_states(character_id, quest_id, state, progress_json)
    SELECT $1, 'active.' || n, 'ACTIVE', jsonb_build_object('id','active.' || n) FROM generate_series(1,127) n`, [questOwner]);
const acceptQuest = (key, quest) => editQuest(key, { ...questOwnerIntent, kind: 'accept', quest,
    row: { ...travelQuest, id: quest, event: 'RealEvent' } });
assert.equal((await acceptQuest(id(13007), travelQuest.id)).status, 'STALE');
assert.equal((await acceptQuest(id(13008), 'active.128')).status, 'COMMITTED');
assert.equal((await acceptQuest(id(13009), 'active.129')).status, 'INVALID');
assert.equal((await editQuest(id(13010), { ...questOwnerIntent, kind: 'progress',
    changes: [{ row: travelQuest, deltas: [{ id: '', amount: 1 }] }] })).status, 'COMMITTED');
assert.equal(await result("SELECT count(*)::int AS result FROM pj_quest_states WHERE character_id = $1 AND state = 'ACTIVE'", [questOwner]), 128);
console.log('Phase 013 online quest reward, replay, backfill, owner-page, and 128 active-slot checks passed');
// Phase 012 shares the real ticket issuer between direct and routed callers.
const routeZone = 'phase_ad_queue';
const otherAccount = id(12000);
await exec("UPDATE pj_accounts SET external_sub = 'phase-ad-owner' WHERE account_id = $1", [account]);
await exec("INSERT INTO pj_accounts(account_id, external_sub) VALUES ($1, 'phase-ad-other')", [otherAccount]);
const createChannel = (channel, population, capacity, draining = false) => result(
    'SELECT pj_world_channel_heartbeat($1) AS result', [{zone: routeZone, channel, worker: id(12000 + channel),
        endpoint: '127.0.0.1:17000', population, capacity, draining}]);
const route = (character, accountId = account) => result(
    'SELECT pj_route_or_enqueue_channel($1,$2,$3,1) AS result', [accountId, character, routeZone]);
assert.equal((await route(buyer, otherAccount)).status, 'CONFLICT');
assert.equal((await createChannel(1, 0, 1)).status, 'OK');
assert.equal((await createChannel(2, 0, 1)).status, 'OK');
const primary = await route(buyer);
assert.equal(primary.status, 'ROUTED');
assert.equal(primary.channel, 1);
assert.ok(primary.ticket);
assert.equal((await result("SELECT pj_issue_world_login('phase-ad-owner', $1, $2, 1) AS result", [seller, routeZone])).status, 'UNAVAILABLE');
const overflow = await route(seller);
assert.equal(overflow.status, 'ROUTED');
assert.equal(overflow.channel, 2);
await exec('DELETE FROM pj_world_login_tickets WHERE zone_id = $1', [routeZone]);
assert.equal((await createChannel(1, 1, 1)).status, 'OK');
assert.equal((await createChannel(2, 0, 1, true)).status, 'OK');
assert.equal((await route(buyer)).position, 1);
assert.equal((await route(seller)).position, 2);
assert.equal((await route(buyer)).position, 1);
assert.equal((await createChannel(1, 0, 1)).status, 'OK');
assert.equal((await route(seller)).status, 'QUEUED');
assert.equal((await route(buyer)).status, 'ROUTED');
await createChannel(2, 0, 1, false); // A delayed pre-drain packet from the same incarnation.
assert.equal(await result('SELECT draining AS result FROM pj_world_channels WHERE zone_id=$1 AND channel_id=2', [routeZone]), true);
await exec("INSERT INTO pj_idempotency_receipts(idempotency_key, operation, status_code, created_at, expires_at) VALUES ($1,'phase_ad_prune',200,NOW()-interval '21 days',NOW()+interval '6 days'),($2,'phase_ad_prune',200,NOW()-interval '21 days',NOW()-interval '1 day')", [id(12003), id(12004)]);
assert.equal((await result('SELECT pj_prune_expired_receipts(7, 100) AS result', [])).status, 'OK');
assert.equal(await result('SELECT count(*)::int AS result FROM pj_idempotency_receipts WHERE idempotency_key = $1', [id(12003)]), 1);
assert.equal(await result('SELECT count(*)::int AS result FROM pj_idempotency_receipts WHERE idempotency_key = $1', [id(12004)]), 0);
for (const rpc of ['rpcWorldChannelRoute', 'rpcSecurityReportViolation', 'rpcMaintenancePruneReceipts']) {
    assert.throws(() => sandbox[rpc]({userId: 'ordinary-player'}, null, {}, '{}'), /requires runtime authentication/);
}
console.log('Phase 012 owner, admission reservation, overflow, FIFO, receipt expiry, and runtime RPC privilege checks passed');
await db.close();
console.log('Earned quest RPC preserves the lease fence for direct edits and replays immutable progress once after restart');
console.log('Nakama reward RPC envelope maps to the same committed SQL receipt across replay and restart');
console.log('Profile, auction, field boss, mail, online dungeon, channel lease, equipment, crafting, quest claim, maintenance and restart replay checks passed');
