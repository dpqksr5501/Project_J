# Start·Land의 입력 반응과 첫 MM 검색

## 목적과 근거

단발과 지속 모션의 구분은 유지한다. [Live Return](OneShot_Live_Return_2026-10-11.md)은 실제 두 포즈를 겹치는 연결 수단이며, 이번 변경은 그 앞의 입력 적합성·후보 판단을 개선한다. 작은 카메라 입력마다 진입 각도 임계값으로 단발을 끊거나, 복귀 직전에 있었던 회전 요구를 잊고 새 Cycle 관측을 시작하는 문제를 줄인다.

[로컬 GASP CMC 조사](../Planning/GASP_ProjectJ_Naturalness_2026-10-06/02_GASP.md)의 기본 MM·실험적 SM 구분과, Start/Loop 등 후보 겹침·플레이어별 보정 구조를 참고했다. [Epic 공식 GASP 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine)도 Chooser로 검색 데이터베이스를 제한하고, 현재 자세와 이동 모델로 검색하는 구조를 설명한다. 로컬 조사본은 5.7.4이고 설치 엔진 API는 5.8이므로 같은 그래프라고 가정하지 않는다. 이번 구현도 GASP 전체 재현은 아니다.

## 입력의 적합성

로컬 Start·Land에는 `bEnableOneShotInputResponse`를 기본 활성화한다. 동일 키의 world 방향은 카메라가 돌면 같이 변하므로, 키 변경은 진입 당시와 현재 입력을 각각 camera frame에서 비교한다. W를 유지한 카메라 곡선을 W→A/S 변경과 혼동하지 않는다. 옆/뒤 키를 유지한 Strafe도 같은 원리를 쓴다.

실제 Steering이 활성화되어 있고 현재 시각 몸체·이동 관성의 잔여 오차가 프로필의 `SteeringMaxVisualYawError` 안이면 작은 입력을 보정하며 단발을 유지한다. 지속적인 실제 곡선 이동에는 아래 Cycle 양보 정책을 적용한다. 총 카메라 회전량만으로 취소하지 않는다. 다른 방향 키로 바뀌거나 보정 범위를 넘으면 즉시 MM으로 넘긴다. Steering 비활성·stale trajectory·지원하지 않는 소유권은 기존 Start/Land 각도 취소 정책으로 돌아간다. Pivot은 기존 의미 입력 reconciliation과 camera 취소 정책을 그대로 쓴다.

Start의 실제 presentation은 의미 상태가 Cycle로 바뀐 뒤에도 클립을 유지할 수 있다. `IsMovingOneShotInputOwner`는 실제 TransitionToLocomotion/TransitionToLand 소유권을 보정·관측의 공통 기준으로 사용한다. 같은 presentation enum을 사용하는 committed Pivot은 명시적으로 제외한다. 의미 상태가 Cycle이라는 이유만으로 살아 있는 Start의 Steering이나 회전 관측을 중단하지 않는다.

최종 몸체 방향은 primary mesh의 완료된 평가를 사용한다. 시각 방향의 프레임은 MM 선택 결과 프레임과 따로 기록한다. MM이 비활성인 단발 재생 중에도 신선한 몸체 방향을 읽을 수 있지만, 오래된 MM 결과를 새 선택 증거로 인정하지 않는다. 이 값은 게임플레이 yaw나 입력 방향을 변경하지 않는다.

물리적으로 접지한 Land 표시를 실제 공중과 구분하여 moving Land의 기존 Steering을 사용할 수 있게 한다. CharacterMovement의 실제 지상 여부가 우선하며 새로운 점프·공중·몽타주·공격·회피·피격·root-motion owner에는 확대하지 않는다.

착지 컴포넌트의 별도 world 방향·누적 actor yaw 취소도 같은 입력 적합성 판정을 사용한다. 로컬의 신선한 Steering snapshot이 유효한 경우에만 적용하고, 기존 최소 착지 유지 시간·취소 이벤트·원격 정책은 유지한다. AnimBP만 개선하고 컴포넌트가 작은 카메라 곡선을 다시 끊는 두 정책의 충돌을 방지한다.

