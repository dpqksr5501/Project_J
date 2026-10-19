# GASP와 Project_J의 회전 자연스러움 비교

현재 설치본·선택 정책의 후속은 [GASP 5.8 조사·적용](../Architecture/GASP58_Locomotion_Migration_2026-10-19.md)이다. 이 비교의 당시 관찰과 제안은 역사 자료로 보존한다.

**2026-10-15 후속 기준:** 아래 본문은 `ad82537d` 당시 비교 기록이다. 현재 후진 Reface 후보·정책과 통합 단발/MM 스택은 이미 구현돼 있으므로 아래 제안을 미구현 목록으로 사용하지 않는다. [Mirror Data 도입 보류 결정](Mirror_Data_Adoption_Decision_2026-10-15.md)에 따라 당시 locomotion을 유지했다. 이후 변경은 위 5.8 후속을 따른다.

2026-10-12 기준 Project_J `ad82537d`의 코드, 이전 GASP 조사, 사용자가 제공한 후진 급회전 화면을 비교한다. 현재 가장 먼저 확인할 차이는 블렌드 시간보다 **후진 중 몸의 방향을 크게 바꾸는 Reface가 검색 후보로 들어오는 조건**이다. 기존 전방 Turn의 허용 범위를 넓히는 방식보다, GASP의 Spin 의미와 실제 후진 Reface 데이터를 구분해서 검증하는 편이 적합하다.

이전 역순 Turn 실험은 폐기된 상태를 유지한다. 이번 비교는 코드·에셋 변경이나 새 재생 검증을 포함하지 않는다. 아래의 구현 상태는 현재 소스와 적용 기록을 기준으로 하며, 화면 품질의 원인 확정에는 동일한 이동 조건의 재생 비교가 필요하다.

## 이전 조사와 현재 구현

다른 채팅 「GASP 정리본」과 「모션 매칭 구조 비교 분석」, [2026-10-06 조사 묶음](GASP_ProjectJ_Naturalness_2026-10-06/README.md)을 재사용했다. 당시 분석의 일부 Project_J 설명은 후속 구현으로 달라졌다.

| 비교 항목 | 이전 조사 또는 GASP | 현재 Project_J |
| --- | --- | --- |
| 후보 데이터베이스 | GASP는 상황에 따라 Loops와 Starts/Pivots/Spin 등을 함께 제공한다. 이전 Project_J 조사에는 단일 PSD 경로가 있었다. | 호환되는 TurnRedirect+Cycle, Cycle+GeneralTurn을 함께 검색한다. 현재도 단일 PSD만 쓴다는 설명은 맞지 않는다. |
| 플레이어별 보정 | GASP MM 내부 Blend Stack에서 Orientation Warping과 Steering을 처리한다. | MM 내부와 외부 스택에 각 플레이어의 실제 에셋·시간을 사용하는 ResetRoot/Steering 경로를 추가한 적용 기록이 있다. 내부 스택을 전혀 활용하지 않는다는 과거 설명은 현재 상태에 적용하지 않는다. |
| 단발 복귀 | 기본 GASP MM과 Experimental State Machine의 직접 스택은 별도 경로다. | 외부 단발→MM 복귀는 실제 outgoing 포즈를 유지하는 Handoff, Pose History 기반 첫 검색, 입력 적합성 및 지속 곡선의 Cycle 양보를 사용한다. 관성 블렌드 하나만 추가한 구조가 아니다. |
| Strafe 예측 | GASP는 CMC와 미래 이동/Facing을 함께 사용한다. | 옆·뒤 Strafe의 예측 yaw rate 안정화가 있으며, 전방 Turn 쿼리와 구분한다. |
| 몸체 표시 | GASP는 시각 루트와 캡슐 차이를 활용한다. | 제한된 Steering/OffsetRoot에 더해 전방 Strafe Run의 과한 원본 가슴·골반 yaw를 표시 포즈에서 보정한다. 해당 보정은 후진 Reface 선택 기능이 아니다. |

