# 지속 곡선 이동 판정과 마스터 ABP 함수 정리

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
- 곡선 복귀에는 Cycle을 열었다. MM이 Loop/Box/Diamond를 선택한 사례가 있으며, Arc를 강제하지 않는다. 실제 비용에 맞는 곡선 대응 동작을 찾을 수 있도록 허용하는 변경이다.

삭제 전 마스터 SHA256: `2973A1B510EE3888D4F36FABF10D6445733C88B4BE34DADD08BC2D10E29C789B`. 백업은 `ABP_Humanoid_Master_BeforeCleanup.uasset`이다. 삭제 후 SHA256: `701E189BE5AAFF7D5D9C4D451CBC3F64BDFA2F8266E936B40936D5D9694CA91A`.

함수 제거에 사용한 일회성 authoring 코드는 검증 루트에 보존하고 빌드 대상에서는 제거했다. 현재 함수 감사 테스트는 읽기 전용이며, 삭제한 함수가 다시 들어오거나 살아 있는 참조가 생기는 것을 검사한다.

이번 결과는 200명 FPS 개선 측정이 아니다. 로컬 입력의 품질 판정을 개선했으며, 이전 군중 측정과 네트워크 동작 계약은 별도 기록으로 유지한다. Cook/패키징 및 사용자의 최종 체감 품질 판정은 자동화 성공만으로 대신하지 않는다.
