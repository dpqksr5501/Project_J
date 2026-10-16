# 200명 애니메이션 최적화 인계 — 2026-10-13

> 현재 후속 계획: [총 200명 전원 표시와 로컬 품질 보존](../../Performance/Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md). 현재 소스의 고비용 후보, 네트워크·전투 영향과 검증 순서는 이 계획을 따른다. 아래 측정·실행 예시는 당시 이력이며 현재 빌드 결과로 대체하지 않는다.

> 최신7차는 [공통 LOD 제작](../Authoring/Mesh_LOD_Authoring.md)과 기존 설정 보존·예외 검수 경로다. 2026-10-16 현재 몸체의 LOD0–2를 사용자 검토 후 채택했으며 LOD3–4 추가는 보류한다. 다른 에셋을 자동 승인하거나 물리 CPU 절감을 입증한 작업은 아니다. 현재 결정과 유지/제거 이유는 위 계획의 첫 절을 따른다.

> 6차 기록은 같은 계획의 [다른 에셋 호환성과 실제 몸체 LOD](../../Performance/Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-6차--다른-에셋-호환성과-실제-몸체-lod)를 따른다. 공통 제어와 Cloth/역할 경계를 보완하고 기존 body에 LOD1·2를 적용했다. GPU 감소와 미확정 frame/CPU 개선, 새 에셋별 제작 검수를 구분한다. 현재 raw/forced LOD writer를 늘리거나 미채택 physics 축소를 재개하지 않는다. 제한 Cook의 공유 Zen 저장소 갱신 영향과 향후 `-SkipZenStore` 분리 조건도 해당 절을 읽는다.

> 후속 구현: MM 캐시 재사용은 `8485562c`와 [런타임 최적화 기록](Animation_Runtime_Optimization_2026-10-13.md), 골격·몸체 프로필은 [실시간 리타깃 프로필](../Architecture/Runtime_Retarget_Profiles_2026-10-13.md)을 함께 확인한다. 후자는 `8485562c` 기반 `codex/runtime-retarget-profiles`의 미커밋 변경으로 작성·검증했다. 아래 Git 표와 측정은 최초 인계 시점의 이력이다. 새 프로필의 200명 CPU 증가는 해결 완료로 취급하지 않으며, 최신 실제 HEAD·변경 소스·에셋 해시부터 확인한다.

## 1. 이 문서의 용도와 출발 상태

다른 채팅에서 최근 애니메이션 재설계·버그 수정·리팩토링을 다시 조사하는 시간을 줄이고, 기존 움직임을 보존하면서 최적화를 이어가기 위한 인계다. **이 문서를 작성하면서 새 최적화나 추가 성능 시험을 수행하지 않았다.** 구현 사실, 이전 실행 결과, 다음 작업 제안을 구분한다.

| 항목 | 확인된 출발점 |
| --- | --- |
| 프로젝트 | `C:\Users\I\Documents\GitHub\Project_J` |
| 프로젝트 파일 | `Project_J.uproject` |
| 엔진 | UE **5.8.3**, 설치 루트 `C:\Program Files\Epic Games\UE_5.8` |
| 현재 브랜치 | `main` |
| 병합·push 완료 커밋 | `c05467868c524ef24ed9382a50db2960b6ca8724` |
| 한글 커밋 제목 | 애니메이션 파이프라인 책임 분리와 착지 복귀 연속성 보완 |
| 보존한 작업 브랜치 | `codex/strafe-backward-turn-trial`, 위 커밋까지 원격 push 완료 |
| 원격 | `origin`, `https://github.com/dpqksr5501/Project_J.git` |
| 기존 측정 CPU | AMD Ryzen 7 9800X3D 8-Core Processor |
| 성능 비교 인원 | 사용자 지정 **200명** |
| 아직 미확정 | 최종 최소 사양·목표 FPS·거리별 품질 예산·실제 원격 접속 목표 |

위 Git 상태는 인계 작성 직전의 확인값이다. 다음 채팅은 `git status`, 현재 HEAD, 원격 상태부터 다시 확인한다. 이 인계 문서와 색인 수정 자체는 위 커밋 이후의 문서 변경이다. 기존 성능 보고서 manifest의 HEAD가 `65da14e8`인 것은 리팩토링을 미커밋 상태에서 측정했기 때문이다. manifest의 변경 소스 해시·snapshot과 함께 해석해야 하며, 그 실행을 `65da14e8`의 깨끗한 코드 측정으로 오인하지 않는다.

## 2. 사용자 의도와 품질 기준

사용자는 MMORPG에 맞는 고품질 움직임과 200명 성능을 함께 원한다. 기존 동작 중 대부분은 만족스러웠고, 주된 문제는 **외부 단발 재생→MM 복귀**, **Strafe Run 전방의 미세/완만한 카메라 회전**, **Land 직후 약한 회전의 순간 튐**이었다. 코드 책임·규모에 한정하지 않고 실행·데이터·네트워크·병렬 처리까지 리팩토링하는 방향을 승인했다.

최적화의 품질 게이트:

- OTM/Strafe, Run/Sprint의 Start·Stop·Jump·Land·Pivot·TIP·Turn/Reface, 공격·회피·피격·몽타주의 우선권을 유지한다.
- Strafe Run의 W+극미세 회전은 안정적인 Loop를 유지하고, 지속 곡선에서는 어울리는 Arc/Loop를 MM이 선택한다. 좌우 회전 반전의 실제 직선 구간에서 Loop가 나오는 것을 무조건 오류로 취급하지 않는다.
- A/D 단독 이동+카메라 회전, 후방·대각선의 기존 Box/Diamond/측면 Loop 후보를 유지한다. 특정 키나 클립 이름으로 차단하는 방식으로 해결하지 않는다.
- 단발의 지속 곡선 종료 기본 조건은 사용자 요청으로 최종 복구한 **0.35초·100cm 이동·이동 방향 변화 3도·방향 일관성 85%**다. 0.2초/80cm/75% 실험값으로 되돌리지 않는다. 다른 입력 취소·자연 종료·Land 충격 보호 조건도 따로 존재하므로 네 값이 모든 출구를 설명하지 않는다.
- 물리 착지와 표시 수명은 구분한다. 복귀했다고 Landing 이벤트를 지우거나, 물리 착지 플래그 때문에 이미 종료한 단발을 다시 재생하지 않는다.
- 실제 발 접촉, 캡슐/루트 움직임, 무기/손 접촉, 의상 물리와 화면 복귀 품질을 지킨다. 컴포넌트 공간 발 변위나 정지 PNG만으로 접지 품질을 인증하지 않는다.

