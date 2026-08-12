# Combat Strafe Turn In Place 구현 가이드 (2026-08-12)

## 목적

Project_J의 Combat Strafe에서 **정지 중 카메라/조준 방향이 몸의 방향과 크게 달라질 때** 90/180도 Turn In Place(TIP) 애니메이션을 재생한다.

이 문서의 범위는 다음으로 한정한다.

- 전투 중 Strafe 회전 모드
- 정지(Idle) 또는 Stop 완료 뒤의 TIP
- 동일 방향을 계속 돌릴 때의 반복 TIP
- TIP 중 `Steering`을 통한 시각 루트 회전 보정

다음은 이 기능의 대상이 아니다.

- 비전투 OTM(Orient To Movement)
- 이동 입력을 누른 상태의 Strafe 방향 전환
- 이동 중 Pivot/Turn Redirect
- 전역 GASP식 Root Motion/Offset Root Bone 누적 회전 정책

---

## 핵심 결론

Project_J의 TIP는 **일반 Motion Matching PSD를 계속 재검색하는 기능이 아니다.**

```text
Combat Strafe + 정지 + 큰 Facing Yaw 차이
    -> C++ locomotion context: bShouldTurnInPlace
    -> 논리 State Controller: TurnInPlace
    -> Chooser: 90/180, 좌/우 단발 애셋 선택
    -> State Controller Blend Stack: 단발 애셋 재생
    -> Blend Stack 내부 Steering: 시각 루트를 목표 facing 쪽으로 보정
```

일반 Motion Matching은 동시에 계속 평가된다. 다만 TIP의 direct Blend Stack이 최종 포즈를 덮는 동안에는 화면에 보이지 않는다.

```text
Regular MM (PSD) ------------------------------------+-> fallback pose
                                                         |
State Controller -> Chooser -> direct Blend Stack ---+-> Blend Poses by Bool -> Locomotion
                                      (TIP override)
```

이 이중 평가 자체는 정상이다. 직접 단발 애셋이 끝난 뒤 즉시 MM 포즈로 복귀하기 위해 필요하다.

---

## 책임 분리

| 영역 | 책임 | TIP에서 하는 일 |
|---|---|---|
| `UProject_JLocomotionAnimStateComponent` | 물리/입력 기반 locomotion 의미 상태 | Combat Strafe, 정지, facing delta를 이용해 `bShouldTurnInPlace` 계산 |
| `UProject_JCharacterAnimInstance` | 게임 스레드 선택, Chooser 캐시, State Controller hold | TIP 상태 진입, 90/180 방향 인덱스, 0.75초 후 재선택, 동일 애셋 재시작 요청 |
| `FProject_JCharacterAnimInstanceProxy` | AnimThread에서 native MM 노드 갱신 | PSD/trajectory 전달, regular MM Pose Search 실행 |
| `ABP_Humanoid_Master` | 최종 포즈 그래프 | MM과 direct Blend Stack을 동시에 평가하고 TIP일 때 direct pose 선택 |
| `CHT_Player_Strafe_TurnInPlace` | 데이터/애셋 선택 | 좌/우 90/180 애셋과 재생 메타데이터 제공 |

### OTM과 Combat Strafe의 구분

| 모드 | 연속 이동 | 정지 회전 |
|---|---|---|
| 비전투 OTM | 기존 OTM Motion Matching | 이 TIP 경로를 사용하지 않음 |
| Combat Strafe | Combat Strafe MM의 Idle/Cycle/Turn Redirect PSD | State Controller direct Blend Stack TIP 사용 |

따라서 OTM에 TIP용 Steering 또는 Chooser를 연결하면 안 된다. Combat Strafe의 정지 상태에서만 동작하도록 C++ gate를 유지한다.

---

## C++ 데이터 흐름

### 1. locomotion context 생성

`UProject_JLocomotionAnimStateComponent`가 매 프레임 locomotion context를 만든다.

