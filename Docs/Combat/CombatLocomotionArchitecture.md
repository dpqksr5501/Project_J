# Combat Locomotion Architecture

## Scope

Combat locomotion is presentation selected by the equipped weapon family. It is not a new player class, a replacement for GAS, or a copy of `ABP_Player` for every job.

```text
Class + permanent advancement + equipped item
  -> AbilitySet / effects / weapon permissions
  -> WeaponAnimProfile -> weapon/job Anim Layer
  -> ABP_Humanoid_Master
```

`BP_Player` is a test pawn and is not part of the production job hierarchy. `ABP_Humanoid_Master` owns shared Motion Matching, montage slots, aim, foot/leg IK, pose history, and mount selection. Every production job owns a thin Blueprint pair such as `BP_GreatswordCharacter` and `ABP_Greatsword_Layers`. The job layer supplies the full-body armed idle, directional BlendSpace, and—when authored—armed jump/fall/landing poses. Shared C++ character state remains available through `AProject_JPlayerCharacter` and `UProject_JCharacterAnimInstance`.

## Job Blueprint Pattern

When a job has its own visual mesh or future runtime extensions, use a native job foundation plus a thin Blueprint:

```text
AProject_JPlayerCharacter
  -> AProject_JGreatswordCharacter
       -> BP_GreatswordCharacter
```

`BP_GreatswordCharacter` assigns the mesh, class/advancement data, and `ABP_Humanoid_Master`. Its greatsword `WeaponAnimProfile` points to `ABP_Greatsword_Layers` through `CombatAnimationLayerClass`. Mount presentation is selected by the mounted actor's `RiderAnimationProfile`, never by a job Blueprint. It does not duplicate player input, GAS ownership, weapon presentation, or SSR validation. The native greatsword class stays empty until it needs genuine job rules such as charge, guard, counter, or weapon-length policy.

## Editor Asset Setup

Keep `BP_Player` and `ABP_Player` as test assets. New production content uses `ABP_Humanoid_Master`; future common changes go there, not into each job graph.

1. Create `ALI_HumanoidCombat` with `FullBody`, `UpperBody`, and `IK` layers in a non-default shared group.
2. Add the interface to `ABP_Humanoid_Master`. Its default layer implementations provide the non-combat Motion Matching fallback, generic upper-body behavior, and common foot/leg IK.
3. Add `ALI_HumanoidMount` and place one `MountedLocomotion` Linked Anim Layer on the `Mounted` input of the master's full-body context blend. All humanoid job meshes using the master automatically receive this common mount presentation path.
4. Create `ABP_Greatsword_Layers` on the compatible skeleton, add `ALI_HumanoidCombat`, and implement only the greatsword-specific layers. `FullBody` contains armed idle, combat movement BlendSpace, and armed airborne poses; `UpperBody` and `IK` are implemented only when the greatsword needs them.
5. Create `BP_GreatswordCharacter` from `AProject_JGreatswordCharacter`, set its Mesh Anim Class to `ABP_Humanoid_Master`, and configure its class/advancement assets.
6. Set `DA_WeaponProfile_Greatsword.CombatAnimationLayerClass` to `ABP_Greatsword_Layers`. At runtime the combat animation component links it on combat entry, and unlinks it on combat exit, weapon change, or mounting.

If a future job uses an incompatible skeleton, create a separate skeleton-family master (for example `ABP_Beast_Master`) rather than forcing it into the humanoid interface. Jobs on compatible humanoid skeletons share the one master.

## Authoring Contract

`ABP_Humanoid_Master` and every job layer implement the same Animation Layer Interface. The master contains linked Full Body, Upper Body, and IK nodes with a safe default implementation. Each job layer derives from the project's native animation instance class so it receives the same thread-safe locomotion, combat, and aim snapshot. The job layer owns its combat branch: armed idle plus a 2D BlendSpace with direction on X (`-180..180`) and speed on Y. `UProject_JCombatAnimationLayerComponent` links the class assigned by `CombatAnimationLayerClass` only while the matching weapon is in combat mode, and unlinks it for mount presentation or weapon/combat changes.

### Full-body montage and procedural leg IK

`DefaultSlot` is the shared **full-body** slot: attacks, dodge, draw/sheath, hit reactions, death, and any montage that authors its own lower-body pose use it. `UpperBody` remains reserved for overlays whose legs must keep following locomotion.

