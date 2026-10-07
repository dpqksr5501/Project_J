# 일반 회전과 180도 회전의 전환

아래 초기 변경과 검증은 작업 이력이다. 현재 회전 규칙은 마지막 **상태 기반 전방 회전 이벤트** 절에 정리한다.

기준 커밋: `c39cb7f`. 사용자가 연결한 Combat Run TurnRedirect를 보존한다. 첫 변경은 C++의 후보 판정·표현 수명을 다뤘다. 아래 후속 수정은 저속에서 CMC 회전 모드가 입력 소비 순서로 꺼지는 문제를 추가로 다룬다. PSS/PSD, 활성 에셋 목록, 에디터 노드, 이동 속도·가속도·회전 속도 설정은 변경하지 않는다.

## 사용자 로그에서 확인한 문제

Strafe의 `MovingTurn=1` 표본 52개는 모두 실제 180도 PSD를 선택했다. 샘플링된 선택 결과이며 재생 시간의 비율은 아니다. 문제 구간에는 `GeneralTurn → Cycle/Diamond → 180 Turn` 전환과, 이동 입력을 유지하면서 감속할 때 외부 Stop 표현이 나타난다. 180 PSD와 Diamond의 비용 경쟁으로 원인을 단정하지 않는다.

일반 회전은 AnimInstance에서 현재 actor/velocity를 다시 조회하고, 180도 회전은 상태 컴포넌트에서 잡은 이동 표본으로 판정했다. 같은 애니메이션 프레임에 다른 물리 시점의 경로 오차가 후보의 진입·종료를 결정할 수 있었다. State Controller의 보충 Stop 조건에도 입력 유지 여부가 빠져 있었다.

## 구현

- `FProject_JTurnRequestSample`에 상태 컴포넌트가 사용한 actor/facing/move/velocity yaw, speed, world time, frame 및 180 판정 결과를 게시한다. 일반 회전은 이 표본을 함께 사용한다. 오래된 표본(2프레임 또는 0.1초 초과), 미래 시각, 비정상 수치, 모드 불일치는 거부한다. 순수 값만 GT/애니메이션 경계를 넘고 시각 root는 판정에 되먹임하지 않는다.
- 일반 회전이 이미 확인되어 활성화된 경우에만 큰 회전 직전의 짧은 연결을 허용한다. 180 판정의 재진입 준비, 진입 속도, 기존 전방 이동과 새 전방 이동 조건을 충족하고, 몸의 목표 오차가 180 진입 기준 직전 15도 범위이며 경로 오차가 135~150도인 경우다. 최대 0.06초만 기존 GeneralTurn 후보를 유지한다. 이 조건으로 180 PSD에 일찍 진입하거나 새로운 GeneralTurn을 열지 않는다. 180 판정 활성화 시 GeneralTurn은 즉시 해제되어 기존 두 후보 상한을 유지한다.
- 승인된 180도 회전이 활성화된 동안에는 이동 의도를 유지한다. 반전 감속으로 잠시 낮은 속도가 나와도 Idle/보충 Stop으로 넘어가지 않는다. 기존 0.75초 최대 수명, 정렬 완료, 목표 변경, 입력 해제, 실제 Stop·공중·착지·Pivot·공격·회피·피격·몽타주·루트 모션 및 모드 변경의 취소 조건은 그대로다.
- 보충 Stop은 입력 해제에만 적용한다. 명시적인 Stop 요청은 계속 우선한다. 벽에 막혀 키만 유지하는 경우는 180 요청을 만들거나 무한정 이동 판정을 유지하지 않는다.

OTM의 facing은 요청 이동 방향이다. Strafe는 컨트롤러의 목표 facing과 요청 이동 방향을 별도로 사용한다. A/S/D의 측면·후방 이동에는 전방 Turn 후보를 열지 않고 Diamond/Box/Hourglass 등의 방향별 Cycle을 유지한다. 카메라를 180도 돌렸다는 사실만으로 180 클립을 강제하지 않는다. 자세·발 위상·현재 궤적에 따른 최종 선택은 기존 MM이 담당한다.