최근 Land 수정과 리팩토링은 자동화·기록 재생으로 검증했다. 최종 사용자 화면에서 모든 Land 튐이 사라졌다는 확인은 아직 없다. 초기에 사용자가 “잘된다”고 한 것은 당시 구현에 대한 피드백이며 이후 모든 변경의 일괄 체감 승인이 아니다.

## 3. 실제 현재 구조

### 재생과 상태 판단

기존 Master의 `StateController SM + 별도 Motion Matching + 외부 Blend Stack + Handoff` 경로를 **`Project Unified Locomotion`의 공통 플레이어 스택**으로 통합했다. `FAnimNode_ProjectJLocomotion`은 엔진 `FAnimNode_MotionMatching`을 확장한다. 독자 검색기를 구현한 것이 아니다.

**네이티브 State Controller와 Chooser는 계속 사용한다.** ABP의 포즈 State Machine과 미사용 함수 그래프 9개를 제거한 것과 C++ 의미 상태 제거는 다르다. 단발의 명령·selection revision·자연 종료·입력 취소·발 단계는 기존 runtime이 판단한다. 단발 중에는 공통 스택만 재생하고 MM 검색은 실행하지 않는다. 첫 MM 복귀에서는 Pose History로 검색한다.

공통 sample graph는 실제 현재 플레이어의 asset/time/mirror/pose-link slot을 읽어 단발 또는 MM의 보정 경로를 선택한다. 해당 분기 블렌드 시간은 0이며 전환 애니메이션을 다시 섞는 Handoff가 아니다. 후단 Inertialization, Lean, Aim, Slot, OffsetRoot, FootPlacement, LegIK, 손 IK·리타깃·Pose History는 역할을 유지한다. 예전 감사의 Master 246→131노드는 당시 기록이며 CPU 절감률이 아니다.

### 후보 데이터와 전방/측면 구분

정렬된 전방 Strafe Run은 `PSD_Combat_Run_ForwardCycle`에서 Arc 6개+Loop 8개를 검색한다. 이는 기존 방향·경로·몸 방향 정렬 정책으로 선택하는 목적별 후보 집합이다. W 키 검사나 runtime Box/Diamond 이름 차단을 추가한 것이 아니다. A/D·후방·대각선·정렬되지 않은 이동은 기존 전체 Cycle/관련 후보를 사용한다.

전방 PSD는 78 entry/5,896포즈에서 14 entry/1,000포즈로 정리했다. 35차원 포즈 값 배열은 825,440→140,000바이트다. 이 배열은 공유 인덱스의 일부이며 캐릭터당 메모리나 전체 CPU가 83% 감소했다는 뜻이 아니다. 전방 PSS는 기존 35차원/8샘플, Pose·Velocity·Trajectory 및 UseContinuingPose 계약을 유지했다. companion normalization set의 역할과 서로 호환되는 schema 구성을 확인해야 한다.

### Land 후속 보간과 상체 보정

첫 관성 Land 복귀 후 일반 스택 블렌드로 MM 후보가 바뀌면서, 잔여 관성 오프셋과 움직이는 스택 목표가 중첩되는 경로를 보완했다. 실제 전방 Land 관성 복귀의 기존 ReturnBlendTime 동안 후속 MM 재선택도 같은 관성 방식으로 요청한다. 검색 간격·continuing bias·후보를 바꾸지 않으며 후보 고정 타이머가 아니다. 재선택은 **원래 요청 시간**을 사용하고 엔진이 deficit를 처리한다.

새 단발 명령, 실제 재관련, 전방 Cycle 이탈, 공중·행동·전신 몽타주·MM interaction에서 수명을 해제한다. 상체 yaw는 숙인 몸통의 forward 투영을 그대로 사용하지 않고 기준 자세를 보정한 world-up twist로 구하며, 숙임 정도로 보정량을 감쇠한다. 180도 swing의 invalid 경로도 보호한다.

구 Handoff의 reflected 타입, native fallback MM, 공용 State Controller getter는 직렬화/참조 호환 때문에 남겨 두었다. **현재 Master에서 실행하지 않는다는 이유만으로 일괄 삭제하지 않는다.** 에셋 참조와 fallback 계약을 감사한 뒤 별도 이행해야 한다. `p.ProjectJ.LiveOneShotReturn` 등록은 공용 CVars로 옮겨 구 Handoff 파일 삭제와 설정의 생존을 분리했다.

## 4. 코드 탐색 지도와 소유권

아래 경로는 프로젝트 루트 기준이다. 런타임 변경은 현재 소스를 먼저 읽고, 과거 문서의 줄 번호에 의존하지 않는다.

