# 전투 애니메이션 합성과 책임 통합 가이드

정리일: 2026-10-03. 전투 포즈 합성, 이동 정책 경계와 무기 계열 레이어의 상세 계약을 한곳에서 읽는다. 기존 세 문서의 제작·입력·콤보·복제·비동기 수명 내용과 체크리스트를 상세 절에 보존했다.

## 적용 순서와 기준

1. 지속 포즈의 선택은 첫 절의 CombatPresentationMode를 기준으로 읽는다. 기본 Upper-Body Overlay는 공용 이동을 유지하며 Full-Body Locomotion은 완전한 무기 이동 세트 검증 후 선택한다.
2. 공격·회피·피격 등 액션 몽타주와 지속 전투 자세의 경로를 구분한다. Root Motion과 서버 판정은 해당 게임플레이 계약을 유지한다.
3. 두 번째 절의 CombatMovementPolicy가 이동 허용과 전투 상태 사이의 경계를 설명한다.
4. 세 번째 절은 초기 무기 레이어·파지·첫 공격·입력 작성 계약과 발전 과정을 담는다. 그 절의 FullBody 기본 설명은 첫 절의 현재 모드 선택과 함께 해석한다.
5. 임포트 몸체의 현재 소켓·손 접촉은 [파지 통합 가이드](../../Animation/Authoring/Weapon_Hand_Contact_System.md)를 따른다. 초기 weapon_r/ik_hand 계열 예시는 현재 에셋의 필수 이름이 아니며 실제 뼈·소켓은 프로필로 매핑한다.

| 질문 | 상세 절 |
| --- | --- |
| 마스터 ABP와 상체·FullBody를 어떻게 연결하는가 | 전투 포즈 합성과 모드 선택 |
| 전투가 이동 상태를 어떻게 제한하는가 | 정책과 책임 경계 |
| 레이어 비동기 로딩·콤보·입력·첫 공격을 어떻게 작성하는가 | 무기 레이어·액션·콘텐츠 작성 계약 |

문서 통합 자체는 런타임 동작 변경이나 새 에디터 검증이 아니다. 날짜가 붙은 후속 Strafe/TIP/원샷 결과는 [전투 목차](../README.md)에서 이어 읽는다.

---

<a id="composition"></a>
## 전투 포즈 합성과 모드 선택

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Combat/Architecture/CombatAnimationComposition.md)

<a id="composition-combat-animation-composition"></a>
원제: **Combat Animation Composition**

> **스켈레톤 및 본 리타기팅 표준:** 휴머노이드 표준 스켈레톤(`SK_Mannequin`)의 본 트랜슬레이션 리타기팅 규칙 및 GASP 호환 표준은 [`HumanoidSkeletonRetargetingStandards.md`](../../Animation/Authoring/HumanoidSkeletonRetargetingStandards.md)를 참조한다.

<a id="composition-decision"></a>
### Decision

Continuous weapon presentation is selected per `WeaponAnimProfile` through
`CombatPresentationMode`. This is presentation data only: movement, GAS state,
hit validation, and equipment ownership do not depend on it.

| Mode | Intended use | Lower body | Leg IK |
| --- | --- | --- | --- |
| `Upper-Body Overlay` | Default. A weapon has a reliable armed upper-body pose but no phase-compatible in-place combat locomotion set. | Shared Motion Matching | Remains enabled |
| `Full-Body Locomotion` | Opt-in only after validating a complete in-place idle/start/loop/stop/airborne weapon set. | Weapon layer | Suppressed by the full-body policy |

The default is `Upper-Body Overlay`. This is deliberate: an incomplete weapon
locomotion set must not replace stable shared locomotion merely because a weapon
is equipped.

<a id="composition-animation-layer-contract"></a>
### Animation Layer Contract

`ALI_HumanoidCombat` has two independent continuous-pose contracts. They must
not be merged into a weapon-specific `ABP` graph.

