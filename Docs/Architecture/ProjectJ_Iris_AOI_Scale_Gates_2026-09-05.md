# Project_J Iris AOI Scale Gates

**Scope:** N2 Iris baseline is complete. This document defines the safety gates before
moving to N10/N30. It does not enable a custom Iris filter, prioritizer, Replication
Graph, dormancy policy, or modify gameplay replication rates.

## Current runtime facts

- `GameNetDriver` is Iris-capable and `net.Iris.UseIrisReplication=1` is enabled.
- The N2 PIE dedicated server and both clients reported `IrisActive=1`.
- `ReplicationDriver=None` means no Replication Graph is connected. It does not mean
  that Iris is disabled.
- `UProject_JNetObjectFilter_Distance` and
  `UProject_JNetObjectPrioritizer_Combat` are **policy calculators only**. They derive
  from `UObject`, have no live Iris registration path, and must not be described as
  active AOI.
- The current engine actor path is therefore standard actor relevancy plus each
  actor's cull distance/update-rate settings.

## Why AOI must remain disconnected for N10/N30

The policy helper currently permits party/guild relevance outside the configured
distance, and boosts combat priority. Those are product decisions, not merely an
optimization. Connecting that policy before measuring its connection/object budget
could make far social actors consume an unbounded share of a connection's replication
budget, or hide an actor class that gameplay still expects to receive.

Likewise, the N2 capture contains high-rate player, PlayerState and wyvern entries
(`100 Hz`, `2 Hz` minimum, `15,000 uu` cull distance). That is a measurement input,
not a safe global rate-tuning target. A two-client capture cannot establish the
correct rate for N30.

## N10/N30 entry gates

Run these checks before interpreting any scale trace:

1. `ProjectJ.DumpNetworkRuntime`
   - Dedicated server reports the intended connection count.
   - Every game world reports `IrisActive=1` and `ReplicationModel=Iris`.
2. `ProjectJ.DumpServerReplicationPolicy 0`
   - Capture the class-count summary without noisy per-actor rows.
   - Record replicated actor count and replicated-movement actor count alongside the
     trace, because population alone is not a replication workload definition.
3. Keep the actor mix and test map fixed between idle, movement and combat captures.
   - Add actors in a documented batch; do not mix a spawn burst with steady-state
     packet measurements.
4. Use a short `ProjectJ.SetNetTraceVerbosity 2` capture with the `net` channel.
   - In Networking Insights select the server / one connection / outgoing packets.
   - Record duration, packet count, `PacketHeaderAndInfo`, `ReplicationData`, top
     project classes, RPCs, and `FailedToWriteSmallObjectCount` / pending-write
     counters.
5. Functional gates: movement, equipment/Inventory FastArray mutation and ability
   activation must still replicate correctly. The known remote stationary 180-degree
   TIP defect is tracked separately and is not an AOI result.

## Decision thresholds (not tuning values)

| Observation | Next action |
|---|---|
| Write failures or persistent pending objects | Diagnose the top class/RPC and scheduling budget before lowering global rates. |
| One actor class dominates replicated payload | Profile that class's properties/conditions first; do not add a broad distance filter. |
| Cost scales with connections while object mix is stable | Evaluate connection-aware Iris filtering/prioritization design. |
| Cost scales with nearby active NPCs | Prototype a narrow NPC-only relevance/rate tier, with explicit gameplay acceptance tests. |
| No pressure at N30 | Preserve defaults and move to a larger, representative workload instead of speculative AOI work. |

## Future adapter contract

Before a custom Iris adapter is written, define per actor class:

- ownership and always-relevant exceptions;
- near/mid/far cull and update-rate buckets;
- party/guild far-relevance budget and maximum distance;
- combat escalation duration and cancellation behavior;
- dormancy eligibility and wake triggers;
- a per-connection observability counter for filtered, prioritized and written objects.

This keeps the existing reusable policy math separate from live engine integration and
makes rollback possible if a relevance rule produces a correctness regression.
