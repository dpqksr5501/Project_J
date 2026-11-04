# Development world-event XP rewards

This is a nonshipping, explicitly enabled consumer of the existing trusted development profile adapter. It does not choose a production economy, authentication provider, database service, or hosting provider.

## Authority and payout

- The authoritative `WorldEncounter` retains effective-damage participation by stable character ID. A completed, successfully defeated round and its frozen server-configured XP are required. Disconnect does not remove that participation while the owning encounter/world remains alive.
- `SubmitWorldEventReward` accepts an actual encounter plus round identity, not client-provided XP. It checks authority, shared world, trusted character session, and the encounter's retained entitlement. Ordinary `SubmitEdit` rejects the server-only event operation.
- The existing progression planner determines XP/level changes. The existing profile command writer commits the new profile core and a per-character `LocalProfileWorldEventClaims` ledger in the same SQLite transaction and receipt.
- Each ledger entry contains immutable round GUID and XP terms. The ledger is append-only and checked against the previous record inside the transaction. A repeated round with identical terms returns the existing result without applying XP. Different terms are rejected.
- Session receipts may expire during reconnect or sequence cleanup. Claim records are separate persisted aggregates and are never removed by that cleanup. An old session ticket is not replayable, but a fresh authenticated request for the same eligible round sees the durable marker.
- Lost commit acknowledgement uses the existing exact-request reconciliation. Durable completion also consults the marker if the receipt cannot prove the outcome. Unknown storage state is not reported as definite nonpayment.

## Bounds and remaining limits

The development ledger holds at most 1,024 paid rounds per character. It never evicts older rounds to admit another payout. At capacity new event rewards fail closed; ordinary inventory and quest operations remain usable. Production pagination/retention and migration are intentionally not invented here.

Paid markers survive reopening the same development database. Unpaid participation and frozen event terms remain process-local. Process/world/encounter destruction therefore loses the ability to establish a previously unpaid entitlement. This implementation does not claim durable event recovery, cross-server event ownership, power-loss guarantees, or production readiness. Those require a durable event lifecycle and approved operating infrastructure.

## Verification

Authored native regression `ProjectJ.MMO.Persistence.WorldEventClaimEpochRecovery` covers real SQLite atomic XP/marker commit, injected post-commit acknowledgement loss, exact retry, file reopen, new-session receipt expiry, persistent marker survival, duplicate rejection, immutable prior terms and capacity. It is an adapter test, not a socket or gameplay encounter test.

The authored `ProjectJ.MMO.Persistence.TrustedDevelopmentQuestReconnect` fixture additionally seeds a completed native encounter entitlement, rejects forged event edits/unknown rounds, calls the actual reward consumer with held publication and lost acknowledgement, detaches/reconnects, retries under a new session without more XP, fills the ledger, and verifies ordinary progression still works after reward-capacity rejection. It does not exercise real GAS damage or remote socket traffic. Both native regressions are unrun in the cloud. Native Unreal compilation and automation must run with the supported Windows engine/toolchain. Cloud static checks are not a substitute for those results.

The encounter retains at most 128 rounds and does not evict unpaid participation. At that bound new tracked spawns stop rather than discard entitlement. Capacity-rejected claims may be revisited by the existing bounded cadence (at most four online candidates per encounter visit); this development implementation does not claim zero retry I/O after the permanent claim ledger fills.

### Additional integration regression (authored, not executed)

`ProjectJ.MMO.Persistence.WorldEventGEAutomaticReward` now starts with an empty contribution ledger, applies real GAS health loss through authenticated source/target bindings, rejects stale source authority, records clamped lethal loss, runs the actual encounter defeat/close path, automatically claims XP, and inspects the stored marker. The fixture arms a registered encounter and drives its visit explicitly. It does not test full asynchronous NPC spawning/ground preparation, wall-clock timer delivery, or actual remote sockets.

The trusted reconnect fixture also holds an old connection's accepted profile writer, replaces the connection twice before releasing it, and checks that only the latest exact lease resumes. A bounded pending-open request wakes when existing writer work drains, not on a new polling timer. Queued profile components reject mutations while loading; obsolete callbacks cannot open a replacement session. These are authored native tests awaiting Windows UHT/UBT and execution.
