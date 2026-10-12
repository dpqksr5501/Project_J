# 지속 곡선 이동 판정과 마스터 ABP 함수 정리

전방 PSD의 최신 구성과 A/D 측면 후보 보호는 [2026-10-13 후속 기록](Strafe_Forward_Intent_2026-10-13.md)을 기준으로 한다. 아래 가중치 실험·후보 수·실패 수치는 각 실행 당시의 기록이다.

## 목적

통합 재생 스택은 유지하면서 두 가지를 개선한다. OTM/Strafe에서 같은 입력을 유지하며 천천히 회전할 때 Start/Land가 끝까지 유지되는 현상과, Strafe W 이동 중 아주 작은 마우스 입력만으로 넓은 Cycle 후보군이 열리는 현상이다.

이전 구조 변경은 `Unified_Locomotion_Implementation_2026-10-12.md`에 기록했다. 이번 변경은 이동·재생 소유권을 다시 바꾸는 작업이 아니라, 단발 종료와 일반 이동의 후보군 판정 규칙을 맞추는 후속 개선이다.

## State Controller의 현재 역할

네이티브 State Controller와 Chooser는 계속 사용한다. Start, Stop, Jump, Land, Pivot, Turn In Place 등의 의미 상태와 단발 애니메이션 선택·필수 유지 조건을 관리한다. `Project Unified Locomotion` 노드가 해당 재생 명령을 받아 MM과 같은 스택에서 실행한다.

기존 ABP의 State Machine 재생 경로와 외부 Blend Stack은 앞선 재설계에서 제거했다. 그 경로에만 사용되던 다음 마스터 함수 그래프 9개를 이번에 정리했다.

- `OnUpdate`
- `OnStateEntry_IdleLoop`
- `OnStateEntry_TransitionToLocomotion`
- `OnStateEntry_LocomotionLoop`
- `OnStateEntry_TransitionToIdle`
- `OnStateEntry_TransitionToInAir`
- `OnStateEntry_InAirLoop`
- `OnStateEntry_TurnInPlace` — 저장된 그래프 이름 끝의 공백도 확인했다.
- `OnUpdate_StateMachineBlendStack`

삭제 전 호출 노드, delegate, 애니메이션 노드의 초기/관련성/업데이트 콜백과 핀 바인딩을 확인했다. 마스터의 살아 있는 그래프에서 참조가 없었다. 네이티브 State Controller API와 직렬화 호환용 함수는 유지한다. 현재 샘플 그래프의 보정, Inertialization, Lean과 나머지 하위 출력 처리는 그대로 사용한다.

## 공통 판정

`FProject_JContinuousCurvePolicy`가 실제 위치·속도 방향과 요청 이동 방향을 관찰한다. 같은 규칙을 단발 종료와 Strafe Settled/Dynamic Cycle 선택에 적용한다. 각 소비자의 상태는 따로 유지하므로 단발 episode의 초기화가 일반 이동의 관찰 이력을 지우지 않는다.

| 설정 | 기본값 | 의미 |
| --- | --- | --- |
| Window | 1.0초 | 판단에 사용할 최근 이동 범위 |
| Confirmation | 0.35초 | 실제 회전 의도가 이어진 최소 시간 |
| TravelDistance | 100cm | 실제 이동해야 하는 최소 거리 |
| HeadingSweep | 3도 | 같은 방향으로 변화한 실제 이동 방향 |
| DirectionConsistency | 0.85 | 왕복 잡음과 일관된 회전의 구분 |
| QuietGrace | 0.12초 | 입력 전달 간격과 짧은 공백 허용 |

위 조건은 함께 충족해야 한다. 타이머만 지난다고 Arc를 강제로 선택하지 않는다. MM에 적합한 Cycle 후보군을 열고, 최종 에셋 선택은 현재 포즈와 궤적의 비용에 맡긴다. 기본값에서는 5도/초의 실제 곡선 이동이 대략 0.6초 후에 확인된다. 최근 1초 동안 3도에도 못 미치는 매우 느린 방향 변화는 미세 보정으로 취급한다. 의도한 감각이 다르면 Locomotion Profile의 `TransitionPolicy.ContinuousCurve`에서 조절한다.

한 번의 작은 목표 방향 변경 뒤 캐릭터가 따라잡는 것만으로 지속 회전으로 판정하지 않는다. 요청 방향과 실제 이동 방향이 함께 같은 방향으로 이어져야 한다. 유예 시간은 회전 확인 시간에 더하지 않는다. 입력이 매 프레임 변하지 않는 경우에도 짧은 공백을 허용하므로, 작은 프레임별 변화나 묶음으로 들어오는 마우스 입력 때문에 증거가 매번 초기화되지 않는다.

정지·막힘·텔레포트·시각 역행·관찰 공백·모드/보행 변경·단발 episode/궤적 초기화는 해당 소비자의 증거를 폐기한다. 고정 크기 64개 샘플을 사용하고 관찰 중 동적 할당은 없다. CMC 이동, 네트워크 동기화와 애니메이션 예산 정책의 소유권을 변경하지 않는다.

## 단발 복귀와 Strafe 후보군

Start는 기존 속도/예측 가속 또는 애니메이션 종료 준비 조건을 먼저 만족해야 한다. Land는 기존 `bCanExitLanding`을 먼저 만족해야 한다. 곡선 증거가 쌓여도 이 보호 구간을 건너뛰지 않는다. 기존의 큰 회전 증거에 의한 빠른 복귀 경로도 유지한다. 반대 방향 미래 궤적은 복귀 증거로 사용하지 않는다.

Strafe에서 두 군데에 있던 순간 회전 속도 기반 확장을 정리했다. 컴포넌트의 15도/초 카메라 판정과 일반 Turn 정책의 8도/초 Strafe 판정이 작은 입력 한 번으로 Loop-only PSD를 해제하지 않는다. 실제 입력 방향 변경, 큰 속도/입력 불일치, 몸체/시선 불일치와 급회전의 기존 대응은 남는다. 곡선이 끝난 뒤에는 기존 Settled 지연과 일반 정책의 유예 시간을 거쳐 안정화한다.

기존 `StrafeDynamicFacingYawRate` 필드는 저장 호환을 위해 deprecated 상태로 유지하지만 현재 후보군 전환에는 사용하지 않는다. 방향별 Diamond 에셋을 일괄 삭제하거나 PSS 가중치를 임의로 올리지 않는다. Loop가 화면에 재생된다는 사실과 실제 검색 PSD가 Loop-only라는 사실을 구분해서 확인한다.

## 검증 기록

원본 로그·마스터 백업은 `Saved/Validation/ContinuousCurve_20261012/`에 보존한다.

- `EditorBuild_Final.log`, `GameBuild_Final.log`: 직접 UBT로 Editor/Game Development 빌드 성공.
- `Regression60/index.json`: 관련 자동화 43개 성공, 실패/미실행 0개. 함수 제거 후 실제 BP/GAS/CMC와 MM으로 검증했다.
- `Regression60/CurvePlayback.txt`: 기존 24개와 추가 12개, 총 36개 조건을 실행했다. 느린 좌우 회전, Start/Land, OTM/Strafe, 순간 입력·왕복 잡음·카메라만의 회전과 실제 MM 검색 결과를 확인했다.
- `Refinement30/index.json`, `Refinement120/index.json`: 각 16개 성공, 실패 0개. 저장된 마스터를 별도 프로세스에서 다시 읽고, 추가 12개 실제 재생 조건과 공통 정책·급회전·Settled 복귀 검사를 실행했다. 5도/초 복귀는 30Hz에서 단발 선택 후 21프레임(0.70초), 120Hz에서 86프레임(0.72초)이었다.
- `Rendered60/index.json`: 실제 RHI의 별도 실행에서 4개 성공, 실패 0개. 추가 12개 재생 조건을 다시 실행하고 Strafe Start/Land의 5도/초 복귀 장면을 `Rendered60/Frames`에 캡처했다. 복귀 프레임과 0.2초 뒤 포즈를 직접 확인했다. 정지 이미지와 컴포넌트 공간 발 변위만으로 월드 발 미끄러짐이나 모든 상황의 체감 품질을 인증하지 않는다.
- 5도/초 곡선은 Start/Land 모두 단발 선택 후 43프레임, 약 0.72초에 복귀했다. 반대 방향 10도/초 곡선은 25프레임, 약 0.42초에 복귀했다. 이 수치는 해당 fixture의 입력 시작 지연·실제 가속·착지 조건을 포함한다.
- 0.5도 순간 입력과 ±0.15도 반복 입력의 Start/Land 후 안정 이동에서 `PSD_Combat_Run_Loop`과 `M_Neutral_Run_Loop_F`를 확인했다. 곡선 복귀 횟수는 0회이며 Diamond가 선택되지 않았다.
- 곡선 복귀에는 Cycle을 열었다. 전체 관찰 중 MM이 Loop/Box/Diamond를 선택한 사례가 있으며, Arc를 강제하지 않는다. Loop 선택은 별도 Loop PSD 및 continuing pose 여부와 구분해야 한다. 아래 MCP 확인에서 현재 Cycle 자체에는 Loop 애니메이션 참조가 없음을 확인했다.

