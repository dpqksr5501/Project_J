# GASP 자연스러움 조사와 Project_J 비교 인수인계

> **2026-10-12 후속 작업:** 새 대화에서 사용자가 조사·코드/에셋 수정·저장·구현·빌드·종료를 허용했고, 원본 후진 Reface 후보의 구현과 검증을 진행했다. 현재 결과는 [조사 및 구현 보고서](GASP_Backward_Reface_Implementation_2026-10-12.md)를 먼저 읽는다. 아래 내용은 작업 시작 전의 인수인계 기록이다.

작성 기준: 2026-10-12. 다음 채팅은 이 문서와 프로젝트 `AGENTS.md`를 먼저 읽고 **조사·비교 단계부터 이어간다**. 사용자는 현재 Run의 전반적인 품질에 만족하며, GASP가 급격한 입력에도 자연스러운 이유를 현재 Project_J와 비교하고 싶어 한다. 당장의 대표 사례는 **Combat Strafe에서 S만 유지하면서 마우스로 빠르게 약 180도 회전**하는 장면이다.

현재 유력한 조사 대상은 후진 Reface의 후보 진입 조건과 시각 루트의 회전 처리다. 아직 원인 확정이나 새 구현 승인은 아니다. 이미 개선한 Run/OTM/Strafe 기능을 다시 만들거나, 폐기한 역순 Turn 실험을 재적용하지 않는다.

## 1. 바로 사용할 새 채팅 요청문

아래 문장은 사용자가 새 채팅에 붙여 넣을 요청문이다. 문서 안의 요청문만으로 새 작업의 권한이 자동 부여됐다고 해석하지 않는다.

> `C:\Users\I\Documents\GitHub\Project_J\Docs\Animation\Planning\GASP_Naturalness_Handoff_2026-10-12.md`와 프로젝트 AGENTS.md를 읽고 GASP 자연스러움 조사 및 현재 Project_J 비교를 이어가줘. 이전 두 채팅과 저장된 조사 자료를 재사용하되, 현재 코드와 에셋 상태를 기준으로 다시 확인해줘. 우선 Strafe에서 S만 누른 채 빠르게 180도 회전할 때 GASP가 SpinTransition의 후진 Reface를 선택하는 조건과, 우리 후보·시각 루트·발 위상의 차이를 조사해줘. 필요한 에셋의 읽기 전용 확인에는 Unreal MCP나 컴퓨터 use를 활용해줘. 바로 수정하거나 에셋을 저장하지 말고, 근거와 좋은 구조를 먼저 토의해줘. 이미 잘되는 OTM, 전방 Strafe Run/Sprint, 제자리 회전, Start/Land, 전투 몽타주, 원격 및 군중 예산 정책을 보호해줘. 도구가 연결되지 않으면 가능한 코드·문서 조사를 이어가고, 에디터 확인이 필요한 항목만 구체적으로 알려줘.

## 2. 현재 저장소와 실행 환경

| 항목 | 인수인계 시 확인한 값 |
| --- | --- |
| 프로젝트 | `C:\Users\I\Documents\GitHub\Project_J\Project_J.uproject` |
| 현재 브랜치 | `codex/strafe-backward-turn-trial` |
| HEAD | `a987c44e` — `문서갱신`. 이번 비교 문서 한 개를 추가한 커밋 |
| 애니메이션 코드 기준점 | `ad82537d` — `Strafe 전방 곡선 이동의 상체·골반 방향 보정` |
| 추적 파일 변경 | 인수인계 작성 직전 없음 |
| 미추적 문서 | 이 인수인계 문서 한 개. 아직 커밋하지 않음 |
| Project_J 엔진 | `.uproject` association 5.8, 현재 `Saved/Logs/Project_J.log`에는 5.8.3-58210709 |
| 로컬 GASP | `C:\Users\I\Documents\Unreal Projects\GameAnimationSample\GameAnimationSample.uproject` |
| GASP 엔진 | `C:\Users\I\Documents\Unreal Projects\GameAnimationSample\Saved\Logs\GameAnimationSample.log`에는 5.7.4-51494982 |
| 운영 Master | `/Game/Animation_Logic/ABPs/ABP_Humanoid_Master` |
| 사용자 로그 | `C:\Users\I\Documents\GitHub\Project_J\Saved\Logs\Project_J.log` |