| 경로 | 현재 책임 / 주의점 |
| --- | --- |
| `Source/Project_JCharacter/Public/Animation/Project_JAnimationSnapshot.h` | USTRUCT 스냅샷 정의. reflected 참조·기존 이름·GC 계약 유지 |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp` | UE lifecycle 진입부. 기존 4,042→578줄, 실행 순서 유지 |
| 같은 폴더의 `Project_JCharacterAnimInstance.Snapshot.cpp` | GT의 물리·입력·역할·원격·탈것·IK 스냅샷 조립 |
| `.Presentation.cpp`, `.PresentationUpdate.cpp` | 단발 표시 수명·State Controller·Chooser·발 단계·명령/행동 경계 |
| `.Search.cpp` | 후보·Chooser properties·검색 정책 게시 |
| `.Budget.cpp` | GT 품질 관측, 시각 전체 평가 보호, history/reset, native update 생략 |
| `Project_JCharacterAnimInstanceProxy.h/.cpp` | GT 게시/최신 snapshot 소비, 검색 요청 병합·완료 acknowledgement, 실제 node 탐색·설정 적용, GC |
| `Project_JMotionMatchingBoundaryPolicy.h` | 값 기반 검색 경계 판단. 자체 검색 dispatch/완료 처리 없음 |
| `Project_JMotionMatchingEngineAdapter.h/.cpp` | UE reflected MM 필드 cache와 중첩 scope 설정 복원. 엔진 버전 의존 경계 |
| `Project_JAnimationQualityPolicy.h/.cpp` | UObject 접근 없는 원본 품질·시각 간격 결정 |
| `Project_JAnimationPresentationHelpers.h` | 내부 방향·발 단계 함수, 기존 계산 유지 |
| `Source/Project_JCharacter/Public/Animation/Project_JPlaybackReturnPolicy.h` | live/inertial 허용 정책과 관성 수명, 검색기/클립 소유자 아님 |
| `Private/Animation/AnimNode_ProjectJLocomotion.cpp` | 공통 재생, 실제 player 시계·관련성·복귀의 update/evaluate |
| `Private/Animation/Project_JCharacterAnimInstance.ContinuityTrace.cpp`, `.SearchCostAudit.cpp` | 진단. 고비용 trace를 성능 실행에 켜 두지 않는다 |
| `Private/Components/Project_JAnimationUpdateCoordinatorComponent.cpp` | 시각 메시 그룹의 보호·주기·분산·보간·wake·복구 |
| `Private/System/Project_JCharacterAnimationBudgetSubsystem.cpp` | 원본 포즈의 기존 ABA 등록/예산 |
| `Public/Animation/Project_JBudgetedSkeletalMeshComponent.h` 및 대응 cpp | 원본 메시의 gameplay pose 요구·예산 연결 |
| `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent*.cpp` | 이동 의미 상태. Ground/Air/Input/Transitions/Support/Base와 본체로 나뉨 |
| `Source/Project_J/Testing/Project_JAuthoredAnimationPerformance.cpp` | 실제 BP·장비·ABP CPU fixture, 고정 120 warmup+120 measured tick |
| `Source/Project_J/Testing/Project_JAuthoredAnimationFixture.h` | authored BP/장비 구성. CPU 장면은 controlled kinematics이고 CMC component tick이 꺼짐 |
| `Source/Project_J/Testing/Project_JOneShotReturnPlayback.cpp`, `Project_JStrafeCameraPlayback.cpp` | 실제 BP/CMC 움직임 및 사용자 CSV 재생, CPU fixture와 다른 검증 |
| `Source/Project_J/Testing/Project_JNetworkLoadFixture.*` | 실제 소켓 서버/클라이언트와 native NPC의 네트워크 장면 |

리팩토링은 새 UObject·tick·자체 작업 큐를 추가하지 않았다. 65개 기존 메서드, snapshot 정의와 helper를 펼친 NativeUpdate 순서는 소스 비교로 보존을 확인했다. 품질 정책 호출부만 본문을 재구성했다. 소스 동등성 확인은 실제 실행 검사의 대체가 아니다.

## 5. 네트워크·병렬 처리에서 지켜야 할 경계

- GT가 Actor/CMC/GAS/복제 이벤트를 읽어 snapshot을 만든다. worker 정책에 World·Actor·Component 접근을 옮기지 않는다. 값 기반 policy의 const 입력과 UObject 참조의 수명/GC 보호를 구분한다.
- GT 게시→최신 snapshot 소비→graph traversal→실제 검색 실행→완료 acknowledgement→관측 순서를 유지한다. linked layer 중첩, 아직 소비하지 않은 history 검색 요구의 병합, revision freshness, 비활성 branch를 검사한다.
- 검색 요청이 생겼다는 이유만으로 revision을 소비하지 않는다. 실제 엔진 검색 완료를 확인하는 Proxy 계약이 있다. MM interaction과 오래된 cached 결과를 일반 검색 성공으로 오인하지 않는다.
- `AddReferencedObjects`와 queued/consumed snapshot의 참조 추적을 유지한다. 무조건 zero-copy나 UObject 포인터 제거는 안전/성능 근거가 필요하다.
- 로컬·AutonomousProxy·SimulatedProxy의 의미 이벤트/입력 출처, 서버 시각·Landing revision·취소·relevance wake를 유지한다. 현재 clip을 매 프레임 복제하는 설계를 추가하지 않는다.
- Iris/AOI/FastArray, 장비의 소유자 전용 데이터, CMC/GAS 권위를 유지한다. 거리만으로 Actor/CMC/GAS tick이나 판정 포즈를 낮추지 않는다.
- 엔진 animation update/evaluation 경로를 사용한다. thread-safe 콜백이 항상 worker에서 실행된다고 가정하지 않는다. 기존 NPC `UE::Tasks`/`ParallelFor`, Mass chunk 병렬 계산과 별개의 경계다. 애니메이션을 그 큐에 다시 넣지 않는다.
- 평가 시점 분산은 병렬 task 제출 자체가 아니다. 시각 그룹 GT 판정에서 원본 worker 완료를 불필요하게 기다리면 직렬 병목이 재발할 수 있다. 실제 포즈 평가 시 원본 의존 관계는 유지한다.
- 시각 메시의 URO/ABA/external control/Leader Pose 중복 제어를 피하고, 등록·해제·파괴·월드 종료·아바타 교체 때 변경했던 정확한 메시의 설정과 요구를 복구한다.

## 6. 이미 적용한 200명 최적화

캐릭터당 숨겨진 `CharacterMesh0`가 Master와 gameplay pose/MM의 원본이다. 보이는 `Greatsword` 몸체에는 리타깃과 RigidBody 3개가 같은 ABP 안에 있다. 나머지 8개는 몸체의 Leader Pose를 공유하며 자체 animation tick이 꺼져 있다. **9 follower가 각각 독립 리타깃하는 구조가 아니다.**

기존 Coordinator가 시각 그룹의 external evaluation/interpolation을 관리한다. 원본 ABA와 gameplay pose 보호는 별도 기존 소유자가 담당한다.

| 조건 | 현재 시각 평가 |
| --- | --- |
| 로컬, 안정 근거리/원본 전체 평가 | 매 프레임 |
| 안정 근거리에서 원본 ABA 감소 중, 중거리 | 최대 30Hz 간격+매 프레임 보간 |
| 원거리 | 15Hz 간격+보간 |
| 화면 밖/보호 없음 | 관리 시각 메시 tick 중지 |
| 공격·피격·판정 요구·몽타주·루트 모션·단발·공중·급회전·Turn/Reface | 전체 평가 보호 및 기존 복귀 여유 |
| 화면 복귀 | 즉시 평가, 0.15초 보호·물리 이력 초기화 |

품질 상승은 즉시, 하강은 0.2초 안정 후 적용한다. 캐릭터별 평가 시점을 분산한다. 다른 external controller/Leader Pose/일부 modular 역할/자체 전체 root motion은 중복 소유하지 않는다. dedicated server의 시각 그룹 tick은 비활성이다. 긴 pose 요구를 짧은 요구가 줄이지 않는다.

비교/복구 수단은 `ProjectJ.VisualAnimationBudget.Enabled 0`, Coordinator의 `bAllowVisualBudget=false`, 메시 `NoVisualBudget` 태그다. 이 스위치를 기본 off로 바꿔 성능 회귀를 덮거나, 판정 보호를 제거해 시간을 줄이지 않는다.

10월 12일 같은 바이너리에서 기능 off/on의 ABA on 평균은 Near 13.61→11.40ms, Far 13.78→10.69ms, AttackBurst 19.76→19.58ms였다. 이 결과와 다음 리팩토링 전후 비교는 서로 다른 실험이다. 개선율을 합산하지 않는다.

그날 `NamedTrace`의 측정 region 120tick에서 Near ABA on 리타깃 12,000회/RigidBody 36,000회, Far 6,000회/18,000회, AttackBurst 24,000회/72,000회였다. Near 시각 관리자 자체는 틱당 약 0.307ms였다. 병렬 worker의 inclusive CPU 누적을 World tick wall time과 더하지 않는다. 이 계측은 최신 리팩토링의 전후 timer 비교가 아니다.

## 7. 최신 CPU 결과와 가장 먼저 해결할 과제

리팩토링 전 2회, 첫 이후 2회, 이후 재확인 3회 실행했다. 200명+follower 1,800개, Near/Far/Hidden/AttackBurst 각각 ABA off/on, ABA 2ms, 조건별 120 준비+120 측정 tick, NullRHI다. 표의 p95는 **반복별 p95 평균**이며 pooled p95가 아니다.

| 조건 | ABA | 평균 이전→첫 이후→재확인, ms | p95 이전→첫 이후→재확인, ms |
| --- | --- | --- | --- |
| Near | off | 18.379→20.833→18.977 | 19.304→25.683→20.073 |
| Near | on | 12.054→13.454→12.345 | 13.321→16.578→12.841 |
| Far | off | 15.230→15.099→15.211 | 16.293→16.261→16.155 |
| Far | on | 10.870→11.173→11.254 | 12.142→12.336→13.454 |
| Hidden | off | 11.298→11.104→11.232 | 12.406→12.239→12.397 |
| Hidden | on | 6.237→6.168→6.237 | 6.540→6.392→6.541 |
| AttackBurst | off | 20.609→21.456→21.536 | 22.671→23.047→24.004 |
| AttackBurst | on | 19.671→21.418→21.302 | 21.892→23.732→24.246 |

재확인 평균은 이전 대비 Near off 약 +3.3%, on +2.4%, AttackBurst on **+8.3%**다. 첫 이후 Near off 최대 38.324ms, 재확인 AttackBurst on 최대 30.206ms도 남겼다. **전체 성능 향상이나 성능 회귀 부재를 입증하지 못했다.** 동시 공격 증가를 원거리 평균으로 숨기지 않는다.

Near off/on의 시각 graph update 관찰값 23,800/11,900, AttackBurst off/on 23,800은 유지됐다. Hidden은 0이다. graph counter 관측은 실제 평가 횟수·MM 검색 횟수가 아니다. 같은 관측량에서도 CPU 시간이 바뀌므로 실제 timer/critical path와 측정 환경을 확인해야 한다.

NullRHI World tick wall time은 병렬 완료를 포함하지만 전체 엔진 프레임·GPU·draw/skin/shadow·실제 200명 렌더 FPS가 아니다. CPU fixture의 CMC tick은 꺼져 있으므로 전체 게임의 이동·네트워크 CPU까지 인증하지 않는다. 200명 동시 공격 60FPS 달성도 아니다. 기존 이전 DLL 자체는 보관하지 않아 그대로 재실행할 수 없다. 깨끗한 이전 commit은 중간 Land 수정까지 포함한 당시 baseline과 같지 않다.

### 다음 작업 순서 — 제안, 아직 구현하지 않음

1. `c0546786` 기준 현재 binary/hash·에셋·환경을 고정하고 최신 baseline을 다시 확보한다. 과거 표를 현재 baseline 대신 쓰지 않는다.
2. 비계측 반복과 별도 `-Trace` 실행으로 AttackBurst/Near의 GT·worker·wait·리타깃·물리·GAS/AbilityTask·Chooser·snapshot·검색 경계를 분해한다. 증가 원인을 코드 분리나 스케줄러 탓으로 미리 단정하지 않는다.
3. 실제 병목에 한해 작은 변경을 한다. 안정 프레임의 불필요한 관측/조회/할당/복사, 값 변경 시에만 필요한 캐시 무효화, 메시 구성 재감사 비용 등을 후보로 검토한다. 이름이 크거나 코드가 긴 함수라는 이유만으로 hot path로 확정하지 않는다.
4. 전투 품질 보호를 유지한 상태에서 시각 후단의 선택적 비용을 조사한다. 원본 판정 포즈와 무기 socket/접촉에 영향을 주는 비용, 원본을 바꾸지 않는 몸체·의상 보정을 구분한다. 낮은 빈도 평가/물리 LOD는 실제 접촉·wake 품질을 확인한 뒤 적용한다.
5. 이후 실제 RHI의 200명 장면에서 CPU/GPU/메모리/장비/그림자/스키닝을 별도 측정하고 Cook/패키징을 검증한다. NullRHI fixture 결과를 렌더 장면 성적으로 전환하지 않는다.

더 긴 측정은 현재 fixture의 120tick 상수를 명시적으로 개선해야 한다. 지금 `-Repeats 5`만으로 한 조건의 측정 길이가 늘지는 않는다. 표본 길이·attack 재생 구간·region 경계·deadline을 함께 기록하고 이전 형식과 구분한다. 동일 조건 반복, 독립 실행 순서/부하 기록, 평균·p95·최대와 실행별 분산을 남긴다. trace의 오버헤드는 비계측 수치와 섞지 않는다.

## 8. 보존해야 할 회귀와 이미 실패한 실험

최근 기능 검증은 고유 82개에서 성공을 확인했다. `Regression` 72개 중 초기 report 저장 실패 1개, `Rendered` 13개 중 낡은 원격 tick 기대 실패 1개를 후속 수정·검증했으며 최초 보고서도 보존한다. 한 실행에서 실패 0개였다고 표현하지 않는다. 전투 5개는 fixture의 MissingPalm 호환 socket 경고가 각각 4개 있다.

리팩토링 전후 착지 재생의 Frame/Unified/Chest/Torso/CurveReturn/요약 **3,816행이 동일**했다. 최종 D3D12 기록은 점프 11회, Land 복귀 11회, 보간 수명 내 MM 재선택 6회, 실제 관성 활성 594프레임을 확인했다. 149 PNG 중 일부 자세를 확인했으며 전체 영상/사용자 체감의 대체는 아니다.

Strafe 전방 데이터 회귀 기준은 기록 곡선 Arc 85% 이상·Box/Diamond 1% 이하, 기록 미세 입력 Loop 95% 이상·Box/Diamond 0이다. 최근 최종 곡선 120/60/30Hz는 Arc 87.56/86.07/85.81%, Box/Diamond 0이었고 미세 입력도 통과했다. A/D 12장면의 실제 포즈·후보 비교를 유지하며 두 애니메이션 종류의 강제 노출을 요구하지 않는다.

폐기/채택하지 않은 실험:

- PSS 미래 Facing 가중치만 높인 실험, 추가 방향/과거 회전/샘플·bias 실험은 Box 노출을 해결하지 못하거나 악화됐다. 현재 전방 PSS의 수치로 임시 실험값을 복원하지 않는다.
- 일반 live 복귀만 사용하는 Land 비교는 첫 복귀 변위가 증가해 채택하지 않았다.
- Character Pose PSS 비교는 비동기 인덱스 미준비로 정상 비교 조건을 만족하지 못했다. 유효 개선 결과로 쓰지 않는다.
- Master의 모든 보정을 하나로 합친 중간 115노드 구성은 일반 MM에도 단발 보정이 적용돼 폐기했다. 현재는 실제 플레이어별 배타 보정 경로다.
- `.35→.2초`, `100→80cm`, `.85→.75`는 사용자 요청으로 되돌린 값이다.

최종 Land 수정은 일부 후보 전환의 가슴 변위를 줄였지만 전체 착지 최대 5.4298도는 그대로였고 일부 컴포넌트 발 변위는 늘었다. 이를 완전한 접지 개선으로 취급하거나 Loop를 금지할 근거로 사용하지 않는다.

## 9. 자료 위치와 버전 보존

아래는 프로젝트 루트 아래의 **로컬** 자료다. `Saved`는 Git ignore이므로 다른 PC/새 clone에는 없을 수 있다. 없으면 다시 측정하거나 필요한 자료만 별도 전달하며, 존재한다고 가정하지 않는다. 원본을 덮거나 지우지 않는다.

| 루트/파일 | 용도 |
| --- | --- |
| `Saved/Validation/AnimationRefactor_20261013/FinalSummary.json` | 최근 검증·에셋·한계 총괄 |
| 같은 루트의 `SourceEquivalence.json`, `PlaybackEquivalence.json` | 65메서드/순서·snapshot, 3,816행 비교 |
| `BeforeSource`, `BeforeSourceHashes.json`, `AssetHashes.json` | 리팩토링 직전 소스 및 에셋 기준 |
| `CPUComparisonFinal.json` | 위 최신 CPU 표 원자료 |
| `Regression`, `Rendered`, `ContractsChecked` | leaf별 automation 결과, 로그와 최초 실패 |
| `VerifiedEditorBuild.log`, `FinalGameBuild.log` | 최근 직접 UBT 성공 |
| `Saved/Validation/AuthoredAnimation_20261005/AnimationRefactorBefore_20261013` | 이전 2회 CPU |
| 같은 루트의 `AnimationRefactorAfter_20261013`, `AnimationRefactorAfterCheck_20261013` | 이후 2회/재확인 3회, 각 `Count200/cpu.json`, `manifest.json`, 실행 args |
| `Saved/Validation/CrowdPresentation_20261012/ControlledComparison.json` | 기존 시각 예산의 동일 바이너리 off/on |
| 같은 루트의 `NamedTrace/TimerSummary.json`, `NamedTrace/Export` | 기존 리타깃·물리·관리자 timer |
| `Saved/Validation/GroupD_20260910/AnimationRefactor_20261013_Lag50Loss3_Parallel/verification.json` | 최근 실제 소켓 결과 |
| `Saved/Validation/LandCandidateHandoff_20261013_1002` | 최신 Land 사용자 로그·수정 전후 기록·D3D12 결과 |
| `Saved/Validation/StrafeLandSlow_20261013` | 숙인 상체 yaw 조사 및 사용자 기록 |
| `Saved/Validation/StrafeShape_20261013` | 전방 PSD·PSS 실험/대조·미세/곡선/측면 결과 |

Git에 포함한 CSV는 `Source/Project_J/Testing/Fixtures/StrafeMicroCamera_20261012.csv`, `StrafeCurveCamera_20261012.csv`, `StrafeLandCamera_20261013.csv`다. Land 기록은 yaw·Aim Pitch 근사·W 여부·점프 11회를 고정 재생한 것이며 원래 PIE의 모든 지형·부하·포즈 이력을 복제하지 않는다.

보호 에셋의 최근 SHA256:

| 경로 | SHA256 |
| --- | --- |
| `Content/Animation_Logic/ABPs/ABP_Humanoid_Master.uasset` | `701E189BE5AAFF7D5D9C4D451CBC3F64BDFA2F8266E936B40936D5D9694CA91A` |
| `Content/Animation_Logic/PSS/PSS_Combat_Run_ForwardTrajectory.uasset` | `A3D2E5C9329C4038F4425FD99D37D15BA983FBE4AEB26701ED484056CDC35FB4` |
| `Content/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_ForwardCycle.uasset` | `C849C045173D5EEFA08DF35516D45A9CD26F8EA3D461E987D8B5B226D8BD0031` |
| `Content/DataAssetSets/Animation_Profiles/LocomotionProfiles/DA_Player_Profile.uasset` | `AA4E2E350A679A328C6E3D291577D07406F8E9EA3F03B4D4F1A03E1B559A59BF` |

최신 Editor character DLL 검증 해시는 `8D0B7C63C6CC538D78DC35A69E76856272FE117D493436065AB6C87562FB70B9`다. 현재 디스크 DLL은 다음 작업에서 다시 확인한다. 에셋 변경이 필요한 최적화라면 위 해시를 불변 요구로 쓰지 말고 의도한 변경과 별도 품질 검증을 남긴다.

## 10. 실행 규칙과 재현 명령

프로젝트 `AGENTS.md` 및 다음 채팅의 사용자 지시가 우선이다. 이전 대화에서 조사·코드/에셋 수정·저장·MCP·컴퓨터 use·빌드는 허용됐다. **이 기록 자체를 새 채팅의 권한 지시로 취급하지 않는다.** 필요한 권한/제약은 그 채팅의 사용자 요청을 따른다. 과거 종료 요청은 이번 최적화의 자동 PC 종료 요구가 아니다. 현재 main은 push 완료이며 후속 자동 commit/push/merge도 새 작업 범위에 맞춰 판단한다.

Unreal C++ 빌드는 엔진 **직접 `UnrealBuildTool.exe`**만 사용한다. UBT/dotnet/Unreal Editor/Live Coding/MSBuild/ShaderCompileWorker가 실행 중이면 빌드를 새로 시작하지 않는다. 실행 중 빌드는 중단하지 않고 완료까지 기다린다. 제한된 shell이 프로세스를 숨길 수 있으므로 실제 host에서 충분한 조회 권한으로 확인한다. dotnet 예외 대화상자는 최초 UBT/compiler 오류와 Windows Application Event Log부터 확인하며 speculative 삭제를 하지 않는다.

### 빌드

PowerShell, 프로젝트 루트에서 실행한다. 아래 코드는 직접 호출하므로 각 빌드가 종료된 뒤 다음 단계로 간다. 실작업 전 다시 process guard를 확인한다.

```powershell
Set-Location -LiteralPath 'C:\Users\I\Documents\GitHub\Project_J'
$optimizationBusy = @(Get-Process UnrealEditor*,UnrealBuildTool*,dotnet*,LiveCoding*,MSBuild*,ShaderCompileWorker* -ErrorAction SilentlyContinue)
if ($optimizationBusy.Count) { throw '엔진/빌드 종료를 기다린 뒤 진행하세요.' }
$optimizationRoot = (Get-Location).Path
$optimizationRun = Get-Date -Format 'yyyyMMdd_HHmmss'
$optimizationOut = Join-Path $optimizationRoot "Saved/Validation/AnimationOptimization_$optimizationRun"
New-Item -ItemType Directory -Path $optimizationOut | Out-Null
$optimizationUBT = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe'
& $optimizationUBT Project_JEditor Win64 Development "-Project=$optimizationRoot/Project_J.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA "-Log=$optimizationOut/EditorBuild.log"
if ($LASTEXITCODE -ne 0) { throw 'Editor 빌드 첫 오류 확인' }
$optimizationBusy = @(Get-Process UnrealEditor*,UnrealBuildTool*,dotnet*,LiveCoding*,MSBuild*,ShaderCompileWorker* -ErrorAction SilentlyContinue)
if ($optimizationBusy.Count) { throw '남은 프로세스의 정상 종료를 기다리세요.' }
& $optimizationUBT Project_J Win64 Development "-Project=$optimizationRoot/Project_J.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA "-Log=$optimizationOut/GameBuild.log"
if ($LASTEXITCODE -ne 0) { throw 'Game 빌드 첫 오류 확인' }
```

`Build-Experiments.ps1`는 opt-in ProjectJExperiments plugin을 추가하는 별도 도구다. 기본 애니메이션 빌드에 필요하지 않다. 이전 빌드 후 파일 끝의 공백만 정리했으며 동작 코드 추가 변경 없이 main에 병합했다.

### CPU baseline과 별도 trace

실행 중 Editor/빌드가 없어야 한다. 스크립트가 시작한 작업은 정상 종료까지 기다리고 별도 빌드/독립 시험을 겹치지 않는다. 각 RunName은 새 이름이어야 한다.

```powershell
$optimizationCpuRun = 'OptimizationBaseline_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
./Scripts/Validation/Measure-AuthoredAnimation.ps1 -RunName $optimizationCpuRun -Counts @(200) -Repeats 5