## 관측과 후보 선택

일반 Turn 정책은 moving Start/Land 동안 이동·Facing 변화율과 alignment demand를 관측한다. 단발 동안에는 후보를 열거나 과거 MM 결과로 완료를 처리하지 않는다. 복귀 첫 샘플에 이미 확인된 correction을 사용할 수 있다. 부드러운 곡선은 rich Cycle을 유지하며, 단순 누적 각도 때문에 Turn을 강제하지 않는다. 옆·뒤 Strafe는 forward Turn pool을 빌리지 않는다.

큰 반전도 기존 `FProject_JMovingTurnPolicy`의 running origin·preparation·forward cone·잔여 몸체 오차를 이용한다. Start/Land 동안은 admission을 미루고, 같은 정책의 value copy로 복귀 자격을 조회한다. 실제 MM 소유권이 복귀하고 신선한 샘플·Steering gate가 유효할 때만 Turn/Cycle 검색 후보를 전달한다. 별도 180도 타이머나 새 회전 manager는 없다. 속도가 처음부터 낮은 Start는 running qualification을 만족하기 전에는 큰 Turn의 origin이 될 수 없다. Sprint의 F/FL/FR 자격, OTM/Strafe 모드, 입력 해제·air/action 소유권은 기존 경계를 유지한다.

AnimInstance NativeUpdate가 locomotion component tick보다 먼저 실행될 수 있다. 입력으로 단발을 해제하는 순간에는 `ProbeOneShotTurnReturn`이 최신 실제 이동 입력·현재 물리 속도·몸체·카메라 방향으로 기존 회전 정책을 value copy에서 조회한다. 일반 tick과 같은 `BuildMovingTurnInput`의 소유권·데이터·Sprint 자격을 사용하며, live 정책은 tick에서 한 번만 갱신한다. 오래된 모드·gait·프레임의 origin을 사용하지 않는다. 첫 검색을 Cycle만 실행한 뒤 다음 프레임에 Turn으로 교체하는 불필요한 검색을 줄이기 위한 처리다.

## 활용하는 노드와 엔진 함수

- Pose History: 최종 출력 collector를 계속 평가하며 복귀에서 reset하지 않는다. 기존 pose-history 기반 invalidate-continuing 검색을 유지한다. `GetPoseHistoryPtr`, `GetNumEntries`, `GetTransformAtTime`으로 실제 이력의 유지 여부를 검사·진단한다.
- MM: 기존 `SetDatabasesToSearch` / `SetInterruptMode` 엔진 경로로 호환 후보를 전달한다. 첫 복귀 검색 요청은 실제 검색까지 유지하고 한 번 처리한 노드는 반복 강제 검색하지 않는다. 입력 하나가 특정 clip/time을 강제하지 않는다.
- MM 내부·외부 Blend Stack: 기존 `GetCurrentBlendStackAnimAsset/Time/Mirrored/MirrorTable` 연결과 플레이어별 ResetRoot·Steering을 활용한다. outgoing 단발은 자기 재생 시계를 진행하고 목표 방향은 최신 snapshot을 사용한다.
- OffsetRoot·FootPlacement·LegIK: 기존 그래프 순서와 소유권을 보존한다. 임의의 새로운 IK나 전체 몸체 보정 노드를 추가하지 않는다.

Pose History의 `SetPoseHistoryNodeTransformTrajectory`와 MM의 `OverrideMotionMatchingBlendSettings` API도 설치 엔진에서 확인했다. 이미 trajectory 핀이 native getter로 연결된 collector를 별도 override하거나, 핀에 덮어써질 BlendSettings 요청을 추가하지 않았다. 과거 자세를 현재 입력으로 새로 생성하거나, 전환 때 history를 지우는 것은 이 설계의 목적에 맞지 않는다.

## 지속 곡선 이동의 Cycle 양보