브랜치 이름에 trial이 남아 있어도 역순 Turn 코드가 활성화된 상태는 아니다. 새 채팅에서는 `git status`, 현재 브랜치와 HEAD를 다시 확인하고, 사용자 변경이 추가됐으면 보존한다. 지금 에디터나 빌드가 실행 중인지는 새로 확인해야 한다. 이 문서는 실행 프로세스 상태를 보증하지 않는다.

인수인계 작성 도중 외부 작업으로 `a987c44e`가 추가됐다. `git show --stat`으로 비교 문서만 추가한 것을 확인했다. 이 채팅에서 커밋이나 push를 실행한 것은 아니다.

## 3. 사용자 목표와 적용 경계

- 급회전뿐 아니라 작은 입력, 지속 곡선, Start/Land 이후 입력 변화에서도 좋은 애니메이션을 선택하며 연결되길 원한다. 특정 Turn이 무조건 나오게 만드는 것이 목표가 아니다.
- OTM은 이동 방향 중심이고 Strafe는 Facing과 이동 방향이 다르다. S 유지, 실제 world 이동, actor Facing, 시각 몸체 Facing을 따로 읽는다.
- Strafe Sprint는 전방 F/FL/FR만 지원하는 현재 계약을 유지한다. 사용자는 F, F+A, F+D라고 표현했다. 후진 Sprint로 확대하지 않는다.
- Master ABP를 공유하는 여러 캐릭터에 적용하고, 캐릭터별 설정·리타깃·LOD·원격 및 MMORPG 군중 예산을 고려해야 한다.
- 수치와 NullRHI 검사만으로 발 디딤·기울기·화면 자연스러움을 승인하지 않는다. 이전 실험은 수치 검사와 체감이 달랐다.
- 이번 범위는 조사 및 비교다. 코드 구현, 에셋 저장, 커밋·push, 컴퓨터 종료는 이번 요청에 포함되지 않는다. 과거 요청을 새 작업의 자동 실행 권한으로 이어받지 않는다.

## 4. 폐기한 역순 Turn 실험

이전에는 기존 전방 Turn을 역순으로 활용하고 후진 Loop·지지발·자세를 맞추는 미저장 실험을 했다. 사용자는 실제 테스트에서 순간이동처럼 보이는 변화와 매우 어색한 움직임을 확인하고 **“이거 하지 말자”**고 취소했다.

실험용 변경은 제거했고 안정 기준 코드로 복원 빌드했다. `Saved/Validation/DiscardedBackwardTurn_20261012/RestoreBuild.txt`의 결과는 `Succeeded`, 소요 약 50초다. 이는 복원 당시의 빌드 결과이며 새 변경의 검증을 대신하지 않는다.

| 보존 자료 | 용도 |
| --- | --- |
| `Saved/Validation/DiscardedBackwardTurn_20261012/SourceSnapshot/` | 폐기 코드의 추적·원인 참고용 보관본. 운영 코드로 복원하지 않는다. |
| 같은 폴더의 `RestoreBuild.txt`, `RestoreBuild.json` | 안정 코드 복원 빌드 근거 |

실험 중 생성 시퀀스의 root-motion/root-lock 계약 문제도 다뤘지만 실험 자체를 폐기했다. 이것을 현재 안정 코드의 미해결 버그나 GASP의 자연스러움 원인으로 단정하지 않는다. 당시 에셋 해시 보존 검사는 시행됐으나, 새 작업에서 에셋을 만지기 전에는 다시 기준점을 확보한다.

## 5. 이미 구현된 기능

아래는 현재 소스와 후속 적용 문서에서 확인한 상태다. 이번 조사에서 Master의 전체 그래프를 새로 추출한 것은 아니므로, 현재 에디터의 실제 연결은 필요한 노드만 다시 확인한다.