```text
CombatLocomotion() -> Pose
  Full-body replacement path.
  Master fallback: shared Motion Matching pose.
  Weapon implementation: only for Full-Body Locomotion profiles.

CombatUpperBody(BasePose: Pose) -> Pose
  Overlay path.
  Master fallback: BasePose unchanged.
  Weapon implementation: BasePose + armed upper-body pose through Layered Blend Per Bone.
```

The common master chooses the path with the thread-safe function
`GetThreadSafeUsesFullBodyCombatLocomotion`. This makes a newly added weapon
family data-driven: it supplies a linked layer class, then chooses composition
mode in its `WeaponAnimProfile`; it never forks `ABP_Humanoid_Master` or player
character code.

<a id="composition-master-animgraph-shape"></a>
### Master AnimGraph Shape

```text
Shared Motion Matching
  -> Save Cached Pose: SharedLocomotion
  -> Branch (UsesFullBodyCombatLocomotion)
       true:  CombatLocomotion Linked Layer
       false: CombatUpperBody Linked Layer (BasePose = SharedLocomotion)
  -> Upper-body montage slot, where applicable
  -> DefaultSlot (full-body actions)
  -> Aim Offset
  -> Foot Placement
  -> Leg IK
  -> mount selection / Output Pose
```

Keep `DefaultSlot` as the full-body action path. Draw/sheath, attacks, dodge,
hit reactions, death, and committed root-motion actions use it. `CombatUpperBody`
is only for a persistent armed stance or a movement-compatible short overlay.

<a id="composition-locomotion-source-contract-otm-and-combat-strafe"></a>
### Locomotion source contract (OTM and Combat Strafe)

The input called `Shared Motion Matching` above is a resolved locomotion pose,
not a promise that every lower-body clip comes from Motion Matching. Project_J
uses two lower-body modes:

```text
Non-combat / OTM
  Existing Motion Matching + OTM State Controller direct one-shots.

Combat / Strafe
  Combat Motion Matching uses Dynamic Cycle while local camera/input trajectory
  changes and optional Loop-only SettledCycle after it stabilizes,
  State Controller Blend Stack for Start, Stop, Jump, Fall Off and Land.
Run Pivot is an opt-in Combat-Strafe Run-only direct one-shot. Combat Strafe
leaves Turn Redirect empty: ordinary direction changes remain in its continuous
Motion Matching Cycle PSDs. Dynamic/Settled is local-only; remote proxies stay
on Dynamic Cycle because they do not own reliable Control-Yaw intent.
```