- Rotation Mode가 `Strafe`
- 공중 상태가 아님
- 이동 입력이 없음
- Ground Motion Mode가 `Idle` 또는 `Stop`
- `DesiredFacingDeltaYaw`의 절댓값이 임계값 이상

일 때 `DerivedLocomotionContext.bShouldTurnInPlace`를 true로 만든다. 현재 구현의 시작 임계값은 30도다.

관련 파일:

- `Source/Project_JCharacter/Public/Project_JLocomotionAnimStateComponent.h`
- `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent.cpp`
- `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentGround.cpp`

### 2. Thread-safe snapshot과 State Controller

`UProject_JCharacterAnimInstance::BuildThreadSafeData()`가 locomotion component의 값을 복사한다.

- `DesiredFacingYaw`: 절대 월드 yaw. Steering의 Target Orientation에 사용한다.
- `DesiredFacingDeltaYaw`: 현재 actor yaw에서 목표 facing까지의 signed delta. 90/180 asset 선택에 사용한다.
- `bShouldTurnInPlace`: 논리 State Controller의 TIP 진입 조건이다.

`ResolveStateControllerPresentationStateWithPlaybackHold()`는 TIP 상태를 유지하면서 재진입을 처리한다.

```text
TurnInPlace 유지 중
  + 여전히 bShouldTurnInPlace == true
  + 현재 TIP 재생 경과 >= 0.75초
  -> 동일 TIP 상태를 다시 선택
  -> Chooser 재평가
  -> direct Blend Stack에 Force Blend 1회 요청
```

이 규칙이 필요한 이유는 90도 한 번으로 아직 남은 yaw 차이를 모두 처리하지 못할 수 있기 때문이다. 동일한 90도 애셋이 다시 선택되어도 시간 0부터 재생돼야 한다.

관련 파일:

- `Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h`
- `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp`

주요 함수/값:

- `GetThreadSafeStateControllerShouldTurnInPlace()`
- `GetThreadSafeStateControllerShouldAbortTurnInPlace()`
- `GetThreadSafeStateControllerTurnInPlaceSteeringAlpha()`
- `GetThreadSafeStateControllerDesiredFacingRotator()`
- `GetThreadSafeStateControllerShouldForceBlend()`
- `TurnInPlaceReentryDelay = 0.75f`
- `bStateControllerForceTurnInPlaceReselect`

### 3. TIP 방향 선택

`DesiredFacingDeltaYaw`는 다음 인덱스로 변환된다.

| Delta Yaw | Chooser index | 애셋 |
|---:|---:|---|
| `[-135, -30]` | 1 | Left 90 |
| `< -135` | 2 | Left 180 |
| `[30, 135)` | 3 | Right 90 |
| `>= 135` | 4 | Right 180 |
| `(-30, 30)` | 0 | 선택 없음 |

Chooser는 이 인덱스를 `CHT_Player_Strafe_TurnInPlace`의 float 범위 열로 받아 애셋을 결정한다.

### 4. 같은 애셋 재생 문제와 Force Blend

Blend Stack은 입력 애셋 레퍼런스가 바뀔 때 자연스럽게 blend한다. 하지만 연속 TIP에서는 같은 `M_Neutral_Stand_Turn_090_R`가 다시 선택될 수 있다. 애셋 레퍼런스가 같으면 Blend Stack은 원칙적으로 새 재생으로 보지 않는다.

그래서 C++는 새 TIP chooser 결과가 선택된 직후 `bForceBlendNextUpdate`를 한 프레임만 true로 발행한다. AnimBP의 Blend Stack `On Update` 함수가 그 신호를 받아 `Force Blend On Next Update`를 호출한다.

이 경로가 없으면 첫 회전 모션이 끝날 때까지 다음 TIP가 시작되지 않는 증상이 발생한다.

---

## AnimBP 구성

### 최상위 Locomotion 그래프

`ABP_Humanoid_Master`에서 다음 두 포즈는 모두 살아 있어야 한다.

1. `Get Current Active Pose Search Database Thread Safe -> Motion Matching`
2. `StateController -> State Machine Blend Stack`