로컬 GASP 분석의 상태별 후보 배열, 현재 자세를 통한 전환, 물리 이동과 시각 root 책임 분리를 참고했다. GASP의 물리 설정이나 에셋 구성을 복사하지 않았다.

## 에디터 확인

에디터를 새로 열어 W 이동 중 작은 회전, 빠른 양방향 170/180/200도 회전, 같은 입력으로 회전 후 재가속, A/S/D 이동 중 카메라 회전을 확인한다. 회전 도중 입력 해제·회피·전투 모드 전환도 확인한다. 추가 에셋 연결이나 노드 수정은 필요 없다.

진단 명령은 `p.ProjectJ.LocomotionContinuityTrace 2`다. 새 `Stage=TurnRequest`에는 두 판정이 공유한 물리 표본과 `AcuteReason`, `Approach`를 남긴다. `GeneralReason=AcuteHandoff`는 짧은 연결이고, `SelectedPSD/SelectedClip`은 실제 선택이다. 저속 회전에서 `Override=1`의 Stop이 끼어드는지 함께 확인한다. 진단을 마치면 같은 명령에 `0`을 사용한다.

## 검증

최종 Editor/Game 직접 UBT 빌드와 자동화 **30/30 검사**가 통과했다. 30/60/144Hz 양방향 회전에서 일반 회전 확인, 짧은 연결, 180도 진입과 후보 상호 배제를 검사했다. 연결이 종료되지 않는 경우의 0.06초 제한, 후방 이동·저속의 연결 생성 거부, stale/future/mode/invalid 표본 및 기존 중단·continuing 계약도 통과했다.

실제 CMC **41개 장면**은 측정 구간 모두 지상 240/240프레임이었다. 기존 A/S/D 90·240도/초 여섯 장면의 Arc 선택은 각각 0프레임이다. 추가 18개 빠른 회전·중단 장면에서 입력을 유지한 구간의 외부 Stop은 합계 0프레임이었다. Strafe 양방향 180/200/211도는 실제 180 PSD를 선택했다. Strafe 211도 및 OTM 211도에서는 속도가 17.61까지 낮아져도 회전 후보가 유지되고 이후 이동을 회복했다. A/S/D의 빠른 회전에는 전방 180 후보가 열리지 않았다. 입력 해제/회피는 GT 요청을 즉시 취소하고, MM은 애니메이션 표본 경계를 통해 취소를 반영했다. 전투 모드 전환도 기존 요청을 해제했다.

OTM의 빠른 카메라 180도 장면은 실제 몸의 오차가 진입 기준 아래여서 180 후보를 열지 않았다. 카메라 각도만으로 강제 재생하지 않는 정상 결과다. 첫 실행의 CMC 검사에서는 이 둘을 같다고 가정하고 MM 취소도 동일 프레임으로 요구해 28/29였다. 실제 GT 요청의 즉시 취소를 별도로 검사하고, OTM의 실제 큰 오차 장면을 추가한 최종 결과는 30/30이다. 첫 보고서도 보존한다.

근거: `Saved/Validation/TurnHandoff_20261007/verification.json`, `TestsFinal/index.json`, `CMCPlaybackFinal.txt`, `AuthoredPlaybackFinal.txt`, `EditorBuildVerified.log`, `GameBuildVerified.log`. Content/Config 변경은 없고 사용자 로그는 byte/hash가 동일하다. 실제 CMC 검사와 통제된 애니메이션 재생 검사를 구분한다. NullRHI는 화면상의 자연스러움과 발 접지를 인증하지 않는다. 전체 cook/패키징·네트워크·군중 성능 검증은 이번 변경 범위에 포함하지 않는다.


## 후속 사용자 로그: 소비된 입력과 저속 회전 소유권