Steering으로 따라갈 수 있다는 사실만으로 현재 Start/Land가 계속 적합하다고 판단하지 않는다. Run/Sprint의 이동 중 단발에 적용하는 `FProject_JOneShotCurvePolicy`는 실제 actor의 planar 이동 거리와 실제 velocity heading의 일관된 변화를 관측한다. 기본값은 곡선 이동 100cm, 순 heading 변화 20도, 순/절대 heading 변화 비율 0.85이며 모두 TransitionPolicy에서 조정한다. 새 만료 타이머나 누적 camera-angle 취소 조건은 없다.

카메라만 도는 Strafe의 직선 world 이동, 좌우 흔들림, 작은 일시 회전, 막힌 이동, teleport/correction은 곡선의 증거가 되지 않는다. 실제 직선 이동으로 돌아오면 증거를 지운다. 현재/미래 이동 속도가 유효한 fresh trajectory만 사용하고, forecast가 반대 방향이면 곡선 증거를 지운다. CMC forecast가 현재 입력을 따라 직선을 예측하는 것은 허용한다. 미래 마우스 입력을 생성하거나 반드시 미래 trajectory에도 원이 보인다고 가정하지 않는다.

소유권·모드·gait·trajectory reset revision이 바뀌거나 sample gap이 생기면 관측을 초기화한다. Start는 기존 목표 속도 도달 비율과 predicted speed gain, 또는 authored EarlyTransition/end 조건으로 보호한다. Land는 실제 접지와 기존 `bCanExitLanding`의 최소 유지 조건을 지킨다. 입력 방향 변경·Steering 한계 초과·큰 반전은 기존 해제 및 Turn/Cycle 판단이 우선한다. 지원하지 않는 Sprint 방향, action/root-motion/air는 새 정책에 들어오지 않는다.

곡선으로 양보하는 첫 검색은 rich Cycle을 열고 Strafe의 straight-only settled pool과 새 General Turn admission은 사용하지 않는다. 이후 MM 소유권에서는 기존 일반/급회전 정책이 계속 동작한다. OTM은 Arc를 포함한 Cycle, Strafe는 A/D/S와 F/FL/FR에 맞는 방향별 Cycle을 검색한다. 특정 Arc·Diamond·Box 클립을 강제하지 않으며, Pose History 및 기존 Handoff는 그대로 사용한다. 실제 Cycle 데이터가 없는 가족에는 양보하지 않는다.

Native 입력 판정은 Chooser의 최종 override publication보다 먼저 실행된다. 의미 상태가 Cycle로 넘어간 Start는 이 시점에 `bRequested`와 override가 꺼져 있을 수 있으므로, 곡선 관측은 실제 StateController의 held presentation을 확인한다. 의미 상태만 읽어 살아 있는 Start의 곡선 관측을 누락하지 않는다.

이번 추가 검증 결과는 `Saved/Validation/OneShotCurve_20261011`에 기록한다. 기존 작은 회전 검증이 40도에서 입력을 멈췄던 것과 달리, 새 검증은 계속 이어지는 카메라 곡선과 옆/뒤 Strafe 및 비전환 사례를 포함한다.

## 비교와 진단

`AnimFlow OneShotCurve`에는 eligibility·보호 구간 해제 여부·양보 여부·실제 거리·heading sweep·방향 일관성이 남는다. 양보 프레임의 `AnimFlow InputResponse` 이유는 `ContinuousCurveToCycle`이다.

```text
p.ProjectJ.OneShotInputResponse 1
p.ProjectJ.AnimFlow 1
p.ProjectJ.OneShotReturnDebug 1
```

`OneShotInputResponse 0`은 이번 입력·관측 정책을 끄는 비교다. `LiveOneShotReturn 0`은 별도로 외부 포즈 crossfade를 끄는 비교다. 두 기능을 구분한다. `AnimFlow InputResponse`에는 release 이유·Steering 가능 여부·몸체/경로 잔여 오차가, `AnimFlow PoseHistory`에는 실제 entry 수·root query 유효 여부·trajectory 샘플·큰 반전 복귀 자격이 남는다. 진단은 기본 비활성이다.

## 지속 곡선 대응의 최종 검증

