# 01. 조사 범위와 증거 장부

## 1. 환경과 판정 범위

| 항목 | 확인 내용 | 수준 |
| --- | --- | --- |
| GASP 루트 | C:/Users/I/Documents/Unreal Projects/GameAnimationSample | S |
| Project_J 루트 | C:/Users/I/Documents/GitHub/Project_J | S |
| GASP 엔진 | 로컬 로그·에디터 기준 5.7.4 | A/S |
| Project_J 엔진 | 열린 에디터 기준 5.8.2 | A |
| GASP 배포 리비전 | 정확한 Marketplace/Fab 패키지 리비전 미식별 | U |
| GASP 조사 경로 | SandboxCharacter_CMC_ABP의 기본 MM, 실험적 SM은 별도 구분 | G/A |
| Mover | CMC와 동일하다고 가정하지 않음 | U |
| 새 실행 | 이번 분석에서 새 PIE·빌드·컴파일 없음 | 작업 기록 |
| 실행 바이너리 | 읽은 C++와 현재 로드된 바이너리의 완전 일치 미검증 | U |

본 문서는 채팅의 구조 분석과 확인 결과를 보존한다. 모든 에셋의 모든 그래프를 조사했다는 뜻은 아니다. 최종 관찰 대상은 운영 소스 메시의 Master 경로이며 모든 직업·리타깃 follower·전투·탑승의 최종 화면은 별도 검증 대상이다.

## 2. 도구가 실제로 확인한 수준

처음에는 Unreal MCP 서버가 연결되지 않았고 에디터도 실행되지 않은 상태였다. 이때 자산 목록/이름으로 그래프를 추정하지 않았다. 사용자가 에디터를 실행하고 MCP/computer-use를 허용한 뒤 실제 조회를 진행했다.

- 에디터 화면: AnimGraph의 활성 포즈 핀, 내부 Blend Stack 그래프, 노드 Details, 함수 바인딩과 본문, Chooser 결과 타입/행 조건/행 Disable 상태, PSD/PSS 설정을 확인했다.
- 그래프 복사 텍스트: 운영 Master의 노드·핀·연결·중첩 그래프 정보를 확보했다. declaration과 property initialization을 구분해 연결을 읽었다.
- Unreal Official MCP: object `get_properties`, 그래프 조회 등의 read-only 도구로 BP/CDO·Mesh·프로필·Chooser ColumnsStructs·PSD 설정을 읽었다.
- 일부 속성은 reflection 조회가 실패했다. 잘못된 속성명 또는 비노출 속성의 실패를 값 None/기능 부재로 해석하지 않았다.
- Chooser 출력 struct만으로 활성 행을 판단하지 않았다. FallOff의 true 출력 두 행과 Pivot 대각 행은 에디터 Disable 상태를 별도로 확인했다.
- 저장·컴파일·set_properties·write_graph_dsl·에셋 생성/수정 호출은 분석에서 수행하지 않았다.

이번 문서의 Evidence 폴더는 확보한 원문 중 재사용 가능한 자료를 보존한다. 원문이 없는 과거 화면 관찰은 채팅에서의 확인 결과로 표시하며, 저장된 화면 캡처가 있다고 가장하지 않는다.

## 3. 근거 ID