`UProject_JCharacterAnimInstance` samples the `DefaultSlot` global montage weight on the game thread and copies the result to its animation proxy. The following Blueprint-thread-safe getters are then used directly in `ABP_Humanoid_Master`:

- `GetThreadSafeFootPlacementAlpha` → `Foot Placement.Alpha`
- `GetThreadSafeLegIKAlpha` → `Leg IK.Alpha`

This prevents the procedural Leg IK solver from overwriting authored full-body poses. The default policy keeps Foot Placement at `1.0`, drives Leg IK to `0.0` at full `DefaultSlot` weight, and also drives Leg IK to `0.0` whenever the combat locomotion layer **or its pending draw/equip transition** is active. This protects weapon idle stances as well as attacks and prevents a `0 → 1 → 0` Leg IK pulse during an equip montage's blend-out. It is configurable in the native Anim Instance class defaults (`Full Body Montage IK Policy`) when a skeleton needs different behavior.

### Persistent combat state

`UProject_JGameplayAbility_CombatToggle` ends immediately after toggling its state. The persistent source of truth is `UProject_JGameplayEffect_CombatMode`, which sets its duration to infinite. In UE 5.8, create `GE_CombatMode` as a Blueprint Gameplay Effect derived from this class, add a `Grant Tags to Target Actor` Gameplay Effect Component, and configure that component to add `State.CombatMode`. Assign the resulting GE to `GA_CombatToggle.CombatModeEffectClass`. The common ability set (`AS_Humanoid_Core`) grants `GA_CombatToggle` to every humanoid class, while job ability sets grant only job-specific abilities.

## Animation Priority

1. Combat locomotion layer: full-body armed idle/move/jump.
2. Upper body: aim offset and explicitly movement-compatible short actions.
3. Full body slot: draw/sheath, melee attacks, dodges, hit reactions, strong casts, death.

When entering combat with a draw/equip montage, the job combat layer is linked as soon as that montage starts, while `State.CombatMode` remains pending until the montage completes. This makes the armed idle the montage's blend-out pose and prevents a one-frame fallback to base locomotion. If the intro is cancelled, the prelinked layer is immediately removed unless combat mode is already active.

Weapon profiles keep their combat layer as a soft class reference. `UProject_JCombatAnimationLayerComponent` requests an asynchronous preload for the locally controlled player's equipped profile and retains the resolved class in a one-entry cache. Inactive simulated proxies do not preload merely because their style replicated; they stream the layer only when combat presentation becomes active. If combat begins before loading finishes, the master AnimBP's safe default layer remains active and the requested class is linked by the completion callback. Combat input never performs a synchronous AnimBP load. Dedicated servers skip this presentation-only path entirely, and stale/cancelled requests cannot replace a newer weapon style.

The layer component exposes an explicit presentation state: `Inactive`, `PreparingCombat`, `CombatActive`, or `SuppressedByMount`. This is intentionally separate from authoritative gameplay state; it establishes mount > intro > active > inactive priority without making an entering montage grant combat gameplay privileges early.

Root Motion is permitted for committed actions such as dodge, charge, execution, and committed melee attacks; it is not ordinary locomotion. Use Motion Warping for target-relative actions. The server remains authoritative for movement, hit timing, and final target validation.

## Weapon Sockets and IK Contract

Existing code defaults to `WeaponSocket_R`, while existing equipment documentation uses `weapon_r`; asset names must be checked in the editor before standardization. New shared-character assets should use these canonical names consistently:

- `weapon_r`: right-hand weapon attachment and hit-trace root.
- `weapon_sheath`: stowed melee weapon attachment on back or hip.
- `weapon_l`: optional dual-wield attachment.
- `ik_hand_r`, `ik_hand_l`: common skeleton hand IK targets.
- `ik_weapon_l`: optional off-hand grip target authored on a two-handed weapon.

Until the skeleton assets are confirmed, preserve their actual names and map them in `WeaponPresentationProfile`; do not bulk-rename skeleton sockets in code. A later draw/sheath Anim Notify may move a persistent weapon visual between `weapon_sheath` and `weapon_r`; current runtime presentation spawns the drawn actor on combat entry and destroys it on exit.

## State and Interruption Policy