2026-10-07 11:12:20에 갱신된 사용자 로그를 `Saved/Validation/TurnHandoffUser_20261007/UserBefore.log`에 보존했다. 새 로그의 선택 표본 236개 중 180 후보가 활성화된 24개는 모두 실제 Combat Run Turn PSD를 선택했다. 네 번의 승인 중 두 번은 정렬 완료, 한 번은 저속 자격 해제, 한 번은 진입 목표에서 60도 넘는 목표 변경으로 종료됐다. 이전에 확인한 입력 유지 중 외부 Stop은 새 로그에서 관찰되지 않았다. 표본 통계이며 전체 프레임 통계가 아니다.

Frame 5028에는 입력이 유지되고 속도가 8이지만 `CMCControllerDesired=0`이었다. 다음 애니메이션 표본 Frame 5029는 이 물리 상태로 `Ineligible`을 판단해 180 후보를 닫고 Diamond를 선택했다. `ApplyCombatRotationMode`는 CMC가 소비하는 PendingMovementInputVector 또는 속도 >10만 이동 근거로 사용했다. 소비 이후 Pending이 비면 키를 유지해도 감속 바닥에서 회전 모드가 꺼질 수 있었다. 앞선 실제 CMC 41장면의 최소 속도 17.61은 이 경계를 검사하지 못했다.

물리 회전 소유권의 입력 근거에 로컬 캐릭터의 현재 raw Move Action 캐시를 추가했다. 이 값은 `SetMoveInput`에서 즉시 갱신되고 `ClearMoveInput`에서 즉시 해제되며, 이전 애니메이션 프레임의 `bHasMoveInput`이나 cosmetic semantic direction을 읽지 않는다. Pending/속도의 기존 경로는 보존한다. Strafe 입력이 유지되는 동안 CMC는 기존 360도/초 제한으로 회전하고, 실제 입력 해제·dead zone·공중·프로필 opt-out·OTM·비로컬 캐릭터에는 이 보강을 적용하지 않는다. 벽에 막혀 입력을 유지할 때도 카메라 방향 회전은 유지되지만, 새 180 후보는 기존 진입 속도/경로 조건을 통과해야 한다.

`ConsumedInputAtBrakingMinimum`은 Pending을 실제로 소비한 뒤 속도 8/0/9.99에서 물리 회전 양방향과 release, dead zone, profile, air, OTM, unpossessed를 검사한다. 기존 `SelectionAndLifecycle`도 실제 CMC 속도와 회전 모드를 갱신하며 0속도 회전 수명 유지 여부를 확인하도록 강화했다. `Stage=TurnRequest`에 `Guard`를 추가해 다음 로그에서 `Ineligible`의 구체적인 자격 실패를 구별한다.

Frame 5364의 `TargetChanged`는 진입 시 107.1도에서 168.5도로 목표가 바뀐 별도 사건이다. 이번 수정은 그 60도 보호 조건이나 진입 속도/각도 기준을 완화하지 않는다. 계속 회전하는 마우스의 이벤트 연속성을 다루려면 방향 전환·과회전·기존 180 클립의 잔여 수명을 함께 검증해야 한다.


후속 수정 검증: Editor/Game 직접 UBT 빌드 모두 성공, 자동화 **31/31 성공**이다. 새 `ConsumedInputAtBrakingMinimum`과 강화된 `SelectionAndLifecycle`도 통과했다. 실제 CMC 41장면의 지상 프레임은 각각 240/240이고 빠른 회전 18장면의 입력 유지 중 외부 Stop 합계는 0이다. 8/0/9.99 속도 검사는 CMC pending 입력 소비·회전 모드 적용·물리 회전을 직접 제어하는 별도 회귀 검사이며, 실제 CMC 41장면에 해당 속도가 자연 발생했다는 뜻은 아니다. NullRHI 검사는 화면 자연스러움을 대신하지 않는다.

근거는 `Saved/Validation/TurnHandoffUser_20261007/Verification.json`, `Tests/index.json`, `CMCPlayback.txt`, `AuthoredPlayback.txt`, `EditorBuild.log`, `GameBuild.log`, `UserAnalysis.json`이다. 첫 실행은 AppData DDC/Zen 캐시 접근이 샌드박스에서 차단돼 시작 단계에서 종료됐으며 `SandboxStartupFailure.log`에 보존했다. 정상 캐시 접근으로 재실행해 검사 결과를 얻었다. 사용자가 남긴 `Saved/Logs/Project_J.log`는 보존본과 SHA-256이 동일하고 Content/Config 변경은 없다.


