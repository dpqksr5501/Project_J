# Project_J Remote Turn-In-Place Replication Plan

**Date:** 2026-09-05  
**Status:** diagnosis complete; implementation pending  
**Scope:** remote simulated-proxy 90°/180° stationary combat turn presentation.

## Confirmed cause

`UProject_JLocomotionAnimStateComponent::ShouldTurnInPlaceForContext()` returns `false` for every non-local context. This is intentional: a simulated proxy has no trustworthy owner controller yaw, so local inference would create a permanent facing delta and repeatedly select a false turn animation.

The current replicated animation event contract contains move-start/stop, fall-off and landing semantics only. It carries no turn direction, event order or server start time. The result is expected: remote movement presents correctly, but remote stationary TIP is never selected. This is not an Iris regression.

## Required contract

Do not replicate continuous controller yaw. Add a compact, server-authoritative one-shot turn semantic event:

| Field | Purpose |
|---|---|
| monotonic sequence | rejects stale replay after loss, relevancy regain, or a newer turn |
| signed bucket | left/right 90° or 180° authored asset selection |
| server start time | remote presentation begins at the correct elapsed age |
| active/cancel state | movement, landing, correction, or newer turn interrupts reliably |

The server validates stationary combat/strafe state and emits the event once. Server actor yaw must be advanced through an authoritative movement/rotation path; remote clients only present the one-shot and must not independently add actor rotation.

## Verification matrix

1. Two clients: A stationary combat strafe, 90° left/right and 180° left/right; observe B.
2. Repeat while interrupting with movement, jump/land, and a second opposite turn.
3. Compare event sequence, bucket and server-age debug output on server/A/B.
4. Repeat with packet lag/loss emulation and an Iris Network Insights capture.
5. Verify no repeated TIP is selected while remote character is stationary after the event ends.

## Ordering

Implement after the immediate Iris FastArray functional smoke test and initial Iris N2 packet trace. This preserves a clean network baseline and avoids conflating a known presentation contract gap with Iris activation or AOI work.