| ID | 근거 | 범위/주의 |
| --- | --- | --- |
| G01 | GASP Content/Blueprints/SandboxCharacter_CMC_ABP.uasset | CMC 기본/실험 경로, 실제 내부 그래프·함수·기본값 확인 |
| G02 | GASP CHT_CMCCharacterAnimations | 다중 PSD 반환과 조건부 후보 겹침; 아래 경로 참고 |
| G03 | GASP MotionMatchingData/Schemas/PSS_Default, PSS_Jump | 특징·가중치·시간·샘플링 확인 |
| G04 | GASP PSD_SM_CMC_Loops, PSD_SM_CMC_Transitions | 실험적 SM 데이터. 기본 경로 전부를 이 이름으로 대체하지 말 것 |
| G05 | GASP 대표 Run Turn L 180 Rfoot 시퀀스 | Branch In·Exclude·continuing override·Block Transition |
| J01 | Project_J BP_GreatSword, Mesh, DA_GreatSword_AnimProfile | 운영 기본 Pawn/AnimBP/프로필 연결 |
| J02 | ABP_Humanoid_Master 주 그래프·내부 그래프·CDO | 최종 출력·MM/외부 스택·History·legacy None |
| J03 | DA_Player_Profile, DA_Player_Combat | 실제 이동·취소·검색·예산 설정 |
| J04 | DA_Player_Locomotion, DA_Player_Combat_Strafe | 현재 단일 PSD 연결; 같은 basename의 에셋 주의 |
| J05 | State Controller 계층 Chooser | 결과 타입, 조건, 시작 시간, 비활성 행 |
| J06 | PSS_Player, PSS_Combat 및 Cycle/Turn PSD | 특징·가중치·검색 설정·후보 구성 |
| J07 | 대표 M_Neutral_Run_Turn_L_180_Rfoot | 실제 시퀀스 설정 및 notify 구간 |
| S01 | Project_JPlayerInputBindingComponent.cpp | HandleMove, 입력 의도 선행 기록, 카메라 기준 월드 입력 |
| S02 | Project_JPlayerCharacter.cpp 및 .Movement.cpp | tick·이동·회전·TIP authored root yaw |
| S03 | Project_JMotionMatchingTrajectoryComponent.cpp | history 1회, CMC 예측, Strafe Facing, 원격 후처리 |
| S04 | Project_JLocomotionAnimStateComponent*.cpp | 상태 판정, 요청 수명, TIP unwrapped, MovingTurn 자격 |
| S05 | Project_JMovingTurnPolicy.h | 큰 반전 수명·각도·cooldown·re-arm |
| S06 | Project_JCharacterAnimInstance.cpp | Chooser cache/평가/진입 MM/override/취소 |
| S07 | Project_JCharacterAnimInstanceProxy.cpp | PSD·runtime 노드 정책·검색 요청·예산·중첩 traversal |
| S08 | Project_JLocomotionProfile.cpp/.h, AssetSet/SelectionPolicy | 설정 해석, DB 해석, validation |
| E01 | UE_5.8 PoseSearchLibrary.cpp | 후보 구성·Branch In·interrupt/continuing의 차이 |
| E02 | UE_5.8 PoseSearchDatabase.cpp 및 derived data 생성 | cost override와 유효 샘플 범위 |
| E03 | UE_5.8 AnimNode_OrientationWarping.cpp | TargetTime>0일 때 CurrentAnimAssetTime 이용 |
| H01 | OneShot_Command_Lifetime_2026-10-05.md/.json | 연속 Pivot·mode 전환·사용자 정상 동작 확인 |
| H02 | Landing_Return_Linked_Search_2026-10-05.md | 이동 착지 복귀와 실제 검색 실행의 후속 로그 |
| H03 | Moving_Turn_180_2026-10-06.md/.json 및 Saved 검증 | 큰 Turn 조기 종료의 과거 실행 증거 |
| H04 | MotionMatching_Return_Crowd_2026-10-05.md | 요청 유지와 군중 예산 계약 |

Project_J Source 근거는 모두 `Source/Project_JCharacter/Private` 또는 `Public` 아래다. 설치 엔진 근거는 `C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Animation/` 아래다. 세부 파일 링크는 관련 문서에서 제공한다.

## 4. 주요 에셋 식별

### GASP

- `Content/Blueprints/SandboxCharacter_CMC_ABP.uasset`
- `Content/Blueprints/SandboxCharacter_Mover_ABP.uasset`: 이름 확인과 CMC 분석을 구분한다.
- `Content/Characters/UEFN_Mannequin/Animations/ExperimentalStateMachineData/CHT_CMCCharacterAnimations.uasset`
- 같은 ExperimentalStateMachineData의 `PSD_SM_CMC_Loops`, `PSD_SM_CMC_Transitions`
- MotionMatchingData/Schemas의 `PSS_Default`, `PSS_Jump`

### Project_J

- `/Game/Character_BPs/GreatSword/BP_GreatSword`
- `/Game/Animation_Logic/ABPs/ABP_Humanoid_Master`
- `/Game/Animation_Logic/ABPs/ABP_Player`: 별도 테스트 경로
- `/Game/DataAssetSets/Animation_Profiles/AnimProfiles/DA_GreatSword_AnimProfile`
- `/Game/DataAssetSets/Animation_Profiles/LocomotionProfiles/DA_Player_Profile`
- `/Game/DataAssetSets/Animation_Profiles/MMProfiles/DA_Player_Locomotion`
- `/Game/DataAssetSets/Animation_Profiles/Combat_MMProfile/DA_Player_Combat_Strafe`
- `/Game/Animation_Logic/PSS/PSS_Player`, `/Game/Animation_Logic/PSS/PSS_Combat`
- `/Game/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Run_Cycle`, `PSD_Run_Turn`
- `/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_Cycle`
- **현재 연결된 Turn:** `/Game/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Combat_Run_Turn`
- 같은 이름의 이전 위치: `/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_Turn`
- `/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_Turn_L_180_Rfoot`

파일명·오브젝트명에서 GreatSword/Greatsword 등 대소문자 표현이 일부 다르다. 에셋 참조의 실제 refPath와 native class를 같이 확인한다.

## 5. 결론 장부