## 상태 기반 전방 회전 이벤트

느린 180도 회전이 한 프레임의 속도·몸의 목표 오차·현재 경로 오차를 동시에 만족해야 했던 문제를 처리한다. 이전의 0.06초 General→acute 연결과 진입 목표에서 60도 벗어나면 즉시 취소하는 규칙을 아래 상태 기반 규칙으로 대체한다. 일반 회전의 기존 확인/안정화 시간과 안전 watchdog은 유지한다.

1. **Observing**: 입력을 유지한 정상 전방 달리기에서 실제 이동 방향을 기록한다. 긴 정상 Arc 곡선은 추적 범위/안전 상한에서 새 기준을 수집하며, 과거 곡선 시작점이 다음 큰 회전에 영구히 남지 않는다. 요청 반대 방향의 순간적인 속도 흔들림으로 기준을 밀어 일반 135도 회전을 과대 계산하지 않는다. 전투 모드 변경, 직접 표현 소유자, 측면·후방 요청은 이전 기준을 폐기한다.
2. **Tracking**: 작은 미정렬부터 기준을 추적하지만 후보·이동 표현 소유권은 바꾸지 않는다. 이 단계를 거쳐 느린 회전의 처음 부분을 놓치지 않는다. 약한 미정렬에서 속도 자격이 사라지면 기준을 폐기해, 벽에서 멈춘 뒤 오래된 달리기를 새 Turn의 근거로 쓰지 않는다.
3. **Preparing**: 실제 경로 오차 또는 완료된 자세의 방향 지연이 커지면 그 달리기 기준을 고정한다. `TravelRedirect`와 `FacingRecovery`를 구분하며, 감속 바닥에서 진입 속도를 다시 요구하지 않는다. 준비 단계는 Cycle 표현이고, 새 General 후보를 임의로 열지 않는다. 이미 확인된 General 회전만 큰 회전 직전까지 연결될 수 있다.
4. **Active**: 같은 방향의 요청이 프로필의 큰 회전 범위에 도달하고 현재 몸/시각 자세에도 보정이 남아 있을 때 Cycle + TurnRedirect 후보를 연다. 마우스를 180도 돌렸다는 사실만으로 강제 재생하지 않는다. 최종 PSD·클립·시간 선택은 MM에 맡긴다.
5. **완료**: 이동 방향과 자세가 회복되면 후보를 닫는다. 몸이 정렬되고 잔여 시각 오차가 큰 회전 진입 범위 아래로 내려온 경우에도 정상 Steering 회복으로 넘긴다. 실제 Turn 선택 뒤 새 Cycle 선택이 확인되면 즉시 종료하고 오래된 Turn continuing 권한을 남기지 않는다. 기하학적 완료에는 기존 continuing 경로를 허용한다.

요청 각도를 부호 있는 연속 값으로 추적해 ±180 경계를 같은 이벤트로 처리한다. 작은 흔들림은 허용하고 의미 있는 반대 방향 요청과 에셋의 지원 범위를 넘어선 회전은 취소한다. 준비/활성 최대 시간은 미완료 상태를 정리하는 안전 상한이며 정상 진입·종료를 기다리는 시간 조건이 아니다.

### 데이터와 책임

`DA_Player_Locomotion` 등 현재 사용하는 Locomotion Profile의 `Locomotion | Turn Event`에 `OTM Forward Turn`, `Strafe Forward Turn`을 추가했다. 속도 자격, 준비 오차, 남은 보정량, 지원 전방 범위, 방향 후퇴 및 watchdog을 각 모드별로 조절할 수 있다. OTM commitment/exit는 새 설정을 쓰고, Strafe의 entry/exit와 opt-out은 호환성을 위해 기존 Combat Profile을 사용한다. 해당 두 값은 Strafe Forward Turn 내부에서 사용하지 않는다는 툴팁을 표시한다. 기본값이 적용되어 추가 에셋 연결이나 노드 수정은 필요 없다. 비정상 수치는 실행 중 유한한 값으로 해석하고, 에디터 데이터 검증은 범위 및 진입/복귀 hysteresis 충돌을 오류로 표시한다.