| 기능 | 현재 구조와 보호 범위 |
| --- | --- |
| 다중 PSD 후보 | 자격이 있는 TurnRedirect+Cycle 또는 Cycle+GeneralTurn. 같은 Schema와 필요한 공유 정규화를 검사한다. |
| 방향별 Cycle | 부드러운 곡선에는 풍부한 Cycle 후보를 유지한다. 옆·뒤 Strafe에 전방 Turn 후보를 빌려 주지 않는다. Dynamic/Settled 경계와 continuing 정리도 존재한다. |
| 큰 전방 회전 | MovingTurn이 running origin·준비·실제/시각 방향 오차·완료를 다룬다. 단순히 짧은 시간 안에 마우스 180도를 요구하는 최초 구조로 되돌리지 않는다. |
| 플레이어별 보정 | MM 내부와 외부 Blend Stack에서 실제 플레이어의 asset/time/mirror를 사용하는 ResetRoot/Steering 적용 기록이 있다. |
| 단발 복귀 | `One Shot → Motion Matching Handoff`가 Start/Stop/Land outgoing 포즈를 실제로 진행하며 MM과 겹친다. TIP/Pivot 및 행동 소유권에는 별도 보호가 있다. |
| Pose History와 첫 검색 | 단발 동안 최종 History를 유지하고 복귀 시 이력을 사용한 첫 MM 검색을 보장한다. MM을 단발 재생 내내 백그라운드 검색하는 구조는 아니다. |
| 단발 입력 적합성 | 작은 입력은 Steering 범위 내에서 유지하고, 실제 지속 곡선은 거리·heading 증거로 풍부한 Cycle에 양보한다. 키 변경과 카메라 곡선을 구분한다. |
| Strafe 예측 안정화 | 로컬 옆·뒤 Strafe의 예측 yaw rate를 안정화한다. 전방 쿼리·OTM·행동/몽타주/root-motion과 구분한다. Facing 오차가 큰 급회전에서 항상 활성인 것은 아니다. |
| 전방 표시 보정 | History 뒤 Lower Body Facing→Upper Body Facing. 전방 Strafe Run의 과한 원본 가슴/골반 yaw를 제한적으로 줄인다. A/S/D의 후진 전신 전환을 만드는 기능은 아니다. |

**폐기해야 할 오래된 결론:** “우리 프로젝트는 항상 단일 PSD다”, “MM 내부 Blend Stack을 전혀 사용하지 않는다”, “단발 복귀에 관성 블렌드만 하나 추가했다”. 이 설명들은 후속 구현 전의 상태와 혼동하기 쉽다.

Master 보정은 primary 메시에서 한 번 평가하고 follower가 리타깃 결과를 소비하는 방향이다. 역할 본 프로필이 임의 스켈레톤에 자동 호환됨을 뜻하지 않는다. 상·하체 표시 보정은 로컬 대상이며 원격 전체에 동일하게 확장된 상태로 보고하지 않는다.

## 6. 이번 GASP 관찰과 가장 중요한 차이

사용자의 GASP 화면에서 정확히 선택된 에셋은 **`M_Neutral_Run_Reface_Start_B_R_180`**이다. 일반 `Run_Start_B_R_180`과 구분한다.

- Loops+Pivots 검색과 Diamond 선택, Box/Diamond/Start가 남은 스택 화면이 있다.
- Loops+SpinTransition 검색 중 Loop를 선택한 화면이 있다.
- CurrentDatabase가 `PSD_Dense_Stand_Run_SpinTransition`, CurrentAsset이 후진 Reface인 화면이 있다.
- 다음 검색 목록에는 Loops만 있어도 Reface가 재생/스택에 남은 화면이 있다.
- 여러 사진은 연속된 trace가 아니다. CurrentDatabase, 검색 목록, 스택 플레이어 목록은 서로 다른 정보를 나타낸다. 이름만으로 실제 기여 가중치나 비용을 추정하지 않는다.

