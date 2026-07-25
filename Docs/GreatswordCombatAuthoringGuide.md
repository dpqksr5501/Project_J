# Greatsword Combat Authoring Guide

This guide is the current authoring contract for Greatsword attacks, combo branches, direct skills, modifiers, and aerial root-motion attacks. It describes the data setup; gameplay selection remains in the shared `GA_Greatsword` (`UProject_JGameplayAbility_Melee`).

## Ownership

```text
Enhanced Input action
  -> DA_Greatsword_InputMapping_Data
  -> InputTag.*
  -> Greatsword AbilitySet entry (GA_Greatsword)
  -> DA_Combo_Greatsword node/transition
  -> DA_Attack_Greatsword_* montage, movement, hit, damage
```

- `InputTag.*` describes player intent, never a montage name.
- `Combo.*` identifies a node in one combo graph.
- `Attack.*` identifies one reusable attack payload.
- `DA_Combo_Greatsword` owns only graph flow. Each node references an `AttackDefinition` that owns the montage, movement policy, hit specification, and damage effect.

## Current Tag Convention

```text
InputTag.Weapon.LMB
InputTag.Weapon.RMB
InputTag.Weapon.Chord.LMBRMB
InputTag.Skill.Q
InputTag.Skill.R
InputTag.Skill.T
InputTag.Skill.Dash
InputTag.Modifier.Shift
InputTag.Modifier.Ctrl
InputTag.Modifier.Alt

Combo.Greatsword.LMB.1 .. 4
Combo.Greatsword.RMB.1 .. 4
Combo.Greatsword.LMBRMB.1 .. 4
Combo.Greatsword.Q.1 .. 4
Combo.Greatsword.R.1 .. 4
Combo.Greatsword.T.1 .. 4
Combo.Greatsword.LMBToRMB.1 .. 4
Combo.Greatsword.RMBToLMB.1 .. 4

Attack.Greatsword.<same branch and index>
```

`LMB` and `RMB` are physical weapon-button intents. `Q`, `R`, and `T` are skill-slot intents and therefore live under `InputTag.Skill`, not under a weapon branch. Gameplay-tag redirects preserve old serialized `Light`, `Heavy`, and `Weapon.Q/R/T` references while content is migrated.

## Input Mapping Data

Every job Blueprint has one `UProject_JSkillInputRouterComponent`; assign its `Input Mapping Data` to the job-specific mapping DA. For Greatsword, use `DA_Greatsword_InputMapping_Data`.

### Chords

Use chords for the two physical attack buttons and for combinations of held buttons/modifiers.

| Intent | Requires LMB | Requires RMB | Required modifiers | Output tag |
|---|---:|---:|---|---|
| Left click | Yes | No | None | `InputTag.Weapon.LMB` |
| Right click | No | Yes | None | `InputTag.Weapon.RMB` |
| Shift + left click (example) | Yes | No | `InputTag.Modifier.Shift` | authored intent tag |

When multiple chord rows match, the highest `Priority` wins. `Required Modifier Tags` must all be held; `Blocked Modifier Tags` must all be absent. Do not use the deprecated single `Requires Modifier` flag for new rows.

### Direct Skill Bindings

Use `Direct Skill Bindings` for Q/R/T. This is where an IA is associated with its output gameplay tag; it is not a separate AttackDefinition.

| Input Action | Output tag | Example |
|---|---|---|
| `IA_Skill_Q` | `InputTag.Skill.Q` | Q skill route |
| `IA_Skill_R` | `InputTag.Skill.R` | R skill route |
| `IA_Skill_T` | `InputTag.Skill.T` | T skill route |

Each row can also require/block modifier tags. Multiple rows may use the same Input Action; again, the matching row with the highest priority wins. `Modifier Bindings` maps `IA_Shift`, `IA_Ctrl`, or `IA_Alt` to their `InputTag.Modifier.*` state tags. A direct binding does **not** become a modifier merely by being present in the mapping DA.