`TurnRedirect` is intentionally OTM-only. A moving turn asset turns the body
toward its travel direction, whereas Combat Strafe keeps body-facing camera-led
while travel may be lateral, backward, or diagonal. Combat direction correction
therefore belongs in a curated Dynamic Cycle PSD, not in a moving-turn family.
The Combat PSD must include only clips that preserve that facing contract. In
particular, `M_Neutral_Run_Arc_Tight_L/R` are excluded from Combat because they
can numerically win a camera-rotating strafe query while visibly presenting an
OTM curve. They remain valid candidates in OTM PSDs. Details and the regression
contract are maintained in
[`MotionMatchingNextSteps.md`](../../Animation/Architecture/MotionMatchingNextSteps.md#combat-strafe-reselect-and-psd-ownership).

The master graph selects the State Controller direct pose only while
`GetThreadSafeStateControllerShouldOverrideMotionMatching` is true. Otherwise
the active Motion Matching pose is used. Both paths then flow through the same
combat upper-body, slots, aim offset, foot placement, Leg IK and Pose History
chain. This is why combat strafe does not need a second master AnimGraph or a
separate full-body combat layer.

Only the mesh's primary AnimInstance may evaluate and own the State Controller
direct Blend Stack. Linked combat layers consume the already-resolved pose; they
must not independently select Start, Stop, Pivot, or a foot variant.

Combat Strafe's State Controller Blend Stack may contain local, per-one-shot
Orientation Warping. It must remain inside that direct path, between `Local To
Component` and `Component To Local`; do not apply it globally to the final
locomotion pose. See
[`CombatStrafe_Implementation_2026-08-04.md`](../Reports/CombatStrafe_Implementation_2026-08-04.md)
for the pin and bone contract.

<a id="composition-combat-strafe-run-pivot-status"></a>
#### Combat Strafe Run Pivot status

`CHT_Player_Strafe_Ground` remains the parent chooser: Start, Stop and Pivot
are its mutually exclusive child choosers. C++ latches one local Pivot request
revision from physical velocity versus local Enhanced Input intent, then the
State Controller holds the selected direct Pivot asset until its authored exit.
An explicit Stop preempts it with the normal Stop one-shot. A later independently
accepted reversal replaces the active Pivot with a fresh Pivot; an ordinary
direction change instead releases directly to Cycle Motion Matching and forces
one reselect. Enhanced Input applies non-zero movement intent immediately so the
Pivot sees the high-speed reversal before CharacterMovement decelerates; only a
Completed/Canceled Stop is deferred until all mappings in that input update have
settled. Keyboard chord semantics are supplied separately by the optional
Boolean `IA_MoveIntent_Forward`, `Backward`, `Left`, and `Right` actions: their
IMC mappings, rather than C++ key checks, define the device-independent meaning.
They are an all-or-nothing optional set: a class/profile must provide all four
actions to enable semantic chord handling; otherwise Project_J retains the
existing `IA_Move` Axis2D presentation fallback. This prevents a partially
configured input profile from publishing a false one-axis intent.
Those edges are coalesced once after the Enhanced Input update, while `IA_Move`
continues to drive gameplay immediately. When opposite semantic directions are
held, the most recently pressed direction wins for that axis. An unmapped analog
device continues to use the existing Axis2D intent fallback.
Because the semantic snapshot is finalized after the input callbacks, Project_J
also latches the actual horizontal velocity direction and speed at the first
semantic edge. That single revision consumes the latch during Pivot evaluation;
it is not a delay window and cannot replay a stale reversal after a real Stop.
An in-progress semantic update is coalesced before presentation consumes it; a
completed semantic snapshot with no held direction clears it as a genuine Stop.
Run Pivot is intentionally cardinal-only: both the prior and new stable input
intent must be Forward, Backward, Left, or Right. Diagonal direction changes
remain the responsibility of Combat-Strafe Cycle Motion Matching and its normal
Start/Stop presentation.
Each accepted Pivot request independently latches its foot from current contact,
then phase history, then the authored fallback; a replacement Pivot never
inherits the previous Pivot's foot variant. Air, Land and a full-body montage
also preempt it. The Pivot leaf must use `UseMotionMatch=false`; its imported
PoseSearch BranchIn metadata is not a Project_J entry-MM dependency.

<a id="composition-greatsword-now"></a>
### Greatsword Now

Set `DA_Greatsword_WeaponProfile.CombatPresentationMode` to
`Upper-Body Overlay`.

In `ABP_GreatSword_Layers -> CombatUpperBody`:

```text
Input Pose (BasePose)
  + Sword_Idle_Seq / armed upper-body Blend Space
  -> Layered Blend Per Bone
       Base Pose: BasePose
       Blend Pose: armed pose
       Branch Filter: spine_01 (or the confirmed humanoid upper-body root)
  -> Output Pose
```

The initial alpha may be `1.0`, because this linked layer only exists during
combat presentation. Keep the lower body out of the blend filter. If an asset
still modifies hips or legs, use a per-bone blend mask or a cleaned upper-body
asset; do not disable shared lower-body IK to hide the issue.

Do not delete the greatsword combat Data Assets. Combo graphs, attack
definitions, weapon presentation, equipment effects, the ability set, draw
montage, and the linked layer are still all used. The only deferred assets are
the unvalidated full-body combat Blend Space and its cropped Start/Loop/Stop
sequences.

<a id="composition-future-full-body-upgrade"></a>
### Future Full-body Upgrade

A future weapon can switch its profile to `Full-Body Locomotion` only when all
of the following are true:

1. Its locomotion clips are in-place and share a common root/pelvis origin.
2. Start, loop, and stop are phase compatible; matching left/right foot sync
   markers are authored.
3. Draw blend-out, attack recovery, air, slope, and network proxy behavior are
   verified with the common full-body montage and IK policy.
4. Its `ABP_<Weapon>_Layers` implements `CombatLocomotion`; the master graph
   and runtime linker remain unchanged.

This allows incremental quality upgrades without a character-class fork or a
data migration.

---

<a id="boundary"></a>
## 이동·전투 정책과 책임 경계

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Combat/Architecture/CombatAnimationArchitectureNotes.md)