# 앞 실행 완료 후 별도 계측
$optimizationTraceRun = 'OptimizationTrace_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
./Scripts/Validation/Measure-AuthoredAnimation.ps1 -RunName $optimizationTraceRun -Counts @(200) -Repeats 1 -Trace
./Scripts/Validation/Export-DTrace.ps1 -RunRoot "Saved/Validation/AuthoredAnimation_20261005/$optimizationTraceRun/Count200" -Animation
```

`Measure-AuthoredAnimation.ps1`의 `-Counts`는 1~200, `-Repeats`는 1~5다. `-Trace`는 선택한 최대 count에만 적용한다. 결과는 `cpu.json`, `Run.log`, `Automation/index.json`, `arguments.json`, 실행 루트 `manifest.json`에 있다. script는 과제 완료·8조건×반복 행 수·leaf test 상태를 검사한다. 1/50/100은 스케일링 보조 비교로 쓸 수 있으나 200명 기준을 대체하지 않는다.

현재 CPU trace region은 `ProjectJ.AuthoredCPU_*`다. `ProjectJ_UnifiedLocomotion`, `ProjectJ_VisualAnimationBudget`, `Project_J_AnimBuildThreadSafeData`, `Project_J_AnimBuildOptimizationPolicy`, `Project_J_AnimStateControllerChooser`, `Project_J_AnimPublishProxy`, `Project_J_AnimPoseSearchDatabaseChooser`, `Project_J_AnimPublishChooserProperties`, `ProjectJ_AnimProxy_*`, `Project_J_AnimNativeThreadSafeUpdate_*`를 먼저 비교한다. 리타깃 `IK Retarget Processor Run`, `FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread`, engine parallel evaluation/wait, GAS/AbilityTask의 실제 timer도 확인한다. inclusive 중첩·worker 누적·GT wall/critical path를 따로 보고 실제 실행 thread를 확인한다.

### 움직임·계약 회귀

Automation 필터는 명시한 prefix에 맞는 검사만 수행한다. `ProjectJ.Animation` 전체에는 CPU fixture도 들어가므로 무심코 한 번에 실행하지 않는다. CPU 측정과 trace/동작 캡처를 분리한다. 아래는 최소 시작 예시이며 변경 위험에 따라 관련 검사 범위를 확장한다.

```powershell
Set-Location -LiteralPath 'C:\Users\I\Documents\GitHub\Project_J'
$optimizationRoot = (Get-Location).Path
$optimizationEditor = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$optimizationRegression = 'Saved/Validation/OptimizationContracts_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
New-Item -ItemType Directory -Path $optimizationRegression | Out-Null
& $optimizationEditor "$optimizationRoot/Project_J.uproject" -unattended -NullRHI -nosound -NoLiveCoding -nop4 `
  '-ExecCmds=Automation RunTests ProjectJ.Animation.Pipeline+ProjectJ.MMCrowd+ProjectJ.Internal.Animation.SnapshotBoundary+ProjectJ.Architecture.Animation.RemoteOneShotSemantics' `
  '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$optimizationRegression/Automation" "-ABSLOG=$optimizationRegression/Run.log"