현재 후보 연결의 근거는 [AssetSet](../../../Source/Project_JCharacter/Private/Animation/Project_JMotionMatchingAssetSet.cpp)의 `FindTurnCycleCompanion`이다. 같은 Schema와, 전처리가 있으면 공유 Normalization Set을 요구한다. 후속 플레이어 보정은 [연속성 구현 기록](GASP_ProjectJ_Naturalness_2026-10-06/07_LOCOMOTION_CONTINUITY.md), 단발 복귀는 [Live Return](../Architecture/OneShot_Live_Return_2026-10-11.md)과 [입력 반응](../Architecture/OneShot_Input_Response_2026-10-11.md)에 기록돼 있다. 현재 Master의 전체 그래프를 이번에 새로 추출한 것은 아니므로 적용 기록과 현재 소스에서 확인되는 계약을 구분한다.

## GASP 화면에서 확인되는 Reface의 역할

사용자 화면에는 다음 조합이 보인다. 사진의 나열 순서를 연속된 프레임 기록으로 간주하지 않는다.

| 화면 정보 | 확인 가능한 의미 |
| --- | --- |
| 검색 목록 Loops+Pivots, 선택 Diamond, 스택 Box/Diamond/Start | 전환 후보와 지속 이동 후보가 동시에 열리며 이전 플레이어도 스택에 남는다. |
| 검색 목록 Loops+SpinTransition, 선택 Loop | Spin 후보를 열었다고 즉시 특정 Reface로 강제 전환하는 것은 아니다. |
| CurrentDatabase SpinTransition, CurrentAsset Reface_Start_B_R_180 | 이 Reface가 SpinTransition 데이터베이스에서 선택된 사례가 있다. |
| 검색 목록은 Loops인데 스택에 Reface가 남음 | 현재 선택/재생 데이터베이스와 이번 검색 후보 목록은 같을 필요가 없다. 재생 유지와 outgoing 블렌드의 기여는 별도로 확인해야 한다. |

정확한 이름은 `M_Neutral_Run_Reface_Start_B_R_180`이다. 일반 `Run_Start_B_R_180`과 구분한다. Epic은 Spin을 이동하면서 몸의 방향을 바꾸는 전환으로 설명하며, 샘플이 그 용도에 **refacing starts를 사용한다**고 명시한다. 따라서 이름에 Start가 있다는 이유만으로 정지 후 출발 조건이 성립했다고 해석하면 안 된다. [Epic 공식 GASP 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine)

기존 로컬 GASP 5.7.4 조사에는 Spin 판단의 시각 root–actor 오차, 속도 조건, Pivot과의 우선 관계가 기록돼 있다. 공식 문서는 현재 5.8 문서이므로 로컬의 정확한 임계값·부등호·활성 Chooser 행을 대신 증명하지 않는다. 화면만으로는 왜 그 순간 Spin이 열렸는지, Reface가 어떤 비용으로 이겼는지, 각 스택 플레이어의 실제 가중치를 알 수 없다.

## 현재 후진 급회전과 직접 관련된 차이

### 후진 이동과 몸의 회전을 구분하는 후보

[현재 데이터 가족](../../../Source/Project_JCharacter/Public/Animation/Project_JMotionMatchingAssetSet.h)은 Cycle, SettledCycle, TurnRedirect, GeneralTurn이다. GASP의 Loops+SpinTransition에 대응하는 독립된 Spin 후보 슬롯은 이 지속 MM 가족에 없다.

다만 Project_J에도 `ShouldSpinTransitionForContext`와 Chooser에 전달하는 `bChooserShouldSpinTransition` 값은 존재한다. 현재 판정은 이동 입력·속도·`DesiredFacingDeltaYaw`를 사용한다. 그러므로 “Spin 관련 로직이 전혀 없다”는 설명도 부정확하다. **값을 계산하는 것과, 실제 MM 검색에 후진 Reface PSD를 제공하는 것은 별개**다. 현재 Chooser의 해당 값 사용 행과 실제 PSD 등록은 최소 범위의 에디터 조회로 확인해야 한다.

후진 Reface 원본 `B_L_180`, `B_R_180` 에셋은 프로젝트에 존재한다. 파일 존재만으로 현재 Cycle/Turn/단발 Chooser의 검색 가능 엔트리라고 판단하지 않는다.