<a id="boundary-combat-animation-architecture-notes"></a>
원제: **Combat Animation Architecture Notes**

이 문서는 Locomotion과 Combat의 책임 경계를 유지하기 위한 메모입니다. MMORPG 캐릭터는 직업, 무기, 스킬, 상태이상이 계속 늘어나므로 combat 조건이 locomotion 내부에 직접 섞이지 않도록 관리합니다.

<a id="boundary-combat-movement-policy"></a>
### Combat Movement Policy

`FProject_JCombatMovementPolicy`는 combat state와 locomotion decision 사이의 현재 boundary입니다.

- PlayerCharacter는 replicated state와 GAS tag read를 소유합니다.
- Policy struct는 sprint, jump, ground start, overlay, combat rotation, intro interruption 결정을 plain value에서 계산합니다.
- Weapon/job-specific profile setting은 policy에 입력되고 locomotion state machine 내부로 직접 들어가지 않습니다.
- Motion Matching timing을 안정적으로 유지하면서 class/weapon combat variant를 추가할 수 있습니다.

<a id="boundary-responsibility-boundary"></a>
### Responsibility Boundary

<a id="boundary-locomotion"></a>
#### Locomotion

- Idle, Start, Locomotion, Stop 상태 판단
- JumpStart, FallOff, Landing 상태 판단
- local input과 replicated movement event를 animation-facing state로 해석
- sprint intent와 이동 phase를 AnimInstance/Chooser에 제공
- 공중, 착지, 회전, 방향 전환 context 제공

<a id="boundary-combat"></a>
#### Combat

- 무기 장착/해제
- 공격 시작/종료
- 회피 시작/종료
- 피격 반응 시작/종료
- 공격/회피/피격 중 sprint, jump, rotation policy 결정
- combat aim offset, upper body slot, montage playback policy 결정
- combat 관련 gameplay tag 관리

<a id="boundary-current-boundary-functions"></a>
### Current Boundary Functions

Combat 상태가 Locomotion 조건문 안에 직접 섞이지 않도록 character 또는 policy function으로 제어합니다.

- `IsSprintLocomotionAllowed`
- `IsJumpLocomotionAllowed`
- `IsGroundStartAllowed`
- `IsGroundStopAllowed`
- `IsCombatLocomotionOverlayAllowed`
- `ShouldUseCombatRotationMode`
- `ShouldInterruptCombatIntroOnHit`
- `GetEffectiveCombatAimAlpha`

<a id="boundary-current-supporting-pieces"></a>
### Current Supporting Pieces

