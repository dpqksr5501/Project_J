# Combat Locomotion Architecture

## Scope

Combat locomotion is presentation selected by the equipped weapon family. It is not a new player class, a replacement for GAS, or a copy of `ABP_Player` for every job.

```text
Class + permanent advancement + equipped item
  -> AbilitySet / effects / weapon permissions
  -> WeaponAnimProfile -> weapon/job Anim Layer
  -> ABP_Humanoid_Master
```

`BP_Player` is a test pawn and is not part of the production job hierarchy. Promote the tested common graph into a new production asset named `ABP_Humanoid_Master`; do not make the test asset a dependency. The master owns shared Motion Matching, montage slots, aim, foot/leg IK, pose history, and mount selection. Every production job owns a thin Blueprint pair such as `BP_GreatswordCharacter` and `ABP_Greatsword_Layers`. The job layer supplies the full-body armed idle, directional BlendSpace, and—when authored—armed jump/fall/landing poses. Shared C++ character state remains available through `AProject_JPlayerCharacter` and `UProject_JCharacterAnimInstance`.

## Job Blueprint Pattern

When a job has its own visual mesh or future runtime extensions, use a native job foundation plus a thin Blueprint:

```text
AProject_JPlayerCharacter
  -> AProject_JGreatswordCharacter
       -> BP_GreatswordCharacter
```

`BP_GreatswordCharacter` assigns the mesh, class/advancement data, `ABP_Humanoid_Master`, and the mounted layer class if needed. Its greatsword `WeaponAnimProfile` points to `ABP_Greatsword_Layers` through `CombatAnimationLayerClass`. It does not duplicate player input, GAS ownership, or equipment runtime. The native greatsword class owns only its job-specific combat component and future extension points such as charge, guard, or weapon-length policy.

## Editor Asset Setup

Keep `BP_Player` and `ABP_Player` as test assets. Duplicate the tested common AnimGraph into a new production asset named `ABP_Humanoid_Master`; future common changes go there, not into each job graph.

1. Create `ALI_HumanoidCombat` with `FullBody`, `UpperBody`, and `IK` layers in a non-default shared group.
2. Add the interface to `ABP_Humanoid_Master`. Its default layer implementations provide the non-combat Motion Matching fallback, generic upper-body behavior, and common foot/leg IK.
3. Preserve the existing `MountedLocomotion` interface and mount blend in the master. All humanoid job meshes using the master automatically receive this common mount presentation path.
4. Create `ABP_Greatsword_Layers` on the compatible skeleton, add `ALI_HumanoidCombat`, and implement only the greatsword-specific layers. `FullBody` contains armed idle, combat movement BlendSpace, and armed airborne poses; `UpperBody` and `IK` are implemented only when the greatsword needs them.
5. Create `BP_GreatswordCharacter` from `AProject_JGreatswordCharacter`, set its Mesh Anim Class to `ABP_Humanoid_Master`, and configure its class/advancement assets.
6. Set `DA_WeaponProfile_Greatsword.CombatAnimationLayerClass` to `ABP_Greatsword_Layers`. At runtime the combat animation component links it on combat entry, and unlinks it on combat exit, weapon change, or mounting.

If a future job uses an incompatible skeleton, create a separate skeleton-family master (for example `ABP_Beast_Master`) rather than forcing it into the humanoid interface. Jobs on compatible humanoid skeletons share the one master.

## Authoring Contract

`ABP_Humanoid_Master` and every job layer implement the same Animation Layer Interface. The master contains linked Full Body, Upper Body, and IK nodes with a safe default implementation. Each job layer derives from the project's native animation instance class so it receives the same thread-safe locomotion, combat, and aim snapshot. The job layer owns its combat branch: armed idle plus a 2D BlendSpace with direction on X (`-180..180`) and speed on Y. `UProject_JCombatAnimationLayerComponent` links the class assigned by `CombatAnimationLayerClass` only while the matching weapon is in combat mode, and unlinks it for mount presentation or weapon/combat changes.

## Animation Priority

1. Combat locomotion layer: full-body armed idle/move/jump.
2. Upper body: aim offset and explicitly movement-compatible short actions.
3. Full body slot: draw/sheath, melee attacks, dodges, hit reactions, strong casts, death.

Root Motion is permitted for committed actions such as dodge, charge, execution, and committed melee attacks; it is not ordinary locomotion. Use Motion Warping for target-relative actions. The server remains authoritative for movement, hit timing, and final target validation.

