# Project_J Iris FastArray Validation

Date: 2026-09-05  
Scope: `UProject_JInventoryComponent` and `UProject_JEquipmentManagerComponent`

## Purpose

Iris activation proves that the net driver is running, but it does not prove that a specific
`FFastArraySerializer` payload reaches the intended client. This development-only diagnostic
prints a compact, stable summary from every PIE game world so that a server row can be compared
with its client replica after an actual inventory or equipment mutation.

## Command

Run this in any PIE console after the operation has replicated:

```text
ProjectJ.DumpIrisFastArrayState
```

It emits rows such as:

```text
LogProjectJIrisFastArray: Display: IrisFastArray World=... NetMode=... PlayerState=... Inventory={Items=... ArrayKey=... StateHash=...} Equipment={Items=... ArrayKey=... StateHash=...}
```

- `Items`: current FastArray element count.
- `ArrayKey`: increments when the array changes.
- `StateHash`: combines only replicated identity/value fields and deliberately excludes the
  transport-only `ArrayKey`. Equal server/client values for the same PlayerState indicate that the
  observed FastArray state matches, including a count-preserving
  change such as an equipment swap.

## Comparison policy

- Use `PlayerId`, not the local `PlayerState` object name, to match the same player across
  server/client PIE worlds.
- Inventory is intentionally `COND_OwnerOnly`. Compare the server's PlayerState inventory row
  only with that PlayerState's owning client. A non-owning client can correctly show an empty or
  absent inventory state.
- Equipment is replicated to relevant clients. Compare the same PlayerState's equipment state on
  the server and both clients.
- Use a real server-authoritative action: add/remove or modify an inventory stack, then equip and
  unequip an item. Capture the command output after each settled operation.
- A command snapshot is state validation, not a bandwidth measurement. Capture a separate
  Network Insights trace for bytes, packets, serialization time, and replication scheduling.

## Delta callback smoke test

The initial snapshot does not prove the add/change/remove code paths. The project now has
development-only observation counters; they do not create items, invoke RPCs, or alter gameplay
state. This deliberately keeps a replication diagnostic from becoming a privileged inventory
mutation path.

1. Enter the two-client PIE session and wait until the initial equipment is settled.
2. In any PIE console, clear observations from all PIE worlds:

   ```text
   ProjectJ.ResetIrisFastArrayDeltaEvents
   ```

3. Use the normal, server-authoritative equipment/inventory interaction already exposed by the
   test level or UI. Perform one **equip** followed by one **unequip**. If the test content has a
   stackable inventory item, also change its stack or consume one item, then restore it through
   the normal gameplay flow.
4. Wait roughly one second for replication, then run both commands:

   ```text
   ProjectJ.DumpIrisFastArrayDeltaEvents
   ProjectJ.DumpIrisFastArrayState
   ```

Expected result:

- Equipment: the remote client row reports `Add>=1` after equip and `Remove>=1` after unequip.
  The server's counter may remain zero because these counters intentionally report client-side
  FastArray callbacks, not server mutations.
- Inventory: only the owning client may report its callback(s), because this array is
  `COND_OwnerOnly`; the non-owner must not be treated as a failure.
- The final state dump must match the authoritative server's `Items` and `StateHash` for the same
  `PlayerId`, subject to the owner-only policy above.

Save the two command blocks in `Project_J.log`. If a callback counter is non-zero but the final
fingerprint differs, treat it as a payload/state issue. If the authoritative state changes but
the expected client callback remains zero, treat it as an Iris FastArray delta delivery issue.

## Shipping behavior

`ProjectJ.DumpIrisFastArrayState` is compiled only in non-shipping builds. The production
inventory/equipment replication path is unchanged.
