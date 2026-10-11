# Strafe A/S/D 예측 안정화

사용자가 보고한 문제는 A/S/D를 유지하며 카메라를 돌릴 때 모션이 버벅이는 현상이다. 실제 입력·카메라·CharacterMovement는 유지하고 Motion Matching이 사용하는 미래 경로의 회전율을 안정화한다. 상세 재현에서 남은 짧은 재선택을 확인한 뒤, 사용자 승인으로 Strafe Run Cycle의 Continuing Pose Cost Bias를 0에서 -0.03으로 저장했다. 현재 모션을 검색 비용에서 소폭 우대하며 Turn 후보나 특정 Diamond·Box 선택을 강제하지 않는다.

## 로그와 재현

12:52:02에 종료한 상세 로그의 Cycle 검색에서 짧은 재선택이 확인됐다. Frame 3948의 Box_RL_B_Lfoot는 블렌드 경과 0.008초에 가중치 0.042였고, 두 프레임 뒤 Diamond_FR_B_Lfoot로 바뀌었다. 동일 요청·Cycle 데이터베이스·override 없음·강제 재검색 없음에서도 발생했다. Frame 3651과 6081의 S 이동은 actor와 camera가 정렬된 상태에서도 예측 회전율이 입력 패킷에 따라 흔들렸다. 이번 로그에는 `ContinuousCurveToCycle` 해제가 없어 지속 곡선 양보가 직접 원인이라는 증거는 없다.

설치된 UE 5.8의 MotionTrajectory는 프레임별 controller yaw 차이를 시간으로 나누고 최대 회전율로 제한한 뒤 미래 이동 방향을 생성한다. 입력이 없는 프레임과 큰 입력이 묶여 도착하는 프레임이 반복되면 같은 평균 회전에도 예측 곡률이 바뀐다. 실제 CMC 검증에서 시뮬레이션 120Hz에 카메라 입력을 40Hz로 묶어 이 조건을 재현했다. 이는 사용자 장치·Parsec가 원인이라는 판정이 아니라 입력 도착 간격의 영향을 검사하는 조건이다.

별도로 실제 검색 인덱스의 non-loop 클립 끝과 admission 가능 구간을 조회했다. Box의 마지막 검색 가능 시점에 새로 진입하면 곧 인덱스 밖으로 진행할 수 있다. 하지만 BruteForce 비교에서 끝부분 진입을 시험적으로 막아도 A/S/D 재선택이 줄지 않아 이 변경은 채택하지 않았다. 시험용 수정은 Source에서 제거했으며 PSD·PSS·노티파이·ContinuingPoseCostBias는 수정하지 않았다. `ProjectJ.StrafeDirection.PlaybackCoverage`는 현재 데이터를 읽고 보고서만 생성한다.

## 구현과 적용 범위

`UProject_JMotionMatchingTrajectoryComponent`가 엔진의 `UpdateDataFromCharacter` 직후 raw controller yaw rate를 지수 필터에 전달한다. 필터 결과에 기존 최대 회전율 제한을 적용한 뒤 `UpdatePrediction`과 Combat Strafe의 facing 예측이 같은 값을 사용한다. 제한한 값부터 필터링하면 드문 입력 패킷의 회전량을 잃으므로 raw 값을 먼저 처리한다. 원래 raw rate는 진단과 기존 회전 정책을 위해 보존한다.

Locomotion Profile의 `bEnableStrafePredictionYawSmoothing` 기본값은 true, `StrafePredictionYawRateHalfLife` 기본값은 0.025초다. 이는 예측 회전율의 반감기이며 Turn 진입·단발 유지의 새 시간 조건이 아니다. 기존 에셋은 새 C++ 속성 기본값을 사용한다.

적용은 로컬 OnFoot의 접지한 Combat Strafe 옆·뒤 이동으로 제한한다. 이동 입력과 camera 사이 각도가 프로필의 기존 Strafe 전진 Turn cone 밖인 경우에만 적용한다. W 및 전진 대각 이동은 원래 예측을 유지한다. 실제 body와 camera의 차이가 기존 `SteeringMaxVisualYawError`를 넘는 급회전, OTM, 원격, 입력 해제, 공중, 공격·회피·피격, 몽타주와 root-motion 소유권에도 원래 회전율을 즉시 사용한다. 같은 프레임에 trajectory를 다시 생성해도 회전율을 두 번 소비하지 않으며, trajectory reset·시간 역행·오래된 표본은 이력을 초기화한다.