삭제 전 마스터 SHA256: `2973A1B510EE3888D4F36FABF10D6445733C88B4BE34DADD08BC2D10E29C789B`. 백업은 `ABP_Humanoid_Master_BeforeCleanup.uasset`이다. 삭제 후 SHA256: `701E189BE5AAFF7D5D9C4D451CBC3F64BDFA2F8266E936B40936D5D9694CA91A`.

함수 제거에 사용한 일회성 authoring 코드는 검증 루트에 보존하고 빌드 대상에서는 제거했다. 현재 함수 감사 테스트는 읽기 전용이며, 삭제한 함수가 다시 들어오거나 살아 있는 참조가 생기는 것을 검사한다.

이번 결과는 200명 FPS 개선 측정이 아니다. 로컬 입력의 품질 판정을 개선했으며, 이전 군중 측정과 네트워크 동작 계약은 별도 기록으로 유지한다. Cook/패키징 및 사용자의 최종 체감 품질 판정은 자동화 성공만으로 대신하지 않는다.

## 사용자 로그 후속: 곡선 반전과 전방 예측 안정화

이전 지속 곡선·Master 정리·200명 시각 예산·문서 갱신은 `114fc2d8`의 한글 커밋으로 보존했다. 아래는 그 이후의 별도 변경이다. 기존 단발/MM 통합 재생 스택과 네이티브 State Controller는 유지한다.

사용자 로그 원본은 `Saved/Validation/StrafeUser_20261012/User.log`, SHA256은 `64C09432107A3A2EE41ADAA62969AAE92CF3A3413A263B53655B67448BEB3071`이다. 선택 표본 218개를 읽었다. Frame 15742에는 이미 -120.2도/초로 이동 방향이 회전 중인데도 `PSD_Combat_Run_Loop`의 `M_Neutral_Run_Loop_F`가 선택됐고, Frame 15794에서 Cycle의 왼쪽 Arc로 돌아왔다. 두 로그 시각의 차이는 약 0.433초다. 여러 반전에서 같은 패턴을 확인했다.

### 단발 종료와 후보군 유지의 구분

최근 1초의 좌·우 회전량이 상쇄되면 엄격한 일관성 증거가 사라진다. 이 증거를 단발 종료와 일반 이동 후보군 유지에 똑같이 적용하면, 실제 반대 회전이 이어져도 잠시 직진 전용 후보로 좁혀질 수 있다.

- `bActive`는 기존의 엄격한 누적 방향·거리·일관성 조건을 유지한다. Start/Land 종료는 계속 이 값과 의미 보호 구간을 따른다.
- `bRoutingActive`는 이미 확인된 곡선의 후보군 유지에만 사용한다. 최근 짧은 구간의 실제 이동과 요청 방향이 함께 일관되게 회전하면, 긴 이력에 이전 방향이 남아 있어도 기존 Cycle 후보군을 이어 간다.
- 짧은 증거가 끊긴 뒤 유지할 수 있는 시간은 기존 `Confirmation` 값으로 제한한다. 새 곡선의 최초 진입 조건을 완화하거나 Arc·특정 방향 클립을 강제하지 않는다.
- 정지·막힘·이력/모드 변경·텔레포트·관찰 공백 등 기존 초기화는 즉시 유지 상태도 폐기한다. 작은 반복 잡음은 오래된 곡선의 증거를 계속 갱신하지 못한다.

컴포넌트와 GeneralTurn에 전달하는 곡선 값은 같은 `bRoutingActive`를 사용한다. 짧은 이력도 기존 64개 샘플에서 읽으며 새 task·재생기·동적 할당을 추가하지 않는다. 후보군이 유지된다는 사실과 최종 클립의 적합성은 별개다. 아래 MCP 확인에서 현재 Cycle 자체에 Loop 참조가 없음을 확인했으므로, 종전의 “Cycle 안의 Loop가 경쟁한다”는 설명은 잘못됐다.

### 후보를 보존한 전방 예측 안정화

기존 카메라 각속도 예측 안정화는 측면·후방 Strafe만 대상으로 했다. 전방에서는 작은 입력이 묶여 들어올 때 순간 각속도의 0/스파이크가 검색 궤적을 흔들 수 있어, 물리적으로 정렬된 전방 이동에도 기존 필터를 제한적으로 적용한다.

실제 이동·컨트롤러·급회전 판단 값은 필터링하지 않는다. 향후 이동/시선 예측에 같은 안정화 각속도를 사용한다. 지상·로컬·OnFoot·전투 Strafe·입력 유지·속도 50 초과·이동 정렬 오차 15도 이하가 필요하며, 공격·회피·피격·몽타주·루트 모션과 급회전 준비/활성/접근은 제외한다. 기존 몸체/시선 오차 보호와 원본 초기화도 유지한다. 최종 전방 범위는 짧은 구간의 실제 이동 회전율로 판정하며, 기본 상한 `StrafeForwardPredictionMaximumYawRate=120`도/초를 넘으면 기존 예측을 사용한다. 묶음 입력의 순간 스파이크 자체로 판정을 반복하지 않는다. 전방 안정화 구간에 들어가거나 나올 때 필터를 초기화해, 이전 급회전 각속도가 정지한 카메라의 예측으로 넘어가지 않게 한다.

첫 비교에서 5도/초의 매우 느린 이동은 전체 필터 확장 시 재선택이 22→29회로 늘었다. 이를 채택하지 않고, 최근 실제 이동 회전율이 `StrafeForwardPredictionMinimumYawRate` 이상일 때만 전방 확장을 사용하도록 좁혔다. 기본값은 8도/초이며 프로필에서 0~45 범위로 조절한다. **이 값은 예측 필터의 적용 기준이며, 느린 곡선의 후보 허용이나 Start/Land 종료 기준이 아니다.** 5도/초의 실제 곡선도 기존 PSD 후보군을 사용할 수 있다. 측면·후방의 기존 필터 조건은 바꾸지 않는다.

PSD·PSS·시퀀스·Normalization·블렌드 시간·continuing bias는 저장하거나 변경하지 않았다. Arc·Box·Diamond를 이름으로 제외하지 않았고, Loop만 선택하도록 강제하지 않았다. 기존 `p.ProjectJ.StrafePredictionYawSmoothing 0` 또는 프로필의 `bEnableStrafePredictionYawSmoothing=false`로 예측 안정화를 비교할 수 있다.

곡선 후보 유지 자체의 비교/복구는 `p.ProjectJ.StrafeCurveRoutingContinuity 0`으로 이전의 엄격한 증거 기반 후보 판정을 사용한다. 기본값은 1이며, 단발 종료의 엄격한 증거에는 이 스위치를 적용하지 않는다.

### 후속 검증과 해석

근거는 `Saved/Validation/StrafeCurve_20261012/`에 보존한다. 최초 `PacketOn`·`PacketOff`·`BruteOn`은 전방 필터 전체 확장 단계다. `_Final`은 최소 8도/초 적용을 추가한 중간 단계이며, 상한·구간 변경 초기화와 급회전 검증 검토를 포함한 최종 실행은 `_Reviewed` 접미사를 사용한다.

- `EditorBuild_Reviewed.log`, `GameBuild_Reviewed.log`: 직접 UBT Editor/Game Development 빌드. 이전 단계의 빌드 로그도 덮어쓰지 않고 보존한다.
- 곡선 유지 정책은 30/60/120Hz × 5/21/90도/초 × 매 프레임/20Hz 입력 묶음을 검사한다. 반전 중 후보 유지, 엄격한 단발 증거와의 구분, 잡음 수렴·이력 초기화를 확인한다.
- 실제 CMC/MM fixture는 전방 21도/초, 21/90/5도/초 좌우 반전, ±0.15도 반복 입력과 0.5도 단일 입력의 6개 장면을 사용한다. 새로운 검사에서도 실제 기존 Cycle 후보군을 쓰며 에셋을 저장하지 않는다.
- 최초 정확 검색인 BruteForce와 PCAKDTree는 해당 6장면의 재선택·짧은 재생·끝 구간 진입 수치가 모두 같았다. 이 증거로 PCA 검색 설정을 수정하지 않는다.

초기 급회전 회귀는 450도/초의 왼쪽 반전 Case 41에서 “Turn 클립을 반드시 선택” 검사에 실패했다. `Rapid_Protected41_NoPrediction`과 `Rapid_PreviousPolicy41`에서 새 예측과 새 후보 유지 판정을 모두 끈 이전 경로도 동일하게 실패했다. 후보 열림 18프레임, 준비 18프레임, 준비된 진입 1회였으며 MM은 기존 Arc로 교정을 이어 갔다. 따라서 새 예측의 회귀로 단정하거나 특정 Turn을 강제하는 제품 수정은 하지 않았다.

검사는 기존 Turn 선택을 확인하거나, 실제 유효한 평가 시선이 프로필의 완료 각도 이내로 교정됐는지 확인하도록 수정했다. 준비·진입·입력 해제·속도 회복·오래된 요청 해제 검사는 유지한다. `Rapid_Reviewed41`의 실제 최종 시선 오차는 0.024도, 프로필 완료 기준은 15도였고 검사가 성공했다. 처음 실패한 실행을 성공 기록으로 덮어쓰지 않는다. 전체 장면 회귀와 이 단일 재현은 별도 결과로 구분한다.