주요 기본값은 정상 달리기 자격 180cm/s, 준비 경로 45도/시각 방향 60도, 남은 보정 최소 60도, commitment 150도, 새 이동의 전방 범위 45도, 같은 이벤트 요청 상한 225도다. 이 값은 현재 전방 Turn 에셋의 적용 범위를 표현한다. 전체 마우스 누적 각도에 대한 강제 재생 규칙이 아니다. Strafe의 측면·후방 이동은 전방 이벤트를 취소하고 방향별 Cycle을 유지한다. 고정된 월드 경로를 유지하며 카메라만 돌리는 전용 Spin이나 옆걸음 전용 Turn을 추가한 것은 아니다.

완료된 기본 메시 평가에서 선택 PSD와 mesh 기준축을 제거한 root facing을 값 캐시에 게시한다. 상태 컴포넌트는 이 GT 캐시만 읽고 worker proxy를 조회하거나 추가로 동기화를 기다리지 않는다. 2프레임/0.1초 초과, 미래 시각, 모드 불일치, 외부 직접 표현, 무관한 MM 결과 및 interaction 선택은 사용하지 않는다. 시각 정보가 없으면 몸의 facing을 사용한다. 이 정보는 애니메이션 후보 판정에만 적용하며 CMC yaw·이동 목표·카메라·복제 값을 변경하지 않는다.

GASP CMC 분석의 현재/미래 경로에 따른 Pivot, root/capsule 방향 지연에 따른 Spin, Chooser 후보 배열과 실제 선택 결과에 따른 후속 결정을 참고했다. Project_J의 시각 yaw 제한은 45도이며 전방 Turn 데이터의 범위도 다르므로 GASP의 Spin 130도 조건을 복사하지 않았다. 근거는 `Docs/Animation/Planning/GASP_ProjectJ_Naturalness_2026-10-06/02_GASP.md`와 `01_EVIDENCE.md`다.

### 에디터 확인과 로그

에디터를 다시 열고 OTM/Strafe 각각 W를 유지한 채 양방향 180도 회전 속도를 바꿔 확인한다. 빠른 회전 후 조금 더 같은 방향으로 돌리기, 반대 방향으로 되돌리기, A/S/D 중 카메라 회전, 회전 중 키 해제·회피·모드 전환도 확인한다. 이미 몸이 따라잡은 완만한 회전은 계속 Cycle일 수 있다.

`p.ProjectJ.LocomotionContinuityTrace 2`의 `Stage=TurnRequest`에 `Preparing`, `Demand`(0 없음/1 경로 반전/2 시각 방향 복귀), `Sweep`, `RemainingFacing`, `VisualValid`, `VisualYaw`를 추가했다. `Tracking`은 후보를 열지 않는 기준 추적, `PreparedEnter`는 준비 상태에서의 진입, `Aligned`는 정렬 완료, `CycleHandoff`는 실제 Cycle 선택으로의 종료다. `DirectionReversed`, `OverRotation`, `PreparationTimeout`, `Timeout`은 취소/안전 종료를 구분한다. `MovingTurnTrace`의 MinSpeed는 실제 적용한 프로필의 달리기 자격 값이다. 확인 후 trace를 0으로 끈다.


### 변경 owner와 되돌리기