원본 화면 다섯 개는 작성 시점에 `C:\Users\I\AppData\Local\Temp\`에서 존재를 확인했다. 필요한 경우 이미지 뷰어로 읽고, Temp 파일이 사라졌다면 위 관찰과 비교 문서의 텍스트 근거를 사용한다. 파일명을 캡처 시간 순서로 해석하지 않는다.

| Temp 아래 파일명 | 화면의 주요 내용 |
| --- | --- |
| `codex-clipboard-ca25b4e5-65a2-4574-8c27-02c5dc78f7ff.png` | SpinTransition의 B_R_180 Reface 선택 |
| `codex-clipboard-106aefde-f846-4ef1-bd43-7d7b436939ad.png` | Pivots/Diamond와 Loops+Pivots 검색 |
| `codex-clipboard-6ed45882-b7f8-4120-bece-c72a39a4673c.png` | Loop 선택과 Loops+SpinTransition 검색 |
| `codex-clipboard-83d2f2d9-443d-492e-9954-c239859ccd5c.png` | Reface 0.68초 및 Loops 검색 |
| `codex-clipboard-dacebf00-7839-4e71-87bc-fa1d1f33b192.png` | Reface 0.42초 및 이전 Diamond/Box 스택 |

Epic은 Spin을 이동하면서 몸의 방향을 크게 바꾸는 전환으로 설명하며, 샘플에서 그 용도에 refacing starts를 사용한다고 명시한다. 그래서 해당 Reface가 나왔다는 사실을 일반 Start 재진입이나 정지→재출발의 증거로 해석하면 안 된다. [공식 GASP 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine)

현재 Project_J의 지속 MM 가족은 Cycle/SettledCycle/TurnRedirect/GeneralTurn이며, 별도 Spin PSD 슬롯은 없다. **그러나 Spin 판정 코드 자체는 있다.** `ShouldSpinTransitionForContext`가 입력·속도·DesiredFacingDeltaYaw를 판정하고 `bChooserShouldSpinTransition`으로 전달한다. 실제 Chooser 행과 MM 후보 연결을 확인하기 전에는 “완전히 없다” 또는 “이미 연결돼 있다”고 단정하지 않는다.

원본 `M_Neutral_Run_Reface_Start_B_L_180.uasset`, `B_R_180.uasset`은 Project_J의 `Content/Characters/UEFN_Mannequin/Animations/Run/`에 존재한다. 현재 PSD의 enabled entry, searchable 구간, 단발 Chooser 연결까지 확인한 것은 아니다.

현재 일반 Start는 입력 유지 시간과 속도 제한 등이 있다. Pivot은 의미 입력 revision을 소비한다. S 유지+카메라 회전은 world 의도를 바꾸지만 로컬 S 자체를 바꾸지 않으므로, GASP의 물리 기반 Pivot/Spin과 결과가 다를 수 있다. 정확한 프레임 결과는 trace로 확인한다.

전방 Turn cone을 후진까지 풀면 과거 A/S/D 방향 불일치를 재도입할 수 있다. 후속 구조 후보는 **조건에 맞는 후진 Reface+방향별 Cycle의 경쟁**이다. 후보로 검토할 뿐 아직 구현 결정은 아니다.

## 7. 근거 읽기 순서

전체 프로젝트나 큰 raw graph를 처음부터 읽지 말고 아래 순서로 필요한 부분을 읽는다. 프로젝트 기준 상대 경로이며 새 채팅의 기준점은 프로젝트 루트다.

1. [이번 비교 문서](GASP_ProjectJ_Naturalness_Comparison_2026-10-12.md): 현재 차이와 미확인 범위.
2. [기존 조사 README](GASP_ProjectJ_Naturalness_2026-10-06/README.md), [증거 장부](GASP_ProjectJ_Naturalness_2026-10-06/01_EVIDENCE.md), [GASP 분석](GASP_ProjectJ_Naturalness_2026-10-06/02_GASP.md): 기본 MM/Experimental SM 구분, 후보·쿼리·Steering·루트 계약.
3. [후속 연속성 구현](GASP_ProjectJ_Naturalness_2026-10-06/07_LOCOMOTION_CONTINUITY.md): 단일 PSD 및 내부 스택 설명이 바뀐 근거.
4. [Turn Handoff](../Architecture/Turn_Handoff_2026-10-07.md), [Live Return](../Architecture/OneShot_Live_Return_2026-10-11.md), [단발 입력 반응](../Architecture/OneShot_Input_Response_2026-10-11.md).
5. [Strafe 예측 안정화](../Architecture/Strafe_Prediction_Stability_2026-10-11.md), [상체](../Architecture/Strafe_Upper_Body_Facing_2026-10-11.md), [하체](../Architecture/Strafe_Lower_Body_Facing_2026-10-11.md) 표시 보정.

`GASP_ProjectJ_Locomotion_Parity.md`와 8월 Pivot 보고서는 오래된 시점의 설명도 포함한다. 현재 연결의 근거로 바로 사용하지 말고 후속 소스와 대조한다. 조사 묶음의 `Evidence/mcp_properties.json`과 raw Master clipboard는 당시 제한된 조회 자료다. 모든 GASP 함수의 원본 그래프를 보유한 자료가 아니다.

다른 채팅을 더 읽어야 한다면 다음 식별자를 사용한다. 두 채팅에는 메시지를 보내지 말고 필요한 최근 turn을 읽기만 한다.

| 실제 채팅 제목 | Codex thread ID |
| --- | --- |
| GASP 정리본 | `01a10a7c-4a07-75d1-85c2-62d487db71ca` |
| 모션 매칭 구조 비교 분석 | `01a10f0c-7072-7181-a462-b2c566569204` |

## 8. 최소 범위의 소스 지도

아래는 프로젝트 루트 기준 경로다. 함수 이름으로 좁게 검색하고 파일 전체를 반복 출력하지 않는다.

| 경로 | 확인할 계약 |
| --- | --- |
| `Source/Project_JCharacter/Public/Animation/Project_JMotionMatchingAssetSet.h` | 후보 컨텍스트와 Run/Sprint PSD 가족 |
| `Source/Project_JCharacter/Private/Animation/Project_JMotionMatchingAssetSet.cpp` | `FindTurnCycleCompanion`, Schema/Normalization/cook 호환성 |
| `Source/Project_JCharacter/Public/Animation/Project_JGeneralTurnPolicy.h` | GeneralTurn 자격, TravelNotForward, Dynamic/Settled 및 완료 |
| `Source/Project_JCharacter/Public/Animation/Project_JMovingTurnPolicy.h` | 전방 급회전의 준비·시각 오차·진입·완료·continuation |
| `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent.cpp` | `IsStartingForContext`, `IsPivotingForContext`, `ShouldSpinTransitionForContext`, `UpdateLocalMoveIntentSnapshot` |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp` | Chooser publication과 상태/단발 소유권 |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Continuity.cpp` | Steering gate와 GeneralTurn 후보 요청 |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstanceProxy.cpp` | 후보 적용·interrupt·첫 검색·MM result/stack capture |
| `Source/Project_JCharacter/Private/Animation/Project_JMotionMatchingTrajectoryComponent.cpp` | CMC 기반 경로·Strafe Facing·yaw rate 안정화 |
| `Source/Project_JCharacter/Private/Animation/AnimNode_ProjectJOneShotHandoff.cpp` | 실제 outgoing 포즈 유지와 복귀 소유권 |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.FlowTrace.cpp` | semantic/publication/search/evaluated 프레임 관계 |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.ContinuityTrace.cpp` | 평가 완료 포즈 방향과 보정 진단 |