첫 전체 회귀에서 Strafe W의 지연된 -180도/450도·초 장면은 급회전 후보를 열었지만 authored Turn을 선택하지 못했다. 초기에는 전진 필터의 영향으로 추정했으나, 옆·뒤 범위로 제한한 실행과 기능 OFF 실행에서도 같은 실패가 나왔다. 따라서 이번 필터의 회귀라는 추정은 철회한다. 전진의 기존 검색 결과를 보존하기 위해 옆·뒤 적용 범위는 유지한다. 검사 실패를 지우거나 Turn 선택 조건을 완화하지 않았다.

이 필터는 프로젝트의 재현 결과에 따른 개선이다. GASP가 동일 필터를 사용한다고 확인한 것은 아니다. 기존 Pose History, 외부 Handoff, 내부 MM Blend Stack, 플레이어별 Steering과 OTM/Strafe Turn 정책은 계속 사용한다.

## 비교 결과

동일 40Hz 묶음 카메라 입력과 실제 에셋·CMC·MM 노드로 비교했다. 각 장면의 관측 구간은 120~359프레임이며 override 없이 새 MM 검색 결과가 선택된 횟수를 센다. 아래 숫자는 사용자 플레이 시간 비율이나 렌더링 품질 점수가 아니다.

| 입력 | 원래 재선택 | 안정화 후 재선택 | 안정화 후 짧은 구간 |
| --- | ---: | ---: | ---: |
| A, 90도/초 | 30 | 11 | 10 |
| S, 90도/초 | 20 | 14 | 12 |
| D, 90도/초 | 27 | 7 | 1 |
| A, 240도/초 | 26 | 6 | 5 |
| S, 240도/초 | 19 | 2 | 1 |
| D, 240도/초 | 22 | 2 | 1 |

짧은 구간은 다음 새 선택까지 실제 MM blend time 0.2초를 채우지 못한 경우다. 여섯 장면 모두 양쪽 실행에서 Arc와 전진 GeneralTurn 후보/선택은 0이었다. 480도/초 A/S/D 재선택은 18/26/25에서 16/23/21로 줄었지만 빠른 카메라와 CMC 몸 회전의 차이, Arc 선택 및 짧은 구간이 남는다. S 90도/초도 재선택 14회·짧은 구간 12회·검색 끝에 가까운 진입 4회가 남아 모든 체감 문제가 해결됐다고 판단하지 않는다.

검증 근거는 `Saved/Validation/StrafeRegression_20261011`에 보관한다. `PacketBaseline.txt`와 `PacketFiltered.txt`가 최초 비교이며 `PacketFiltered/index.json`은 실제 CMC와 필터 정책 검사 2/2 성공을 기록한다. 순수 정책 검사는 양 방향 묶음 입력, 같은 프레임 재조회, 입력 정지 후 감쇠, bypass, stale 표본, reset 및 기능 해제를 확인한다.

최종 옆·뒤 범위의 `ScopedPlayback.txt`에서도 위 A/S/D 여섯 장면의 재선택·짧은 구간·끝부분 진입 수치가 동일했다. 기능 OFF의 `BaselineRapidPlayback.txt`는 원래 재선택 수치를 재현했다. W·OTM 장면의 Ground/Moving/Arc, 재선택, GeneralTurn, 급회전 요약은 ON/OFF 사이 차이 0이었다. 옆·뒤 Strafe인 Case 0~8과 33~35는 이 전진 비교에서 제외했다. 기본 40Hz 묶음 입력은 첫 12장면에 적용하며 지연된 급회전 장면의 연속 입력은 기존 조건을 유지한다.

## 최종 검증과 남은 항목