- Editor/Game Development 직접 UBT 빌드 모두 성공 (`EditorBuildOwnership.txt`, `GameBuild.txt`).
- 최종 자동화 54/54 성공, 실패·검증 경고 0 (`Verified/index.json`). 기존 52개와 새 공간 기반 정책·실제 곡선 재생 검증을 함께 실행했다.
- 실제 곡선 재생 24장면 (`CurveVerified.txt`): 지속 곡선 18장면 모두 실제 단발을 한 번 해제하여 첫 프레임에 rich Cycle의 fresh MM 검색을 실행했다. OTM/Strafe × Run/Sprint의 Start/Land, Strafe Run A/D/S의 Start/Land, Strafe Sprint FL/FR의 Start/Land를 포함한다. 짧은 5도 입력, 좌우 교대 입력, 카메라만 도는 직선 world 이동의 Start/Land 여섯 장면은 곡선 양보를 일으키지 않았다.
- 45도/초 입력의 기본 W 사례는 Start 진입 후 33~34프레임, Land 진입 후 33~34프레임에 양보했다. 이는 이 fixture의 공간 조건을 만족한 결과이며 시간 기준으로 사용하지 않는다. Strafe W에서는 Arc, A/D/S에서는 Box·Diamond, Sprint FL/FR에서는 방향별 Loop·Diamond가 선택된 사례를 기록했다. OTM 첫 검색의 Loop 선택도 정상으로 인정한다. Arc를 강제하거나 주관적인 모션 품질을 이 로그만으로 입증하지 않는다.
- 기존 작은 회전·키 변경·큰 착지 반전 16장면 (`InputVerified.txt`), 원래 단발 복귀 여덟 장면 (`ReturnVerified.txt`), Sprint/Strafe CMC·일반 Turn·MovingTurn·Pivot·소유권·군중 검색 회귀 검증도 통과했다. 실제 Pose History와 outgoing 포즈 겹침을 유지했다.
- 별도 순수 정책 검증은 양 방향 및 5/30/60도/초의 곡선, 보호 구간, 반대 forecast, 막힌 이동, teleport, 소유권/history 변경, 좌우 흔들림을 검사했다.

에디터에서는 Start/이동 착지 후 이동 키를 유지하며 카메라를 계속 천천히 돌린다. OTM과 Strafe의 W를 먼저 비교한 뒤 Strafe Run A/D/S 및 Sprint F/FL/FR을 확인한다. 잠깐만 돌리고 멈추는 입력과 좌우 흔들림도 비교한다. 그래프 추가나 에셋 저장 없이 적용되며 기존 Standard 전환 설정을 유지한다. 실제 화면의 접지와 자연스러움은 PIE에서 확인한다.

## 곡선 양보 적용 전 입력 유지 검증

결과는 `Saved/Validation/OneShotInput_20261011`에 보관한다.

- Editor/Game Development 직접 UBT 빌드: 모두 성공 (`EditorBuildFinal.txt`, `GameBuild.txt`).
- 최종 자동화: 52/52 성공, 실패 0 (`FinalVerified/index.json`). 새 입력 정책·실제 에셋 재생, 기존 단발 복귀·Pivot·Turn·모드 연속성·Steering 소유권·군중 검색 정책을 함께 검증했다. 기존 Sprint CMC 29장면과 Strafe Camera CMC 57장면도 포함한다.
- 새 실제 에셋 재생 16장면: OTM/Strafe × Run/Sprint 각각 Start 작은 카메라 입력, Start 방향 키 변경, Land 작은 카메라 입력, Land 180도 입력을 확인했다 (`InputPlaybackFinalVerified.txt`). 작은 입력의 Start/Land 여덟 조합 모두 실제 재생 중 회전 관측 33프레임을 확인했다. Land는 실제 착지 에셋을 58프레임 유지했다.
- 급회전 Land 네 조합: 복귀 첫 프레임의 실제 MM 노드 검색 배열에 호환 데이터베이스 2개가 전달됐으며, continuing pose를 재사용하지 않는 새 검색을 확인했다. OTM Run·Strafe Run·OTM Sprint에서는 Cycle, Strafe Sprint에서는 TurnRedirect가 선택됐다. 후보 전달은 Turn 클립의 강제 재생과 다르다.
- 실제 Pose History에 2개 이상의 entry와 유효한 root query가 유지됐고, 원래 단발 복귀 여덟 장면에서 outgoing 에셋 재생 시간·실제 두 포즈 가중치를 확인했다 (`ReturnPlaybackFinalVerified.txt`). 기존 내부 MM·외부 Blend Stack의 플레이어별 보정 그래프 연결도 검사했다.