- Equipment ownership is persistent inventory state. Drawn/sheath is transient combat presentation: hand versus stow socket, combat layer, and action permission.
- Mounting currently cancels the local draw presentation, restores movement-facing rotation, and unlinks the combat layer. Death and mounting must ultimately force the weapon visual to its safe stowed socket; the server-authoritative gameplay command that removes persistent combat state is the next integration point.
- Advancement is permanent progression. It is not a combat-time toggle and does not belong in the locomotion state machine.
- Lock-on is intentionally out of scope. If added later, it needs its own facing/trajectory policy.

## Combat Blend Space Data Bridge

The combat layer receives these thread-safe values directly from `UProject_JCharacterAnimInstance`:

- `GetThreadSafeCombatLocomotionSpeed()` — planar movement speed.
- `GetThreadSafeCombatLocomotionDirection()` — planar velocity relative to actor facing (`-90` left, `0` forward, `+90` right, `+/-180` back).
- `GetThreadSafeCombatLocomotionStartRequested()` / `GetThreadSafeCombatLocomotionStopRequested()` — the canonical start/stop phase requests generated by the locomotion state component. Combat state machines must use these rather than duplicating speed-threshold heuristics in each weapon layer.

Job Linked Anim Layers must not calculate movement from Pawn references in their Event Graphs. They inherit `UProject_JCharacterAnimInstance` and use these thread-safe functions directly in the AnimGraph:

```text
GetThreadSafeCombatLocomotionSpeed
GetThreadSafeCombatLocomotionDirection
```

Speed is actual XY velocity. Direction is actual XY velocity relative to the actor-facing rotation: `-90 = left`, `0 = forward`, `+90 = right`, and `-180/180 = backward`. This gives local and simulated-proxy characters the same directional Blend Space coordinates and keeps camera/input policy outside job animation layers.

## Data-Driven Weapon Combos

`CombatStyleDefinition` references one immutable `UProject_JComboDefinition`. Its nodes contain graph identity, start inputs, conditions, an `AttackDefinition`, and outgoing transitions. The referenced attack owns montage, movement and hit data. The shared `UProject_JGameplayAbility_Melee` owns all transient state: active node, one buffered input, combo-window state, and montage execution. This keeps job Blueprints and `AProject_JPlayerCharacter` free of weapon-specific branching.

`ComboDefinition` is the only attack-order and input-branching source. `WeaponAnimProfile` owns only continuous combat animation selection plus draw/sheath montages. `AttackDefinition` owns the individual attack payload, while the animation layer owns continuous combat locomotion. While a combo is active, a follow-up combo input is routed only as a Gameplay Event to that active ability; it cannot activate another ability spec sharing the same input tag and restart the chain.

```text
DA_CombatStyle_Greatsword
  -> DA_Combo_Greatsword
       Light_1 --LightAttack--> Light_2
       Light_1 --HeavyAttack--> Light_To_Heavy_2
```

The ability is granted once by the weapon AbilitySet. Put `InputTag.Weapon.LightAttack` in the entry's `InputTag` and `InputTag.Weapon.HeavyAttack` in `AdditionalInputTags`; both inputs then activate the same ability spec. The player input component sends each discrete combat input locally for prediction and to the server through its RPC path. The server runs the same graph and remains the authority for hit confirmation, tags, stamina, cooldowns, and cancellation.

Author `UProject_JAnimNotifyState_ComboWindow` around the intended input period of each montage section. Its begin/end events open and close the graph's input window. Only one valid next input is buffered; this prevents macro-style unlimited queueing and makes the result deterministic. Node/edge owner-tag requirements support stances, advancement, aerial branches, and buff-gated finishers without new code. Each combo definition participates in Data Validation and rejects missing/duplicate node tags, missing montages, duplicate per-node input edges, missing start nodes, and unresolved edge targets.

Use one montage with named sections for a simple chain. The runtime also supports changing montage per node, but transitions should be authored only at a safe cancel boundary. Root-motion policy is explicit per node: ordinary strikes are In-Place; committed lunges are Root Motion Montage; target-relative actions use Root Motion + Motion Warping. The ABP remains `Root Motion from Montages Only`.

## Command Inputs (Black Desert-style)