- 수정본 Editor Development 직접 UBT 빌드 성공: `PredictionScopedEditorBuild.txt`.
- 전체 자동화 56개 중 55개 성공, 1개 실패: `ScopedVerified/index.json`. Start/Land 곡선 재생 24장면, 작은 입력·키 변경·큰 반전 16장면, 기존 Handoff 여덟 장면과 실제 Pose History 유지, Sprint CMC·데이터, Pivot·일반 Turn·MovingTurn·소유권·군중 정책, 새 필터 및 읽기 전용 인덱스 진단은 통과했다. 전체 통과로 보고하지 않는다.
- 유일한 실패는 `StrafeCameraCMC`의 Case 41(W, -180도, 450도/초)에서 authored acute Turn 선택을 기대한 조건이다. ON과 OFF 모두 AcuteFrames=18, PreparedFrames=18, PreparedEnterFrames=1, SelectedAcuteFrames=0이며 Arc Cycle이 선택됐다. 급회전 후보 전달과 Turn 클립 선택은 다르다. 현재 데이터·검색 조건의 이 사례는 별도 조사 항목이며 이번 필터가 해결했다고 주장하지 않는다.
- OFF 비교는 `-ForceDPCVars=p.ProjectJ.StrafePredictionYawSmoothing=0`을 사용했다. `BaselineRapid.log`의 CVar 설정과 실제 재선택 수치로 해제 적용을 확인했다. 같은 57장면의 다른 검사 조건은 유지했으며 OFF 검사도 위 조건으로 실패했다 (`BaselineRapid/index.json`).
- Game Development 직접 UBT 빌드 성공: `PredictionGameBuild.txt` (129.54초). Editor/Game 모두 엔진의 직접 `UnrealBuildTool.exe`를 사용했고 각 실행이 정상 종료한 뒤 다음 검증을 시작했다.
- Source·문서 공백 검사 통과. Master 저장본의 크기 1,135,436바이트와 수정 시각 2026-10-11 10:19:38 KST 유지. 사용자 원본 `Project_J.log`와 보존한 `UserDetailed.log`의 SHA256 일치도 확인했다. Master·PSD·PSS·노티파이·레벨은 저장하지 않았다.

## 에디터 비교

새 Editor 빌드로 에디터를 다시 열고 기존 Standard 전환과 데이터 연결을 유지한다. Strafe Run의 A/S/D 각각 카메라 정지 → 천천히 회전 → 빠르게 회전 → 정지를 비교하고 W 및 기존 Sprint F/FL/FR도 확인한다.

```text
p.ProjectJ.AnimFlow 1
p.ProjectJ.LocomotionContinuityTrace 2
p.ProjectJ.StrafePredictionYawSmoothing 1
```

마지막 값 1은 기본 적용, 0은 원래 예측으로 비교한다. 먼저 같은 이동을 0으로 실행하고 1로 바꿔 반복한다. 이 스위치는 예측 필터만 바꾸며 승인 후 저장한 Strafe Run Cycle의 비용 -0.03은 양쪽 비교에 유지된다. 진단 로그는 기본 비활성이고 비교 스위치는 에셋 저장 없이 적용된다. 실제 화면의 접지·모션 자연스러움은 사용자 PIE 재현으로 확인해야 한다. 에셋 저장은 아래 승인한 Strafe Run Cycle 한 개에 한정한다.

## 13:47:48 사용자 재확인: 체감 문제 지속

사용자는 S/A/D에서 버벅임이 남는다고 보고했다. 새 로그 `User_134748.log`는 원본과 SHA256이 같은 보존본이다. 13:46:42에 안정화 스위치 0, 13:46:57에 1이 적용됐지만 `AnimFlow`와 `LocomotionContinuityTrace` 기록은 모두 0개였다. 비교 스위치는 진단을 활성화하지 않는다. 직전 최종 안내에서 진단 명령을 생략한 점을 바로잡아, 위 세 명령을 모두 적용한 재현이 필요하다. 이 로그만으로 재선택·예측·Steering·블렌드 중 남은 원인을 판정하거나 체감 문제가 해결됐다고 보고하지 않는다. 이번 확인에서는 Source나 에셋을 추가 수정하지 않았다.

## 13:50:45 상세 재현과 continuing 비용 비교

`User_135045.log`에는 진단 5,427줄과 안정화 1 적용이 기록됐다. raw rate가 0인데 filtered rate가 -12.7인 Frame 5828 등에서 예측 필터의 적용을 확인했다. 실행 간 입력·길이가 달라 이전 사용자 로그와 재선택 비율을 직접 비교하지 않는다.

