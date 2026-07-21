# Scalable Combat Foundation

## Goal

The production path must allow a new job or advancement to be added without adding job-specific branches to the shared player character, master AnimBP, input executor, or melee Gameplay Ability.

The stable runtime chain is:

```text
Class / Advancement / Equipped Weapon
  -> CombatStyleDefinition
    -> WeaponAnimProfile (animation only)
    -> ComboDefinition (input graph)
      -> AttackDefinition (one reusable authored attack)
    -> CommandSet (command aliases and sequences)
    -> AttackSet (stable attack lookup)
    -> AbilitySets (style lifetime)
Equipped Weapon
  -> WeaponPresentationProfile (weapon actor and drawn socket)
```

There are no legacy runtime fallbacks. Invalid or incomplete assets fail validation instead of silently selecting data from another profile.

## Responsibility boundaries

### CombatStyleDefinition

- Stable `CombatStyleTag`, such as `CombatStyle.Greatsword`.
- Aggregates animation, combo, command, attack and AbilitySet data.
- Does not store runtime combo state.
- May be selected by the base class, an advancement override, or equipped weapon.

### AttackDefinition

- One reusable attack independent of how it is triggered.
- Owns montage/section, play rate, movement policy, hit parameters and server-selected damage GE.
- May be referenced by a combo node, a direct skill or AI behavior.
- The client never supplies the damage GE.

### ComboDefinition

- Owns only input transitions, buffering and owner-tag conditions.
- Every node must reference `AttackDefinition`.

### WeaponAnimProfile

- Owns weapon type/stance, combat intro montage and linked combat Anim Layer.
- Never owns weapon actors, sockets, command tables, combo graphs or damage data.

### WeaponPresentationProfile

- Owns the spawned weapon actor class and drawn socket.
- Is selected by equipped-item data, so weapon skins can share one CombatStyle.
- Is client presentation data and is not consulted by combat authority.

### AbilitySet lifetime

- The ASC stores grant records by stable source ID.
- Player ASCs live on PlayerState, so records survive pawn replacement.
- Sources use namespaces such as `Class.*`, `Advancement.*`, `CombatStyle.*`, and `Equipment.*`.
- Removing a source cancels active abilities before clearing specs and effects.

### Server hit authority

- The active combo node registers with `CombatHitValidationComponent`.
- `Melee Hit Trace` AnimNotifyState opens and closes the authoritative hit window.
- SSR requests carry the active node and a monotonic sequence.
- The server rejects inactive nodes, node mismatches, closed windows, duplicate targets, replayed requests and excessive request rates.
- Damage is selected only from the server's active `AttackDefinition`.

## Greatsword asset migration

Create or reconnect these assets after this breaking refactor:

1. `DA_Attack_Greatsword_L1` (`Project_JAttackDefinition`)
2. `DA_Attack_Greatsword_L2`
3. Any additional branch attacks
4. `DA_AttackSet_Greatsword` (`Project_JAttackSet`)
5. `DA_CombatStyle_Greatsword` (`Project_JCombatStyleDefinition`)
6. `DA_WeaponPresentation_Greatsword` (`Project_JWeaponPresentationProfile`)

For every attack:

- Assign its `Attack.Greatsword.*` tag.
- Assign montage, section and play rate.
- Select the root-motion/motion-warping policy.
- Assign the server damage GameplayEffect.
- Keep a `Melee Hit Trace` notify state only around actual damaging frames.

In `DA_Combo_Greatsword`, assign each node's `AttackDefinition`. Montage, movement and hit fields no longer exist on combo nodes.

In `DA_CombatStyle_Greatsword`, assign:

- `CombatStyle.Greatsword`
- the existing Greatsword WeaponAnimProfile
- Greatsword ComboDefinition
- Greatsword CommandSet
- Greatsword AttackSet
- style AbilitySets

Then connect the style to `DA_Class_GreatSword.DefaultCombatStyle` and/or the equipped weapon's `CombatStyleDefinition`.

In the equipped Greatsword item definition, assign both `CombatStyleDefinition` and `WeaponPresentationProfile`. Old `WeaponAnimProfile` equipment references are intentionally unsupported.

## Rules for future jobs

- Do not add `if (Greatsword)` or weapon-class switches to shared runtime code.
- Do not create a new native melee GA when only data differs.
- Create a dedicated GA only for a genuinely new execution mechanic.
- Do not put damage, combo, or command data back into animation profiles.
- Treat Gameplay Tags and DA IDs as persistent identities; save games and backend data must not store raw asset paths.
- Presentation assets should eventually use client bundles; server gameplay data must remain loadable without meshes, audio or animation evaluation.

## Remaining production hardening

The foundation now supports data-driven jobs, but live-service completion still requires measured work rather than speculative code:

- Replace direct equipment stat mutation with authored infinite GameplayEffects.
- Add hostility, invulnerability, line-of-sight and world-obstruction rules to hit validation.
- Preload Primary Asset bundles during character select/equip instead of relying on synchronous fallback loads.
- Move rewind history to a fixed-size ring buffer and enable it only for relevant combatants after profiling.
- Add data-version migration when persistent backend schemas are introduced.
- Profile dedicated server, 2-client prediction and high-loss network behavior before choosing final tick/rate values.