최종 `_Reviewed` 실행은 모두 성공했다. `Regression_Reviewed`의 관련 64개와 별도 실제 CMC/MM 검사 1개를 합쳐 고유 검증 65개가 성공했다. 별도 `Rapid_Reviewed`는 57개 장면, `PacketOn/PacketOff/DenseOn/BruteOn/Rendered_Reviewed`는 각각 6개 장면을 같은 실제 캐릭터로 실행한다. 한 자동화 테스트 안의 장면 수를 테스트 개수에 더하지 않는다. `Rendered_Reviewed`는 실제 RHI 실행이며 정지 이미지나 사용자의 체감 품질을 인증하는 기록이 아니다.

최종 같은 바이너리의 20Hz 입력 묶음 비교는 다음과 같다. 후보 유지 기능은 두 실행 모두 켜고, 기존 예측 안정화 CVar만 off/on으로 비교했다. 120Hz에서 직진 준비 1초, 조향 4초, 직진 복귀 1초를 실행하며 표의 수치는 준비 뒤 관찰 구간 전체의 재선택이다.

| 전방 장면 | 재선택 off→on | 0.2초 미만 재생 off→on | 검색 끝 구간 진입 off→on |
| --- | --- | --- | --- |
| 21도/초 지속 회전 | 27→17 | 16→6 | 5→5 |
| 21도/초 좌우 반전 | 33→20 | 24→9 | 7→9 |
| 90도/초 좌우 반전 | 38→19 | 30→11 | 8→2 |
| 5도/초 좌우 반전 | 22→22 | 11→11 | 10→10 |

±0.15도 잡음과 0.5도 단일 입력도 각각 재선택 3→3, 짧은 재생 1→1로 기존 결과를 유지하고 Loop 후보로 수렴했다. 지속 곡선·반전의 검사 구간에서 Loop 전용 후보로 강제 복귀한 프레임은 0개였다. Cycle의 기존 Arc·Box·Diamond 등 후보는 계속 허용한다. 당시 Cycle에도 Loop가 들어 있다고 해석한 부분은 아래 MCP 확인으로 정정한다. 일부 끝 구간 진입은 늘거나 남으므로 모든 품질 지표가 개선됐다고 표현하지 않는다. 정확 검색도 최종 PCA와 같은 6장면의 수치를 보였으며, 인덱스·가중치·에셋을 바꾸지 않았다.

`ReviewedSummary.json`은 최종 leaf test 상태, 비교 수치와 소스·DLL·게임 실행 파일 해시를 포함한다. 현재 Master SHA256은 기존 `701E189BE5AAFF7D5D9C4D451CBC3F64BDFA2F8266E936B40936D5D9694CA91A`와 같으며, 이 후속 작업에서 에셋을 저장하지 않았다.

이번 변경은 애니메이션의 곡선 후보 유지와 예측 입력에 한정한다. CMC·GAS 판정·복제 코드는 수정하지 않았으며, 200명 시각 예산의 평가·보간·로컬/전투 보호 정책도 변경하지 않았다. 기존 군중 CPU 개선율을 새 Strafe 변경의 개선율로 사용하지 않는다. 자동화 성공을 사용자의 체감 승인이나 월드 발 접지·의상 품질의 전수 확인으로 대신하지 않는다.

### 추가 사용자 재현: 미세한 연속 입력과 후보 평가

18:59 사용자 재현 원본은 `Saved/Validation/StrafeMicro_20261012/User.log`에 별도 보존했다. SHA256은 `17BDFF7ED6D9CE60184B1BAE987053629469C1BBF28D59113386581D1ED0043D`이며, 선택 표본은 209개다. 사용자는 처음 크게 회전한 장면은 미세 회전 재현이 아니며 그 이후 장면이 실제 문제라고 명시했다. 따라서 초기 Arc 선택을 미세 회전의 증거로 사용하지 않는다. 아래는 후반의 작은 회전 표본이며 정확한 사용자 동작 구간의 시작을 임의로 단정하지 않는다.

- Frame 2741, 18:59:33.099: `PSD_Combat_Run_Loop`에서 `PSD_Combat_Run_Cycle`로 바뀌고 `M_Neutral_Run_Diamond_F_BL_Lfoot`가 새로 선택됐다. 외부 단발 Override는 0이고 이동 속도는 500cm/s다. Frame 2789와 2801의 실제 활성 플레이어는 같은 Diamond이며, 블렌드 분율은 1이다. 대표 결과 캐시에만 남은 클립이 아니다.
- Frame 2789~2832: 이동 방향 회전율 표본은 약 3.1~5.8도/초, 이동 요청과 카메라 방향 차이는 0~-0.2도다. 이 구간에서도 Diamond가 재생되고 Frame 2832에는 Box로 재선택됐다. `ContinuousCurve` 판정으로 Cycle 후보군을 사용한다.
- 후반에는 원본과 예측 각속도가 모두 `0` 또는 `21`도/초인 표본이 있다. 전방 필터의 최소 실제 회전율 조건 아래에서는 기존 원본 예측 경로를 사용하므로, 이전 개선으로 모든 미세 입력의 예측 변화가 제거된 것은 아니다. 로그의 `MoveYawRate`와 필터의 짧은 이력 회전율은 서로 다른 관찰 값이므로 둘을 동일하게 해석하지 않는다.
- Cycle 안에서 Box/Diamond의 후반 구간으로 들어가는 재선택이 반복된다. Frame 2538의 Box는 2.667초, Frame 2654의 Diamond는 2.767초에서 선택됐다. 다만 이 시각만으로 현재 PSD의 유효 샘플 범위나 클립 종료까지의 거리를 확정하지 않는다.
- Loop 전용 후보로 잠깐 돌아간 구간도 남았다. Frame 2503→2524는 약 0.175초, Frame 2725→2741은 약 0.133초 뒤 Cycle로 다시 바뀌었다. 관찰된 중간 정지/변속을 포함한 실제 입력이므로 일정 각속도 반전 fixture의 성공을 이 재현의 해결로 확대하지 않는다.

이번 자료는 후보 전환과 실제 재생은 확인하지만 Loop·Arc·Box·Diamond 각각의 채널별 검색 비용은 담지 않는다. 따라서 PSS 가중치가 원인이라고 확정하지 않는다. 후보군 진입/유지 코드, 예측 궤적, PSD의 검색 샘플 범위와 PSS의 pose/phase/trajectory 비용을 함께 조사해야 한다. 기존 테스트는 0.5도 단일 입력과 양방향 ±0.15도 잡음을 검사했고, 이번과 같은 한 방향의 불규칙한 작은 연속 입력 및 이전 곡선 이력 이후의 재진입까지 해결됐다는 보장은 제공하지 않았다.

후속 구현 판단은 미세 조정의 안정성과 실제 완만한 곡선의 후보 허용을 함께 유지해야 한다. 곡선 후보를 일괄 닫거나 클립 이름으로 Diamond/Box를 제외하지 않는다. PSD에서는 어울리는 곡선 구간과 검색 가능한 시작/끝 구간을 확인하고, PSS에서는 동일한 쿼리에 대한 후보별 비용 분해를 비교한다. 공유 스키마의 가중치를 일괄 변경하기 전에 다른 gait·OTM·Turn 후보에 미치는 영향을 확인한다. 이번 추가 기록 단계에서는 코드·에셋 설정을 변경하거나 재빌드하지 않았다.

### Unreal MCP 실물 조회 결과

사용자의 명시적 MCP 조사 요청 이후 로컬 UE 5.8 MCP에 연결해 실제 에셋을 읽었다. 결과는 같은 증거 폴더의 `Settings.json`, `Schemas.json`, `ChannelValues.json`, `ChannelProperties.json`, `References.json`, `LoopReferences.json`, `ClipRanges.json` 및 원본 RPC 응답으로 보존했다. 에셋 설정/저장 도구는 호출하지 않았다.

**가장 먼저 해결할 후보 구성 문제:** 현재 `PSD_Combat_Run_Cycle`의 Run 시퀀스 참조는 Arc 6개, Box 16개, Diamond 32개, Hourglass 16개이며 Loop 참조는 없다. `PSD_Combat_Run_Loop`는 Loop 8개를 참조한다. Loop F/B의 역참조도 Combat Run 계열에서는 별도 Loop PSD만 가리킨다. 이 숫자는 Asset Registry의 애니메이션 의존성 개수이며, 인덱스의 pose 수나 직접 읽은 엔트리 수로 표현하지 않는다.

후반 사용자 로그의 Frame 2741/2789/2801은 `Candidates=1`, `PrimaryPSD=PSD_Combat_Run_Cycle`, `CompanionPSD=None`이다. 네이티브 `SelectGaitDatabase()`는 SettledCycle과 Cycle을 택일하고, 동반 후보 조회도 이 상황에서 Loop PSD를 추가하지 않는다. 따라서 현재 구조를 “동일 후보군에서 Loop보다 Diamond가 낮은 비용으로 승리”했다고 설명한 것은 잘못이었다. **작은 연속 회전이 Cycle을 열면 Loop의 새 검색 후보가 빠지는 구조적 문제**를 먼저 다뤄야 한다. 이전 Loop continuing pose/블렌드가 잠시 남는 것과 새 Loop 후보 검색은 구별한다.