- `UProject_JWeaponPresentationComponent`: 공용 무기 액터 생성·제거와 프로필 소켓 부착을 담당합니다.
- `UProject_JCombatHitValidationComponent`: 모든 직업이 공유하는 서버 측 히트 검증(SSR)을 담당합니다.
- `UProject_JCharacterUIBindingComponent`: UI ViewModel attribute binding을 담당합니다.
- `UProject_JPlayerInputBindingComponent`: EnhancedInput binding과 gameplay method 호출을 분리합니다.
- `UProject_JReplicatedAnimEventComponent`: replicated animation event counter 갱신/적용 의미를 담당합니다.
- `UProject_JNetObjectPrioritizer_Combat`: combat gameplay tag를 replication priority로 변환합니다.
- `Project_J::AnimationProfileValidation`: combat/weapon/locomotion profile 설정 실수를 PIE 시작 시 경고합니다.

<a id="boundary-recommended-implementation-order"></a>
### Recommended Implementation Order

1. 무기 장착/해제 상태 확정
2. 공격 montage 재생과 종료 이벤트 연결
3. 공격 중 sprint/jump/rotation policy 확인
4. 회피 action 추가
5. 피격 reaction 추가
6. CombatAnimProfile에 무기별 속도, 회전, aim offset, montage 설정 추가
7. 필요한 경우에만 combat 전용 locomotion overlay 추가

<a id="boundary-guardrails"></a>
### Guardrails

- 공격/회피/피격 조건을 `LocomotionAnimStateComponent`의 start/stop/jump/land 조건문 안에 직접 섞지 않습니다.
- Chooser variable 이름을 combat 구현 중 자주 바꾸지 않습니다.
- combat mode만 켰다는 이유로 기본 locomotion이 다른 구조로 갈아타지 않습니다.
- 실제 무기/스킬 데이터가 생기기 전에 과도한 추상 계층을 만들지 않습니다.

<a id="boundary-editor-test-baseline"></a>
### Editor Test Baseline

- Combat mode만 켠 상태에서도 기본 locomotion이 정상 동작해야 합니다.
- 공격/회피/피격 중 sprint/jump 제한 여부가 policy function 기준으로 명확해야 합니다.
- `IsGroundStartAllowed`, `IsGroundStopAllowed`, `IsCombatLocomotionOverlayAllowed`가 locomotion 내부 조건 대신 combat boundary 역할을 해야 합니다.
- 공격 montage가 하체 locomotion을 불필요하게 깨지 않아야 합니다.
- 원격 캐릭터도 combat state와 movement state가 서로 다른 타임라인으로 어긋나지 않아야 합니다.

---

<a id="layers"></a>
## 무기 레이어·액션·콘텐츠 작성 계약

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Combat/Architecture/CombatLocomotionArchitecture.md)

<a id="layers-combat-locomotion-architecture"></a>
원제: **Combat Locomotion Architecture**

<a id="layers-scope"></a>
### Scope

Combat locomotion is presentation selected by the equipped weapon family. It is not a new player class, a replacement for GAS, or a copy of `ABP_Player` for every job.

```text
Class + permanent advancement + equipped item
  -> AbilitySet / effects / weapon permissions
  -> WeaponAnimProfile -> weapon/job Anim Layer
  -> ABP_Humanoid_Master
```

`BP_Player` is a test pawn and is not part of the production job hierarchy. `ABP_Humanoid_Master` owns shared Motion Matching, montage slots, aim, foot/leg IK, pose history, and mount selection. Every production job owns a thin Blueprint pair such as `BP_GreatswordCharacter` and `ABP_Greatsword_Layers`. The job layer supplies the full-body armed idle, directional BlendSpace, and—when authored—armed jump/fall/landing poses. Shared C++ character state remains available through `AProject_JPlayerCharacter` and `UProject_JCharacterAnimInstance`.

<a id="layers-job-blueprint-pattern"></a>
### Job Blueprint Pattern

When a job has its own visual mesh or future runtime extensions, use a native job foundation plus a thin Blueprint:

```text
AProject_JPlayerCharacter
  -> AProject_JGreatswordCharacter
       -> BP_GreatswordCharacter
```

