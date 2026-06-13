# Skill System Architecture

This note records the current C++ skill/GAS shape and the intended extension path for Project J's MMORPG combat input.

## Current Shape

Project J already has the core GAS ownership model needed for player skills:

- `AProject_JPlayerState` owns the replicated player ASC and attribute set.
- `AProject_JBaseCharacter` initializes ASC actor info and grants default class or advancement ability sets.
- `UProject_JCharacterClassDefinition` grants class ability sets.
- `UProject_JCharacterAdvancementDefinition` can add or replace advancement ability sets.
- `UProject_JEquipmentItemDefinition` can grant an equipment ability set while equipped.
- `UProject_JAbilitySet` grants gameplay abilities/effects and can attach an `InputTag` to each ability spec.

The intended long-term skill grant path is:

```text
Class / Advancement / Equipment
  -> UProject_JAbilitySet
  -> UProject_JAbilitySystemComponent
  -> GameplayAbility specs with InputTags
```

## Current Input Path

Player input is still mostly explicit and character-driven:

```text
Enhanced Input
  -> UProject_JPlayerInputBindingComponent
  -> AProject_JPlayerCharacter method
  -> UProject_JCombatStateComponent / ASC
```

Examples:

- Sprint input calls `AProject_JPlayerCharacter::StartSprint` / `StopSprint`.
- Combat toggle input calls `AProject_JPlayerCharacter::ToggleCombatMode`.
- Attack input calls `AProject_JPlayerCharacter::TriggerPlayerAttack`.

This is fine for the current prototype, but it will not scale cleanly to MMORPG-style skill chords such as left click + right click, left click + shift, or weapon/class-specific overrides.

## InputTag Foundation

`UProject_JAbilitySystemComponent` now exposes the basic InputTag entry points:

- `TryActivateAbilitiesByInputTag`
- `AbilityInputTagPressed`
- `AbilityInputTagReleased`

These functions search activatable ability specs by `DynamicSpecSourceTags`, which are populated by `UProject_JAbilitySet::GiveToAbilitySystem` from each ability entry's `InputTag`.

Current attack input uses this new InputTag activation path first, then falls back to the legacy ability-tag activation path. This keeps existing Blueprint ability assets working while allowing new abilities to be granted and activated purely through `AbilitySet.InputTag`.

## Combo And Montage Events

The current melee ability is montage/event driven:

- `UProject_JGameplayAbility_Melee` is local predicted.
- It plays an attack montage.
- `UProject_JAnimNotifyState_ComboWindow` sends `Event.Combat.ComboWindow`.
- The melee ability listens for light/heavy input events during the combo window.
- Hit notifies can feed target data into server-side rewind validation.

Keep input tags and animation events conceptually separate:

- `InputTag.*` should mean "the player pressed/released an input intent."
- `Event.*` should mean "gameplay or animation emitted an event inside an active ability."

The current light attack path still sends the input tag as a gameplay event for combo buffering. That is acceptable for migration, but future combat work should avoid overloading one tag namespace for both activation and ability-internal events.

## Recommended Next Step

Add a small skill input router instead of adding more hardcoded methods to `AProject_JPlayerCharacter`.

Suggested shape:

```text
Enhanced Input
  -> SkillInputRouter
  -> resolved InputTag
  -> Project_JAbilitySystemComponent::AbilityInputTagPressed/Released
```

The router should own:

- raw action state: left mouse, right mouse, shift, number keys, etc.
- chord resolution: left click, right click, left click + shift, left click + right click
- priority rules when multiple chords match
- optional class/weapon input mapping data
- press/release/hold timing, if needed

The router should not own:

- montage section selection
- damage application
- locomotion state
- Motion Matching database or trajectory logic

Those stay in abilities, components, and animation systems.

## Boundaries

Locomotion and skill animation should remain separate:

- Locomotion Motion Matching owns ground movement, start/stop, turn, jump, fall, and landing query data.
- Stylish skills should be GameplayAbilities using montages, gameplay events, root motion or movement tasks, and replicated state tags.
- Combat overlays can layer on top of locomotion, but locomotion should not become the skill selection system.

## Guardrails

- Prefer AbilitySet-granted InputTags for new skills.
- Keep character methods as compatibility wrappers only when they coordinate character-specific systems.
- Do not add every new skill as a new `AProject_JPlayerCharacter` method.
- Do not make Motion Matching choose attacks or skills.
- Keep `InputTag.*` for input intent and `Event.*` for ability/montage events.
- For MMO scaling, keep authoritative effects and validation on the server while allowing local predicted abilities for responsiveness.