```

실제 움직임 필터는 `ProjectJ.Animation.OneShotReturn`, `ProjectJ.Animation.OneShotInput`, `ProjectJ.Animation.Naturalness.RecordedStrafeMicro`, `RecordedStrafeCurve`, `StrafeCameraCMC`, `StrafeLateralCamera`다. Naturalness의 마지막 이름도 전체 prefix `ProjectJ.Animation.Naturalness.`를 붙인다. 원본/시각 GC·graph·remote locomotion·TIP/Turn/Reface/Pivot·combat continuity도 변경 영역에 맞게 포함한다.

- 기록 Strafe: `-ProjectJStrafePlaybackHz=30/60/120`, 명시 출력 `-ProjectJStrafePlaybackOutput=...`.
- 단발/착지: `-ProjectJOneShotPlaybackHz=30/60/120`, `-ProjectJOneShotReturnOutput=...`, 곡선 검사는 `-ProjectJOneShotCurveOutput=...`.
- `ProjectJ.Animation.OneShotReturn.RecordedStrafeLand`는 기본 Git CSV를 읽고 Strafe Run Case 5를 재생한다. 선택적 입력은 `-ProjectJOneShotCameraReplay=...`이며, 기록 착지 검사도 곡선 재생 경로이므로 명시 출력은 `-ProjectJOneShotCurveOutput=...`다.
- 실제 RHI 착지 캡처는 별도 실행에서 `-NullRHI`를 제거하고 `-d3d12 -ProjectJRenderOneShotReturn`을 사용한다. 같은 출력 파일을 여러 독립 실행에 재사용하지 않는다.
- `ProjectJ.Animation.VisualBudget.Lifecycle`은 실제 시각 보간/wake/역할/복구를 검증한다. 렌더 캡처는 `-NullRHI` 없이 `-ProjectJRenderVisualBudget`을 사용하고 현재 fixture 전제조건을 읽는다.
- `ProjectJ.Animation.A_Prepare.UnifiedLocomotionGraph` 등 production graph 읽기 검사를 유지한다. 테스트를 실행하며 에셋을 저장하는 코드는 추가하지 않는다.
- `ProjectJ.GroupB.CombatContinuity` 5개는 `a.Budget.Enabled=1`, `a.Budget.BudgetMs=0.1` 전제조건을 확인한다. CPU 비교의 2ms와 다른 기능 검증 조건이다.

자동화는 exit code만 보지 않고 `Automation/index.json`의 leaf `state`, errors/warnings/notRun/inProcess와 fixture 요약·출력 저장 성공을 확인한다. 실패 원본을 지우고 재실행 성공으로 덮지 않는다. 현재 Strafe 기본 보고서 출력은 GUID 디렉터리를 쓰며 explicit 출력은 그대로 존중한다.

### 실제 소켓·지연/손실 회귀

```powershell
$optimizationNetworkRun = 'OptimizationNetwork_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
./Scripts/Validation/Measure-Network.ps1 -RunName $optimizationNetworkRun `
  -ClientCount 2 -NPCCount 200 -Animation -ParallelNet -PacketLag 50 -PacketLoss 3 -Port 17881
```