## 9. 다음 조사와 검증 순서

### 조사 대상의 실제 연결 확인

GASP의 활성 캐릭터가 CMC인지 Mover인지, 기본 MM인지 Experimental State Machine인지부터 확인한다. 사용자는 비교 당시 설정을 몰랐다. 이전 로컬 조사에서는 CMC 기본 MM을 확인했으나 이번 사용자 실행에 동일하게 적용됐다고 가정하지 않는다.

필요한 조회 범위는 GASP의 실제 Spin 함수, 후보 Chooser의 관련 활성 행, `PSD_Dense_Stand_Run_SpinTransition`의 후진 Reface 엔트리, MM 내부 플레이어 그래프와 OffsetRoot 모드다. Project_J는 현재 Pawn→Master→실제 Profile/AssetSet/Chooser 연결, Spin 값 사용 행, 후진 Reface 엔트리만 먼저 확인한다. Schema/정규화·enabled/searchable 구간·root-motion 설정·미러·발 접촉/notify 구간도 후보 비교에 포함한다.

같은 basename의 PSD가 여러 곳에 있을 수 있으므로 전체 object path를 기록한다. 노드 Details의 정적 값은 핀/함수/native setter로 덮어써질 수 있다. 노드의 존재나 저장된 기본값만으로 실제 평가·검색에 기여한다고 판단하지 않는다.

### 동일한 이동 상태의 재생 비교