### 전방 Turn 정책의 보호 범위

[GeneralTurn 정책](../../../Source/Project_JCharacter/Public/Animation/Project_JGeneralTurnPolicy.h)은 후진·옆 Strafe에 전방 Turn 데이터가 들어가지 않도록 요청 방향과 실제 이동 방향을 검사한다. [MovingTurn 정책](../../../Source/Project_JCharacter/Public/Animation/Project_JMovingTurnPolicy.h)도 전방 요청과 이동 자격을 사용한다. 이는 A/S/D에서 전진 Arc/Turn을 빌려 쓰던 문제를 막기 위한 경계다.

이 경계를 단순히 풀면 기존 방향 불일치를 재도입할 수 있다. S 유지 급회전에는 전방 Turn을 역재생하는 방법보다 **원래 후진 Reface가 설명하는 이동·Facing 관계**를 먼저 확인하는 것이 맞다. 다만 해당 클립이 현재 이동 속도와 발 위상에 적합한지는 별도 문제다.

### Start와 Pivot의 의미

[Locomotion 상태 코드](../../../Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent.cpp)의 `IsStartingForContext`는 입력 유지 시간, 속도, 가속/예측 속도 증가, 접지 및 착지 조건을 사용한다. S를 오래 유지한 상태에서는 물리적인 감속이 생겨도 일반 Start가 다시 열린다고 볼 수 없다.

같은 파일의 Pivot은 의미 입력 revision과 실제 반전 방향을 함께 사용한다. `UpdateLocalMoveIntentSnapshot`은 로컬 입력 방향이 바뀔 때 revision을 증가시킨다. S를 그대로 누르고 카메라만 돌리면 world 이동 의도는 변하지만 로컬 S 방향은 유지된다. 이미 소비한 revision에서는 다시 Pivot을 요청하지 않으므로, 이 정책을 GASP의 매 프레임 물리 기반 Pivot/Spin과 동일하게 보면 안 된다. 최초 미소비 revision 등 예외의 실제 결과는 프레임 로그로 확인한다.

### 검색 중단과 재생 유지

[현재 Proxy](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstanceProxy.cpp)는 기본 `DoNotInterrupt`와 핵심 상태 변경의 interrupt를 사용한다. Strafe의 Dynamic/Settled 가족 및 MovingTurn 경계에는 추가 보호가 있다. 따라서 GASP 화면의 `DoNotInterrupt` 하나를 복사하는 것이 해법은 아니다.

Continuing pose, 검색 가능한 시간 구간, 재선택 이력, 실제 블렌드 가중치는 후보가 열린 뒤의 선택을 좌우한다. 사진에 여러 이름이 보이는 것만으로 과도한 재선택이나 부드러운 합성 중 어느 쪽인지 판정할 수 없다.

## 자연스러움을 함께 만드는 요소

GASP의 자연스러움은 후보 공급, 검색 쿼리, 실제 재생 연결, 시각 루트/발 처리의 조합으로 보는 것이 타당하다. Epic 문서는 MM 내부의 플레이어별 보정과 캡슐에서 독립적인 시각 루트 회전도 설명한다. 이 구조는 급한 캡슐 변화에도 authored 몸 회전을 이어갈 수 있게 하지만, 실제 후진 장면의 품질을 설명하는 단일 원인으로 확정할 수는 없다. [Epic 공식 GASP 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine)

이전 조사에서 확인한 GASP/Project_J의 가속·감속·마찰·회전 한도와 PSS 가중치는 서로 달랐다. 그 값들은 당시 에셋 관찰이며 현재 양쪽 설정을 새로 대조한 값은 아니다. 현재 Project_J에는 후속 Strafe 예측 안정화와 전방 표시 보정도 있다. 옛 수치만 복사해 CMC나 PSS를 바꾸면 기존 Run/Sprint 응답과 Diamond/Box 분포를 함께 바꿀 수 있다.