Cycle·Strafe·이동 중·override 없음의 표본에서 A 115개, S 118개, D 86개를 확인했다. Actor-camera 차이가 5도 미만이고 root offset이 25도 미만인 경우에도 클립 변경이 각각 29/23/18개 관측됐다. 이는 이벤트·주기 진단의 표본 수이며 실제 점프 총수나 재생 점유율이 아니다. 예를 들어 S의 Frame 5323은 Box_RL_B_Lfoot 새 블렌드 경과 0.008초, actor-camera 차이 0도, root offset 18.3도였다. 0.067초 뒤 Frame 5331에 Loop_B 새 블렌드가 시작됐고 outgoing Box의 상대 가중치는 0.384였다. 두 프레임의 실제 블렌드 시간은 0.2초다. 끝부분 진입이나 큰 카메라 오차만으로 설명되지 않는 교체가 남아 있다.

NodeSettings의 BlendTime 0.2, SearchThrottle 0.05, MaxActiveBlends 4와 프로필 값은 일치했다. A의 Arc 표본 19개는 모두 actor-camera 차이가 컸고, D의 Arc 표본 11개 중 actor-camera가 정렬된 두 개도 root offset이 컸다. S에는 Arc가 없었다. Arc를 일괄 차단하거나 Turn 후보를 늘릴 근거로 사용하지 않는다. 실제 Pose History collector의 외부 trajectory와 유효 root query도 유지됐다.

설치 엔진의 `UPoseSearchDatabase::ContinuingPoseCostBias`는 현재 모션의 검색 비용에 더하는 값이며 기본값은 -0.01이다. 음수는 현재 모션을 소폭 우대하지만 검색을 중단하거나 클립을 고정하지 않는다. 비교 전 `PSD_Combat_Run_Cycle`의 저장 값은 0이었다. 해당 속성은 DDC hash에서 제외되므로 런타임 값 비교에 인덱스 재빌드가 필요 없다. 노티파이의 continuing override가 있는 구간은 DB 기본값과 다르게 동작할 수 있다. GASP 자료에도 continuing·진입 제한·Pose History가 매 update 검색과 실제 교체를 구분하는 요소로 기록돼 있으나, GASP가 모든 Cycle에 동일 숫자를 쓴다고 주장하지 않는다.

`FStrafeCameraPlayback`의 개발 전용 플래그 `-ProjectJStrafeContinuingBias=`로 Strafe Run Cycle만 임시 변경하고 각 world 종료 전에 원래 비용과 검색 모드를 복구했다. OTM은 이 변경 대상에서 제외한다. Modify/PostEditChange/Save는 호출하지 않는다. 실제 방향 입력과 MM/CMC의 12개 장면을 같은 묶음 입력으로 비교했다.

위 플래그는 비용 비교를 위한 당시의 시험 도구다. 승인한 값을 에셋에 저장하고 새 프로세스 검증을 마친 뒤 커밋 전 정리에서 임시 continuing 비용 변경·복원 분기를 제거했다. 현재 재생 검증은 PSD에 저장된 비용을 사용한다. 기존 BruteForce·Velocity 비교 도구는 이번 작업 전부터 있던 별도 진단으로 유지했다.

| 장면 | 기존 값 0 재선택/짧은 구간 | -0.01 | -0.03 |
| --- | --- | --- | --- |
| A 90도/초 | 11 / 10 | 8 / 1 | 5 / 1 |
| S 90도/초 | 14 / 12 | 11 / 7 | 4 / 1 |
| D 90도/초 | 7 / 1 | 6 / 3 | 5 / 1 |
| A 240도/초 | 6 / 5 | 5 / 4 | 3 / 2 |
| S 240도/초 | 2 / 1 | 2 / 1 | 2 / 1 |
| D 240도/초 | 2 / 1 | 2 / 1 | 2 / 1 |