State Controller는 논리 상태를 갱신하기 위해 항상 평가되어야 한다. 직접 Blend Stack이 유효한 단발 애셋을 선택했을 때만 `Blend Poses by Bool`로 regular MM보다 우선한다.

직접 Blend Stack의 입력 핀은 다음 Thread-safe getter에 연결한다.

| Blend Stack pin | Getter |
|---|---|
| Animation Asset | `GetThreadSafeStateControllerSelectedAnimation` |
| Animation Time | `GetThreadSafeStateControllerSelectedAnimationStartTime` |
| Loop | `GetThreadSafeStateControllerSelectedAnimationShouldLoop` |
| Blend Time | `GetThreadSafeStateControllerSelectedAnimationBlendTime` |
| Blend Profile | `GetThreadSafeStateControllerSelectedAnimationBlendProfile` |

Override bool은 `GetThreadSafeStateControllerShouldOverrideMotionMatching`의 반전으로 현재 그래프 계약을 유지한다.

### Blend Stack 내부 그래프: TIP Steering

`State Machine Blend Stack`을 더블 클릭한 내부 그래프에서 다음 순서를 유지한다.

```text
Blend Stack Input
  -> Local To Component
  -> Orientation Warping
  -> Steering
  -> Component To Local
  -> Output Pose
```

`Steering` 노드 설정/연결:

| Steering pin | 연결 |
|---|---|
| Component Pose | `Orientation Warping` 출력 |
| Alpha | `GetThreadSafeStateControllerTurnInPlaceSteeringAlpha()`와 `enable_turninplacesteering` curve 값의 곱 |
| Target Orientation | `GetThreadSafeStateControllerDesiredFacingRotator()` |
| Current Anim Asset | `Get Current Blend Stack Anim Asset` |
| Current Anim Asset Time | `Get Current Blend Stack Anim Asset Time` |
| Animated Target Time | `0.5` |
| Procedural Target Time | `100000` |

`enable_turninplacesteering` curve는 애셋의 실제 커브 이름과 대소문자까지 동일해야 한다. 현재 프로젝트의 소문자 이름을 그대로 사용한다.

`Orientation Warping`은 기존 Combat Strafe 대각 이동 보정용이다. TIP 구현을 위해 새 전역 Orientation Warping을 추가하지 않는다.

### Blend Stack On Update 바인딩

`State Machine Blend Stack` 노드의 Details 패널에서 `On Update`를 `OnUpdate_StateMachineBlendStack`에 바인딩한다.

함수 내부 그래프:

```text
OnUpdate_StateMachineBlendStack(Context, Node)
  -> Branch(GetThreadSafeStateControllerShouldForceBlend)
      True -> Convert to Blend Stack Node(Node)
           -> Force Blend On Next Update(Blend Stack Node)
  -> Return
```

`Convert to Blend Stack Node`의 `Result`가 false인 경우에는 호출하지 않도록 Branch 조건에 함께 묶어도 좋다. 이 함수는 Blend Stack 노드 자체의 update 순간에 실행돼야 하므로, 일반 Event Graph에서 호출하면 thread-safe 규칙에 맞지 않는다.

---

## Chooser/애셋 저작

### `CHT_Player_Strafe_TurnInPlace`

행마다 다음을 확인한다.

| 결과 애셋 | State Controller Turn In Place Index | UseMM | Loop | StartTime | Blend Time | Tags |
|---|---:|---:|---:|---:|---:|---|
| Turn 90 Left | 1 | false | false | 0.0 | 0.3 | `TurnInPlace` |
| Turn 180 Left | 2 | false | false | 0.0 | 0.3 | `TurnInPlace` |
| Turn 90 Right | 3 | false | false | 0.0 | 0.3 | `TurnInPlace` |
| Turn 180 Right | 4 | false | false | 0.0 | 0.3 | `TurnInPlace` |

> 이 Chooser는 `State Controller Turn In Place Index for Chooser` float 열을 사용한다. 범위는 각 행에서 `1~1`, `2~2`, `3~3`, `4~4`로 저작한다. 30/135도 경계는 C++가 이 index로 변환하는 정책이다.