| 실물 설정 | Combat Run Cycle | Combat Run Loop |
| --- | --- | --- |
| PSS | `PSS_Combat_Run_Continuity` | `PSS_Combat` |
| Normalization | `NS_Combat_Run_Continuity` | 없음 |
| Continuing pose bias | -0.03 | 0 |
| Looping cost bias | 0 | 0 |
| 샘플링 빈도 | 30Hz | 30Hz |
| PSD 끝 제외 기본값 | -0.3초 | -0.3초 |
| 검색 | PCAKDTree, PCA 4, 이웃 200 | 동일 |

Cycle PSS의 Trajectory Weight는 4다. 샘플은 과거 -0.4초 PositionXY 0.4, 현재 VelocityXY 32와 FacingDirectionXY 2, 미래 +0.35초 PositionXY/FacingDirectionXY 각각 0.7, +0.7초 PositionXY 0.5·VelocityXY 8·FacingDirectionXY 0.5다. Loop PSS는 현재/미래 플래그를 묶은 기존 4개 샘플을 사용한다. 두 PSS 모두 Pose Weight 1, 발 L/R·골반의 위치/속도 샘플, 별도 골반 Velocity Weight 5이며 `UseContinuingPose`를 사용한다. 표시 가중치는 정규화 이후 실제 후보별 비용과 동일하지 않다. 이미 높은 속도 가중치를 더 올리는 조치는 비용 분해 없이 선택하지 않는다.

실제 프로필은 곡선 증거 1초 창·0.35초 확인·3도·100cm·0.85 일관성, quiet grace 0.12초, Loop 수렴 지연 0.25초, 전방 예측 안정화 8~120도/초와 half-life 0.025초다. 느린 연속 곡선은 검색 후보를 열 수 있지만 예측 필터는 적용되지 않을 수 있는 구분이 실물 설정에도 존재한다.

PSS Continuity의 역참조에는 Cycle/Turn/GeneralTurn뿐 아니라 BackwardReface/RefaceCycle도 있다. Normalization Set에는 Cycle/Turn/GeneralTurn이 포함된다. PSS Combat는 Sprint/Idle/Start/Stop 등도 참조하므로 공유 스키마를 일괄 변경하면 영향 범위가 커진다.

조사 한계: MCP의 공개 ObjectTools에는 PSD의 private `DatabaseAnimationAssets`와 인덱스, 시퀀스의 전체 notify 배열이 노출되지 않는다. 따라서 엔트리별 SamplingRange/차단 notify와 실제 유효 샘플 끝을 직접 확인하지 못했다. MCP에는 과거 쿼리의 후보별 채널 비용 조회 도구도 없다. 클립 길이는 Box LR F Lfoot 3.2초, Rfoot 3.233초, Diamond BR F Lfoot 3.233초, Diamond F BL Lfoot 4.467초로 읽었지만, 이 값에서 현재 인덱스 샘플 끝을 추정하지 않는다.

구현 우선순위는 **Cycle에서도 정규화가 호환되는 직진 Loop와 실제 곡선이 경쟁하도록 후보 구성을 보완 → 불규칙한 미세 입력/정지/반전을 재현 → 비용 분해로 필요한 PSS·PSD 구간 보정만 선택**이다. Loop PSD의 낮은 검색 비용과 기존 단발/급회전 보호는 유지하는 방향이다. 이 단계는 조사와 설명 정정이며, 제품 코드·PSD·PSS 수정이나 빌드는 아직 하지 않았다.

### 전방 Strafe Run 미세 회전 후속 구현

MCP에서 확인한 후보 누락을 보완하되 기존 이동 family의 검색 통계는 보존한다. Run family에 optional `ForwardCycle`을 추가하고, `DA_Player_Combat_Strafe`에 `PSD_Combat_Run_ForwardCycle`을 연결했다. 기존 Cycle의 70개 곡선 엔트리와 Loop PSD의 8개 엔트리를 그대로 복사한다. 엔트리별 SamplingRange·flags·metadata는 네이티브 내보내기로 비교하며, 원본 시퀀스나 기존 PSD/PSS는 저장하지 않는다. 신규 `NS_Combat_Run_ForwardCycle`은 기존 정규화의 Cycle 참조를 ForwardCycle로 바꾸고 기존 Turn/GeneralTurn 참조는 유지한다. 신규 풀 안에서 곡선 검색의 참조 분포를 보존하기 위한 구성이다. Loop가 추가되므로 통계가 기존과 수치적으로 동일하다는 뜻은 아니다. 기존 Cycle/Turn/GeneralTurn 및 Reface의 정규화·연결 자체는 수정하지 않는다.

실제 인덱스에는 Loop 608포즈와 곡선 5,288포즈가 들어간다. 전체 5,896포즈의 feature vector 저장량은 825,440바이트다. 이는 PCA·KDTree·메타데이터 등까지 포함한 전체 메모리 수치가 아니다. PSD/index/시퀀스는 캐릭터가 공용으로 참조하며 캐릭터마다 78개 애니메이션이나 인덱스를 복제하지 않는다.

#### 적용 조건과 다른 이동의 보호

- 로컬 입력 소유자의 지상 OnFoot·Combat Strafe·Run·Cycle이며 입력을 유지하고, 실제 CMC 가속이 현재 속도를 제동하는 방향이 아니어야 한다. 실제 속도는 기존 Turn 최소 속도 이상이다.
- 전방 입력은 진입 5도/유지 10도 범위를 사용한다. 방향 변경·속도/입력 불일치·몸체/카메라 불일치, Pivot/TIP·공격·회피·피격·몽타주·루트 모션은 기존 보호 조건을 따른다.
- 회전율로 ForwardCycle 자격을 닫지 않는다. 최초 8/12도 진입/유지 제한은 21:26 사용자 재현에서 미세 포인터 입력의 짧은 물리 회전량을 감당하지 못했다. 아래 최초 검증과 후속 재현 기록을 구분한다. 지속 곡선 증거와 단발 종료 조건은 기존 규칙을 유지하며, 검색의 향후 궤적이 Loop/곡선 선택을 결정한다.
- 자격을 갖춘 전방 입력은 안정 상태와 곡선 상태 모두 ForwardCycle 한 PSD를 유지한다. `SettledCycle` bit는 증거/기존 방향 정책을 위한 값이며 이 범위에서 Loop 전용 PSD로 전환하는 명령이 아니다. Loop와 곡선이 같은 통계·질의 포즈·시계로 경쟁한다. 측면·후방·대각선·Sprint·OTM·원격 캐릭터는 기존 family를 사용한다. GeneralTurn/Turn/Reface 및 계속 재생 허용도 ForwardCycle보다 우선한다.
- optional slot이 없거나 `p.ProjectJ.StrafeForwardCycle 0`이면 이전 라우팅과 전방 예측 적용 범위로 돌아간다. 새로운 의미 선택 bit는 GT 값 스냅샷으로 전달하고 워커에서 UObject/입력 상태를 새로 조회하지 않는다.

ForwardCycle 자격은 순간 곡선 증거 bit와 별개로 유지한다. 새 정규화 PSD로 실제 데이터베이스가 바뀌면 현재 포즈 이력으로 한 번 검색해 이전 정규화의 continuing cost를 폐기한다. 기존 재생 스택과 블렌드 시간은 유지하고, 같은 데이터베이스에 머무르는 프레임마다 복귀 검색을 재요청하지 않는다. ForwardCycle의 정상 결과도 Cycle 완료 피드백으로 인정한다.

전방 예측 안정화는 ForwardCycle 자격 범위에서 기존 최소 8도/초 아래에도 적용한다. 기존 최대 120도/초 제한과 물리 정렬·행동 보호는 유지한다. 실제 입력·CMC·컨트롤러·단발 종료·급회전 판단은 그대로이며, 향후 궤적에 전달하는 값만 기존 필터로 안정화한다. 적용 범위를 벗어난 전방과 측후방은 이전 예측 조건을 따른다. 구간 변경 시 필터 초기화도 유지한다.

#### Loop 경쟁과 비용 진단

최초 inclusive pool만 추가한 비교에서는 불규칙한 미세 입력이 Loop 0/240→240/240프레임으로 바뀌었지만, 기존 회전 후 0.35초 멈췄다가 반대 방향으로 작게 입력하는 장면은 35/240→140/240에 그쳤다. 이것을 완료로 처리하지 않았다.

명시적 자동화 진단에서 평가 후 pose history와 현재 continuing result로 쿼리를 **재구성**했다. 원래 검색 프레임의 저장 쿼리가 아니며, raw pose 비교에는 reselect history·차단 필터·continuing bias가 모두 반영된 최종 순위가 들어 있지 않다. `PacketOn_Cost16`의 Frame 396에서는 Loop와 Diamond의 trajectory 비용이 둘 다 약 0.00009지만 Loop의 pose 비용은 약 0.00175, 현재 Diamond의 pose 비용은 0이었다. 유사 표본을 여러 프레임에서 확인했다. 이는 continuing pose 질의의 포즈 유지 경향을 보여 주는 참고 증거이며 최초 선택 원인의 완전한 비용 캡처로 표현하지 않는다.

PSS 가중치를 올리거나 Box/Diamond를 삭제하는 대신, 신규 PSD의 엔진 기본 기능인 `LoopingCostBias=-0.005`를 작은 Loop 선호로 사용한다. 기존 continuing bias -0.03은 유지한다. 후보를 차단하거나 특정 클립을 강제하지 않으며, 실제 곡선이 더 낮은 비용이면 여전히 선택 가능하다. 임시 인덱스에서 같은 loop cost를 적용하고 원복하는 `-ProjectJForwardLoopBias=` 시험을 먼저 실행해 해당 반전 장면의 Loop 240/240을 확인한 뒤 신규 PSD에 저장했다. 기존 PSD의 bias는 변경하지 않는다.