실제 rendered foot contact와 주관적 자연스러움은 NullRHI 로그로 승인하지 않는다. 이번 작업은 Source와 문서만 변경하며 기존 사용자 Master 저장본·PSS·PSD·Chooser·레벨을 저장하지 않았다.

에디터에서는 기존 Standard 전환 설정을 유지한다. OTM/Strafe 각각 Run/Sprint에서 Start·이동 착지 직후 W를 유지하며 천천히 카메라를 돌리고, Start 중 방향 키를 바꾸거나 이동 착지 직후 크게 반전하는 경우를 비교한다. Strafe Sprint는 기존 허용 방향인 F/FL/FR 안에서 테스트한다. `AnimFlow InputResponse`의 해제 이유와 첫 MM 선택 결과를 함께 보면 보정 유지와 재검색을 구분할 수 있다.

## 후속 Strafe A/S/D 체감 문제: 첫 조사

12:36:49에 종료한 사용자 로그에서는 새 `ContinuousCurveToCycle` 해제가 한 번도 기록되지 않았다. 사용자는 가장 어색한 경우가 A/S/D 이동 중 카메라 회전이라고 확인했다. Published 입력의 camera-relative 방향으로 옆/뒤 이동을 분류한 Evaluated 표본 349개에서 전진 Turn 클립은 0개, Arc는 20개였다. 예를 들어 S의 Frame 9116은 actor/camera 차이가 약 87도, A의 Frame 9960은 약 95도였다. 이는 sampled flow 기록이며 실제 재생 시간 비율이나 최종 시각 몸 방향을 증명하지 않는다. 전진 W에서 관측한 짧은 Turn→Cycle 변경을 A/S/D 문제의 원인으로 혼동하지 않는다.

실제 CMC `StrafeCameraCMC`를 새 에디터 프로세스에서 재실행했다. 기능 ON의 23개 장면과 OFF 비교의 12개 장면은 각각 자동화 검사 1/1 성공했다. 두 실행 모두 A/S/D의 90·240도/초 여섯 장면에서 Arc와 전진 GeneralTurn 후보/선택은 0이었다. 480도/초 A/S/D의 Arc는 ON 161/42/0프레임, OFF 141/49/0프레임이었다. 단발 유지 정책이 초기 검색 자세와 후속 선택에 영향을 주므로 숫자가 같지는 않지만, 기능을 꺼도 빠른 회전의 Arc 선택은 남는다. 이 비교는 새 곡선 해제가 원인이라는 근거나 체감 문제가 해결됐다는 근거가 아니다.

빠른 회전에서는 CMC 몸 회전 한도 360도/초와 카메라 기준 이동 입력, 몸 기준 이동 방향이 달라진다. 첫 로그에 최종 root/Steering/예측 방향이 없으므로 Arc가 물리·시각 방향에 적합한지 확정하지 않았다. 이 첫 조사에서는 Source/PSS/PSD/Master 설정을 변경하거나 에셋을 저장하지 않았다. 근거는 `Saved/Validation/StrafeRegression_20261011`의 User.log, EnabledPlayback.txt, DisabledPlayback.txt 및 두 자동화 보고서다.

이후 사용자의 상세 로그를 받아 묶음 카메라 입력에 따른 미래 경로 흔들림과 Cycle의 짧은 재선택을 재현했다. Source에서 Strafe의 예측 회전율만 안정화한 구현·비교·남은 제한은 [Strafe 예측 안정화](Strafe_Prediction_Stability_2026-10-11.md)에 기록한다. 작은 입력 유지와 지속 곡선 양보를 없애거나 Turn 선택을 강제한 변경은 아니다. 실제 시각 품질은 사용자 재확인이 필요하다.