`BP_GreatswordCharacter` assigns the mesh, class/advancement data, and `ABP_Humanoid_Master`. Its greatsword `WeaponAnimProfile` points to `ABP_Greatsword_Layers` through `CombatAnimationLayerClass`. Mount presentation is selected by the mounted actor's `RiderAnimationProfile`, never by a job Blueprint. It does not duplicate player input, GAS ownership, weapon presentation, or SSR validation. The native greatsword class stays empty until it needs genuine job rules such as charge, guard, counter, or weapon-length policy.

<a id="layers-editor-asset-setup"></a>
### Editor Asset Setup

Keep `BP_Player` and `ABP_Player` as test assets. New production content uses `ABP_Humanoid_Master`; future common changes go there, not into each job graph.

1. Create `ALI_HumanoidCombat` with `FullBody`, `UpperBody`, and `IK` layers in a non-default shared group.
2. Add the interface to `ABP_Humanoid_Master`. Its default layer implementations provide the non-combat Motion Matching fallback, generic upper-body behavior, and common foot/leg IK.
3. Add `ALI_HumanoidMount` and place one `MountedLocomotion` Linked Anim Layer on the `Mounted` input of the master's full-body context blend. All humanoid job meshes using the master automatically receive this common mount presentation path.
4. Create `ABP_Greatsword_Layers` on the compatible skeleton, add `ALI_HumanoidCombat`, and implement only the greatsword-specific layers. `FullBody` contains armed idle, combat movement BlendSpace, and armed airborne poses; `UpperBody` and `IK` are implemented only when the greatsword needs them.
5. Create `BP_GreatswordCharacter` from `AProject_JGreatswordCharacter`, set its Mesh Anim Class to `ABP_Humanoid_Master`, and configure its class/advancement assets.
6. Set `DA_WeaponProfile_Greatsword.CombatAnimationLayerClass` to `ABP_Greatsword_Layers`. At runtime the combat animation component links it on combat entry, and unlinks it on combat exit, weapon change, or mounting.

If a future job uses an incompatible skeleton, create a separate skeleton-family master (for example `ABP_Beast_Master`) rather than forcing it into the humanoid interface. Jobs on compatible humanoid skeletons share the one master.

<a id="layers-authoring-contract"></a>
### Authoring Contract

`ABP_Humanoid_Master` and every job layer implement the same Animation Layer Interface. The master contains linked Full Body, Upper Body, and IK nodes with a safe default implementation. Each job layer derives from the project's native animation instance class so it receives the same thread-safe locomotion, combat, and aim snapshot. The job layer owns its combat branch: armed idle plus a 2D BlendSpace with direction on X (`-180..180`) and speed on Y. `UProject_JCombatAnimationLayerComponent` links the class assigned by `CombatAnimationLayerClass` only while the matching weapon is in combat mode, and unlinks it for mount presentation or weapon/combat changes.

<a id="layers-full-body-montage-and-procedural-leg-ik"></a>
#### Full-body montage and procedural leg IK

`DefaultSlot` is the shared **full-body** slot: attacks, dodge, draw/sheath, hit reactions, death, and any montage that authors its own lower-body pose use it. `UpperBody` remains reserved for overlays whose legs must keep following locomotion.

`UProject_JCharacterAnimInstance` samples the `DefaultSlot` global montage weight on the game thread and copies the result to its animation proxy. The following Blueprint-thread-safe getters are then used directly in `ABP_Humanoid_Master`:

- `GetThreadSafeFootPlacementAlpha` → `Foot Placement.Alpha`
- `GetThreadSafeLegIKAlpha` → `Leg IK.Alpha`

This prevents the procedural Leg IK solver from overwriting authored full-body poses. The default policy keeps Foot Placement at `1.0`, drives Leg IK to `0.0` at full `DefaultSlot` weight, and also drives Leg IK to `0.0` whenever the combat locomotion layer **or its pending draw/equip transition** is active. This protects weapon idle stances as well as attacks and prevents a `0 → 1 → 0` Leg IK pulse during an equip montage's blend-out. It is configurable in the native Anim Instance class defaults (`Full Body Montage IK Policy`) when a skeleton needs different behavior.

