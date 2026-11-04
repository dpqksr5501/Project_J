# Trusted development profile: reconnect during accepted I/O

## Failure and recovery boundary

A replacement connection can authenticate while its predecessor still owns the profile writer gate. Previously the replacement's open returned `false` on transient `Busy`. The old writer subsequently drained, but nothing resumed profile loading, so GameMode never spawned that otherwise valid connection.

Only trusted-development opens with a current native session may now wait on `EAdmission::Busy`. The subsystem retains a bounded pending request with the original weak PlayerState, exact account/character/session lease, and ready callback. Pending opens count against the 32-binding budget, report loading, and do not expose a ready profile. Existing component command delegates are installed with a retired capability so quest, progression, inventory, equipment and crafting cannot fall back to prototype mutation while queued; the real profile binding replaces those delegates when loading starts. Retirement preserves rejection rather than reopening unfenced mutation. Invalid identity, catalog failure, capacity and other terminal errors are not converted into retries.

Every profile load/command/checkpoint/progress writer posts its normal game-thread completion after releasing its gate. That completion retries pending opens even when the old gameplay binding has already retired. The retry reruns normal authority, session, world, pristine-state and duplicate-profile admission; it does not capture a newer lease for a stale request. If another writer still owns the gate, the request remains pending until a later drain notification. There is no polling timer or default-data fallback.

`Detach` cancels a queued request for that owner. Shutdown retires all pending callbacks. A second replacement does not inherit the first replacement's callback or authority. The existing GameMode success callback then resumes startup for its exact verified controller/PlayerState/lease.

## Authored regression and validation limits

`ProjectJ.MMO.Persistence.TrustedProfileBusyReconnect` uses the real native trusted-session/profile path. It blocks an accepted old-session XP command at `BeforeCommandEncode`, accepts two successive replacement connections while the gate is still held, and checks that loading remains pending. Releasing the old worker must restore its committed XP only to the latest session without a manual retry. A subsequent XP command must commit normally. Fixture teardown releases the worker before shutdown waits.

The fixture is not a remote socket or spawned-avatar test. It has been authored but not executed in this cloud checkout because the supported Unreal/Windows toolchain is unavailable. `git diff --check` and the MMO architecture validation pass; they do not establish native build/runtime success.
