# Combat VFX Architecture

## Runtime boundary

Combat gameplay remains `AttackDefinition`, montage, hit validation, and GameplayEffect driven. VFX never contributes damage geometry, hit timing, or server authority.

```text
AttackTag -> CombatPresentationSet -> AttackPresentationProfile -> Cue definition
Montage Notify State -> CombatPresentationComponent -> Niagara
```

`UProject_JCombatPresentationComponent` is non-ticking. Niagara components never replicate; the server sends only a compact active-attack/loop-cue recovery state plus unreliable cue events for responsive remote presentation. It guarantees cleanup when an attack changes, ends, is cancelled, or the owner is destroyed.

## Override order

For one cue of one `AttackTag`, the resolved definition is selected in this order:

1. `CombatStyleDefinition.CombatPresentationSet` (base style)
2. `CharacterAdvancementDefinition.CombatPresentationOverrideSet` (optional advancement override)
3. `WeaponPresentationProfile.CosmeticPresentationOverrideSet` (optional explicit skin override)

An override set changes only cues it contains. This permits Demon Greatsword and Angel Greatsword to reuse the same `Attack.Greatsword.*` damage/combo definitions while replacing their trail or impact presentation. A weapon skin should normally use its override only for intentional cosmetic replacement; it must not duplicate combat data.

## Authoring

1. Make an `AttackPresentationProfile` per visual attack family and assign Niagara systems to `PresentationCue.Combat.Trail`, `PresentationCue.Combat.Release`, or project-specific cue tags.
2. Add those profiles to a `CombatPresentationSet`, keyed by the stable `Attack.*` tag.
3. Assign the base set to the CombatStyle. Add only changed cues to advancement and skin override sets.
4. Place `Project J Combat Presentation Cue` as a Notify State over the precise montage interval for a looping trail. Its CueTag must match the profile.
5. For a weapon trail, attach the Niagara system to the weapon's existing Base/Tip-authored socket contract. The Niagara asset owns endpoint sampling; gameplay trace sockets remain independent.

## Confirmed impacts

The current server hit path confirms a target but does not preserve a confirmed impact point, normal, or physical material. Do not make an impact VFX authoritative yet. When material-aware impact work begins, extend the confirmed-hit context and emit a GameplayCue (or an equivalent confirmed cosmetic event) from that result. Local swing/trail cues remain montage-driven and predicted.

## Performance rules

- Do not add a Niagara pool until a trace captures Spawn/Destroy, GC, or hitch cost.
- Do not tick C++ per trail frame. Use Niagara, authored bounds, culling, and scalability tiers.
- Validate owner and simulated-proxy presentation under URO, cancellation, weapon swap, death, packet loss, and relevance changes.