세 실행 모두 자동화 1/1 성공이며 위 여섯 장면에서 Arc와 전진 GeneralTurn은 0이었다. -0.01은 D 90도/초의 짧은 구간이 늘어 충분한 개선으로 선택하지 않았다. -0.03은 90도/초 세 방향의 짧은 구간 합계를 23에서 3으로 줄였으나 rendered 품질을 입증하지 않는다. 근거는 `ContinuingBaseline.txt`, `ContinuingMinus001.txt`, `ContinuingMinus003.txt` 및 각 자동화 보고서다. 개발용 비교 도구의 직접 Editor UBT 빌드도 성공했다 (`ContinuingTrialEditorBuild.txt`).

후속 급회전·입력 해제 57개 장면도 실행했다 (`ContinuingRapid003.txt`). 자동화 검사는 기존 Case 41의 authored Turn 선택 조건 하나로 실패했으며 새 실패는 없었다. 34개 RapidSummary의 acute 후보·선택 프레임, 준비, 해제 후 속도와 false Stop 관측은 기존 `ScopedPlayback.txt`와 차이 0이었다. 이 결과는 기존 실패를 해결하거나 전체 통과로 보고하는 근거가 아니다. 느린 W의 Cycle 선택 분포는 비용 조정으로 달라질 수 있지만 검색·Turn 자격은 계속 유효하다.

## 승인한 데이터 적용

사용자가 “Strafe Run Cycle 한 개 저장 승인”으로 승인한 뒤 `/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_Cycle`의 `ContinuingPoseCostBias`를 0에서 -0.03으로 저장했다. `ApplyContinuingBias.py`는 프로젝트 Saved 경로와 변경 전 값 0을 확인하고 이 PSD 하나만 저장했다. `ContinuingBiasApplied.json`과 `ApplyContinuingBias.log`에 저장 결과가 기록됐다. PSS 특징/가중치, 샘플 인덱스, 블렌드 시간과 기존 예측 필터는 추가 수정하지 않았다.

저장 전 원본 PSD의 SHA256은 `8466F74DFB41B5068FDB611277D5F5E970706112FDFCA037BCBA5CD0515B6ACA`로 모든 임시 시험 후에도 같았다. 원본은 `PSD_Combat_Run_Cycle_before_003.uasset`에 보관했다. 저장 후 SHA256은 `98337B84FEEFE6842E1761A3150BD96C8BB0ACB47B1D29AEADB8A347BF34A5A1`이다. Master의 크기 1,135,436바이트와 수정 시각 2026-10-11 10:19:38 KST는 유지됐다.

새 Editor 프로세스에서 임시 비용 변경 플래그 없이 저장본을 다시 읽어 실제 CMC/MM 12장면을 검증했다 (`ContinuingSaved.txt`, `ContinuingSaved/index.json`). 모든 장면에서 DB 값 -0.030을 확인했고 자동화 1/1 성공, 36.09초였다. 12장면의 EntrySummary·Summary·GeneralSummary 총 36줄은 임시 -0.03 시험과 차이 0이었다. 느린 A/S/D의 재선택은 5/4/5회, 짧은 구간은 각각 1회였으며 90·240도/초의 여섯 장면 모두 Arc·전진 GeneralTurn은 0이었다. 비용을 임시로 덮어쓴 시험 결과가 아니라 저장된 에셋의 재현 결과다.

빠른 회전의 잔여 짧은 재선택과 기존 Case 41 실패는 위 비교 결과대로 남아 있다. 화면의 모션 자연스러움을 완전히 해결했다는 판정은 사용자 PIE 확인 전까지 유보한다. 이번 승인 적용은 PSD 한 개와 이 기록에 한정하며 다른 에셋이나 레벨은 저장하지 않았다. Source·문서 공백 검사도 통과했다.

## 14:22:49 사용자 PIE 확인

사용자가 yaw 스무딩 적용 후 체감이 좋다고 보고했다. 새 로그를 `User_142249.log`로 보존했다. AnimFlow 1, LocomotionContinuityTrace 2, StrafePredictionYawSmoothing 1 명령과 Evaluated 표본 287개가 기록됐다. Frame 7995는 raw camera rate 0, 예측용 filtered rate 57.4이며, Frame 8308은 raw 0에 filtered -2.7로 입력 사이 프레임에도 예측 회전율이 이어지는 것을 확인했다. 스위치 1 입력 전 실제로 기능이 꺼져 있었다고 단정하지 않는다.