포트는 사용 가능 여부를 확인한다. 한 harness의 서버+클라이언트 동시 실행은 의도된 네트워크 시험이며, 그동안 별도 빌드를 하지 않는다. 최근 결과는 서버/클라이언트 성공, exit 0/0/0, timeout 0이었다. authored player는 2개, NPC는 native 200개이며 **200개 접속** 검증이 아니다. ParallelNet 설정 활성화와 실제 worker 수행을 독립 입증한 것은 구분한다. ToolsetRegistry/EditorToolset의 `unreal.AgentSkill`·`PythonTestRunner` 시작 오류가 기록됐지만 fixture는 통과했다. 프로젝트 오류와 구분하되 전체 로그가 오류 없음이라고 쓰지 않는다.

### 사용자 PIE 로그

필요한 단일 움직임 재현에서 콘솔:

```text
p.ProjectJ.AnimFlow 2
p.ProjectJ.LocomotionContinuityTrace 3
```

Strafe Run의 W+미세 회전, W+좌우 완만한 반전, Land 직후 W+약한 회전을 따로 기록한다. `Stage=ReturnBlend`와 `Stage=Inertial`, 단발 종료 reason, 후보·시간·revision·node weight·소유권을 함께 본다. 최신 `Saved/Logs`의 실제 파일 시각/해시를 확인하고 각 재현을 `Saved/Validation/<고유이름>/User.log`로 보존한다. CPU 측정 전에는 두 CVar를 0으로 되돌리고 비용 audit/시험용 override도 끈다. 오래된 00:26 기록을 최신 로그로 오인한 일이 있어 파일명을 추정하지 않는다.