`ComboDefinition` is not a global key-sequence table. It controls one active attack chain after its ability has started. `UProject_JCombatCommandSet` is the separate weapon-authored table that turns a recent ordered input suffix into the GAS input tag for a skill. A skill can therefore have a direct quick-slot command and one or more contextual sequences without copying its cooldown, cost, montage, or hit logic.

```text
DA_CombatStyle_Greatsword
  -> DA_CommandSet_Greatsword
       Command.Greatsword.A
         [InputTag.Weapon.LightAttack,
          InputTag.Weapon.HeavyAttack,
          InputTag.Weapon.LightAttack]
         -> InputTag.Weapon.Skill1
```

For a direct binding, map the key or quick slot to the same `InputTag.Weapon.Skill1`; it does not need a one-entry command. For each command, author an ordered sequence, maximum time between consecutive presses, result input tag, priority, required/blocked owner tags, and whether the final raw press is consumed. The longest matching sequence wins; equal lengths use explicit priority. Set `bConsumeMatchedInput` for `LMB -> RMB -> LMB` commands so the final LMB does not also advance the normal light chain. Set it off only when deliberately wanting both the command skill and the raw button action.

The command resolver retains at most 16 presses and resets on weapon command-set changes, matched commands configured to clear history, or out-of-order network timestamps. The owning client predicts the resolution, but sends the raw press and server-synchronised timestamp; the server resolves the same data asset instead of trusting a client-supplied skill tag. GAS still authoritatively checks cost, cooldown, owned tags, cancellation, and hit validation.

Use owner-tag conditions for stance, weapon mode, airborne/grounded state, buff state, CC, mount, death, and authored cancel windows. Do not encode those rules in the sequence itself. Directional and Shift commands should first be resolved by `SkillInputRouter` into a distinct input tag, then used in the same ordered command sequence. This keeps keyboard, controller, accessibility remaps, and future mobile mappings independent of skill data.

## Greatsword First Attack: Editor Setup

The first milestone is deliberately small: entering combat and pressing LMB plays exactly one greatsword attack montage. Do this before authoring a multi-hit chain, command skill, hit trace, or root-motion attack.

### Tag Convention

Native C++ owns shared player-state and common button tags such as `InputTag.Weapon.LightAttack` and `InputTag.Weapon.HeavyAttack`. Project configuration owns content-facing tags in `Config/DefaultGameplayTags.ini`:

```text
InputTag.Weapon.Skill1

Combo.Greatsword.Light.1
Combo.Greatsword.Light.2
Combo.Greatsword.Light.3
Combo.Greatsword.LightToHeavy.2

Command.Greatsword.Special1
```

`InputTag.Weapon.Skill1` is a generic input intent, not the name of one particular ability. A direct key binding and an authored command may both dispatch it to the same granted ability. Do not make global tags for every animation asset name. Node and command identifiers describe gameplay intent; montage assets remain references in Data Assets.

### Assets to Create

```text
DA_CombatStyle_Greatsword      (UProject_JCombatStyleDefinition)
  -> DA_Greatsword_WeaponProfile
  -> DA_Greatsword_Combo       (UProject_JComboDefinition)
  -> DA_Greatsword_CommandSet  (UProject_JCombatCommandSet; optional for first attack)
  -> DA_Greatsword_AttackSet

DA_Greatsword_AnimProfile      (UProject_JCharacterAnimProfile)
  -> shared locomotion/combat profiles

DA_Greatsword_Presentation     (UProject_JWeaponPresentationProfile)
  -> weapon actor + drawn socket

AS_Greatsword_Base             (UProject_JAbilitySet)
  -> GA_Melee or a Greatsword child ability
```

The Command Set can remain empty for the first LMB test. It becomes necessary only when a sequence such as LMB -> RMB -> LMB must activate a separate skill.

### DA_Greatsword_WeaponProfile

Use the actual skeletal-mesh socket names, not assumed names. For the current humanoid greatsword setup, the intended starting values are:

```text
WeaponType                 = Two-Hand Sword
WeaponStance               = TwoHanded
CombatAnimationLayerClass  = ABP_Greatsword_Layers
CombatIntroMontage         = draw montage, if authored
CombatIntroMontagePlayRate = 1.0
CombatOutroMontage         = sheathe montage, if authored
CombatOutroMontagePlayRate = 1.0
```