#### 빌드와 재현 기록

증거는 `Saved/Validation/StrafeForward_20261012/`에 보존한다. 최초 `Install_Initial`은 duplication 후 DDC 의존성 캐시가 이전 70개 엔트리 인덱스를 재사용해 실제 Loop 포즈가 없는 검사에 실패했다. 첫 실패와 이후 재실행을 구분한다. 재시작한 `Verify_Reload`는 78개 엔트리와 Loop 608포즈를 확인했다. 생성 코드에 정상 `Modify()` 알림을 추가해 최종 키 요청 전에 캐시를 무효화하고, 검색 포즈가 없으면 저장 전에 실패하도록 보완했다. 최종 `Install_Final`은 성공했다. 캐시 디렉터리나 사용자 에셋을 삭제하지 않았다.

비용 진단의 최초 컴파일은 외부 테스트에서 protected proxy API에 접근해 실패했다. 진단을 AnimInstance 내부의 Editor/dev automation 전용 메서드로 옮기고 빌드했다. 일반 실행·Game에는 쿼리 재구성/전체 인덱스 비교 루프가 추가되지 않는다. `-ProjectJForwardCostAudit`를 준 자동화 실행에서만 호출한다.

Final 실행은 설치된 에셋 자체를 사용하고 임시 bias 옵션을 주지 않는다. 입력 묶음/매 프레임 입력·정확 검색·실제 RHI, 불규칙한 작은 연속 입력과 회전→정지→미세 반전, 측면/후방/대각선 배제 및 기존 급회전·단발·모드 전환·군중 정책 회귀를 확인한다. 결과 수치와 범위는 아래 최종 검증 기록으로 확정한다. 기존 200명 CPU 개선율을 이번 전방 후보 수정의 성능 개선율로 재사용하지 않는다.

#### 최초 구현의 검증 기록 — 사용자 재현으로 한계 확인

설치된 에셋의 `PacketOn_Final`과 같은 바이너리에서 `p.ProjectJ.StrafeForwardCycle 0`으로 비교한 `PacketOff_Final`의 결과다. CMC 120Hz에서 직진 준비 1초, 조향 4초, 직진 복귀 1초를 실행하고, 미세 입력 표본은 Frame 360~599의 240프레임을 사용했다. 입력 크기 0.175도를 4/7/5/6/3/9/4/8프레임 간격으로 누적하는 불규칙한 한 방향 입력을 추가했다. 회전 후 반전 장면은 21도/초 회전 1초·정지 0.35초 뒤 반대 방향의 같은 작은 입력을 쓴다.

| 장면 | Loop 유지 off→on | 클립 재선택 off→on | 0.2초 미만 재생 off→on | 인덱스 끝 구간 진입 off→on |
| --- | --- | --- | --- | --- |
| 불규칙한 미세 전방 입력 | 0/240→240/240 | 14→4 | 4→1 | 5→0 |
| 회전·정지 후 반대 미세 입력 | 35/240→240/240 | 10→8 | 4→3 | 2→1 |
| 5도/초 좌우 반전 | 별도 집계 없음 | 22→3 | 11→1 | 10→0 |

측면/후방/전방 대각선의 세 장면은 ForwardCycle 선택이 0이며 off/on의 표본별 Direction/Prediction 행과 집계가 같다. 기존 21도/초 곡선·21/90도 좌우 반전의 조향 검사 구간에서도 ForwardCycle 선택은 0이다. 다만 전체 관찰에는 조향 종료 뒤 새 전방/안정 후보로 돌아가는 과정이 포함돼, 90도 반전의 전체 재선택은 19→22, 짧은 재생은 11→14로 증가했다. 모든 전환 지표가 개선되거나 다른 모든 프레임이 완전히 같다고 표현하지 않는다.

`Brute_Final`은 미세 입력 두 장면에서 PCA와 동일한 결과를 보였다. 11개 장면 중 10개 집계는 같고, 기존 전방 대각선 장면에서는 Brute의 재선택·짧은 재생이 달랐다. 해당 장면은 새 후보군을 사용하지 않으므로 이번 미세 입력의 원인을 PCA 설정으로 결론 내리거나 기존 인덱스 설정을 바꾸지 않는다. `Dense_Final`과 실제 D3D12 SM6 `Rendered_Checked`에서도 두 미세 입력 장면의 Loop 유지 240/240을 확인했다. RHI 성공은 사용자의 체감 승인이나 월드 발 접지의 전수 인증이 아니다.

`Rapid_Final`의 급회전 57개 장면과 `Directions_Checked`의 기존 A/S/D 장면 회귀가 성공했다. `Regression_Final`의 66개 중 한 leaf에 최초 실패가 있었다. Strafe Start의 5도/초와 -10도/초 최초 곡선 복귀에서 실제 검색은 신규 ForwardCycle이었지만, 검사가 기존 Cycle 이름만 허용했던 문제다. rich 후보군에 ForwardCycle을 허용하되 실제 첫 검색·non-continuing 결과·검색 타이머 0·pose history·단발 최소 hold·General/Turn 배제 조건은 유지했다. `Curve_Checked`의 해당 12개 장면 재실행은 성공했다. 최초 실패를 보존하고, 동일 제품 코드의 기존 65개 성공과 재실행 1개 및 별도 CMC/MM leaf를 합쳐 최신 고유 검증 67개가 성공한 것으로 집계한다. 장면 수와 반복 실행을 테스트 개수에 더하지 않는다.

`FinalSummary.json`은 최초 보고서와 최신 leaf 상태를 모두 포함하고 원본 에셋 해시 비교 및 off/on/정확 검색 차이를 기록한다. 기존 PSS·PSD 파일의 원본 해시는 전부 동일하다. 저장 범위는 신규 ForwardCycle/Normalization 두 에셋과 기존 `DA_Player_Combat_Strafe`의 optional slot 연결이다. Master·기존 시퀀스·CMC·GAS·복제·시각 예산 코드는 이번 후보 수정에서 저장/변경하지 않았다.

빌드는 `EditorBuild_Final.log` 및 검사 기대값 보완 후 `EditorBuild_Checked.log`로 확인한다. 직접 UBT Editor/Game Win64 Development 빌드는 성공했으며 Game 기록은 `GameBuild_Checked.log`다. 제품 런타임 코드는 검사 보완 때 변경하지 않았다. `FinalManifest.json`에는 최종 소스·DLL·게임 실행 파일·신규 에셋과 Master의 해시를 보존한다. Master는 기존 `701E189BE5AAFF7D5D9C4D451CBC3F64BDFA2F8266E936B40936D5D9694CA91A`와 같다. 문서 검사는 197개 파일·1,276개 로컬 링크에서 깨진 링크 0개, 선택적 로컬 아카이브 부재 12개였다. 새 200명 렌더/FPS·네트워크·Cook 성능 측정은 이번 단계에서 실행하지 않았으며, 새 검색 경로는 로컬 플레이어에 한정하고 기존 200명 시각 예산 정책 검증을 유지한다. 전방 PSD의 메모리·재선택 수치만으로 군중 FPS 개선을 주장하지 않는다.

#### 21:26 실제 사용자 재현 후 보정

최초 합성 입력의 성공을 실제 입력의 해결로 확대해 판단한 오류를 기록한다. 사용자는 같은 현상을 다시 확인했다. 최신 에디터 로그에는 신규 ForwardCycle의 DDC 로딩과 새 `ForwardCycle` 진단 필드가 있었으므로 구버전 실행 문제로 처리하지 않았다. 추적을 켠 후의 원본은 `Saved/Validation/StrafeStill_20261012/User.log`이며 SHA-256은 `5C260AFD9DB2D529330D5710B59F7B5645CAC5277FFD89B61F2ACB460FADDF28`이다.

실제 입력에서는 W를 유지하고 몸체/카메라가 정렬된 상태에서도 8/12도 회전율 제한을 넘으면 ForwardCycle이 닫혔다. 기존 Cycle은 Loop 포즈가 없으므로 Box/Diamond로 바뀌었다. 예를 들어 Frame 3342에서는 `ForwardCycle=0`, 이동 회전율 -22.6도/초, Diamond 선택이 기록된다. 그 뒤 -4.4도/초로 줄어든 Frame 3572에서도 기존 후보군에 Diamond가 계속됐다. 원시 포인터의 한 프레임 0/21/42도 값과 사람이 느끼는 미세 조정의 범위를 같은 기준으로 해석하지 않는다.

후반 Frame 3200~4007의 808개 카메라 yaw 표본을 정규화해 [회귀 데이터](../../../Source/Project_J/Testing/Fixtures/StrafeMicroCamera_20261012.csv)로 보존했다. 실제 기간은 6.736초, 누적 yaw 변화는 -81.9도다. 고정 .175도 간격의 합성 입력보다 실제 평균과 입력 묶음이 크고 불규칙했다. 새 `ProjectJ.Animation.Naturalness.RecordedStrafeMicro` 검사는 실제 BP·CMC·충돌 바닥·Master로 재생하며 파일 오류, Box/Diamond 선택과 Loop 유지 부족을 실패로 처리한다. 원래 월드 지형이나 전체 초기 포즈 이력을 재현한 것은 아니다.