| 위치 | 이번 책임 |
| --- | --- |
| `Project_JTurnEventSettings.h`, Locomotion Profile / `IsDataValid` | 모드별 적용 범위·안전 상한과 데이터 검증 |
| `FProject_JMovingTurnPolicy::Update` | 준비 기준, 큰 회전 진입·유지·완료·취소의 단일 소유자 |
| `UProject_JLocomotionAnimStateComponent::UpdateMovingTurnPolicy` | 현재 물리 표본·입력 의도와 이전 완료 자세 캐시를 이벤트에 전달하고 결과 게시 |
| `UProject_JCharacterAnimInstance::NativePostEvaluateAnimation` | 기본 메시의 평가 완료 결과를 GT 값 캐시에 게시 |
| `FProject_JGeneralTurnPolicy::Update` | 기존 일반 회전과 준비 중 큰 회전 사이의 후보 연결, 동시 소유 금지 |
| Context Builder의 `ResolvePhaseFamily` | 준비 중 Cycle, 활성 중 Turn, 직접 표현 우선권 |
| 기존 selection / MM / Blend Stack / Steering / root / IK / retarget | 기존 역할과 실제 pose/time·발 위상·클립별 보정 경로 유지 |

물리 표본은 현재 GT 요청이며 시각 표본은 직전 완료 평가다. 두 시점의 차이를 숨기지 않고 frame/time freshness와 mode/relevance로 제한한다. 현재 게임플레이 yaw를 시각 yaw로 덮어쓰지 않는다. 추가 Tick·RPC·복제 필드·Chooser 반복·worker UObject 접근은 없다. 로컬 기본 메시의 완료 지점에서 값 복사와 root yaw 계산이 추가되며, 새 성능 개선 수치나 군중 CPU 수치를 주장하지 않는다.

원복할 때는 이번 준비 이벤트·완료 피드백·프로필 설정·상태 연결과 대응 검사만 함께 되돌리고 직접 UBT 재빌드한다. 같은 파일에 앞서 구현된 저속 raw 입력 보강과 공유 물리 표본 등이 있으므로 파일 전체를 기준 커밋으로 restore하면 안 된다. 사용자 Skeleton/PSD/PSS/AnimBP는 이번 변경으로 저장하지 않았으므로 그 에셋을 원복 대상으로 잡지 않는다. 기존 스켈레톤 소켓 변경을 보존한다.


### 최종 검증

Editor/Game 직접 UBT 빌드 모두 성공, 자동화 **36/36 성공**이다. 새 검사는 30/60/144Hz 양방향 OTM/Strafe의 감속 후 준비 진입, 정상 곡선 뒤 새 회전 기준, 연속 정렬 곡선, 시각 복귀와 실제 Cycle 종료, ±180 경계·과회전·방향 후퇴·중단·벽의 오래된 자격·시계 리셋, 준비 중 직접 표현 우선권 및 프로필 데이터 검증을 포함한다. 기존 실제 producer 검사도 몸 정렬과 아직 반전 중인 이동 방향을 구분하도록 강화했다.

실제 CMC **57장면**의 측정 구간은 각각 지상 240/240프레임이다. A/S/D 90·240도/초 여섯 장면은 Arc 선택 각각 0, 입력 유지 중 보충 Stop 합계 0이다. 기존 정상 OTM/Strafe 곡선은 General과 acute Turn을 선택하지 않는다. 추가 16장면은 1.5초 전방 달리기 뒤 양방향 180도를 450/600/900/1200도/초로 돌린다. Strafe 8장면은 모두 준비 진입과 실제 Turn PSD 선택을 확인했다. OTM 600/900/1200도/초 6장면도 그렇다. OTM 450도/초 2장면은 몸이 충분히 따라잡아 준비 후보와 acute Turn 모두 0이다. 최종 포즈/시간은 MM이 선택하므로 OTM의 180도 카메라 회전에도 90/135도 포즈가 선택될 수 있다.

완료된 N1-N6 및 기존 일반 회전 authored graph 검사는 통과했고 135도 회전을 큰 회전으로 과대 계산하지 않는다. 57장면 중 기록한 급회전 프레임의 종료 이유는 정렬 20회, 실제 Cycle handoff 2회이며 Timeout 0회다. 기록 범위 밖의 종료까지 전체 이벤트 수라고 주장하지 않는다. 스트레스 취소/timeout 자체는 별도 정책 검사로 검증한다.