<a id="layers-persistent-combat-state"></a>
#### Persistent combat state

`UProject_JGameplayAbility_CombatToggle` ends immediately after toggling its state. The persistent source of truth is `UProject_JGameplayEffect_CombatMode`, which sets its duration to infinite. In UE 5.8, create `GE_CombatMode` as a Blueprint Gameplay Effect derived from this class, add a `Grant Tags to Target Actor` Gameplay Effect Component, and configure that component to add `State.CombatMode`. Assign the resulting GE to `GA_CombatToggle.CombatModeEffectClass`. The common ability set (`AS_Humanoid_Core`) grants `GA_CombatToggle` to every humanoid class, while job ability sets grant only job-specific abilities.

<a id="layers-animation-priority"></a>
### Animation Priority

1. Combat locomotion layer: full-body armed idle/move/jump.
2. Upper body: aim offset and explicitly movement-compatible short actions.
3. Full body slot: draw/sheath, melee attacks, dodges, hit reactions, strong casts, death.

When entering combat with a draw/equip montage, the job combat layer is linked as soon as that montage starts, while `State.CombatMode` remains pending until the montage completes. This makes the armed idle the montage's blend-out pose and prevents a one-frame fallback to base locomotion. If the intro is cancelled, the prelinked layer is immediately removed unless combat mode is already active.

Weapon profiles keep their combat layer as a soft class reference. `UProject_JCombatAnimationLayerComponent` requests an asynchronous preload for the locally controlled player's equipped profile and retains the resolved class in a one-entry cache. Inactive simulated proxies do not preload merely because their style replicated; they stream the layer only when combat presentation becomes active. If combat begins before loading finishes, the master AnimBP's safe default layer remains active and the requested class is linked by the completion callback. Combat input never performs a synchronous AnimBP load. Dedicated servers skip this presentation-only path entirely, and stale/cancelled requests cannot replace a newer weapon style.

The layer component exposes an explicit presentation state: `Inactive`, `PreparingCombat`, `CombatActive`, or `SuppressedByMount`. This is intentionally separate from authoritative gameplay state; it establishes mount > intro > active > inactive priority without making an entering montage grant combat gameplay privileges early.

Root Motion is permitted for committed actions such as dodge, charge, execution, and committed melee attacks; it is not ordinary locomotion. Use Motion Warping for target-relative actions. The server remains authoritative for movement, hit timing, and final target validation.

<a id="layers-weapon-sockets-and-ik-contract"></a>
### Weapon Sockets and IK Contract

Existing code defaults to `WeaponSocket_R`, while existing equipment documentation uses `weapon_r`; asset names must be checked in the editor before standardization. New shared-character assets should use these canonical names consistently:

- `weapon_r`: right-hand weapon attachment and hit-trace root.
- `weapon_sheath`: stowed melee weapon attachment on back or hip.
- `weapon_l`: optional dual-wield attachment.
- `ik_hand_r`, `ik_hand_l`: common skeleton hand IK targets.
- `ik_weapon_l`: optional off-hand grip target authored on a two-handed weapon.

Until the skeleton assets are confirmed, preserve their actual names and map them in `WeaponPresentationProfile`; do not bulk-rename skeleton sockets in code. A later draw/sheath Anim Notify may move a persistent weapon visual between `weapon_sheath` and `weapon_r`; current runtime presentation spawns the drawn actor on combat entry and destroys it on exit.

<a id="layers-state-and-interruption-policy"></a>
### State and Interruption Policy