전방 입력 자격에서 8/12도 제한을 제거했다. 기존 입력 각도 5/10도, 속도/입력 정렬, 실제 제동·행동·급회전·몽타주·루트 모션 보호는 유지한다. Loop와 곡선은 같은 검색에서 궤적과 포즈로 경쟁하며, 특정 클립 이름으로 런타임 후보를 차단하지 않는다. 단발 종료의 엄격한 곡선 증거 조건과 CMC 입력은 수정하지 않았다.

자격만 넓힌 중간 구현은 실제 미세 입력을 개선했지만 기존 90도/초 곡선의 Arc를 잃어 회귀 검사에 실패했다. Loop bias를 0으로 한 비교에서도 Arc가 0프레임이었다. 이 결과를 성공으로 처리하지 않았다. 신규 정규화를 ForwardCycle 하나로 제한하면서 기존 Cycle/Turn/GeneralTurn의 참조 분포까지 줄어든 영향인지 확인했다. 기존 정규화의 Cycle만 ForwardCycle로 치환한 임시 비교에서는 Arc가 369프레임 선택됐고 실제 미세 입력 개선도 유지됐다.

이에 신규 `NS_Combat_Run_ForwardCycle`의 참조를 ForwardCycle, 기존 Combat Run Turn, 기존 GeneralTurn의 세 PSD로 보정했다. 기존 정규화와 PSS·PSD·시퀀스는 저장하지 않는다. 기존 곡선의 모든 결과가 수치적으로 같다는 뜻은 아니며 신규 풀의 검색 통계만 바뀐다. 최초 1개 PSD 정규화 구성과 그 검증 기록은 위에 보존한다.

실제 카메라 입력 재생의 최초 비교는 Loop 104/733→732/733프레임, 재선택 11→2회, 인덱스 끝 진입 2→0회다. 수정 후 남은 1프레임은 첫 단발 복귀 경계의 캐시된 `PSD_Idle / M_Neutral_Stand_Idle_Loop` 결과이며 Box/Diamond가 아니다. 실제 최종 에셋 재생은 120Hz 732/733, 60Hz 366/367, 30Hz 183/184프레임 Run Loop를 기록하고 세 경우 모두 Box/Diamond 0프레임으로 통과했다. 재표본화는 같은 시간/yaw를 사용하므로 프레임 수 자체를 성능 개선율로 비교하지 않는다.

후속 경계 검사에서 안정 상태의 Loop 전용 PSD 우선순위도 제거했다. 90도/초 곡선 뒤 같은 미세 입력을 재생한 중간 `Precurve120`은 관찰 749프레임 중 Box 14프레임을 기록해 실패했다. 이 구간의 `ForwardCycle=1`과 실제 선택 PSD를 확인했으며, 안정/곡선 증거가 바뀔 때 서로 다른 스키마·통계의 Loop 전용 PSD와 ForwardCycle을 오가는 구조가 남아 있었다.

이제 자격을 갖춘 전방 W는 안정/곡선 여부와 관계없이 ForwardCycle 한 PSD를 유지한다. 안정 증거 bit가 바뀌어도 같은 풀 안에서는 정규화 경계 검색을 새로 요청하지 않는다. 전방 범위를 실제로 나갈 때는 안정 bit와 무관하게 이전 continuing cost를 폐기한다. 측후방·급회전·optional slot 부재·기능 off의 기존 경로는 보존한다. 로컬 플레이어의 안정 상태 검색도 78개 엔트리 풀을 사용하므로 기존 8개 Loop PSD와 검색 비용이 동일하다고 주장하지 않는다. 군중의 원격/NPC 경로는 이 자격에 들어오지 않는다.

안정 Loop를 먼저 재생하고 큰 곡선을 거친 `StablePrecurve120`에서는 실제 Arc 212프레임을 확인했고, 이후 미세 입력의 전환 0.5초 뒤 749/749프레임 Loop 및 Box/Diamond 0프레임을 기록했다. 60/30Hz 변형도 통과했다. 전환 구간 자체를 삭제하거나 체감 승인으로 간주하지 않는다. 같은 초기 조건을 사용하는 별도 비교와 최종 관련 검사는 아래 결과 기록을 따른다.

최종 코드는 `PacketOn_StableFinal`의 11개 장면, 실제 입력의 120/60/30Hz 재생, 이전 곡선 이력을 가진 재생, 동일 인덱스의 Brute 검색 및 D3D12 재생을 검증했다. 이전 실패와 같은 2초 곡선 선행 조건인 `MatchedPrecurve120`에서도 Arc 203프레임을 실제 선택한 뒤 관찰 구간 Loop 749/749 및 Box/Diamond 0으로 통과했다. 더 엄격한 안정 Loop→큰 곡선→미세 입력 변형은 앞서 기록한 120/60/30Hz 결과로 구분한다.

최종 90도/초 반전 장면은 Arc 413프레임, 재선택 11회, 0.2초 미만 재생 4회, 인덱스 끝 진입 0회다. 측면/후방/대각선의 Direction/Prediction 표본은 최초 기능 on 기록과 전부 같다. 보호 조건을 벗어나는 기존 이동은 원래 데이터베이스를 사용한다. 기존 PSD/PSS 62개 원본 파일과 Master ABP의 해시는 유지됐다.

`FocusedFinal`에는 실제 클립 문제와 별개로 이전 “Loop 전용 PSD 이름”을 고정한 검사 실패가 있었다. 동일 구조에서 실제 선택이 ForwardCycle로 바뀐 것을 확인하고, 설정된 전방 풀을 기대하도록 보완했다. 안정 증거 조건은 유지하며 **실제로 Loop 클립을 선택한다는 검사**를 추가했다. 최초 실패 보고서는 보존한다. `FocusedChecked`는 단발 곡선 복귀 12개 장면과 후보/수명/포즈 이력/검색 실행/모드/상태 컨트롤러 관련 leaf를 포함해 성공했다.

최종 관련 검사는 고유 19개 leaf가 성공했다. 안정/곡선 단일 PSD 보정 전의 전체 회귀 67개 및 급회전 57개 장면 성공은 앞 단계의 기록이며 최종 코드에서 전체 67개를 다시 실행한 것으로 합산하지 않는다. 그 전체 회귀의 경고 5개 leaf는 생산 BP가 아닌 C++ 전투 fixture의 palm 부재에 따른 기존 호환 소켓 사용 경고였고 오류는 0이었다. `Saved/Validation/StrafeStill_20261012/FinalSummary.json`은 단계와 최초 실패를 나눠 저장한다.

최종 직접 UBT 빌드는 Editor `EditorBuild_StablePool.log`와 검사 보완 후 `EditorBuild_TestChecked.log`, Game `GameBuild_StableFinal.log`에서 성공했다. 마지막 검사 보완은 Editor 자동화 코드이며 제품 런타임은 변경하지 않았다. `FinalManifest.json`에 현재 소스·바이너리·에셋·회귀 데이터 해시를 보존했다. 에디터에서 실제 체감은 아직 사용자 확인 전이다. 신규 전방 경로의 200명 렌더/FPS, 네트워크 및 Cook 측정을 다시 수행한 것으로 표현하지 않는다.

#### 지속 곡선 확인 시간 0.2초 조정

이후 사용자가 미세 회전 개선의 체감을 확인했고, Start/Land 이후 지속 회전에 더 빨리 반응하도록 확인 시간을 0.35초에서 0.2초로 낮추도록 요청했다. 실행 중인 에디터에서 Unreal MCP로 실제 `DA_Player_Profile`의 `TransitionPolicy.ContinuousCurve.Confirmation`을 0.2초로 변경하고 해당 에셋만 저장했다. 변경 전후 프로퍼티를 비교해 TransitionPolicy의 다른 값이 같음을 확인했다. 증거와 변경 전 에셋 사본은 `Saved/Validation/CurveConfirmation_20261012/`에 보존한다. 위의 0.35초 기록은 조정 전 설정이다.

이 값은 Run/Sprint·OTM/Strafe의 단발 곡선 복귀뿐 아니라 일반 Strafe 곡선 증거와 짧은 반전 구간의 후보 유지에도 공유된다. 관찰 창 1초, 이동 거리 100cm, 누적 회전각 3도, 방향 일관성 85%, QuietGrace 0.12초 및 Start/Land 종료 보호는 유지한다. 필요한 거리·회전각·보호 조건이 충족되어야 하므로 0.2초 경과만으로 MM 복귀나 Arc 선택을 강제하지 않는다. 빠른 회전의 별도 복귀 경로도 유지한다.

이번 변경은 직렬화된 프로필 값의 조정이며 C++ 기본값·폴백과 런타임 구현은 수정하지 않았다. MCP 설정 재조회와 저장 성공을 확인했으며 새 C++ 빌드나 전체 회귀·200명 성능 측정을 수행한 것으로 간주하지 않는다. 현재의 StateController 의미 상태 관리, 단발/MM 공통 재생 스택, 전방 Loop/곡선의 동일 PSD 경쟁 구조를 유지한다. 완만한 회전 중 Arc에서 Box로 바뀌는 별도 현상은 새 사용자 로그의 실제 선택 비용과 궤적을 확인한 뒤 판단한다.

