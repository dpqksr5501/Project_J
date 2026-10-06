# 이동 중 Strafe 회전의 모션 매칭 연결

2026-10-05, UE 5.8. [이전 F 하차·TAB 수정](Project_J_Mount_Strafe_Fix_2026-10-04.md)의 후속이다. 이전 수정은 capsule의 급회전을 제한했지만, OTM에서 S를 유지하다 TAB으로 Strafe에 진입할 때 회전 모션을 찾을 정보까지 충분히 전달하지 않았다. 이번 변경은 회전 예측·이동 의미·검색 정책을 연결한다.

2026-10-06 후속: [이동 중 180도 Turn](../../Animation/Architecture/Moving_Turn_180_2026-10-06.md)은 아래의 30도 facing-only Turn 진입을 큰 전진 경로/몸 방향 반전으로 제한한다. TAB 자체의 catch-up은 Turn 진입 사유가 아니며, 미래 facing 예측·history 보존·기존 CMC 정책은 유지한다. 아래 내용은 당시 구현·검증 기록이다.

## 확인한 원인과 비교 범위

ArtisticSW2026의 `BasePlayer.cpp`, `GA_PlayerRoll.cpp`, `SWTrajectoryComponent.cpp`, `LocomotionAnimStateComponent.cpp`, `MotionMatchingAnimInstance.cpp`를 읽었다. 구르기는 montage/root motion으로 실행되고, 이후 이동 중 capsule은 별도 catch-up으로 Strafe 방향에 접근한다. 방향 전환 감지 시 전환용 PSD를 열고 재검색하는 경로도 있다. 실제 구르기 에셋·PSD·ABP 연결을 조회하지 않았으므로 특정 복귀 모션이 선택되는 이유 전체를 확정한 것은 아니다. ArtisticSW2026은 수정하지 않았다.

Project_J에서는 다음 경계가 빠져 있었다.

- Combat Strafe 이동은 명시적 Pivot 등의 예외 외에는 Cycle로 분류됐다. S 유지 중 TAB 전환은 입력 방향 변화가 없어 기존 입력 기반 재검색 조건을 만족하지 않는다.
- 엔진의 Strafe 미래 facing은 현재 mesh 방향과 카메라의 회전 속도를 사용한다. 카메라가 고정돼 있으면 CMC가 아직 카메라 방향으로 돌아가는 중이어도 미래 facing에 그 catch-up이 나타나지 않는다.
- 회전 정책 변경이 전체 궤적 history를 초기화해 전환 직전의 이동 특징을 지웠다.
- 이동 입력이 있는 `DesiredFacingDeltaYaw`는 authored movement one-shot용 이동 방향을 나타낼 수 있다. S의 이동 방향과 camera-facing 목표를 같은 값으로 취급하면 회전 의도를 놓친다.

## 변경

### 예측과 실제 이동의 계약

`PredictCombatStrafeFacing`은 grounded moving Strafe에서 CMC의 실제 `RotationRate.Yaw`와 controller desired yaw를 사용해 미래 회전을 예측한다. 고정 카메라의 180도 catch-up도 쿼리에 담으며, 카메라가 움직이면 기존 카메라 yaw-rate 예측을 목표 방향에 반영한다. CMC와 같은 `FixedTurn`을 사용한다. 예측은 미래 sample의 facing만 갱신하며 이동 위치, 과거/현재 facing, capsule을 직접 변경하지 않는다.

공중·idle·OTM·비소유 Pawn·공격·구르기·피격·실제 animation/root-motion source가 회전을 소유하는 경우에는 이 예측을 적용하지 않는다. 원격 캐릭터의 미복제 controller 목표를 추정하거나 새 회전 RPC를 추가하지 않는다. 원격 모습은 기존 replication·trajectory 경로를 사용하며 2인 PIE 확인 대상이다.

회전 정책 변경은 `NotifyRotationModeChanged`로 future prediction의 freshness를 무효화하고 history를 보존한다. 다음 생성이 새 정책을 반영한다. 같은 프레임에 재생성이 필요해도 history는 한 번만 갱신하고 prediction만 새로 만든다. 수동 reset·초기화·presentation wake 등 기존 실제 history-reset 경로는 유지한다.

### 이동 전환과 검색

CombatAnimProfile에 다음 선택적 조정 값을 추가했다.

