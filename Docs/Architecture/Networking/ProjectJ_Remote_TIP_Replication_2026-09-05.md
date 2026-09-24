# Remote Turn-In-Place Replication

## Purpose

Simulated proxies intentionally do not infer turn-in-place (TIP) from their
fallback controller rotation. That prevents a stationary remote character from
permanently selecting a turn pose from stale rotation data, but previously
meant a locally performed 90/180 degree combat-strafe TIP was invisible to
other clients.

This implementation adds a compact, server-confirmed visual event to the
existing `UProject_JReplicatedAnimEventComponent` contract.

## Payload and authority

The replicated state adds an event sequence/order, server timestamp, a `uint8`
chooser bucket, and the locally sampled absolute facing target:

| Bucket | Meaning |
|---:|---|
| 1 | Left 90 |
| 2 | Left 180 |
| 3 | Right 90 |
| 4 | Right 180 |

The locally controlled character emits one request on the rising TIP edge.
The request is sent by reliable Server RPC. The server accepts the event only when
the owner is in combat mode, moving on ground, and has planar speed no greater
than 20 cm/s; it then stamps the event with server world time and forces a
normal replicated update. The target yaw is a **cosmetic presentation target**:
it aligns the remote turn's final facing to the local source without granting
combat, aim, hit-validation, or movement authority.

Remote clients apply the bucket and fixed target in server event order, request
a short URO bypass through the existing coordinator, and expose it as a
temporary kinematic-facing override. Their desired yaw is recomputed from the
fixed target as the capsule rotates; it is never re-based on the newly rotated
proxy yaw. The override ends once the target is within 5 degrees (or the
short timeout expires), preventing repeated full turns.

## Animation integration assumptions

No asset or Blueprint was modified. The existing State Controller/Blend Stack
must already consume these C++ contracts:

- `bShouldTurnInPlace`
- `DesiredFacingDeltaYaw`
- `GetThreadSafeStateControllerTurnInPlaceIndex()`

The bucket is translated to the same signed yaw ranges used by the existing
chooser: -90, -180, +90, and +180, while the target yaw supplies the final
world-space facing. Therefore the authored
`CHT_Player_Strafe_TurnInPlace` chooser rows remain the source of animation
selection.

## Validation

In 2-client dedicated-server PIE:

1. Put player A in combat/strafe while stationary.
2. Rotate A's camera through left and right 90/180 turns.
3. Observe player A from client B. Each turn should play once and then return
   to idle; movement must cancel it immediately.
4. Enable `p.ProjectJ.TIPDebug 2` if needed and verify a
   `RemoteAnimSemantic ... Type=TurnInPlace` line on client B.

The server-side validation intentionally checks stationary state rather than
reconstructing controller yaw. TIP is cosmetic; gameplay hit direction and
aim authority remain separate systems. If a future combat requirement needs
server-verifiable aim yaw, add it to the authoritative combat/aim state rather
than expanding this animation event.