| 비교 장면 | 목적 |
| --- | --- |
| Strafe S 유지, 좌/우 빠른 약 180도 회전 | 핵심 후보 진입·몸 방향 전환·후진 발 디딤 비교 |
| Strafe S 유지, 조금 더 완만한 큰 회전 | 순간 입력만 통과하는 조건인지 확인 |
| Strafe S의 작은/느린 곡선 | Reface가 너무 자주 나오거나 기존 Diamond/Box를 덮는지 확인 |
| Strafe A/D, W 곡선 및 전방 큰 회전 | 방향별 데이터 경계와 기존 품질 보호 |
| OTM Run/Sprint, Strafe Sprint F/FL/FR | 기존 자격과 방향 계약 보호 |
| Start/Land 복귀, TIP, 전투 몽타주/공격/회피/피격 | pose·root·행동 소유권 중복 방지 |

처음부터 모두 실행할 필요는 없다. S 핵심 장면에서 차이를 확인하고, 구현안이 생길 때 관련 회귀를 확장한다. 카메라 회전율·실제 속도/가속·velocity heading·actor/root yaw가 비슷한 구간을 맞춘다. 같은 마우스 픽셀이나 입력 시간만으로 비교하면 이동 모델 차이가 섞인다.

기록할 값은 held local input/intent revision, 속도·가속·future speed, velocity/입력/control/actor/visual-root 방향, Start/Pivot/Spin 자격, 검색 PSD 배열, 선택 PSD/asset/time/playrate/continuing/search cost, 새 블렌드 여부, 플레이어별 time/weight, Steering/OffsetRoot 소유권 및 양발 접촉이다. 타임스탬프와 snapshot/result/evaluation 프레임을 맞춰 오래된 결과를 새 선택으로 읽지 않는다.

### 원인을 좁힌 뒤 구조 토의

1. 후보가 공급되지 않는지 먼저 구분한다.
2. 후보는 열리지만 비용·진입 구간·발 위상 때문에 선택되지 않는지 확인한다.
3. 올바른 후보를 골라도 시각 루트/Steering/블렌드 합성에서 품질이 달라지는지 확인한다.
4. 남은 차이가 이동 모델이라면 가속·감속·마찰·yaw를 각각 분리해 비교한다.

GASP PSS·BlendTime·SearchThrottle·최대 스택 수를 일괄 복사하거나, root 회전을 무제한 누적하거나, 후진을 전방 Turn으로 강제하지 않는다. 데이터 기반 후진 Reface 후보가 타당하다면 Schema/정규화·소유권·진입/완료 hysteresis·continuing 정리·Cycle 복귀·검색 비용을 함께 설계한다. 기존 단발/지속 구분을 전면 폐기할 필요가 있는지는 근거를 보고 결정한다.

## 10. 기존 진단 명령과 자료 보존

현재 소스에서 확인한 읽기 전용 진단 CVar다. 필요한 짧은 장면만 캡처하고 끝나면 0으로 끈다. 출력에 빈 항목이 있다고 기능 부재로 단정하지 않는다.

```text
p.ProjectJ.AnimFlow 2
p.ProjectJ.MovingTurnTrace 2
p.ProjectJ.LocomotionContinuityTrace 2
```

- `AnimFlow 2`: 상태 edge 외에 Idle/Cycle도 10Hz로 관측한다. 급회전 전 프레임을 모두 보장하지 않으므로 필요한 경우 진단 보강안을 별도로 판단한다.
- `MovingTurnTrace 2`: 매 update의 local Turn 자격을 기록한다. S가 전방 자격에서 제외되는 것 자체는 정상 보호일 수 있다.
- `LocomotionContinuityTrace 2`: edge/10Hz, graph branch와 Facing을 기록한다. 모드 3의 전방 Strafe 상세 출력을 후진의 전 프레임 trace라고 오해하지 않는다.
- 단발 복귀를 별도로 확인할 때 `p.ProjectJ.OneShotReturnDebug 1`을 사용한다.
- `p.ProjectJ.MMPivotDebug 1`, `p.ProjectJ.MMTransitionDebug 1`은 native 프레임 capture가 있다. dump 연결은 실제 구현을 확인하고 사용한다. CVar 설명의 함수 이름을 확인 없이 콘솔 Exec 명령으로 안내하지 않는다.