`UseMM=false`가 의도다. 이는 TIP 애셋을 regular MM처럼 지속 검색하지 않고, Chooser가 고른 단발 애셋을 direct Blend Stack에서 재생한다는 뜻이다.

### 애셋 커브

각 TIP 시퀀스에 다음 curve가 존재하는지 확인한다.

```text
enable_turninplacesteering
```

권장값:

- 회전이 필요한 구간: `1`
- 시작/마무리에서 Steering을 약하게 하려면 해당 구간을 `0 -> 1 -> 0`으로 저작
- 모든 구간에 필요 없으면 0인 구간을 남긴다.

---

## 해결한 문제와 원인

### 문제 1: 첫 회전만 되고 연속 회전이 안 됨

**증상**

- 90도 TIP가 재생된 뒤 마우스를 계속 돌려도 다음 TIP가 시작되지 않음
- 기존 애니가 끝난 뒤에야 다음 회전이 가능함

**원인**

- State Controller가 같은 TIP 애셋을 다시 선택해도 Blend Stack은 애셋 레퍼런스가 같다고 판단함
- 따라서 재생 시간이 0으로 되돌아가지 않았음

**해결**

- TIP 상태가 유지된 지 0.75초 이상이면 chooser를 재선택
- `bForceBlendNextUpdate`를 한 프레임 발행
- Blend Stack `On Update`에서 `Force Blend On Next Update` 호출

### 문제 2: 연속 TIP 뒤 제자리에서 Run 애니가 잠깐 보임

**증상**

- 플레이어는 정지 상태
- TIP 단발 애셋이 끝난 직후 `M_Neutral_Run_Hourglass...` 같은 Run 에셋이 잠깐 보임

**덤프 증거**

```text
Phase=6 RequestedPSD=PSD_Combat_Run_Cycle
Anim=M_Neutral_Run_Hourglass_RL_BL_Lfoot

이후

Phase=0 RequestedPSD=PSD_Combat_Idle
Anim=M_Neutral_Stand_Idle_Loop
Players=2
```

`Players=2`는 regular MM 내부 Blend Stack에서 Run이 blend-out 중이라는 뜻이다. 문제의 근본은 blend 시간이 아니라, TIP 중 MM의 바닥 PSD가 Run Cycle로 선택된 것이었다.

**원인**

`EvaluatePoseSearchDatabaseOnGameThread()`가 Start/Stop/Pivot/TurnInPlace 등 direct one-shot 상태를 기본적으로 `Cycle`로 평탄화했다.

```text
TurnInPlace -> CombatSelectionContext.PhaseFamily = Cycle
             -> PSD_Combat_Run_Cycle
             -> TIP 종료 순간 그 pose가 노출
```

**해결**

`TurnInPlace`만 예외로 `Idle` PSD를 유지한다.

```text
TurnInPlace -> CombatSelectionContext.PhaseFamily = Idle
             -> PSD_Combat_Idle
             -> TIP 종료 뒤 Idle pose로 즉시 복귀
```

수정 위치:

`Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp`

Start/Stop/Pivot의 기존 Cycle fallback은 이 변경으로 건드리지 않는다.

---

## 디버깅

### Console Variables

```text
p.ProjectJ.TIPDebug 2
p.ProjectJ.MMNetDebug 2
p.ProjectJ.MMTransitionDebug 1
```

| 명령 | 확인 내용 |
|---|---|
| `p.ProjectJ.TIPDebug 1` | TIP 상태/선택 애셋 변화만 기록 |
| `p.ProjectJ.TIPDebug 2` | 위 정보와 0.1초 단위 TIP telemetry |
| `p.ProjectJ.MMNetDebug 1` | PSD 선택, revision, force reselect 변화 |
| `p.ProjectJ.MMNetDebug 2` | MM state/trajectory 주기 로그도 추가 |
| `p.ProjectJ.MMTransitionDebug 1` | MM 내부 Blend Stack transition trace 캡처 활성화 |