| 값 | 기본 | 의미 |
| --- | --- | --- |
| `bEnableStrafeFacingRedirect` | true | 이동 중 facing 전환의 예측·모션 검색 활성화 |
| `StrafeFacingRedirectEntryAngle` | 30도 | 이동 중 camera-facing 차이가 이 값을 초과하면 전환 시작 |
| `StrafeFacingRedirectExitAngle` | 5도 | 시작된 전환을 유지하다 정렬되면 Cycle 복귀 |

회전 모션 검색의 의미는 기존 `Turn` phase를 사용하며 새 gameplay state나 montage를 만들지 않는다. 입력 방향 변화가 없어도 회전 전환 진입 edge가 한 번 재검색을 요청한다. 이동 전환에서는 continuing pose를 후보로 유지해 pose-history의 발 디딤 특징을 보존한다. 전환을 유지하는 매 프레임마다 강제 재검색하지 않는다.

Start·Stop·Pivot·점프·낙하·착지·stationary TIP의 기존 우선순위와 direct Blend Stack 소유권을 유지한다. 기존 minimum redirect hold는 정렬 직후의 짧은 database 왕복을 막으며, 상위 전투 동작이나 이동 중단은 이를 선점한다. 이후 Dynamic Cycle을 거쳐 기존 안정 시간 조건을 만족하면 SettledCycle로 들어간다.

### 에셋 계약

기존 CombatStrafeMotionMatchingAssetSet의 Run/Sprint `TurnRedirect`에 **해당 Strafe 이동 전환 후보를 담은 PSD**가 설정돼 있으면 검색한다. 선택적 슬롯이 비어 있으면 기존 Dynamic `Cycle`로 fallback하며, 회전 중 Loop-only `SettledCycle`이나 OTM PSD를 대신 빌리지 않는다. 일반 방향 입력 변경을 모두 Pivot이나 Turn으로 바꾸지 않는다.

기존 Master ABP, Offset Root Bone, Blend Stack, Chooser와 montage를 수정하지 않았다. 새 getter pin 연결은 필요하지 않다. 기존 Dynamic Cycle 안에 전환 후보가 있으면 그대로 검색할 수 있다. 전용 Strafe 전환 PSD를 별도로 사용하는 경우 사용자가 Combat DA의 해당 `TurnRedirect` 슬롯에 연결할 수 있다. OTM 전용 에셋을 이름만 보고 같은 슬롯에 넣지 않는다.

## 검증

직접 `UnrealBuildTool.exe`로 Editor/Game Win64 Development 빌드를 성공했다(exit 0). 최종 로그는 `Saved/Validation/StrafeFacingRedirect_20261005/BuildEditor_Final2.log`와 `BuildGame.log`다. 기존 105개와 신규 3개를 포함한 회귀 **108개 모두 성공**했으며 정상 성공 104개·경고 포함 성공 4개, 실패·미실행·실행 중 0이다. 기존과 동일한 native fixture의 socket/profile 미설정 경고 8건 외에 새 경고는 없다. report는 `Saved/Automation/StrafeFacingRedirect_20261005_Final2/index.json`다.

[검증 JSON](Project_J_Strafe_Facing_Redirect_Validation_2026-10-05.json)에 최종 결과와 변경 소스 11개(기존 10개·신규 테스트 1개)의 작업 전후 hash를 기록했다. 새 테스트는 고정 카메라와 S 유지, 30/60/120fps 실제 CMC와 예측의 일치, 180도 방향 경계, history 보존·같은 프레임 중복 갱신 방지, 상위 동작/opt-out, 전환 진입 재검색, 선택적 PSD fallback 및 검색 연속성을 검사한다. 실제 에셋 연결과 재생 포즈의 시각 품질은 아래 PIE 확인 대상이다.

## PIE 확인

1. 에디터를 다시 열고 OTM에서 S를 유지한 채 TAB 전환·해제를 반복한다. 전환 중 카메라 회전·입력 변경·정지·재전환도 확인한다.
2. 실제 사용하는 Combat DA에서 Dynamic Cycle과 선택적 TurnRedirect가 의도한 PSD인지 확인한다. 전환 모션이 일반 걷기보다 적합한 후보가 되는지 Motion Matching debugger로 선택 asset/time과 trajectory facing을 확인한다.
3. 제자리 회전, Start·Stop, 대각 점프, 공격·구르기·피격 후 복귀를 확인한다.
4. 2인 PIE에서 원격 모습과 지연 상태를 확인한다. NullRHI 테스트는 실제 authored pose·발 미끄러짐·네트워크 시각 품질의 확인을 대신하지 않는다.
