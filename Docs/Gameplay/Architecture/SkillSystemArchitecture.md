# Skill System Architecture

This note records the current C++ skill/GAS shape and the intended extension path for Project J's MMORPG combat input.

2026-10-13 시작 [심화 감사](../../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)에서 device 재바인딩의 최신 세대, 종료 후 inventory/equipment 명령 거부, 장비 제공자 OnRetired에 따른 runtime 자기 grant 반환을 보완했다. provider 종료는 저장된 장비 데이터의 삭제나 가짜 Unequip으로 대체하지 않는다.

## Current Shape

Project J already has the core GAS ownership model needed for player skills:

- `AProject_JPlayerState` owns the replicated player ASC and attribute set.
- `AProject_JBaseCharacter` initializes ASC actor info and grants default class or advancement ability sets.
- `UProject_JCharacterClassDefinition` grants class ability sets.
- `UProject_JCharacterAdvancementDefinition` can add or replace advancement ability sets.
- `UProject_JEquipmentItemDefinition` can grant an equipment ability set while equipped.
- `UProject_JAbilitySet` grants gameplay abilities/effects and can attach an `InputTag` to each ability spec.

The intended long-term skill grant path is:

현재 keyed grant의 실제 spec/effect handle은 ASC registry가 소유하며 제공자는 source ID와 generation lease만 보유한다. 동기 callback 후에도 같은 generation인지 확인하며, 반환 전에 제공자 container를 분리한다. 성장·장비·입력의 종료와 late initialize는 서로 별도 수명 경계다. [심화 감사의 실제 재현·검증](../../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)을 참조한다.

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
- Primary attack input resolves to `InputTag.Weapon.LMB`.
- Secondary attack input resolves to `InputTag.Weapon.RMB`.
- Q/R/T resolve through `DirectSkillBindings` to `InputTag.Skill.Q`, `InputTag.Skill.R`, and `InputTag.Skill.T`.
- Held modifier actions resolve to `InputTag.Modifier.*`; matching chord/direct-binding rows can require or block any combination of those tags.
- LMB+RMB uses the mapping data's `Simultaneous Chord Grace Seconds` (default `0.08`) and resolves to `InputTag.Weapon.Chord.LMBRMB` only when both button presses arrive within that window. A released quick single tap dispatches immediately rather than waiting for the full grace period.

빠른 단일 tap은 release 전에 held 상태에서 의미 tag를 결정하고, release 상태를 게시한 뒤 dispatch한다. dispatch callback이 같은 버튼을 다시 누르거나 취소하면 이전 completion은 새 입력을 해제하지 않는다. 버튼 revision이 이 수명을 구분한다. 입력 실행과 CombatState는 PlayerState ASC의 현재 Avatar가 자기 Pawn인지 검사한다. 과거 Pawn의 유효한 ASC 포인터를 새 Pawn의 입력·상태 권한으로 사용하지 않는다. 실제 R→RMB 데이터 및 실패/복구 근거는 [전체 감사](../../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)를 읽는다.

The character still keeps compatibility wrappers, but new skill inputs should go through the router so class, weapon, and chord mappings can evolve without adding one method per skill to `AProject_JPlayerCharacter`. See [Greatsword Combat Authoring Guide](../../Combat/Authoring/GreatswordCombatAuthoringGuide.md) for the current data setup.

`UProject_JSkillInputRouterComponent` reads chords, direct skill bindings, and modifier bindings from `UProject_JSkillInputMappingData`. If no mapping data is assigned, it falls back to the component's local `Chords` array and then to the native LMB/RMB defaults. Production job Blueprints should assign mapping data.

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

The router owns:

- raw action state: left mouse, right mouse, shift, number keys, etc.
- chord resolution: LMB, RMB, LMB + Shift, LMB + RMB, and other authored chords
- Q/R/T direct action resolution and modifier-aware priority selection
- priority rules when multiple chords match
- optional class/weapon input mapping data
- press/release/hold timing, if needed

LMB/RMB chords deliberately keep the established GAS InputTag route after local resolution. This preserves the current local-predicted ability and root-motion path. Physical-button timing is not yet independently verified by the server; introduce that only with dedicated multiplayer/root-motion and packet-loss coverage.

The router should not own:

- montage section selection
- damage application
- locomotion state
- Motion Matching database or trajectory logic

Those stay in abilities, components, and animation systems.

## Ability Set Requirement

Every concrete Blueprint ability asset must be listed in `AbilitySet.GrantedAbilityEntries` with its explicit input tags. One combo ability can own multiple inputs through `AdditionalInputTags`; an AttackSet or ComboDefinition reference alone never activates a skill.

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

## 전투 의도의 원격 전달 — 2026-10-13

개별 press 의도는 ASC의 reliable RPC로 전달해 GAS predicted activation과 같은 owning actor channel에서 순서를 보존한다. RPC는 입력 당시 ExpectedAvatar를 포함하며 server의 현재 Avatar가 다르면 버린다. Core CombatInterface의 native receiver는 Character 입력 실행기로 전달하고, 서버는 InputTag 계층·serial·rate·age·command 조건을 다시 검사한다. 클라이언트가 Command 결과나 montage event를 직접 주장하지 못한다. 서버 raw-intent 재생은 별도로 ability activation을 반복하지 않는다.

serial은 uint32 모듈러 순서다. wrap 이후 신규 입력은 허용하지만 duplicate/stale 값과 반 범위 이상 도약은 거부한다. 이동·몽타주·스냅샷의 기존 예측/복제 형식은 유지한다. 20% 손실의 이전 R 누락→RMB 시작 재현, 변경 및 fixture 준비 조건은 [전체 감사](../../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)에서 확인한다. 입력 press/release의 물리 타이밍을 서버가 직접 검증하는 시스템은 아니다.