#### Arc→Box 재현 후속: 전방 미래 궤적 비용 보정

23:09 사용자 로그를 `Saved/Validation/StrafeArcBox_20261012/User.log`로 보존했다. SHA256은 `569DCEDE84FF861CB7D5C76D983A275C603F14D2B1BA1CD12D0C99283FE0C2E7`이다. MM이 출력하는 선택 표본 309개에서 Box 진입 21회를 확인했으며 20회는 ForwardCycle PSD와 곡선 후보 판정을 유지했다. 약 56~76도/초의 같은 방향 회전에서도 Arc와 Box가 교차했다. 따라서 이번 문제는 후보군이 닫히거나 다른 PSD로 바뀌는 이전 문제와 구분한다.

Frame 20268~22900의 실제 카메라 yaw 2,633개를 21.95초 재생 데이터로 추출했다. 전체 포즈 이력·월드 지형까지 복제한 것은 아니지만, 실제 BP·CMC·Master ABP에서 원본 입력을 재생한 기준 설정은 Box/Diamond 707/2,609프레임을 선택해 문제를 재현했다.

기존 PSS의 현재 평면 속도 가중치는 32, 미래 속도는 8인데 미래 위치·방향은 0.35초에서 각각 0.7, 0.7초에서 각각 0.5였다. 순간 속도에 비해 미래 경로의 모양을 약하게 비교하는 구성이 원인에 기여하는지 임시 PSS로 비교했다. 진단의 재구성 비용은 평가 후 포즈 이력으로 다시 만든 값이며, 원래 검색 쿼리와 재선택 필터를 그대로 캡처한 값으로 표현하지 않는다. 클립 이름은 진단·검사의 분류에만 사용하고 런타임 후보를 차단하지 않는다.

중간 미래 샘플 두 개를 추가한 비교는 Box/Diamond 77프레임으로 줄었으나 차원이 35→43개, 첫 관찰 구간의 끝 진입이 0→3회로 증가했다. 미래 위치·방향의 가중치만 4배로 한 비교는 차원 35개를 유지하며 7프레임으로 줄었다. 더 큰 8배 비교는 42프레임과 끝 진입 3회를 기록했으며, 기존 정규화 참조를 그대로 유지한 3배 비교는 21프레임이었다. 최초 비교의 정규화용 PSD 사본 대신 기존 참조를 유지한 4배 비교에서도 7프레임으로 결과가 같았다. 가중치가 클수록 모든 지표가 좋아진다는 뜻은 아니다.

전방 전용 `PSS_Combat_Run_ForwardTrajectory`를 작성하고 기존 `PSD_Combat_Run_ForwardCycle`에만 연결했다. 0.35초의 미래 위치·방향 가중치는 각각 2.8, 0.7초는 각각 2.0이다. 이 샘플 시각은 검색 목표의 미래 시간이며 0.2초의 지속 판정 확인 시간과 다른 설정이다. 현재 속도·미래 속도·과거 궤적·포즈와 pelvis 속도 채널·샘플 시각·정규화 그룹·검색 차원·78개 후보와 범위·Loop bias는 보존한다. 정규화의 ForwardCycle/기존 Turn/기존 GeneralTurn 참조도 유지한다. 원본 PSS, 측후방·대각선, Sprint/OTM/원격의 기존 PSD와 급회전의 후보를 저장하거나 수정하지 않는다. 프로필의 0.2초 확인 시간과 Master ABP·상태 컨트롤러도 유지한다.

명시적 `-ProjectJApplyForwardTrajectory` 작성 검사는 변경 전 ForwardCycle을 백업하고 신규 PSS와 ForwardCycle 두 에셋만 저장한다. 일반 `ProjectJ.StrafeForward.TrajectorySchema` 검사는 읽기 전용이다. 실제 저장 결과의 검색 가능한 포즈는 5,896개, 특징 벡터 값은 825,440바이트로 기존과 같다. 이 값은 전체 인덱스 메모리나 200명 FPS 측정이 아니다.

저장된 에셋의 120Hz 재현은 Arc 2,433/2,609프레임, Box/Diamond 7프레임을 기록했다. 남은 7프레임은 재생 시간 10.533~10.583초이며 원래 기록에서 카메라 회전이 잠시 멈췄다가 반대 방향을 시작하는 경계에 해당한다. 모든 Box 선택이 0이거나 모든 전환 지표가 개선됐다고 표현하지 않는다. 첫 5초 관찰의 재선택은 13→11회, 0.2초 미만 재생은 5→6회, 끝 진입은 0→0회다. 이는 전체 21.95초의 재선택 집계가 아니다.

`StrafeCurveCamera_20261012.csv`와 `ProjectJ.Animation.Naturalness.RecordedStrafeCurve`로 새 재현을 보존한다. 곡선 재현은 MM 관찰이 기록 시간의 90% 이상, Arc 85% 이상, Box/Diamond 1% 이하를 검사한다. 기존 미세 입력의 Loop 유지 검사는 별도로 유지한다. 최초 30/60Hz 검사는 품질 조건은 통과했으나 고정 최소 프레임 수 때문에 실패했고, 이를 시간 대비 관찰 비율로 바로잡았다. 최초 실패 보고서는 보존한다. 임시 스키마 비교 중 43/35차원의 캐시 불일치 경고도 기록하며, 저장된 생산 에셋의 최종 인덱스 검사와 구분한다.

저장된 설정의 `Checked60`은 Arc 1,145/1,242프레임과 Box/Diamond 12프레임, `Checked30`은 Arc 577/620과 Box/Diamond 0으로 통과했다. 120Hz 동일 인덱스의 `FinalBrute` 정확 검색과 `FinalRendered` D3D12 렌더 재생도 각각 Arc 2,433/2,609 및 Box/Diamond 7로 통과했다. 서로 다른 프레임 수를 성능 개선율로 비교하지 않는다. 이 결과는 렌더 경로가 실행됐음을 확인하며 발 접지·체감에 대한 사용자 승인으로 간주하지 않는다.

현재 설정과 코드의 전체 회귀 `Regression`은 고유 69개 leaf가 통과했고 오류는 0이다. Start/Land 곡선·단발 입력 대응·복귀·방향 조합·모드 전환·포즈 이력·숨김 복귀·병렬 snapshot/군중 정책과 신규 곡선·미세 입력 재현 및 생산 데이터 검사를 포함한다. C++ 전투 fixture에 palm이 없는 기존 호환 소켓 경고는 5개 leaf에서 발생했으며 생산 BP의 오류로 합산하지 않는다. 원본 PSS/PSD 62개와 Master ABP의 해시, 0.2초를 저장한 DA_Player_Profile의 해시를 재확인했다.

직접 UBT의 최종 Editor 빌드는 `EditorBuild_Checked.log`에서 성공했다. 최초 임시 진단 빌드는 정규화 참조의 const 포인터를 수정하려는 컴파일 오류로 실패했으며, 참조 처리 수정 후 재빌드했다. 실패 로그도 보존한다. 이번 변경은 생산 PSS/PSD와 Editor/dev 진단·회귀 코드에 한정되며 Game/Cook 및 200명 성능 측정을 새로 수행했다고 표현하지 않는다.

`PacketOn_ArcBoxFinal`의 11개 곡선·미세 입력·측후방·대각선 장면도 통과했다. 90도/초 반전 장면의 Arc는 453프레임이며, 두 전방 미세 입력 장면은 각각 Loop 240/240프레임을 유지했다. 측후방·대각선은 전방 PSD 선택이 0이다. 전체 회귀와 합친 고유 leaf는 70개다.

앞선 0.35초 프로필의 `PacketOn_StableFinal`과 비교할 때 측후방·대각선의 실제 이동·예측 값은 같지만 선택 클립과 일부 전환 집계는 달랐다. 이를 이번 전방 PSS의 영향으로 단정하지 않았다. 현재 0.2초 프로필에서 전방 기능을 끈 `PacketOff_ArcBoxSide17/18/19` 비교는 기능 on의 이동·예측·선택 클립 720개 표본과 전부 같았고, 재선택·짧은 재생·끝 진입 집계도 각각 같았다. 따라서 이전 프로필과 모든 클립 결과가 같다는 주장과, 동일 프로필에서 전방 보정이 측후방에 적용되지 않는다는 검증을 구분한다. 최종 결과와 해시는 `Saved/Validation/StrafeArcBox_20261012/FinalSummary.json` 및 `FinalManifest.json`에 저장한다.

#### 2026-10-13 지속 곡선 복귀 조건 소폭 완화

사용자는 Run Start 초반의 작은 마우스 회전 반응은 유지하고, Start/Land가 MM으로 조금 더 쉽게 복귀하도록 기존 조건만 조정하기로 했다. 확인 시간은 0.2초로 유지하고 이동 거리 100→80cm, 이동 방향 변화 3→2.5도, 방향 일관성 85→75%로 낮췄다. 거리와 일관성은 사용자가 직접 지정한 값이다. 유지 시간을 늘리거나 Start 전용의 더 엄격한 조건을 추가하지 않는다.

