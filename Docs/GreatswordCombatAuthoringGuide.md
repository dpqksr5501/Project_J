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

## Independent Weapon Motion and Ground Contact

This is the reusable visual-motion path for a weapon whose authored motion must
depart from the primary hand during a Montage, for example a greatsword planted
on the floor, a spear drag, or an aerial spin. It does **not** add a bone,
virtual bone, or socket to the shared humanoid skeleton.

```text
WeaponPresentationProfile (per visual weapon/presentation family)
  -> spawned BP weapon actor (WeaponRoot -> WeaponMesh)
  -> drawn character socket
  -> weapon-local grip/contact sockets

Weapon Motion Notify State (per Montage interval)
  -> compact inline transform keys
  -> optional Weapon Ground Contact Notify State (per contact interval)
```

### Ownership and asset count

- Create `BP_<Weapon>_Weapon` from `AProject_JWeaponPresentationActor`. Its
  root must remain `WeaponRoot`; place the visual static mesh under
  `WeaponMesh`. This actor is cosmetic and is spawned locally by
  `UProject_JWeaponPresentationComponent`.
- Create one `UProject_JWeaponPresentationProfile` only for a distinct visual
  presentation: a different mesh, drawn/sheath socket policy, grip layout, or
  terrain-contact layout. A skin with the same layout may reuse it. It is not
  one Data Asset per Montage, and combat/gameplay data remains elsewhere.
- The Montage owns the actual motion keys. Therefore a Montage that has a
  distinct planted/dragging section gets its own **Weapon Motion** Notify
  State and keys inline; no extra motion Data Asset is created.
- The shared Master ABP reads generic grip targets from
  `UProject_JWeaponPresentationComponent`. It should not receive greatsword-
  named nodes or a special skeleton.

### Weapon-local sockets

Author these on the weapon Static Mesh, never on `SKM_Quinn_Simple`.

| Socket | Required when | Placement |
| --- | --- | --- |
| `WeaponGrip_R` | Primary-hand IK is enabled | The right hand's intended grip point. Normally its IK alpha is `0` because `hand_r` already drives the weapon attachment. |
| `WeaponGrip_L` | Two-handed pose / left-hand IK | The left hand's intended grip point. |
| `WeaponGroundProbe_Tip` | Ground Contact is used | Exact lowest authored point that should touch the floor, normally the blade tip. |
| `WeaponGroundProbe_Base` | Optional fallback only | An alternate contact point only for weapon assets that lack the tip socket. It is not averaged with the tip. |

In `DA_Greatsword_Presentation`, set `Weapon Actor Class`, `Drawn Socket Name`,
and `Motion Presentation > Supports Independent Motion`. Configure the grip and
ground-contact socket names there. The contact solver reads its trace height,
length, clearance, maximum translation, trace channel, and interpolation speed
from this profile, so none of those values are hard-coded into a Montage.

### Montage authoring workflow

1. In the Montage, add **Weapon Motion** as a Notify State over only the
   interval where the weapon may move independently.
2. Set `Entry Blend Seconds` and `Exit Blend Seconds` for the state boundary.
   They automatically fade the entire key timeline from/to the ordinary drawn-
   socket pose. Start with `0.08` seconds; increase only when the authored
   action visibly needs a softer hand-off. This is not another key and avoids
   a one-frame pose jump even when the first/last authored key is non-identity.
3. Add two or more `Motion Keys`. `Normalized Time` is local to this Notify
   State: `0` is its left edge and `1` is its right edge. Each
   `Relative Transform` is relative to the weapon root at the drawn character
   socket; identity (`Location 0,0,0`, `Rotation 0,0,0`, `Scale 1,1,1`) is the
   ordinary attached pose.
4. Click a yellow marker on the Weapon Motion state bar. The editor scrubs to
   that exact key and activates the standard viewport transform widget. Use
   **W** for translation and **E** for rotation, then drag the widget while
   viewing the actual weapon preview. The selected key is written back to the
   Montage and supports Undo/Redo. Press **Esc** to leave key-edit mode.
5. Use the Details panel for precise values or to add/delete/re-time keys. Do
   not rely on dragging a numeric rotation field through zero; type an exact
   negative value when that is clearer.
6. Add **Weapon Ground Contact** as a separate Notify State only over the
   frames that should be constrained to terrain. It overlaps Weapon Motion,
   has no keys of its own, and can be shorter than the visual-motion interval.

The Persona preview uses the same key evaluation as runtime. Its normal
Preview Attached Asset is still required so the static weapon is visible in the
Montage editor. The preview transform has exactly one editor writer: the
`Project_JCharacterEditor` Montage preview path. Do not add transform-setting
logic to `UProject_JAnimNotifyState_WeaponMotion` for Persona; a second writer
causes the visible source-pose-to-key-pose reset/jitter while scrubbing.

### Runtime and networking contract

The weapon actor, inline transform keys, hand IK targets, and ground correction
are presentation only. The owning ability/Montage remains responsible for
networked combat timing; gameplay events, hit validation, damage, movement,
and root motion do not depend on the cosmetic weapon actor. Every client
evaluates the same Montage and local weapon profile, while the terrain trace is
an intentionally local visual correction.

For a runtime-only visual discrepancy, use:

```text
Project_J.Combat.WeaponPresentationDebug 1
```

It reports the spawned actor class, attachment parent/socket, root transform,
and active Montage position. Disable it after inspection with
`Project_J.Combat.WeaponPresentationDebug 0`.

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
9. For any planted/dragging weapon attack, verify the preview attached weapon has its weapon-local grip/probe sockets, edit the inline Weapon Motion keys in the Montage, then test flat ground and a slope in client PIE. Keep `Weapon Ground Contact` limited to the actual contact frames.