`WeaponPresentationProfile` owns the visual sockets: set `DrawnSocketName` to the hand socket and `SheathedSocketName` to the back socket (for example `WeaponSocket_Back`). Add the native **Sheathe Weapon** Anim Notify to `CombatOutroMontage` exactly on the frame where the hand releases the weapon. The runtime also transfers the weapon at montage end as a fallback, but the notify is what makes the transfer look correct.

`WeaponType` is a broad equipment family. Greatsword is expressed by the profile, class, layers, and assets; it does not require a separate enum value while it shares the two-handed-sword rules.

### DA_Greatsword_Combo: One Node

Create one element in `Nodes` and leave `Transitions` empty.

```text
NodeTag           = Combo.Greatsword.Light.1
StartInputTags    = [InputTag.Weapon.LightAttack]
AttackDefinition = DA_Attack_Greatsword_L1
bAllowInputBuffer = true
Transitions       = empty
```

The montage needs a slot compatible with `GA_Melee`. Add the project `MeleeHit` Notify only when hit detection is being tested. Add `UProject_JAnimNotifyState_ComboWindow` only when adding a second node; it has no value in a one-attack test.

### Ability Set and Input

Grant `GA_Melee` (or an eventual `GA_Greatsword_Melee` child) once through `AS_Greatsword_Base`:

```text
InputTag            = InputTag.Weapon.LightAttack
AdditionalInputTags = empty for the first attack
```

The input router's default LMB mapping already resolves to `InputTag.Weapon.LightAttack`. Do not add a Blueprint attack call. The route is:

```text
LMB
  -> SkillInputRouter
  -> SkillInputExecution (client prediction + raw-input server RPC)
  -> GA_Melee
  -> DA_Greatsword_Combo / Combo.Greatsword.Light.1
  -> AnimMontage
```

Finally assign `DA_Greatsword_AnimProfile` and `AS_Greatsword_Base` to `BP_GreatswordCharacter` through the existing class/advancement setup. Keep `BP_Player` and `ABP_Player` as test assets; do not add production greatsword logic there.

### Add the Second Light Attack Later

After the first attack works, add `Combo.Greatsword.Light.2`, author its montage/section, then add this transition to Light.1:

```text
InputTag       = InputTag.Weapon.LightAttack
TargetNodeTag  = Combo.Greatsword.Light.2
```

Place a `ComboWindow` notify state over the portion of Light.1 where the next input should be accepted. The ability accepts at most one buffered valid input. This is intentional: it provides responsive combat without unlimited macro queueing.

### Add a Command Skill Later

For `LMB -> RMB -> LMB` to activate a distinct special ability, add a command entry:

```text
CommandTag               = Command.Greatsword.Special1
OrderedInputSequence     = [LightAttack, HeavyAttack, LightAttack]
MaxTimeBetweenInputs     = 0.45
Priority                 = 10
ResultInputTag           = InputTag.Weapon.Skill1
bConsumeMatchedInput     = true
bClearHistoryOnMatch     = true
```

Grant the special ability in the same Ability Set with `InputTag.Weapon.Skill1`. Map a direct key or quick slot to that same input tag if it should also be usable without the sequence. Required and blocked owner tags are for real state restrictions such as combat stance, CC, mounted, dead, or a later authored cancel window; only use tags that runtime code genuinely grants.

## Validation Checklist

## Removed Compatibility Paths

- `UProject_JWarriorComponent` and `UProject_JCombatComponent` were removed. Weapon presentation and SSR validation are now common player components, so a job class contains only genuine job rules.
- `TriggerPlayerAttack`, legacy skill-input fallback, deprecated client equipment request wrappers, and legacy `AbilitySet` arrays were removed.
- Existing Blueprint and Data Asset values on those removed properties do not migrate automatically. Re-save affected assets after assigning `CombatHitValidationComponent` settings and `GrantedAbilityEntries` / `GrantedEffectEntries`.

- Link/unlink each weapon-family layer on local and simulated-proxy combat transitions.
- Verify no combat layer remains linked while mounted.
- Test eight-direction movement, jump, landing, draw, sheath, attack, dodge, hit reaction, and death per weapon family.
- Verify distant-player tiers omit expensive aim/IK while preserving visible combat montages.
- Validate each Combo Definition and test local prediction plus at least two-client PIE for Light→Light, Light→Heavy, invalid-input rejection, buffering before a window, CC/death/mount cancellation, and weapon swap during an active combo.