## 11. 더 읽을 기준 문서와 완료 조건

최소 읽기 순서:

1. [파이프라인 리팩토링](../Architecture/Animation_Pipeline_Refactor_2026-10-13.md): 최종 소유권·동등성·검증·CPU 한계.
2. [200명 시각 예산](Crowd_Visual_Presentation_Budget_2026-10-12.md): 메시 구성·ABA/시각 분리·분산·복구·동일 바이너리 비교.
3. [Strafe Land 후속](Strafe_Land_Return_2026-10-13.md), [전방 후보 구성](Strafe_Forward_Intent_2026-10-13.md): 현재 품질 경계와 폐기한 실험.
4. [통합 재생](Unified_Locomotion_Implementation_2026-10-12.md), [지속 곡선·Master 정리](Sustained_Curve_And_Master_Cleanup_2026-10-12.md): native State Controller와 플레이어별 보정 계약.
5. [성능 문서 색인](../../Performance/README.md), [네트워크 색인](../../Networking/README.md), [실행·스레딩 감사](ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md), [시각 메시 소유권](../Architecture/Visual_Presentation_Mesh_Ownership.md).
6. [원격 단발](../Locomotion/RemoteOneShotReplication.md), [기록 fixture](../../../Source/Project_J/Testing/Fixtures/README.md)와 해당 테스트 소스.

