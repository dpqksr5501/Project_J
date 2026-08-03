# Combat Strafe handoff (2026-08-03)

## 목적

이 문서는 Project_J의 다음 작업인 **Combat Strafe locomotion**을 다른 대화 또는
작업자에게 넘기기 위한 현재 기준점이다. 구현에 착수하기 전 아래 문서를 순서대로
읽는다. 과거 handoff에는 당시의 설계/에셋 상태가 남아 있으므로, 현재 계약은
`GASP_ProjectJ_Locomotion_Parity.md`와 실제 C++를 우선한다.

1. `Docs/GASP_ProjectJ_Locomotion_Parity.md` — 현재 GASP 대 Project_J 정책과
   chooser 계약의 기준 문서.
2. 이 문서 — Combat Strafe의 범위, 금지사항, 요청할 자료.
3. `Docs/MotionMatching_StateController_Handoff_2026-08-01.md` — State Controller
   도입 배경 및 오래된 설계 결정. 현재 문서와 충돌하면 1번이 우선.
4. `Docs/CombatLocomotionArchitecture.md`, `Docs/CombatAnimationArchitectureNotes.md`
   — 전투 상태와 전투 애니메이션의 기존 계약.
5. 아래 C++ 파일과 연관 profile/state-component 코드.

```text
Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h
Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp
Source/Project_JCharacter/Public/Animation/Project_JLocomotionProfile.h
Source/Project_JCharacter/Private/Animation/Project_JLocomotionProfile.cpp
```

## 절대 지켜야 할 분리

Project_J는 GASP와 달리 항상 Strafe가 아니다.

```text
Non-combat / OTM (Orient To Movement)
  - 이동 방향으로 캡슐이 회전한다.
  - Forward 기반 regular Motion Matching 및 OTM one-shot이 현재 주 경로다.

Combat Strafe
  - 조준/컨트롤 방향을 향한 채 이동할 수 있다.
  - GASP의 6방향 Movement Direction, Pivot, Strafe chooser를 이식할 대상이다.
```

따라서 OTM에 Strafe 방향 애셋, Pivot, Target Rotation Delta selector 또는
Steering/Offset Root를 강제로 넣지 않는다. Combat Strafe의 계산/chooser/PSD/애셋
계약은 OTM과 별도 분기여야 한다. 공통화는 입력 스냅샷, profile 접근, 안전한
thread-safe 전달처럼 양쪽 의미가 같은 부분으로만 제한한다.

## 현재 정상 동작 중인 OTM 계약

다음은 회귀시키면 안 된다.

- State Controller는 논리 상태 머신이고 포즈를 직접 출력하지 않는다.
- State Controller Chooser가 단발성 `AnimationAsset`과 metadata를 고른다.
- 선택 결과는 State Controller Blend Stack으로 재생한다.
- regular Motion Matching은 PSD 기반 cycle/arc 검색을 계속 담당한다.
- 최종 ABP는 transition one-shot이 실제 재생 중일 때만 Blend Stack을 우선한다.
  이 조건은 `GetThreadSafeStateControllerShouldOverrideMotionMatching`이다.
- Idle/Loop chooser의 단순 `HasSelectedAnimation`은 MM override 조건으로 쓰면 안
  된다. MM Turn/cycle을 가리는 충돌이 난다.
- OTM Start/Stop/Jump/Land/Fall Off는 State Controller chooser로 관리한다.
- Stop은 `InputFacingDeltaYaw`가 아니라 `StopVelocityDeltaYaw`로 방향을 고른다.
- Start/Reface는 `StateControllerInputFacingDeltaYawForChooser`를 쓴다.
- 단발성 L/R foot variant는 `contact_l`, `contact_r` curve와 phase fallback으로
  선택한다. Strafe loop의 방향/bias와 같은 개념이 아니다.
- Fall Off는 일반 Jump Start와 `bStateControllerFallOffForChooser`로 구분하며,
  최대 hold 시간이 있다.

관련 Chooser의 현재 역할:

```text
CHT_Player_OTM_Ground  : OTM Idle / Start / Reface / Stop
CHT_Player_InAir       : Jump Start / Fall Off / InAir Loop
CHT_Player_Land        : Light / Heavy Land
```

에셋 편집 상태를 추정하지 말고, 필요할 때 사용자가 제공한 화면 또는 명시적 Unreal
MCP 허가로만 확인한다.

## 최근에 시도 후 폐기한 항목

OTM Start 중 마우스 회전 15°에서 같은 Start state를 강제 재선택하여 Reface Start를
고르는 실험은 시각적으로 기대한 결과를 내지 못해 **폐기되었다**. 이 실험을
되살리거나 OTM Turn으로 우회하지 않는다. Combat Strafe 작업은 이 미해결 OTM Reface
문제를 고치려는 작업이 아니며, 해당 경로를 변경하지 않는다.

## GASP 개념을 가져오는 방식

GASP의 `Update_MovementDirection`은 Strafe에 적합하다.

```text
Future Velocity 또는 Acceleration
  + Strafe 기준 회전
  -> local direction angle (-180..180)
  -> F / B / LL / LR / RL / RR
```

의미:

```text
F  : forward
B  : backward
LL : left + left foot forward
LR : left + right foot forward
RL : right + left foot forward
RR : right + right foot forward
```

이 enum은 Combat Strafe loop/selectors용이다. `MovementDirectionBias`는 매 프레임
발 curve를 읽는 값이 아니라 좌/우 Strafe에서 어떤 foot-forward loop variant를
선호할지 정하는 안정적인 정책 값이다.