The job Blueprint needs only the mapping DA and the legacy shared actions (`LMB`, `RMB`, movement, camera, etc.). Do not add one Blueprint input property/function per Q/R/T skill.

## AbilitySet Requirement

Input routing only produces an input tag. The tag can activate `GA_Greatsword` only when the Greatsword AbilitySet grants that ability with the tag.

- Put the main activation tag in the `GA_Greatsword` AbilitySet entry's `Input Tag`.
- Put every additional start tag in `Additional Input Tags`.

For example, if LMB, RMB, Q, R, and T must all enter the shared combo ability, that entry must own all five input tags. Adding a Q attack to an AttackSet or ComboDefinition alone does not make Q activate the ability.

## Combo Graph Authoring

`Start Input Tags` are used only while no combo is active. A node reached from a transition normally leaves this field empty.

Example routes:

```text
LMB -> RMB -> RMB
LMB.1 -> LMBToRMB.1 -> LMBToRMB.2

LMB -> RMB -> RMB -> RMB
LMB.1 -> LMBToRMB.1 -> LMBToRMB.2 -> Q.2
```

For the second route, add this transition to `Combo.Greatsword.LMBToRMB.2`:

```text
Input Tag       = InputTag.Weapon.RMB
Target Node Tag = Combo.Greatsword.Q.2
```

`Combo.Greatsword.Q.2` may have an empty `Start Input Tags` field, but it must reference the Q2 AttackDefinition. If Q should also start its own sequence, configure `Combo.Greatsword.Q.1` with `Start Input Tags = [InputTag.Skill.Q]`, then add its `Q -> Q.2` transition separately.

The transition is accepted only during the montage's `ComboWindow` notify, or earlier if the source node enables input buffering. Place the notify at a deliberate cancel point, not at the visual end of every animation.

## Simultaneous LMB + RMB Chords

The mapping data supports a true two-button chord without creating a new Blueprint input action. Add this chord row to `DA_Greatsword_InputMapping_Data`:

```text
Requires LMB = true
Requires RMB = true
Priority           = 100
Input Tag          = InputTag.Weapon.Chord.LMBRMB
```

Leave the normal LMB/RMB rows at priority `0`. `Simultaneous Chord Grace Seconds` defaults to `0.08`. The first mouse button is temporarily held as a candidate; if the other is pressed before the timer expires, only the chord input is dispatched. If no second press arrives, the original LMB/RMB input is dispatched after the grace period. Releasing a single button before the timer expires dispatches that ordinary input immediately, preserving quick-tap responsiveness.

Add `InputTag.Weapon.Chord.LMBRMB` to `GA_Greatsword`'s AbilitySet `Additional Input Tags`, then use it as either a start input or a transition input. For the first attack in this branch, use `Combo.Greatsword.LMBRMB.1` and `Attack.Greatsword.LMBRMB.1`.

The grace period is a deliberate input-design tradeoff: `0.05-0.06` seconds favors ordinary-click responsiveness; `0.08` is the balanced default; `0.10-0.12` favors chord accessibility. Use a single shared value per job/mapping asset, then tune it with real players and controller accessibility testing.

The chord result uses the same established GAS input-tag and local-prediction route as LMB/RMB, preserving root-motion prediction and normal server authority for ability activation, movement, and hit validation. If the project later requires competitive anti-cheat validation of physical LMB/RMB timing, add a dedicated server-side raw-button protocol only after it is tested against root-motion prediction and packet-loss behavior.

### LMB + RMB Authoring Checklist

No `IA_LMBRMB` or IMC mapping is needed. The router combines the already-bound `IA_Attack_LMB` and `IA_Attack_RMB` actions.