현재 [Strafe 예측 안정화 코드](../../../Source/Project_JCharacter/Private/Animation/Project_JMotionMatchingTrajectoryComponent.cpp)는 로컬 옆·뒤 Strafe, 접지, 행동/몽타주/root-motion 배제 및 Facing 오차 조건을 사용한다. 빠른 180도에서 이 안정화가 계속 켜진다고 가정하면 안 된다. [상체](../Architecture/Strafe_Upper_Body_Facing_2026-10-11.md)·[하체](../Architecture/Strafe_Lower_Body_Facing_2026-10-11.md) 보정 역시 전방 Run Cycle 중심이므로 S 급회전의 새 전신 전환을 만들어 주지 않는다.

## 다음 비교의 우선순위

1. **후진 Reface 후보 공급**: GASP의 실제 Spin 함수·활성 Chooser 행·SpinTransition PSD의 후진 Reface 엔트리를 읽는다. Project_J의 Spin 값 사용 행, 단발 Reface 결과 및 MM 데이터 등록을 함께 확인한다. 원본 시퀀스의 root yaw·이동 방향·검색 가능 구간·발 접촉·좌우 미러를 비교한다.
2. **동일한 물리 상태의 선택과 합성**: 양쪽에서 S 유지 좌/우 급회전을 실행하고 실제 카메라 yaw, 속도·가속·velocity yaw, actor/visual-root yaw가 비슷한 구간을 맞춘다. 같은 마우스 이동량만 맞추면 CMC 차이가 섞인다. 후보가 열리는 시점부터 선택 clip/time과 continuing, 새 블렌드, 플레이어 가중치 및 발 접지를 비교한다.
3. **보정과 이동 모델 분리**: 후보가 충분한데도 차이가 남으면 Steering/OffsetRoot와 이동 모델을 하나씩 비교한다. 블렌드·PSS·가속을 한 번에 변경하지 않는다. 최종 렌더링에서 발 디딤·기울기·방향 연결을 함께 평가한다.

핵심 기록은 held input/intent revision, 속도·실제 가속·예측 속도, velocity/입력/control/actor/visual-root 방향, Start/Pivot/Spin 자격, 검색 PSD 목록, 선택 PSD/asset/time/continuing/search cost, 스택별 time/weight, Steering/OffsetRoot의 소유권과 양발 접지다. 결과를 읽기 위해 키보드 후진을 물리적인 후진 및 몸체 Facing과 따로 구분한다.

후속 구조 후보는 **지속 MM 안에서 조건에 맞는 후진 Reface+방향별 Cycle을 경쟁시키는 방식**이다. 아직 구현 결정은 아니다. 공유 Schema/정규화, 해당 pose의 진입 가능 구간, 회전 완료와 continuing 정리, 기본 후보에 대한 Loop 복귀, 전투/몽타주/root-motion 우선권을 함께 충족해야 한다. 특정 클립이나 시작 시각을 각도만으로 강제하지 않는다.

OTM과 잘 동작하는 전방 Strafe Run/Sprint는 기존 정책을 유지한다. 후진 후보를 검증하더라도 제자리 회전·Start/Land·전투 몽타주·원격 표현 및 군중 검색 예산의 경계를 먼저 정한다. 원격 전체에 고빈도 검색이나 로컬 전신 보정을 확장하는 근거로 이번 비교를 사용하지 않는다.

## 비교의 확인 범위

- 현재 코드의 후보 가족·전방 보호·Start/Pivot/Spin 판정·interrupt·Strafe 예측 조건을 읽었다.
- 이전 두 채팅과 저장된 GASP 조사·후속 적용 기록을 현재 코드와 구분했다.
- 사용자 화면의 SpinTransition에서 후진 Reface 선택과 Epic의 Spin 설명을 대조했다.
- 이번 세션의 도구 목록에는 Unreal 에디터 조회 MCP가 노출되지 않아, 현재 양쪽 Chooser/PSD/전체 Master 그래프의 새 조회와 동시 재생 trace는 수행하지 않았다. 이 한계 때문에 후진 Reface의 현재 후보 누락 및 화면 품질의 직접 원인을 확정하지 않는다.
- 코드·에셋·레벨·빌드는 변경하지 않았다. 기존 조사 묶음을 덮어쓰지 않고 이 비교 문서만 추가한다.