`LiveOneShotReturn`, `OneShotInputResponse`, `StrafePredictionYawSmoothing`, `TurnCycleCandidates`, `LocomotionSteering` 등은 진단 출력이 아니라 동작을 바꾸는 비교 스위치다. 처음 baseline에서는 임의로 끄지 말고, 원인 분리를 위해 필요할 때 하나씩 바꾼다. 로그 활성 상태의 CPU 수치를 일반 플레이 성능으로 해석하지 않는다.

기존 로그는 보존한 뒤 새 장면의 실행 시각과 actor를 기록한다. 현재 채팅의 예전 로그 전체 삭제 요청을 새 로그의 삭제 권한으로 이어받지 않는다. 재현용 임시 `.py`와 출력은 기존 프로젝트 규칙에 맞는 임시/ignored 위치에 두고, 커밋 요청이 있을 때 실제 기능에 필요한 파일만 검토한다.

관련 기존 재생/검증 폴더는 `Saved/Validation/OneShotReturn_20261011`, `OneShotInput_20261011`, `OneShotCurve_20261011`, `StrafeRegression_20261011`, `StrafeUpperBody_20261011`, `StrafeLowerBody_20261011`이다. 일부 로그는 후속 보강 전 상태이므로 실행 날짜·플래그·프로필·active graph를 확인하고 사용한다.

## 11. 권한과 빌드 정책

- 프로젝트 `AGENTS.md`와 새 채팅의 사용자 지시가 우선이다. 이전 에셋 저장 승인은 특정 Master/PSD 저장에 한정됐으며 새로운 PSD/Chooser 저장의 포괄 승인이 아니다.
- MCP는 새 채팅의 명시적 요청 또는 허가를 확인한 뒤, 코드·설정만으로 알 수 없는 관련 에셋을 최소 조회한다. 대량 조회가 필요하면 먼저 범위를 알린다.
- 읽기 전용 비교에서는 에셋 생성·compile·save나 레벨 수정을 섞지 않는다. 이후 미저장 실험도 사용자의 명시적 요청 범위에서 진행하고, 실제 적용 여부·메모리 변경·저장 여부를 구분해 기록한다.
- Unreal C++ 빌드는 직접 `UnrealBuildTool.exe`로 실행하고 완료까지 기다린다. UBT/dotnet/Editor/Live Coding/MSBuild/ShaderCompileWorker가 실행 중이면 새 빌드를 시작하지 않는다. 실행 중인 빌드를 중단하지 않는다.
- 과거 “이번 빌드만 종료” 승인은 이번 이후 빌드에 적용하지 않는다. dotnet 예외 창은 첫 UBT/compiler 오류와 Windows Application Event Log부터 확인한다. 경로 문제로 단정해 파일을 변경·삭제하지 않는다.
- 컴퓨터 use는 해당 스킬과 현재 도구 문서를 읽고 사용한다. 이전 세션 변수/탭/연결이 남아 있다고 가정하지 않는다.
- 인수인계 당시에는 Unreal 에디터 조회 MCP가 도구 목록에 없었다. 새 채팅에서는 가용 도구를 다시 확인한다. 연결되지 않으면 에셋 상태를 추측하지 말고 가능한 Source/문서 조사와 필요한 좁은 에디터 확인 항목을 진행한다.

## 12. 다음 조사 완료 기준

다음 단계의 결과물은 실제 활성 경로를 기준으로 한 GASP/Project_J 비교표, 후진 Reface 후보 진입 조건, 동일한 S 급회전의 선택·시각 루트·발 접지 근거, 남은 미확인 항목과 구조 제안이다. 원인이 후보 공급인지 검색 비용인지 표시 보정인지 이동 모델인지 구분하고, 기존 소유권과 비용 경계를 어떻게 보호할지 설명한다.

사용자와 구조를 토의하기 전에 임의 구현을 시작하지 않는다. 품질 평가는 실제 렌더링과 사용자 체감을 포함한다. 이 채팅에서는 비교 문서와 인수인계만 작성했으며 코드·에셋·빌드·운영 상태를 변경하지 않았다. 비교 문서는 외부 작업으로 커밋됐고, 인수인계는 아직 미추적 상태다.