- Equipment ownership is persistent inventory state. Drawn/sheath is transient combat presentation: hand versus stow socket, combat layer, and action permission.
- Mounting currently cancels the local draw presentation, restores movement-facing rotation, and unlinks the combat layer. Death and mounting must ultimately force the weapon visual to its safe stowed socket; the server-authoritative gameplay command that removes persistent combat state is the next integration point.
- Advancement is permanent progression. It is not a combat-time toggle and does not belong in the locomotion state machine.
- Lock-on is intentionally out of scope. If added later, it needs its own facing/trajectory policy.

<a id="layers-combat-blend-space-data-bridge"></a>
### Combat Blend Space Data Bridge

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

<a id="layers-data-driven-weapon-combos"></a>
### Data-Driven Weapon Combos

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

<a id="layers-command-inputs-black-desert-style"></a>
### Command Inputs (Black Desert-style)

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

<a id="layers-greatsword-first-attack-editor-setup"></a>
### Greatsword First Attack: Editor Setup

The first milestone is deliberately small: entering combat and pressing LMB plays exactly one greatsword attack montage. Do this before authoring a multi-hit chain, command skill, hit trace, or root-motion attack.

<a id="layers-tag-convention"></a>
#### Tag Convention

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

<a id="layers-assets-to-create"></a>
#### Assets to Create

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

<a id="layers-da_greatsword_weaponprofile"></a>
#### DA_Greatsword_WeaponProfile

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

<a id="layers-da_greatsword_combo-one-node"></a>
#### DA_Greatsword_Combo: One Node

Create one element in `Nodes` and leave `Transitions` empty.

```text
NodeTag           = Combo.Greatsword.Light.1
StartInputTags    = [InputTag.Weapon.LightAttack]
AttackDefinition = DA_Attack_Greatsword_L1
bAllowInputBuffer = true
Transitions       = empty
```

The montage needs a slot compatible with `GA_Melee`. Add the project `MeleeHit` Notify only when hit detection is being tested. Add `UProject_JAnimNotifyState_ComboWindow` only when adding a second node; it has no value in a one-attack test.

<a id="layers-ability-set-and-input"></a>
#### Ability Set and Input

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

<a id="layers-add-the-second-light-attack-later"></a>
#### Add the Second Light Attack Later

After the first attack works, add `Combo.Greatsword.Light.2`, author its montage/section, then add this transition to Light.1:

```text
InputTag       = InputTag.Weapon.LightAttack
TargetNodeTag  = Combo.Greatsword.Light.2
```

Place a `ComboWindow` notify state over the portion of Light.1 where the next input should be accepted. The ability accepts at most one buffered valid input. This is intentional: it provides responsive combat without unlimited macro queueing.

<a id="layers-add-a-command-skill-later"></a>
#### Add a Command Skill Later

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

<a id="layers-validation-checklist"></a>
### Validation Checklist

<a id="layers-removed-compatibility-paths"></a>
### Removed Compatibility Paths

- `UProject_JWarriorComponent` and `UProject_JCombatComponent` were removed. Weapon presentation and SSR validation are now common player components, so a job class contains only genuine job rules.
- `TriggerPlayerAttack`, legacy skill-input fallback, deprecated client equipment request wrappers, and legacy `AbilitySet` arrays were removed.
- Existing Blueprint and Data Asset values on those removed properties do not migrate automatically. Re-save affected assets after assigning `CombatHitValidationComponent` settings and `GrantedAbilityEntries` / `GrantedEffectEntries`.

- Link/unlink each weapon-family layer on local and simulated-proxy combat transitions.
- Verify no combat layer remains linked while mounted.
- Test eight-direction movement, jump, landing, draw, sheath, attack, dodge, hit reaction, and death per weapon family.
- Verify distant-player tiers omit expensive aim/IK while preserving visible combat montages.
- Validate each Combo Definition and test local prediction plus at least two-client PIE for Light→Light, Light→Heavy, invalid-input rejection, buffering before a window, CC/death/mount cancellation, and weapon swap during an active combo.