반면 Project_J one-shot의 `OneShotFoot`은 현재 재생 중인 포즈의
`contact_l`/`contact_r`를 읽어 Start/Stop/Land/Jump의 연결을 자연스럽게 만드는 값이다.
두 정책을 하나의 enum 또는 하나의 latch로 합치지 않는다.

## 권장 구현 순서

### 1. 실제 Combat/RotationMode 계약 확인

먼저 C++에서 아래 사실을 확인한다.

- Combat 시작/종료 시 `RotationMode`가 어떤 enum으로 바뀌는가.
- `Combat == true`와 `RotationMode == Strafe`가 항상 동치인지, 아닌 경우의 우선순위.
- Autonomous proxy, simulated proxy에서 control rotation 및 입력을 사용할 수 있는 범위.
- Strafe 기준 yaw가 Actor yaw인지, aiming/control yaw인지.
- regular MM PSD가 OTM/Strafe를 현재 공유하는지 분리하는지.

게임플레이 권한은 Character/locomotion component에 남기고, AnimInstance는 game-thread
snapshot을 만든 뒤 worker-thread ABP에는 snapshot getter만 공개한다.

### 2. 최소 Strafe Direction snapshot

처음에는 C++에 Combat Strafe 전용 방향 값을 만든다.

- local-space direction angle을 로그 가능하게 유지한다.
- velocity가 거의 0이면 직전 유효 방향을 유지하거나 F로 fallback한다.
- 경계값에서 F/L/B가 매 프레임 튀지 않도록 hysteresis를 둔다.
- OTM에서는 이 값이 asset/PSD 선택에 영향을 주지 않도록 한다.
- Combat Strafe에서만 reflected chooser property와 thread-safe getter로 publish한다.

### 3. 애셋 보유 범위에 맞춰 chooser 설계

애셋이 실제로 존재하는 범위만 먼저 사용한다.

```text
가능한 1차: F / B / L / R loop
가능한 2차: LL / LR / RL / RR loop
후속: Start / Stop / Pivot / Turn in Place / Jump / Land
```

권장 parent selector는 다음과 같다.

```text
RotationMode + Combat
  ├─ OTM     -> 기존 CHT_Player_OTM_Ground
  └─ Strafe  -> 새 Combat Strafe Ground chooser
                 -> Presentation State / Gait / StrafeDirection
                 -> 필요 시 OneShotFoot / Pivot / speed range
```

새 Strafe 행이 기존 OTM table의 `Any` 행을 우연히 가로채지 않게 RotationMode/Combat
조건을 명시한다. row 순서에 의존하지 말고 조건을 서로 배타적으로 만든다.

### 4. pose 경로를 확인한 뒤 최소 ABP 편집

새 PSD/chooser를 연결하기 전, ABP가 Strafe pose를 받아야 하는 지점을 확인한다.

- OTM/MM/StateController Blend Stack 경로를 끊지 않는다.
- Pose History는 최종 Output 직전의 단일 owner를 유지한다.
- Aim Offset, Foot Placement, Leg IK는 기존 최종 locomotion pose 뒤에 유지한다.
- Combat upper-body slot/layer와 locomotion의 우선순위를 확인한다.

## 작업 전에 사용자에게 받을 정보

다음 정보를 받기 전에는 6방향/pivot/one-shot을 가정해 구현하지 않는다.

1. Combat Strafe 애셋 목록 및 정확한 이름
   - Walk/Run/Sprint별 F/B/L/R, LL/LR/RL/RR 존재 여부
   - Loop, Start, Stop, Pivot, Turn, Jump, Land의 존재 여부
2. Strafe 전용 Pose Search Database 또는 Motion Matching Asset Set 상태
3. Combat Strafe 관련 현재 ABP/AnimGraph 화면
   - RotationMode 또는 Combat enum 분기 위치
   - cached pose, linked layer, upper-body layer 연결
4. Combat 진입/종료와 RotationMode를 갱신하는 C++/BP 위치
5. Strafe가 카메라/조준/Actor 중 무엇을 기준으로 하는지의 게임 디자인 결정
6. Strafe 애셋에도 `contact_l`, `contact_r` curve와 Lfoot/Rfoot variant가 있는지

## 디버그와 검증

기존 콘솔 명령을 사용한다.

```text
p.ProjectJ.MMTransitionDebug 1
DumpMotionMatchingTransitionTrace
```

새 로그는 매 frame spam을 피하고 값이 변할 때만 아래를 남긴다.

```text
Combat 상태 / RotationMode
Strafe local direction angle
현재와 이전 StrafeDirection
Gait
선택한 PSD 또는 AnimationAsset
State Controller presentation state
선택 시 OneShotFoot 및 contact curve 값
```

검증은 최소 다음 순서로 한다.

1. OTM Idle/Run/Sprint/Start/Stop/Jump/Land/Fall Off가 기존처럼 동작한다.
2. Combat Strafe 진입/해제에서 T-pose, 빈 pose, OTM asset 혼입이 없다.
3. F/B/L/R 경계에서 direction flicker가 없다.
4. 입력 없음, 저속, 급격한 카메라 회전, Combat 종료, 공중 진입을 각각 확인한다.
5. autonomous proxy와 simulated proxy에서 로컬 입력/컨트롤 yaw를 잘못 공유하지 않는다.

## 코드 변경 후 필수 검증

다음 Editor target 빌드를 실행한다.

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" `
  Project_JEditor Win64 Development `
  "-Project=C:\Users\I\Documents\GitHub\Project_J\Project_J.uproject" -WaitMutex
```

Unreal MCP는 사용자가 명시적으로 허가한 경우에만, 필요한 특정 에셋 범위만 조회한다.