## Weapon Sockets and IK Contract

Existing code defaults to `WeaponSocket_R`, while existing equipment documentation uses `weapon_r`; asset names must be checked in the editor before standardization. New shared-character assets should use these canonical names consistently:

- `weapon_r`: right-hand weapon attachment and hit-trace root.
- `weapon_sheath`: stowed melee weapon attachment on back or hip.
- `weapon_l`: optional dual-wield attachment.
- `ik_hand_r`, `ik_hand_l`: common skeleton hand IK targets.
- `ik_weapon_l`: optional off-hand grip target authored on a two-handed weapon.

Until the skeleton assets are confirmed, preserve their actual names and map them in `WeaponAnimProfile`; do not bulk-rename skeleton sockets in code. A draw/sheath Anim Notify moves the persistent weapon visual between `weapon_sheath` and `weapon_r`; it should not spawn/destroy the weapon on every toggle.

## State and Interruption Policy

- Equipment ownership is persistent inventory state. Drawn/sheath is transient combat presentation: hand versus stow socket, combat layer, and action permission.
- Mounting currently cancels the local draw presentation, restores movement-facing rotation, and unlinks the combat layer. Death and mounting must ultimately force the weapon visual to its safe stowed socket; the server-authoritative gameplay command that removes persistent combat state is the next integration point.
- Advancement is permanent progression. It is not a combat-time toggle and does not belong in the locomotion state machine.
- Lock-on is intentionally out of scope. If added later, it needs its own facing/trajectory policy.

## Data-Driven Weapon Combos

Weapon profiles do not contain a growing list of `LightAfterHeavy` fields. A profile references one immutable `UProject_JComboDefinition`; its nodes contain a montage/section, movement policy, start inputs, and outgoing input transitions. The shared `UProject_JGameplayAbility_Melee` owns all transient state: active node, one buffered input, combo-window state, and montage execution. This keeps job Blueprints and `AProject_JPlayerCharacter` free of weapon-specific branching.

`ComboDefinition` is the only attack-order and input-branching source. `WeaponAnimProfile` no longer contains primary-attack montage, section, input, reaction, BlendSpace, or slot fields: those responsibilities previously overlapped the combo graph, action abilities, or the job animation layer. A combo node owns its own hit specification, while the animation layer owns continuous combat locomotion.

```text
DA_WeaponProfile_Greatsword
  -> DA_Combo_Greatsword
       Light_1 --LightAttack--> Light_2
       Light_1 --HeavyAttack--> Light_To_Heavy_2
```

The ability is granted once by the weapon AbilitySet. Put `InputTag.Weapon.LightAttack` in the entry's `InputTag` and `InputTag.Weapon.HeavyAttack` in `AdditionalInputTags`; both inputs then activate the same ability spec. The player input component sends each discrete combat input locally for prediction and to the server through its RPC path. The server runs the same graph and remains the authority for hit confirmation, tags, stamina, cooldowns, and cancellation.

Author `UProject_JAnimNotifyState_ComboWindow` around the intended input period of each montage section. Its begin/end events open and close the graph's input window. Only one valid next input is buffered; this prevents macro-style unlimited queueing and makes the result deterministic. Node/edge owner-tag requirements support stances, advancement, aerial branches, and buff-gated finishers without new code. Each combo definition participates in Data Validation and rejects missing/duplicate node tags, missing montages, duplicate per-node input edges, missing start nodes, and unresolved edge targets.

Use one montage with named sections for a simple chain. The runtime also supports changing montage per node, but transitions should be authored only at a safe cancel boundary. Root-motion policy is explicit per node: ordinary strikes are In-Place; committed lunges are Root Motion Montage; target-relative actions use Root Motion + Motion Warping. The ABP remains `Root Motion from Montages Only`.

## Validation Checklist

- Link/unlink each weapon-family layer on local and simulated-proxy combat transitions.
- Verify no combat layer remains linked while mounted.
- Test eight-direction movement, jump, landing, draw, sheath, attack, dodge, hit reaction, and death per weapon family.
- Verify distant-player tiers omit expensive aim/IK while preserving visible combat montages.
- Validate each Combo Definition and test local prediction plus at least two-client PIE for Light→Light, Light→Heavy, invalid-input rejection, buffering before a window, CC/death/mount cancellation, and weapon swap during an active combo.