00:05~00:06의 사용자 로그를 `Saved/Validation/OneShotSensitivity_20261013/User.log`로 보존했다. SHA256은 `02CC7DCEB42AACC23210335BB28A285B6C940C12B802A170E49A3177555D3A01`이다. 입력 복귀 16회의 이유는 모두 `ContinuousCurveToCycle`이었다. `BeyondSteeringRange`나 `DirectionalInputChanged` 같은 즉시 취소 원인으로 판단하지 않는다. 기록에서는 Start/Land의 실제 이동 거리와 누적 방향 변화가 조건을 충족한 뒤 복귀했다. 빠른 Run 속도에서는 거리 조건도 짧은 시간에 충족되므로, “작은 입력 직후처럼 보인다”는 체감과 한 프레임만으로 조건을 우회한다는 주장을 구분한다.

실제 생산 BP의 착지 컴포넌트는 `LandingMinHoldTime=0.25초`, 입력 취소 유예도 0.25초였다. 이 보호와 착지 타이머는 수정하지 않는다. 지속 곡선 증거가 충분해도 `bCanExitLanding`이 허용될 때만 복귀한다. 사용자의 기록에는 `CanExit=0` 표본 36개가 있었으며 이 때 곡선 복귀는 발생하지 않았다. 플레이어의 이동·회전 시작 시점, 물리적 방향 변화의 축적과 증거 초기화가 다르면 Land 복귀 시점도 달라질 수 있다. 외부 클립의 근사 재생 시간은 실제 착지 보호 시계와 동일하다고 간주하지 않는다.

에디터 종료 후 엔진의 Python commandlet로 `DA_Player_Profile`의 세 프로퍼티만 변경하고 해당 에셋만 저장했다. 변경 전 에셋과 구조체 텍스트를 보존하고, 수정한 세 값을 원복한 사본이 변경 전 TransitionPolicy 전체와 같음을 확인했다. 관찰 창 1초, QuietGrace 0.12초, 빠른 곡선의 별도 100cm/20도/85% 경로, 입력 취소 각도, PSS/PSD 및 Master ABP는 유지한다. 이 설정은 기존 구조대로 일반 Strafe 곡선 후보의 증거에도 공유된다. 기존 공통 스택과 상태 컨트롤러의 런타임 코드는 수정하지 않았으며 C++ 재빌드를 새로 수행한 것으로 표현하지 않는다. 저장 전후 값과 착지 보호 조회는 `Result.json`에 남겼다.

새로 로드한 생산 프로필의 집중 검증 `Focused`는 관련 7개 leaf가 오류 없이 통과했다. 실제 BP에서 느린 OTM/Strafe Start/Land 회전과 고립된 작은 입력·반복 흔들림을 재생하는 12개 장면, 입력 호환·지속 증거, 기존 실제 미세 입력·곡선 기록과 전방 생산 데이터 검사를 포함한다. 실제 미세 입력 재생은 Loop 738/739프레임, Box/Diamond 0을 기록했다. 첫 캐시된 경계 프레임과 관찰 프레임 수는 앞선 설정과 다르므로 성능 개선율로 비교하지 않는다. 원본 PSS/PSD 62개와 Master ABP의 해시도 같았다. 이번에 전체 70개 회귀나 새 200명 성능 측정을 수행한 것으로 표현하지 않는다.

#### 2026-10-13 사용자 요청으로 원래 판정 수치 복원

사용자가 완화한 조건을 되돌리고 확인 시간도 0.35초로 복원하도록 요청했다. 최종 설정은 **0.35초·100cm·3도·방향 일관성 85%**다. 에디터가 종료된 상태에서 현재 파일과 두 단계 변경 전 백업의 해시 체인을 확인하고, 0.2초 변경 전의 원본 `DA_Player_Profile`을 복구했다. 복원된 파일의 SHA256은 `AA4E2E350A679A328C6E3D291577D07406F8E9EA3F03B4D4F1A03E1B559A59BF`로 원본과 같다. Arc→Box 보정용 전방 PSS/PSD와 런타임 코드는 유지한다. 위 완화 설정의 검증은 과거 기록이며, 복원 후 새 빌드나 재생 검사를 수행한 것으로 표현하지 않는다. 복원 직전 파일과 검증 결과는 `Saved/Validation/OneShotSensitivity_20261013/DA_Player_Profile.BeforeRollback.uasset` 및 `Rollback.json`에 남겼다.

#### 2026-10-13 커밋 전 코드 정리와 착지 튐 조사

위 복원 시점 이후의 확인 기록이다. 최종 프로필은 0.35초·100cm·3도·85%로 유지한다. 임시 Forward 정규화·미래 궤적 가중치 sweep·Loop bias 변경 코드와 복구용 필드를 제거했다. 신규 전방 생산 데이터 검사의 에셋 작성·저장 경로도 제거하여 `ProductionData`와 `TrajectorySchema`는 읽기 전용 검증과 `Saved` 보고서 작성만 수행한다. 앞서 언급한 `ProjectJApplyStrafeForwardPool`, `ProjectJApplyForwardTrajectory`는 당시 작성 과정의 기록이며 최종 코드에서 제공하지 않는다. 실제 입력 회귀 데이터, Brute 비교 및 명시적으로 요청한 Editor/dev 검색 비용 재구성은 유지한다.

커밋 준비 중 사용자가 Strafe 착지 후 마우스/이동 입력에 순간 튐을 보고했다. 종료된 에디터의 기록은 `Saved/Validation/StrafeLandPop_20261013/User.log`에 보존했다(SHA256 `151BBA3EFFB7EC70B004DB5500B5F170BD79D116E14E7E46DB58E7B7A287B811`). 느린 곡선 복귀 네 구간에서 루트는 프레임당 약 0.5~0.8도, 가슴은 약 12~16도씩 변했다. 해당 관찰 창에서는 같은 Arc 클립의 시간이 진행하고 있어 후보가 반복적으로 바뀌는 현상으로 단정하지 않는다. 복귀 시 상체 보정의 적용과 그 이전 입력 포즈, 남아 있는 Land 샘플의 보정, 착지 중 사용하는 기존 Cycle 후보를 함께 확인해야 한다.

실제 BP·CMC·Master ABP의 일반 단발 입력 대응 16장면은 `Baseline`에서 통과했다. 이 장면들은 착지 후 회전이 멈추므로 지속 회전 재현과 구분했다. `ContinuousBaseline`은 120Hz Strafe Run 착지 후 45도/초 지속 회전을 따로 재생했다. 복귀 시 outgoing Land의 방향 값은 연속적이었고, 컴포넌트 공간 가슴 회전의 첫 복귀 변위는 -3.22도였다. 사용자가 기록한 급변과 같은 크기를 재현한 것으로 간주하지 않는다. 남은 Land의 방향 값을 고정하는 수정이나 상체/후보 판정 변경은 추가하지 않았다. **이번 커밋은 착지 튐 해결 완료를 의미하지 않는다.** 후속 재현에 사용할 단발 장면 선택 옵션과 복귀 포즈·outgoing 보정 값의 읽기 전용 기록을 추가했다.

정리 후 직접 UBT Editor 빌드는 `Saved/Validation/CommitCleanup_20261013/EditorBuild_LandProbe.log`에서 성공했다. 복원된 프로필의 `Focused` 검사는 **21개 성공·1개 실패·경고 0개**다. 느린 OTM/Strafe Start/Land 12장면, 미세 입력, 곡선 증거와 반전, 착지 문맥, 후보·정규화 경계 및 worker 군중 정책과 생산 데이터 검사를 포함한다. 미세 입력은 Loop 732/733 및 Box/Diamond 0을 기록했다. 실패는 `RecordedStrafeCurve`의 Box/Diamond 1% 이하 기준이며, 앞서 0.2초 설정에서 통과한 전체 회귀 기록으로 이 실패를 덮지 않는다. 원본 PSD/PSS 62개와 Master ABP, 복원된 프로필의 해시를 다시 확인했다.

`CurveIsolated`는 실패한 곡선 기록만 같은 바이너리·복원된 프로필·120Hz로 다시 실행했다. Arc 2,385/2,596(91.87%), Box/Diamond 28/2,596(1.079%)이며, 세 번의 짧은 후보 구간 때문에 1% 이하 기준이 다시 실패했다. 이전 0.2초 기록의 7프레임과 같다고 표현하지 않는다. 기록 파일과 판정 기준을 수정하지 않았고, 프로필 복원 뒤 최초 진입·포즈 이력·후속 검색의 차이를 더 조사해야 한다. 현재 변경을 보존하는 커밋이며 전체 품질 검증 완료나 착지 튐 수정 완료를 의미하지 않는다. `Focused`에서 곡선/미세 재생의 공통 출력 파일은 마지막 미세 재생으로 덮여 있으므로 곡선의 상세 원자료는 별도 `CurveIsolated/Playback.txt`를 따른다.

직접 UBT Game Win64 Development 빌드도 `CommitCleanup_20261013/GameBuild.log`에서 성공했다. Editor/dev 전용 비용 진단과 실제 BP 재생 코드는 Game 실행 경로에 포함되지 않는다. 원본 로그·빌드·자동화 보고서는 `Saved/Validation/CommitCleanup_20261013` 및 `StrafeLandPop_20261013`에 보존하며 실행 파일과 원본 사용자 로그를 Git에 포함하지 않는다. 집계와 남은 실패는 `CommitCleanup_20261013/Summary.json`에 남겼다. 새 200명 성능·GPU·Cook/패키징 검증을 수행한 것으로 표현하지 않는다.