재현 뒤 콘솔에서 실행:

```text
DumpMotionMatchingTransitionTrace
```

### 로그 판독

`TIPDiag`의 주요 항목:

| 필드 | 정상 기대값 |
|---|---|
| `InTIP` | TIP 동안 `true` |
| `ShouldTurn` | 남은 yaw 차이가 임계값 이상인 동안 `true` |
| `ForceBlend` | 새 TIP 선택 프레임에만 `true` |
| `Asset` | `M_Neutral_Stand_Turn_*` |
| `SteeringAlpha` | curve와 TIP 상태가 모두 유효한 구간에 0보다 큼 |
| `Input` | TIP 중에는 `false`; `true`가 되면 즉시 취소 가능 |

MM trace의 주요 항목:

| 필드 | TIP 중 기대값 |
|---|---|
| `RequestedPSD`, `NativePSD` | `PSD_Combat_Idle` |
| `Anim` | Combat Idle의 idle sequence |
| `Phase` | Idle (`0`) |
| `Players` | 전환 중 2일 수 있으나, 이전 Run 에셋이 새로 선택되면 안 됨 |

### 자주 틀리는 지점

- `enable_turninplacesteering` curve 대소문자가 노드의 Curve Name과 다름
- Steering의 Target Orientation에 delta yaw를 넣음. 현재 getter는 **절대 Desired Facing yaw**를 반환한다.
- Steering Alpha에 TIP getter만 연결하고 curve를 곱하지 않음
- `OnUpdate_StateMachineBlendStack`을 Event Graph에서 호출하거나, Blend Stack 노드 Details의 `On Update`에 바인딩하지 않음
- Chooser의 TIP row가 `UseMM=true` 또는 `Loop=true`
- 전투/Strafe 외에도 TIP gate를 열어 OTM animation을 침범함
- `State Controller`가 MM branch와 함께 평가되지 않아 logical state가 멈춤

---

## PIE 회귀 체크리스트

1. 비전투 OTM에서 이동/정지는 기존 동작을 유지한다.
2. Combat Strafe에서 이동 입력 + 마우스 회전은 TIP가 아니라 기존 Strafe MM으로 동작한다.
3. Combat Strafe에서 정지 후 약 30도 이상 회전하면 적절한 Left/Right 90 또는 180 TIP가 나온다.
4. 같은 방향으로 계속 회전하면 약 0.75초 뒤 동일 애셋도 다시 시작한다.
5. 회전 도중 이동 입력을 넣으면 TIP가 취소되고 Strafe locomotion으로 복귀한다.
6. TIP가 끝난 뒤 Run 에셋이 제자리에서 노출되지 않고 `PSD_Combat_Idle`로 복귀한다.
7. Jump/Fall/Land/Stop one-shot과 OTM 경로가 회귀하지 않는다.
8. `p.ProjectJ.TIPDebug 2`에서 local autonomous actor의 `ActorYaw`, `ControlYaw`, `DesiredYaw`, `Delta`가 일관되게 갱신된다.

---

## 유지보수 원칙

- TIP 반복 주기(현재 0.75초)는 성능용 MM polling 주기가 아니다. 직접 단발 애셋 재선택을 허용하는 presentation 규칙이다.
- regular MM은 별도 최적화 정책에 따라 업데이트된다. TIP 때문에 regular MM을 매 프레임 강제 재검색하지 않는다.
- 전역 `Steering`, 전역 `Offset Root Bone Accumulate`, `Root Motion from Everything`을 켜서 이 구조를 대체하지 않는다. 현재 프로젝트의 capsule yaw/CharacterMovement 소유권과 충돌할 수 있다.
- 새로운 무기/캐릭터는 Combat Strafe TIP chooser row와 `enable_turninplacesteering` curve를 함께 추가한다.
- TIP 애셋 길이나 회전량이 달라지면 30/135도 구간과 0.75초 재진입 시간을 함께 플레이테스트한다.