Cycle·Strafe·입력 있음·속도 200 이상·override 없음인 표본의 MoveToCamera로 방향을 분류하면 A/S/D가 각각 87/62/70개다. Box·Diamond·Hourglass 및 방향 Loop가 선택됐고 S 표본에는 Arc가 없었다. Actor-camera 차이 5도 미만 및 root offset 25도 미만인 A/S/D 표본 22/17/16개에도 Arc가 없었다. 해당 이동 표본의 전진 GeneralTurn은 모두 0, 평가 root는 모두 유효했고 NodeSettings는 BlendTime 0.2, SearchThrottle 0.05, MaxActiveBlends 4, Inertial 0을 유지했다.

짧은 클립 변경 표본은 여전히 관측되므로 교체가 완전히 사라졌다는 의미는 아니다. 이벤트·주기 로그이고 재현 입력과 길이가 다르므로 이전 사용자 로그와 감소율을 직접 계산하지 않는다. 이번 로그의 위 이동 표본에는 W가 없어 기존 W 급회전 조사 항목까지 확인했다고 보고하지 않는다. 분석 요약은 `Detailed_142249.json`, 읽기 전용 분석 도구는 `InspectUser142249.py`에 보관했다. 이번 확인에서는 런타임 코드·설정·에셋을 추가 변경하지 않았다.

## 커밋 전 정리

현재 정상 동작을 유지하는 범위로 정리했다. 비용 비교용 `ProjectJStrafeContinuingBias` 임시 변경·복원과 Master 최초 적용용 preview/apply 교체·저장 코드를 제거했다. 현재 Master 그래프 테스트는 저장된 연결을 읽기만 한다. Pivot 제외 검사에서 이미 앞선 guard가 처리한 중복 조건을 제거했다. 곡선·일반 입력 재생 테스트의 기본 보고서 경로도 각각 CurvePlayback/InputPlayback으로 나눠 증거를 덮어쓰지 않게 했다. 로그·Python 적용 도구·에셋 백업·테스트 출력은 Saved 아래에 두고 커밋에서 제외한다.

Strafe 예측 필터는 CVar 기본값 1 및 프로필 기본값 true로 자동 적용된다. 매번 콘솔 명령을 입력할 필요는 없다. 로컬 접지 Strafe 옆·뒤 이동과 물리적 facing 오차의 기존 적용 조건을 유지하며 W·전진 대각·OTM의 예측은 바꾸지 않는다. 진단 CVar는 기본 비활성이다.

별도 읽기 전용 점검에서 Animation_Logic/PSD 아래 PSD 50개는 모두 PCAKDTree, PCA 4차원, Leaf 16, KNN 200이었다 (`PSDOptimization.json`). 유사 포즈·PCA 값 병합 임계값은 둘 다 0이다. 검색용 PCA 차원 축소와 추가 유사 데이터 압축을 구분하며, 이번 확인으로 설정을 변경하지 않았다.

최종 정리본의 Editor/Game Win64 Development 직접 UBT 빌드는 모두 성공했다 (`CommitFinalEditorBuild.txt`, `CommitFinalGameBuild.txt`). 저장된 에셋과 임시 비용 변경 없는 최종 코드로 전체 56개 자동화를 실행해 55개 성공·경고 성공 0·실패 1을 확인했다 (`CommitVerified/index.json`, 412.31초). Start/Land 곡선 24장면, 입력 변경 16장면, 실제 복귀 8장면과 소유권·Pivot·군중·Sprint·예측 필터 검사는 통과했다. 실패는 기존 Case 41의 지연된 W -180도 authored Turn 선택 조건 하나이며 후보 18프레임·준비 진입 1회·실제 Turn 선택 0프레임으로 기존 결과와 같다. 34개 RapidSummary도 `ContinuingRapid003.txt`와 차이 0이었다. 전체 통과로 보고하지 않는다.

최종 느린 A/S/D의 재선택 5/4/5회 및 짧은 구간 각각 1회가 유지됐다. 검증 로그에는 에셋 저장 기록이 없고 Master 저장 시각·크기와 PSD 저장 후 SHA256도 유지됐다. Source·문서와 staged 공백 검사를 통과했으며 검증을 위한 로그·백업은 커밋 대상에 포함하지 않는다.