| 결론 | 확인 수준 | 한계/재확인 |
| --- | --- | --- |
| 운영 기본은 Master이며 ABP_Player는 자식이 아님 | G/A/S/D | 월드별 실제 spawn은 새 PIE 확인 필요 |
| 일반 검색 신규 후보는 C++가 정한 단일 PSD | G/A/S | 이전 continuing 정보/블렌드 유지와 전체 PSD 동시 경쟁은 다름 |
| Combat Cycle에 Hourglass/Diamond, 확인한 OTM Run Cycle에는 없음 | A | 사용자의 관찰 당시 actual PSD/모드 미식별 |
| GASP에 여러 PSD의 조건부 후보 겹침 | G/A | 모든 상태에 모든 PSD가 열리는 것은 아님 |
| Project_J MM 내부 보정 그래프는 Input→Result | G | 전투/리타깃 전체 경로 부재를 뜻하지 않음 |
| 외부 Steering은 TIP gate | G/S | Start/Land 일반 steering 활성의 증거가 아님 |
| 외부 OW enable_warping은 시간 핀, Alpha 아님 | G/E03 | 현재 TargetTime=0; 시각적 영향 별도 검증 |
| Offset Root Bone은 운영 그래프에 연결됨 | G/S | Release가 즉시 0이라는 뜻 아님 |
| InAirLoop 선택과 외부 override 조건 불일치 | G/A/S | 긴 낙하의 최종 포즈는 새 PIE 미확인 |
| MovingTurn 활성 중에도 speed>=180 자격 필요 | S/A/H | 최신 실행·시각 품질은 새 검증 필요 |
| TIP는 ±180 경계 unwrapped, MovingTurn은 별도 방향 latch 없음 | S | 모든 극단 입력의 품질 보장 아님 |
| History는 후처리 뒤 PoseCount=2 | G/A | native fallback 10과 구분 |
| 원격·군중 예산과 단발 수명은 보존 가치 있음 | S/A/H/I | 실제 비용 수치·최종 모습 미측정 |

## 6. 오래된 문서와 실제 연결의 차이

`GASP_ProjectJ_Locomotion_Parity.md`의 일부 표/추가 기록은 Offset Root Bone disconnected/없음 또는 다른 mode 설명을 포함한다. 현재 Master에는 노드가 연결되어 있고 native Release/Interpolate 정책이 공급된다. 당시 문서는 역사적 의도·변경 이유로 보존하되 현재 연결보다 우선하지 않는다.

Turn 문서·과거 trace의 PSD 경로/PSS 설명도 현재 새 Combat Turn 연결과 다를 수 있다. **현재 연결은 새 위치의 PSS_Combat 네 후보 PSD**다. 같은 basename을 동일한 데이터라고 가정하지 않는다.

Land 취소 후 Idle 약 60ms 경유는 과거 문제다. 후속 소스와 H02 실행 기록에서 Cycle 복귀 보완이 확인되므로 현재 미해결로 재기록하지 않는다. 연속 Pivot 수명 수정도 사용자 정상 확인을 포함하며 재도입할 작업으로 취급하지 않는다.

MovingTurn의 기존 8개 episode 원본 분석은 [보존한 PIE1053 분석](Evidence/MovingTurnTrace_PIE1053.Analysis.json)을 참고한다. 이 기록의 Strafe schema는 PSS_Player다. 현재 새 Turn의 PSS_Combat 연결과 구분하며 옛 trace가 현재 데이터까지 검증했다고 해석하지 않는다.

## 7. 별도 확인이 필요한 항목

- 정확한 GASP 패키지 리비전, Mover, 모든 database LOD/상태 조합.
- 실행 바이너리와 소스 일치, 실제 월드의 Pawn/AnimInstance.
- 긴 낙하 최종 출력, 순간 보정과 발 미끄러짐, 170/190·연속 반전 영상.
- 모든 시퀀스의 root/contact curve·좌우 발 위상, 리타깃 후 접촉 품질.
- Follower/PostProcess/전투 Linked Layer/탑승의 추가 후처리.
- SourceMesh/VisualMesh/net smoothing이 최종 root offset과 foot trace에 미치는 영향.
- 군중 1/50/100/200 CPU/GPU·메모리·검색·trace 비용과 update starvation.

## 8. 외부 참고 자료의 위치

아래는 후속 개념 확인용 공식 자료다. 로컬 그래프의 활성 여부는 이 자료가 아니라 G/A/S 근거로 판단했다. 온라인 문서는 5.8 최신 페이지로 바뀔 수 있으며 로컬 GASP 5.7.4와 동일함을 보장하지 않는다.

- [Epic Game Animation Sample](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine): 샘플의 목적과 capsule-driven 기반, Chooser·보정 확인 참고.
- [Epic Motion Matching](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine): schema/database/query 개념 참고.