첫 실행 32/35, 두 번째 34/36 보고서도 보존한다. 초기 완료 조건이 일반적인 시각 잔여값을 너무 오래 요구했고, 기준 갱신을 느슨하게 하면 135도를 과대 계산하거나 느린 반전의 처음을 놓칠 수 있었다. 작은 미정렬의 Tracking과 확정 준비를 분리하고 기준의 역방향 밀림을 막은 최종 실행은 36/36이다. 기대 조건을 느슨하게 바꾸어 성공 처리한 것이 아니며, 실제 정렬된 OTM 450 장면에는 Turn을 요구하지 않는다.

근거는 `Saved/Validation/TurnEvent_20261007/Verification.json`, `Tests/index.json`, `CMCMetrics.json`, `CMCPlayback.txt`, `AuthoredPlayback.txt`, `EditorBuildVerified.log`, `GameBuild.log`다. 사용자 Skeleton과 원래 `Saved/Logs/Project_J.log`의 SHA-256은 작업 전후 동일하다. 이번 단계에서 에셋·Config를 저장/수정하지 않았다. NullRHI의 CMC/graph/최종 bone 출력 확인과 화면의 자연스러움은 구분한다. 최종 체감·발 접지는 에디터에서 확인하며, 이번 단계의 전체 cook/패키징·네트워크 세션·군중 성능은 미검증이다.

### 화면 테스트와 커밋 전 정리

computer-use로 PIE를 실행하고 Unreal의 `Input.+key` 명령으로 실제 Enhanced Input과 이동·AnimGraph 경로를 테스트했다. 물리 마우스 입력을 직접 재현한 것은 아니다. 장애물 없는 OTM W 이동의 약 -42도/초 회전 재검증은 선택 로그 205개 중 Idle 6개, Run_Loop_F 199개, Turn 0개였다. Strafe W의 약 42도/초 회전도 Turn 0개였고, A/S/D에서는 Box·Diamond·Hourglass 등이 선택됐다. A 구간 시작에는 직전 빠른 회전에서 이어진 Arc 표본 4개가 포함됐으며 S/D는 Arc 0개였다. 빠른 연속 회전에서는 OTM/Strafe 모두 큰 Turn과 준비 진입을 확인했다. 정확히 180도 부근에서 회전 입력을 멈춘 뒤의 유지 동작은 이 화면 테스트로 확정하지 않았다. 선택 로그 수는 렌더링 프레임 수나 재생 시간 비율이 아니다.

새 테스트 원본과 조건별 요약은 `Saved/Validation/ComputerUse_20261007/Results.txt`와 같은 폴더에 보존했다. 종료 후 주입 입력과 진단 CVar를 해제하고 PIE를 종료했다. 기존 사용자 로그는 백업하고 에디터 출력 로그 화면만 비웠다.

커밋 전 정리에서는 진단 사유를 얻기 위한 정책 복사·추가 Update를 제거하고 실제 Update 결과를 읽도록 변경했다. 예측 진단 함수는 회귀 검사에만 사용한다. 기존 Trace는 기본 꺼짐이며 Shipping에서는 로그를 출력하지 않는다. 회귀 검사는 개발용 조건으로 제한되므로 유지한다. 원본 로그·보고서·빌드 산출물은 Saved에 두고 Git에 추가하지 않는다. 사용자가 앞서 저장한 스켈레톤 소켓 변경은 원본 해시 그대로 커밋에 포함한다.

정리 이후 최종 소스로 Editor/Game 직접 UBT 빌드와 자동화 **36/36 검사**를 다시 통과했다. CMC 57장면은 각각 지상 240/240프레임이며 A/S/D 6장면의 Arc 선택은 각각 0, 입력 유지 중 잘못된 Stop 합계도 0이다. OTM의 이미 정렬된 완만한 회전에는 큰 Turn을 요구하지 않는다. 실제 Update의 진단 사유가 예측 결과와 일치하는 기존 검사도 통과했고 `git diff --check`는 이상이 없었다. 근거는 `Saved/Validation/CommitCleanup_20261007/EditorBuild.log`, `GameBuild.log`, `Tests/index.json`, `CMCPlayback.txt`, `Verification.json`이다. 이 무인 재검증은 화면 테스트에서 남은 물리 마우스 입력과 발 접지의 확인 범위를 확대하지 않는다.