공식·공개 구현 자료는 [통합 재설계 조사](Locomotion_Unified_Playback_Redesign_Research_2026-10-12.md)와 전방 후보 문서에 연결돼 있다. 새 엔진 API/버전 의존 변경을 결정할 때 해당 버전의 로컬 엔진 소스 및 최신 공식 자료로 재확인한다. 이 인계 작성에서는 새 웹 조사를 하지 않았다.

최적화 완료 보고에는 변경 목적·실제 제거한 작업량·품질/네트워크/병렬 계약·직접 빌드·영향 영역 회귀·현재 baseline 대비 동일 조건 CPU/GPU/메모리·남은 위험을 함께 기록한다. 성능 변화가 잡음 범위이면 개선으로 과장하지 않는다. 평균 개선만으로 p95/최대·동시 공격 회귀를 넘기지 않는다. RHI/Cook/체감 확인이 빠지면 명시한다. 메모리 백업/임시 실험값·클립 이름 차단·새 영구 CVar를 생산 코드에 쌓아 두지 않는다.

## 12. 다음 채팅에 붙여 넣을 시작 요청 예시

아래는 사용자가 선택해서 보내는 요청 예시이며 문서 자체가 실행 명령이나 권한 부여는 아니다.

> `C:\Users\I\Documents\GitHub\Project_J\Docs\Animation\Planning\Animation_Optimization_Handoff_2026-10-13.md`를 읽고 200명 기준 애니메이션 최적화를 이어가줘. 현재 main의 실제 상태와 baseline부터 확인하고, 최근 동시 공격 CPU 증가 원인을 timer로 분해해줘. 기존 OTM/Strafe·Run/Sprint·단발 복귀·미세 회전·A/D 측면 후보의 품질을 유지하고 네트워크·GC·병렬 실행·시각 메시 소유권과 충돌 없이 구현·검증해줘. 문서의 결과/한계/폐기 실험을 참고하고 새 결과도 문서로 남겨줘. 코드·에셋 수정/저장, 필요한 MCP·컴퓨터 use·로그 수집·직접 UBT 빌드는 허용하며 이미 허용한 범위를 반복 확인하지 말고 진행해줘. 실행 중 빌드를 중단하거나 엔진/Live Coding과 빌드를 겹치지 말고, push/병합/컴퓨터 종료는 별도 요청을 따르도록 해줘.