1. Add the three LMB, RMB, and LMB+RMB rows shown above to the Greatsword mapping DA. Keep the chord priority above either individual row.
2. Add `InputTag.Weapon.Chord.LMBRMB` to the `GA_Greatsword` AbilitySet entry's `Additional Input Tags`.
3. Create `DA_Attack_Greatsword_LMBRMB` with `Attack.Greatsword.LMBRMB.1`, its montage, hit data, and movement policy. Add it to the Greatsword AttackSet.
4. Add `Combo.Greatsword.LMBRMB.1` to `DA_Combo_Greatsword`; set its `Start Input Tags` to the chord input and assign the new AttackDefinition. Leave it empty only when another node reaches it through a transition.
5. Restart the editor after C++/tag changes, then test LMB-only, RMB-only, LMB->RMB, and RMB->LMB. Enable `Project_J.Combat.ComboDebug 1` if the chord route does not start.

## AttackDefinition and Root Motion

For ordinary attacks, use `Movement Policy = In Place`. For committed lunges, use `Root Motion Montage`; use `Root Motion + Motion Warping` only for target-relative authored actions.

For a jump/airborne root-motion attack such as the R skill:

1. Set `Movement Policy` to `Root Motion Montage`.
2. Enable `Use Flying Movement Mode For Root Motion`.
3. Choose `Root Motion End Movement Policy`:
   - `Land / Walking`: for authored ground -> air -> ground skills; avoids an extra Motion Matching landing animation.
   - `Fall`: for attacks that end above the floor and should fall naturally.
   - `Keep Flying`: for attacks that enter a persistent aerial/flying state; the next ability or state system owns the eventual exit from Flying.
4. Ensure the source animation itself contains vertical root motion on its root bone and the montage uses root motion.

The shared melee ability changes CharacterMovement to `Flying` only while this opted-in root-motion attack is active. This prevents Walking floor constraint from discarding the authored Z movement. On normal completion it follows the selected end policy; on interruption/cancellation it always returns to `Falling` so gravity resumes. Do not enable this option for ground-only root-motion strikes.

The Animation Blueprint should remain configured as **Root Motion from Montages Only**. If an aerial montage still never rises after enabling the option, inspect the source animation/montage root-motion preview: it likely contains no positive Z root translation.

## Debugging

Use the console command below in PIE to log combo selection, buffered inputs, transitions, and montage playback:

```text
Project_J.Combat.ComboDebug 1
```

Useful lines:

- `Start Node Selected`: a start input was recognized.
- `Input Buffered`: input arrived before the ComboWindow opened.
- `Transition Consumed`: the graph chose an outgoing edge.
- `Play Node`: the selected AttackDefinition/montage is being played; `SameMontage=0` means a different montage was selected.
- `No transition`: there is no valid outgoing edge for that input from the current node. It is expected at a terminal node.

Turn it off with `Project_J.Combat.ComboDebug 0` after authoring.

To inspect a weapon that looks different in the Skeleton preview and PIE, run this before entering combat:

```text
Project_J.Combat.WeaponPresentationDebug 1
```

The `LogProjectJWeaponPresentation` output logs the actual spawned weapon class, character SkeletalMesh, attach parent/socket/bone, socket world transform, weapon root relative/world transforms, every StaticMesh component transform, and active montage position. Leave combat and enter it again after enabling the command so the initial `DrawAttach` line is emitted. It then samples every 0.5 seconds while the weapon is drawn. Disable it with `Project_J.Combat.WeaponPresentationDebug 0` when finished.

## New Weapon/Job Checklist

1. Create job-specific Input Mapping Data and assign it to the derived job Blueprint.
2. Map LMB/RMB as chords; map Q/R/T as direct skill bindings; add modifiers only when needed.
3. Grant the shared or job-specific ability through the job's AbilitySet, including all start input tags.
4. Create AttackDefinitions, one per distinct montage/hit/movement payload; add each to the AttackSet.
5. Create ComboDefinition nodes and transitions; keep non-start branch nodes' start tags empty.
6. Place ComboWindow and hit notifies in each montage and test both buffered and late-window input.
7. Test root-motion attacks on flat ground, near ledges, and when interrupted. Verify the character lands/falls cleanly.
8. For every new two-button chord, test both button orders, a quick single-button tap, a held first button beyond the grace period, and client PIE root-motion playback.
