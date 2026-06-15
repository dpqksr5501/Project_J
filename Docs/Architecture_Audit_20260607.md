# Project J Architecture Audit - 2026-06-07

This note replaces an older audit file that had broken text encoding. It keeps the useful architectural decisions that are still visible in the current codebase and links them to the next maintenance priorities.

## Current Baseline

- Player ASC and attributes are owned by `AProject_JPlayerState`.
- `AProject_JBaseCharacter` initializes ASC actor info, default class ability sets, advancement ability sets, and equipment runtime binding.
- `UProject_JAbilitySet` grants abilities and effects, with structured ability entries that can attach `InputTag.*` tags to gameplay ability specs.
- Player combat input now routes through `UProject_JSkillInputRouterComponent`, optional `UProject_JSkillInputMappingData`, and `UProject_JSkillInputExecutionComponent` before reaching GAS input tag press/release handling.
- Inventory and equipment replication use FastArray serializers.
- Equipment runtime grants and removes equipment AbilitySets, equipment GameplayEffects, fallback stat modifiers, visual meshes, and weapon animation profiles.
- Motion Matching remains scoped to locomotion. Skill selection and attack animation policy stay in input, GAS, montage, and combat animation layers.

## Decisions To Preserve

### Module Direction

Keep the dependency direction:

```text
Project_JCore
  -> Project_JGAS
  -> Project_JCharacter
  -> Project_J
```

Shared tags, MMO ids, combat interfaces, backend contracts, and asset-manager level utilities should stay out of concrete character gameplay code when possible.

### Input And Skills

New player skills should prefer this path:

```text
Enhanced Input
  -> SkillInputRouter
  -> InputTag.*
  -> UProject_JAbilitySystemComponent
  -> GameplayAbility
```

`AProject_JPlayerCharacter` may keep compatibility wrappers, but new skills should not become one method per skill on the character class.

### Equipment Runtime

Equipment data should remain data-driven:

```text
Inventory item instance
  -> EquipmentManager
  -> EquipmentRuntime
  -> AbilitySet / Effects / Stat modifiers / Weapon profile / Mesh
```

The server owns gameplay grants and removals. Clients receive replicated equipment state and spawn local visuals.

Player characters resolve equipment runtime binding from PlayerState first, then fall back to the character-local equipment manager. NPCs can use the character-local manager directly.

`UProject_JEquipmentRuntimeComponent` remains the orchestrator, but its internal responsibilities are separated into gameplay grants/removals, local visual mesh lifecycle, and current weapon animation profile selection.

### Motion Matching

Motion Matching owns locomotion query data only:

- ground locomotion
- start, stop, turn, jump, fall, landing
- remote trajectory repair and animation-budget behavior

It should not choose attacks, skill chords, or combo branches.

## Validation Coverage

Current validation should focus on data mistakes that are easy to make in editor assets:

- AbilitySet entries with missing ability/effect classes
- duplicate AbilitySet `InputTag` entries
- invalid ability/effect levels
- equipment definitions with `None` slots
- equipment effect/stat policies with missing payload data
- empty weapon definitions that provide no gameplay, animation, or stat value

## Deferred Work

Keep these deferred until gameplay or profiling makes them necessary:

- large-scale server handover implementation
- full Mass monster combat behavior
- player-grade Motion Matching for all NPCs
- global economy, auction house, guild, and persistence systems
- broad network optimization beyond measured bottlenecks

## Near-Term Useful Work

- Add more concrete skill input chords as actual gameplay needs appear.
- Convert legacy ability-tag activation assets to AbilitySet `InputTag` entries.
- Disable legacy skill input fallback once migrated assets cover the expected inputs.
- Add editor or automation coverage for AbilitySet and equipment validation.
- Keep two-client Motion Matching checks as a manual regression pass when locomotion code changes.
