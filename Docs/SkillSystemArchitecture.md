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

Player input now splits locomotion/combat toggles from skill intent:

```text
Enhanced Input
  -> UProject_JPlayerInputBindingComponent
  -> UProject_JSkillInputRouterComponent for skill buttons
  -> resolved InputTag
  -> UProject_JSkillInputExecutionComponent
  -> UProject_JAbilitySystemComponent::AbilityInputTagPressed/Released
```

Examples:

- Sprint input calls `AProject_JPlayerCharacter::StartSprint` / `StopSprint`.
- Combat toggle input calls `AProject_JPlayerCharacter::ToggleCombatMode`.
- Primary attack input resolves to `InputTag.Weapon.LightAttack`.
- Secondary attack input resolves to `InputTag.Weapon.HeavyAttack`.
- A held skill modifier can change chord priority; the default modified primary chord maps to heavy attack.

The character still keeps compatibility wrappers, but new skill inputs should go through the router so class, weapon, and chord mappings can evolve without adding one method per skill to `AProject_JPlayerCharacter`.

`UProject_JSkillInputRouterComponent` can read chords from `UProject_JSkillInputMappingData`. If no mapping data is assigned, it falls back to the component's local `Chords` array, and then to the native default light/heavy chords.

## InputTag Foundation

`UProject_JAbilitySystemComponent` now exposes the basic InputTag entry points:

- `TryActivateAbilitiesByInputTag`
- `AbilityInputTagPressed`
- `AbilityInputTagReleased`

These functions search activatable ability specs by `DynamicSpecSourceTags`, which are populated by `UProject_JAbilitySet::GiveToAbilitySystem` from each ability entry's `InputTag`.

Skill input uses only this InputTag activation path. Every player-facing input must be represented by an `AbilitySet.GrantedAbilityEntries` entry; there is no legacy ability-tag fallback.

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

The current light attack path also sends the input tag as a gameplay event for combo buffering. This is intentional: it keeps activation and the buffered branch tied to the same player intent while animation-specific timing remains on `Event.Combat.ComboWindow`.

## Implemented Router Shape

The current router shape is:

```text
Enhanced Input
  -> SkillInputRouter
  -> resolved InputTag
  -> SkillInputExecutionComponent
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

## Ability Set Requirement

Every concrete Blueprint ability asset must be listed in `AbilitySet.GrantedAbilityEntries` with its explicit input tags. One combo ability can own multiple inputs through `AdditionalInputTags`.

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
