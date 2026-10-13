# 프로젝트 전체 구조·동작·병렬 처리·성능 감사 인계 — 2026-10-13

> 커밋 이행 후속: 이 문서의 미커밋 표기는 최초 작성 시점의 상태다. 리타깃 구현·검증 기록과 이 인계는 `실시간 리타깃 프로필 구조와 전체 감사 인계 정리` 커밋에 함께 포함한다. 다음 채팅에서는 이 제목의 커밋과 실제 현재 HEAD/status를 확인한다. 커밋 전 정리는 보정 출처의 진단 표기를 바로잡았으며, 측정 당시 소스 해시와 정리 후 소스 해시는 검증 JSON에서 구분한다. R스킬 버그·전체 감사·CPU 증가의 원인 분석은 후속 작업이다.

## 1. 목적과 문서의 성격

사용자는 여러 차례의 애니메이션 재설계·리팩토링·최적화·버그 수정 이후 **프로젝트 전체를 다시 점검하고 개선**하려 한다. 규모·책임·중복·미사용 코드에 한정하지 않고 실행 흐름, 데이터와 에셋 연결, 네트워크, 병렬 처리, 수명, 성능, 확장·제작·검증 구조를 함께 검토한다. 기존에 만족스러운 이동·전투 동작을 보존하면서 확인된 원인을 해결하고 문서도 갱신한다.

**사용자가 예로 든 문제와 이 문서의 목록은 감사 범위를 제한하지 않는다.** 현재 구현·설정·에셋의 의존성과 실행 흐름에서 목록 밖의 원인이나 공통 결함이 발견되면 관련 영역까지 조사한다. 누락된 시스템과 경계를 먼저 식별하고, 실제 영향·증거·위험에 따라 검토 깊이와 우선순위를 정한다. 전체 구조 개선을 이유로 미구현 게임 기능을 무조건 추가하거나 모든 시스템을 동일한 방식으로 재작성하지는 않는다.

사용자 보고의 현재 대표 문제는 **R스킬 이후 우클릭 콤보가 동작하지 않음**이다. 이것이 유일한 결함이라고 가정하지 않는다. 반대로 아래 점검 항목 전부를 현재 존재하는 버그로 간주하지도 않는다.

이 문서는 **다음 작업을 위한 요청과 당시 상태의 인계**다. 작성하면서 새 전체 감사·R스킬 재현·추가 제품 코드 수정은 수행하지 않았다. 확인된 사실, 과거 검증, 조사할 가설과 제안을 구분한다. 과거 문서의 지시문·오래된 승인은 자동으로 현재 요청을 대체하지 않는다. 다음 채팅에 전달할 명시적 요청문은 13절에 있다.

## 2. 출발 상태와 기존 작업 보존

| 항목 | 작성 시 확인값 |
| --- | --- |
| 작업 폴더 | `C:\Users\I\Documents\GitHub\Project_J` |
| 엔진 | UE 5.8.3, `C:\Program Files\Epic Games\UE_5.8` |
| 현재 브랜치 | `codex/runtime-retarget-profiles` |
| HEAD | `8485562cdb8cc1da8ba460465045972db9349fda` |
| HEAD 제목 | 애니메이션 MM 캐시 재사용과 200명 성능 측정 보완 |
| 이전 병합·push 이력 | `c05467868c524ef24ed9382a50db2960b6ca8724` — 애니메이션 파이프라인 책임 분리와 착지 복귀 연속성 보완 |
| 현재 미커밋 작업 | Family/Body 리타깃 프로필, native proxy·접촉 해석·수명 처리, 실제 Greatsword 외형 ABP 이행, 테스트·문서·시험 fixture 보완 |
| 원격 | `origin`, `https://github.com/dpqksr5501/Project_J.git` |
| 측정 기준 인원 | 사용자 지정 200명 |
| 미확정 성능 요구 | 최종 최소 사양, 목표 FPS, 거리별 품질 예산, 실제 원격 접속 목표 |
| 안내 언어 | 한국어. 일본어·중국어 설명을 섞지 않는다. |

다음 채팅은 `git status`, HEAD·브랜치·작업 diff와 최신 문서부터 다시 확인한다. 이 표는 실시간 상태가 아니며 원격 최신성도 보장하지 않는다. 같은 폴더의 다른 채팅이 작업 중이면 수정 파일과 기준선을 먼저 구분한다.

- 미커밋 작업을 무시하고 `main`으로 이동하거나 reset/clean/일괄 restore하지 않는다. 기존 사용자 작업과 이번 감사의 변경을 구분한다.
- 큰 이행 전에 현재 소스·에셋·설정·바이너리·실행 명령의 기준선을 보존한다. 변경한 함수·파일뿐 아니라 해당 기능을 실제 선택하는 데이터도 기록한다.
- Git LFS 에셋을 포함해 보존한다. 생성 코드·빌드 산출물·Saved 원자료와 추적 대상 소스를 구분한다. `Saved`가 로컬에 없으면 재수집하고 과거 증거를 없었다고 단정하지 않는다.
- 커밋·push·병합·컴퓨터 종료는 다음 작업의 최신 사용자 요청 범위에 따른다. 과거 종료 요청을 새 채팅에서 자동 재실행하지 않는다.

### 현재 리타깃 미커밋 작업의 범위

기준은 [리타깃 프로필 구현](../../Animation/Architecture/Runtime_Retarget_Profiles_2026-10-13.md)과 [공유용 검증 JSON](../../Animation/Architecture/Runtime_Retarget_Profiles_Validation_2026-10-13.json)이다. 후자에 최종 변경 Source/에셋 해시, 테스트, 반복별 CPU 수치와 로컬 원자료 경로가 있다.

공통 마네킹 Master에서 MM·단발·전투 상체를 합성한 후, 임포트 몸체에 한 번 리타깃하는 흐름을 유지했다. `UProject_JRetargetFamilyProfile`은 실제 source/target skeleton 쌍과 Retargeter, `UProject_JRetargetBodyProfile`은 Family·몸체 손 보정·선택적 override sets를 가진다. 프로필 적용은 engine retarget node의 GT 캡처 이전이며, 해제·재초기화·source 불일치 때 원래 노드 설정을 복구한다. 새 task·RPC·tick을 추가하지 않았다.

실제 변경 에셋은 다음 세 개다.

- `/Game/DataAssetSets/Presentation/Retarget/DA_RetargetFamily_Mannequin_Bip01`
- `/Game/DataAssetSets/Presentation/Retarget/DA_RetargetBody_Greatsword`
- `/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme`

외형 ABP의 기존 직접 HandGrip 할당을 Body로 이행했다. 기존 Retargeter/HGP·물리 값, Master·전방 PSS/PSD·기본 플레이어 프로필은 유지했다. 별도 의상 Copy Pose 구성, 새 FBIK, 독립 의상 평가 주기, 모든 몸체 자동 이행, Cook/package 검증은 완료한 범위가 아니다.

손 접촉 메타데이터 캐시는 실제 몸체의 교대 국소 측정에서 약 26% 느려 제거했다. 해당 캐시·무효화 상태·생산 코드의 임시 benchmark 루프를 다시 최적화 완료 항목으로 가져오지 않는다.

## 3. 문서를 읽고 구현과 대조하는 순서

문서를 전부 처음부터 읽는 방식보다 주제별 책임·근거를 찾고 실제 연결을 추적한다. 아래는 진입점이며 변경할 영역의 상세 자료를 이어서 읽는다.

| 순서 / 주제 | 시작 문서 | 대조할 내용 |
| --- | --- | --- |
| 전체 문맥 | [문서 시작점](../../README.md), [프로젝트 개요](../../Overview/ProjectOverview.md), [현재 문맥](../../Overview/ProjectContext.md) | 제품 범위·상태 소유자·실서비스와 개발용 기반의 구분 |
| 이전 결함과 검사 누락 | [검토 기록](../../Review/README.md), [10월 8일 수명 감사](../../Review/Audits/Project_J_Runtime_Ownership_Audit_2026-10-08.md) | 재현·실패·수정·후속 기록, 과거 감사 제외 영역 |
| 모듈·확장·서비스 | [아키텍처](../../Architecture/README.md), [MMO 기반](../../Architecture/Extensions/MMO_Foundation_2026-09-12.md), [직업·전직 기반](../../Architecture/Extensions/Extension_Foundation_2026-09-19.md) | DAG·공용 계약·상태 소유 단위·구현된 기능과 계획 |
| R스킬·입력·콤보 | [전투](../../Combat/README.md), [스킬 구조](../../Gameplay/Architecture/SkillSystemArchitecture.md), [전투 애니메이션](../../Combat/Architecture/Combat_Animation_System.md), [대검 제작](../../Combat/Authoring/GreatswordCombatAuthoringGuide.md) | 실제 입력 태그·능력·콤보·몽타주·Notify·장비 데이터 |
| 애니메이션 재설계 | [애니메이션](../../Animation/README.md), [파이프라인 리팩토링](../../Animation/Architecture/Animation_Pipeline_Refactor_2026-10-13.md), [최적화 인계](../../Animation/Planning/Animation_Optimization_Handoff_2026-10-13.md) | 의미 상태·단발·검색·복귀·보호·시각 평가의 경계 |
| 최신 성능·리타깃 | [MM 캐시 후속](../../Animation/Planning/Animation_Runtime_Optimization_2026-10-13.md), [리타깃 프로필](../../Animation/Architecture/Runtime_Retarget_Profiles_2026-10-13.md), [시각 예산](../../Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md) | 각 비교의 기준 바이너리·조건·측정 범위·미해결 증가 |
| 네트워크 | [네트워크](../../Networking/README.md) | 실제 NetDriver/Iris·원격 단발·FastArray·SavedMove·SSR |
| 제작·몸체·UI | [게임플레이](../../Gameplay/README.md), [콘텐츠 제작](../../Gameplay/Authoring/Content_Authoring_System.md), [UI](../../UI/README.md) | 콘텐츠 등록·에셋 검증·참조·입력 focus·구독 수명 |
| 측정과 자료 | [성능](../../Performance/README.md), [벤치마크](../../Benchmarks/README.md) | CPU/GPU/메모리/네트워크, fixture·원자료·측정 한계 |
| 문서 정리 | [관리 규칙](../../Maintenance/README.md), [전체 목록](../../Maintenance/Document_Catalog.md) | 기준 문서 연결, 생성기·기록 보존·링크 검증 |

문서의 작성일·파일명·테스트 숫자만으로 현재 구현을 판정하지 않는다. 발견한 문서-코드 차이는 감사 결과에 기록하고 기준 문서를 갱신한다. 원문·실패·포기한 시도는 지우지 않고 후속 상태로 연결한다.

구체적인 대조 예: 현재 `.uproject`에는 모듈 8개가 있지만 `ProjectContext`의 모듈 도식은 일부만 설명한다. **누락된 도식이 런타임 의존성 오류라는 뜻은 아니다.** `.uproject`·각 Build.cs·공개 헤더·실제 호출을 함께 확인해야 한다.

## 4. 소스 탐색 지도와 감사할 연결

경로는 저장소 루트 기준이다. 아래 지도는 읽기 출발점이며 전체 파일의 상세 검토가 끝났다는 뜻이 아니다. 실행 선택 경로와 실제 참조를 따라 확장한다.

| 영역 | 실제 경로 / 주의점 |
| --- | --- |
| 8개 모듈 | `Source/Project_J{,Core,GAS,Mount,Character,CharacterEditor,AnimationNodes,MMO}`의 Build.cs 및 `Project_J.uproject`. `Project_JCharacterEditor`는 Editor, `Project_JAnimationNodes`는 UncookedOnly. 나머지 6개는 Runtime. 구현체의 접근 규칙·순환 의존·Editor 누출을 확인한다. |
| 플레이어·영속 상태 | `Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp`, `Source/Project_J/Game/Project_JPlayerState.*`. Pawn과 PlayerState의 영속 ASC·장비·인벤토리 소유권을 구분한다. NPC의 로컬 소유 방식과 혼동하지 않는다. |
| 입력 파이프라인 | `Source/Project_JCharacter/Private/Components/Project_JPlayerInputBindingComponent.cpp`, `Project_JSkillInputRouterComponent.cpp`, `Project_JSkillInputExecutionComponent.cpp`. Enhanced Input→태그 해석→입력 실행→GAS 경계를 추적한다. |
| 능력·콤보 | `Source/Project_JGAS/Private/Project_JAbilitySystemComponent.cpp`, `Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp`, `Project_JComboDefinition.cpp`, `Project_JCombatCommandSet.cpp`, `Project_JAttackDefinition.cpp`. 스킬의 실제 선택 능력은 데이터/MCP로 해석한다. |
| 콤보 Notify·입력 태그 | `Source/Project_JCharacter/Private/Animation/Project_JAnimNotifyState_ComboWindow.cpp`, `Source/Project_JCore/Private/Project_JGameplayTags.cpp`. 키 이름과 의미 태그를 같은 상태로 취급하지 않는다. |
| 애니메이션 실행 | `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance*`, `Project_JAnimNode_Locomotion*`, `Project_JStateController*`, `Project_JAnimation*`. 실제 파일은 `rg --files`로 확인한다. 스냅샷·명령 revision·검색 완료·재생 수명의 소유자를 찾는다. |
| 리타깃·접촉 | 같은 Animation 폴더의 `Project_JRetargetAnimInstance*`, `Project_JRetargetBodyProfile.cpp`, `Project_JAnimNode_GuidedHandIK.cpp`, `Project_JHandContact.*`와 대응 Public 헤더. 실제 body/Family 연결·GC·default 복구·몸체 scale/좌표 공간을 확인한다. |
| 장비·표현·예산 | `Source/Project_JCharacter/Private/Components/Project_JEquipmentManagerComponent.cpp`, `Project_JWeaponPresentationComponent.cpp`, `Private/System/Project_JCharacterAnimationBudgetSubsystem.cpp`, `Project_JPresentationBudgetSubsystem.cpp`, budgeted mesh·visual coordinator 관련 파일. 요청 중첩·종료와 실제 평가를 대조한다. |
| NPC·Mass·탐색 | `Source/Project_JCharacter/Private/System/Project_JNPCDecisionSubsystem.cpp`, `Project_JNPCPathSubsystem.cpp`, `Private/Components/Project_JNPCActionComponent.cpp`, `Private/Mass/Project_JMassMovementProcessor.cpp`. scheduler·path 예약·weak owner·generation·join·적용을 확인한다. |
| 공용 서비스 | `Source/Project_JCore/Private/System`의 TargetScoring·VisualAsset·Message·AssetManager·ObjectPool, `Private/Optimization`. reentrant callback·asset lease·용량·worker 입력을 확인한다. |
| 서버·서비스 기반 | `Source/Project_J/Backend/Project_JGatewaySubsystem.*`, `Source/Project_JMMO/Public/MMO`와 Private 구현, Game·handover/social/progression 관련 파일. 개발용 adapter와 실서비스 구현을 구분한다. |
| 테스트·수집 | 각 모듈 `Private/Tests`, `Source/Project_J/Testing`, `Scripts/Validation`. 실제 authored fixture인지, 입력·예측·에셋을 우회하는지 확인한다. |

성격이 다른 경로를 모양이 비슷하다는 이유로 합치지 않는다. 반대로 한 상태를 여러 계층이 쓰거나 변경한다면 원천·파생 값·요청·실행 결과를 구분하고 중복 책임을 줄인다. 파일을 쪼갠 뒤에도 상호 의존과 실행 비용이 그대로인지 확인한다.

## 5. 우선 재현: R스킬 이후 우클릭 콤보 불가

### 확인된 사실과 미확인 정보

사용자가 증상을 보고했다. 이 인계 작성 단계에서는 재현 로그·원인·발생 확률·특정 스킬 에셋을 확정하지 않았다. R 키의 의미 태그 `InputTag.Skill.R`와 LMB/RMB 라우팅 코드가 존재하며, 실제 R스킬과 RMB 후보는 현재 클래스·장비·스킬 데이터로 결정된다. `R` 또는 특정 montage 이름을 조건으로 예외를 덧붙이지 않는다.

### 조사 순서와 기록

1. 현재 대검 플레이어의 IA/IMC, 클래스·AbilitySet·입력 command/스킬 구성, R 능력, RMB 콤보와 몽타주를 관련 범위의 MCP로 확인한다. 실제 데이터를 새 테스트 임의 값으로 대체하지 않는다.
2. 정상 RMB 단독을 대조군으로 두고 R 자연 종료→RMB, R 진행·blend-out 중 RMB, 짧게 눌렀다 놓기·길게 유지·재입력, 취소/피격/회피/장비 해제 후 RMB를 재현한다. 입력 실패 시점과 expected/actual을 기록한다.
3. 입력의 Started/Triggered/Completed/Canceled, 라우터 active button/tag·modifier/chord·버퍼, 실행 component, ASC 활성 ability/spec·태그·예측 키, 콤보 node/window·delegate·montage instance를 한 실행 identity로 연결해 추적한다.
4. 실제 능력 종료/취소, NotifyEnd, montage blend-out/interrupt, subscription 해제, 입력 소비·버퍼 재사용의 순서와 중복을 확인한다. 실패가 입력 전달 전인지, activation 거절인지, combo window 선택인지, 재생·표현 단계인지 먼저 구분한다.
5. 상태 원천과 수명 계약에서 수정한다. 공격 태그 강제 제거·타이머로 재활성화·R 전용 reset·전 능력 무조건 취소로 증상을 가리지 않는다. 의도한 blocking/cooldown과 버그를 구분한다.
6. 기존 LMB/RMB·동시/수식 입력·Q/T/회피·다른 장비/직업 경로를 보호한다. Standalone 결과와 소유 클라이언트 예측·서버 승인·simulated proxy 결과를 구분한다.

기존 `ProjectJ.Combat.ComboInputSubscription`, `ProjectJ.Combat.ComboBlendOutWindows`와 대응 `Source/Project_J/Testing/Project_JCombo*.cpp`는 출발점이다. **이 테스트의 존재·통과가 R→RMB 경로를 검증했다는 뜻은 아니다.** 실제 재현이 실패하고 수정 후 통과하는 의미 있는 회귀 시험을 추가한다.

## 6. 전체 감사의 점검 축

이 목록을 확인 체크 표시만으로 완료하지 않는다. 책임 지도와 실행 증거를 연결하고 누락·미검증 영역도 기록한다.

코드·모듈·설정·콘텐츠 등록에서 찾은 실제 시스템 목록과 이 표를 대조한다. 표에 없는 시스템도 감사 대상에 추가하고, 특정 기능의 결함이 공용 계약에서 비롯됐으면 같은 계약의 다른 소비자까지 확인한다. 검토를 생략한 영역은 이유와 영향도를 남긴다.

| 점검 축 | 추가로 고려할 내용 |
| --- | --- |
| 상태·설계 | 원천/파생/요청/결과 구분, 이중 상태 전이, source of truth, 의미 상태와 시각 상태, 불변식·오류 전파·재진입·중복 실행, fallback의 발동 조건 |
| 입력·전투 | focus 상실·UI 입력 소비·IMC lease, hold/release·modifier·동시 입력, ability grant 회수·Avatar 교체, 태그 카운트·combo buffer/window, montage notify 중복·중단, 공격 예산 보호·실제 판정 포즈 |
| 네트워크·예측 | 서버 검증·owner-only 정보, RPC bounds·속도 제한·중복, prediction key/장비 revision·시퀀스 wrap, reliable/unreliable/fallback의 멱등성, late join·재접속·dormancy/AOI 이탈 복귀, root motion·SavedMove·SSR 시간/히트 이력 |
| 객체·수명 | 살아 있는 객체와 현재 소유자 구분, Destroy~GC 간격, native UObject 참조 수집, delegate/timer/token/lease 해제, EndPlay·PIE 반복·world epoch·possession·비동기 완료의 이전 Avatar 접근 |
| 병렬·스케줄링 | GT 수집→불변 값→worker→GT 적용, 동시 UObject 접근, task 완료·취소·generation, join/잠금 경합·queue 상한·backpressure·공정성·starvation, job 분할/스냅샷 복사/매 프레임 제출 overhead, 재진입 때 이전 결과 |
| 애니메이션·물리 | 원본 포즈와 표현 포즈, 의미 상태·단발·검색·복귀 수명, MM 데이터/schema 호환, pose history/reset, curve/montage/notify 수요, bone/socket 좌표·scale, retarget·IK·cloth 순서, foot contact·LOD·URO/ABA·시간 간격·화면 복귀 |
| 에셋·제작 | 실제 DA/DT/Chooser/ABP/IA/IMC 연결, 잘못된 skeleton 쌍·chain·소켓·섹션, 누락/중복 entry·soft reference·asset registration, validation의 공백·좋은 오류 설명, skeleton별 얇은 ABP와 데이터 확장 |
| CPU/GPU·메모리 | per-frame 전체 검색·중복 계산·할당·copy, 실제 cache hit/invalidation 비용, task wait와 긴 tail, retarget/IK/physics·skin/draw/section·material/VFX/audio, peak 메모리·GC spike·streaming/PSO cold/warm·pool 고갈/반납 |
| 서비스·MMO | authority·상태 소유 단위·CAS/idempotency·bounded retry/timeout·exactly-once completion, cancel 후 tracker 제거·callback 재진입, 세대·저장 schema 호환·부분 실패 복구, 개발용 adapter·backend 미구현 구분 |
| 탈것·UI·사운드 | possession·좌석·전환 실패 복구, 장비/인벤토리 UI 구독과 재열기, focus/input 충돌, Foley·VFX owner/epoch·중복 이벤트·offscreen 보호·예산 합성. 이전 감사의 사운드 제외 범위를 이번 전체 감사에서 자동 계승하지 않는다. |
| 빌드·배포·테스트 | Runtime/Editor/UncookedOnly 경계, Cook 포함·AssetManager·soft refs·CoreRedirects/직렬화, Development/Shipping·서버 조건·CVar defaults, fixture 대표성·테스트 order/누수·관측 overhead·성능 시험 재현성 |

기존 병렬 아키텍처를 활용하되 NPC 타깃 scoring·Mass chunk·navigation·asset load·엔진 animation task가 같은 큐를 쓰는 것으로 일반화하지 않는다. task 수를 늘리거나 GT를 기다리게 하는 변경을 병렬 개선으로 자동 판정하지 않는다. 병렬화가 불리한 작은 작업에는 직렬 경로를 유지할 수 있다.

미사용 판정에는 C++ 호출뿐 아니라 Blueprint 직렬화·에셋 참조·soft load·Cook·fallback·확장 계약을 확인한다. 예전 Handoff reflected 타입, native fallback MM·공용 State Controller getter를 현재 Master에서 안 쓴다는 이유만으로 일괄 삭제하지 않는다. 조건이 겹치더라도 의미·취소 수명·권한이 다르면 통합하지 않는다.

## 7. 유지할 동작과 검증할 경계

- OTM/Strafe, Run/Sprint의 Start·Stop·Jump·Land·Pivot·TIP·Turn/Reface와 전투 상체·FullBody·몽타주·root motion 우선권을 보존한다.
- 현재 Master는 `Project Unified Locomotion`의 공통 재생 스택이다. 네이티브 State Controller·Chooser는 계속 사용한다. ABP State Machine 제거와 의미 상태 제거는 다르다.
- 단발의 지속 곡선 종료 기본값은 사용자가 복구한 **0.35초·100cm 이동·이동 방향 변화 3도·방향 일관성 85%**다. 이전 0.2초·80cm·75% 실험값을 재도입하지 않는다. 자연 종료·입력 취소·Land 충격 보호는 별도 출구다.
- Strafe Run 정렬된 전방의 W 미세 회전, Arc/Loop, 곡선 반전과 Land 직후 약한 회전, A/D·후방·대각선의 기존 후보를 검증한다. 특정 키/클립 이름의 강제 차단으로 해결하지 않는다.
- 애니메이션 예산은 종료 갱신·notify·blend-out·공격 소켓을 멈추지 않아야 한다. 화면 복귀·LOD·아바타 교체 때 stale history/이전 pose나 무기 목표를 새 수명에 재사용하지 않는다.
- MMO 조건에서 로컬의 고품질, 가까운 원격의 중요한 전투·단발, 먼 원격의 비용 조절, hidden 복귀를 구분한다. 서버 gameplay pose/SSR 소켓과 클라이언트 표현 비용을 혼동하지 않는다.
- 임포트 몸체의 추가 뼈·의상, 몸체별 접촉 보정, 공통 마네킹 MM·상체 combat 합성을 유지한다. 애니메이션 전부 개별 bake retarget이나 몸체별 MM 복제는 현재 사용자의 제작 제약과 맞지 않는다.

사용자가 과거에 “잘된다”고 확인한 버전과 이후 자동화만 통과한 버전을 구분한다. 현재 R 버그와 최근 모든 Land/리타깃 품질이 사용자의 최종 체감으로 일괄 승인됐다고 기록하지 않는다.

## 8. 과거 검증의 정확한 범위와 남은 과제

| 기록 | 확인한 것 | 확인하지 못한 것 / 후속 |
| --- | --- | --- |
| 10월 8일 수명 감사 | 실제 결함 5개 수정. 최종 당시 전체 자동화 244개: 성공 225·경고 포함 성공 19·실패 0. 조건 `a.Budget.BudgetMs 0.1` 등 문서 확인 | 현재 변경 전체의 검증·모든 소스/에셋/실서비스 실행 증명 아님. 사운드 제외와 대표 경계 상세 검토 범위가 있었음 |
| 최신 리타깃 기능 시험 | 회귀 27+렌더 6, 중복 제외 32개 성공. 테스트 오류 0, 최소 장비 fixture 경고 2. 최종 BodyContracts/ProductionRuntime/GC 재검사 성공 | R→RMB 미검증. 전체 프로젝트 테스트 통과 기록 아님. 정지 PNG 149개/일부 확인은 접지·전체 동영상 품질 인증 아님 |
| 최신 실제 네트워크 | server+client 2, native NPC 200, Iris 병렬 설정, lag 50ms/loss 3%, AOI/FastArray/애니메이션·정상 해제 성공 | 200명 실제 접속 또는 200개 authored retarget body 네트워크 부하 아님. Toolset Python 시작 오류는 별도 존재 |
| MM 캐시 최적화 | `8485562c`: 기존 generated MM 인덱스를 적용 경로에 재사용. 해당 AttackBurst ABA on timer 0.267075→0.085192ms | 전체 CPU 향상 입증 아님. 반복 편차·p95/최대값·기존 리팩토링 +8.3% 원인 미확정 |
| 리타깃 전후 200명 CPU | 같은 v2 조건 비계측 2회: Near ABA off 18.115→19.991ms, AttackBurst on 20.102→22.736ms. 일부 +10~13% 표본 보존 | 원인 미확정·회귀 해소/전체 성능 향상 미입증. 시간대가 달라 교대 비교 필요 |
| 분리한 프로필 타이머 | trace 1회에서 AttackBurst on 설정 처리 누적 약 0.131ms/tick. 기존 engine PreUpdate 제외 | 이 비용만으로 전체 tick 증가 설명 불가. 넓은 초기 timer 1.372ms를 추가 설정 비용으로 취급하지 않음 |

최종 네트워크 fixture는 두 시험 플레이어의 cull distance를 Iris 기본 상한 1500m 안의 1000m로 제한한다. 원래 10000m는 handled ensure를 일으켰다. 중간 `bAlwaysRelevant` 시도는 원격 참조 미준비로 stage 1 실패하여 제거했다. 생산 관심 영역·NPC AOI는 변경하지 않았다. 실패·최종 실행을 모두 보존했다.

로컬 최신 증거:

- `Saved/Validation/RuntimeRetarget_20261013/` — MCP 조회·에셋 백업·소스 백업·build·Regression/Rendered·FinalFixture/FinalGC·CPUComparison·FinalProfileTraceTimers·LandPlaybackEquivalence.
- `Saved/Validation/AuthoredAnimation_20261005/RuntimeRetargetBefore_20261013`, `RuntimeRetargetAfter_20261013`, `RuntimeRetargetAfterTrace_20261013`, `RuntimeRetargetProfileScopeTrace_20261013` — CPU JSON·manifest·source snapshot·해당 trace/export.
- `Saved/Validation/GroupD_20260910/RuntimeRetarget_20261013_Lag50Loss3_Parallel_BoundedFixture` — 성공한 실제 네트워크 시험. `_FixedFixture`는 실패한 중간 시도이며 성공 자료로 사용하지 않는다.

32개와 244개, 서로 다른 시점·조건의 시험 수를 합쳐 현재 전체 성공 수로 보고하지 않는다. assert 통과·엔진 경고/ensure·시작 오류·exit code·미실행을 각각 기록한다.

## 9. MCP·컴퓨터 사용·빌드 절차

먼저 현재 `AGENTS.md`와 사용자 요청을 읽는다. 사용자는 이전 대화에서 MCP·컴퓨터 사용·로그·빌드·코드/에셋 변경과 저장을 허용했다. 새 채팅의 명시적 요청은 13절을 함께 전달하는 방식으로 범위를 분명히 한다. 이미 허용된 범위를 반복 확인하지 않되, 실행 환경의 접근 제한을 우회하지 않는다.

- Source·Config·문서 분석을 먼저 한다. 실제 연결이 필요할 때 관련 Blueprint·ABP·DA·DT·Chooser·IA/IMC·레벨을 MCP로 최소 조회한다. 광범위한 검색이나 긴 schema 조회 전에 범위를 짧게 알린다.
- MCP가 연결되는지, 현재 Editor/PIE 상태와 project가 맞는지 확인한다. 연결 실패·도구 미지원·검색 0개·속성 읽기 실패를 에셋 부재나 기능 미구현으로 단정하지 않는다. 수정 전후 값과 compile·저장·재조회 결과를 남긴다.
- 이번 retarget 작업에서 공식 MCP로 실제 에셋을 생성·설정·compile·저장·재조회했다. native AnimGraph의 DSL/FindNodes/asset image 지원이 부족해 generated runtime audit로 보완했다. MCP가 모든 코드·graph·성능의 완결된 관측 수단은 아니다.
- 필요한 UI 조작은 현재 제공된 컴퓨터 사용 도구·관련 지침으로 수행한다. 사용 불가능한 화면을 추정하지 않는다. 별도 AI 모델의 capacity 알림을 UBT 오류·작업 파일 손상으로 해석하지 않는다.
- **직접 `UnrealBuildTool.exe`로 빌드하고 완료까지 기다린다.** UBT·dotnet·Unreal Editor·Live Coding·MSBuild·ShaderCompileWorker가 남아 있으면 새 빌드를 시작하지 않는다. 실행 중 빌드를 중단하지 않는다. Editor 종료·저장과 진행 중 PIE를 구분한다.
- dotnet 예외 창은 최초 UBT/compiler 오류와 Windows Application Event Log부터 확인한다. 경로 문제로 추정해 파일·캐시를 삭제하지 않는다.
- Git LFS diff가 sandbox의 `sh.exe NtCreateDirectoryObject 0xC0000022`로 실패한 전례가 있다. C++ 오류로 처리하지 않고 정상 접근 범위에서 재확인한다.
- 임시 Python remote execution 활성화는 자동 승인 검토가 보안 설정 변경 범위가 과하다는 이유로 거절했다. 활성화하지 않았고 로컬 WM_CLOSE 정상 종료 경로를 사용했다. 같은 원격 설정을 임의로 켜는 우회책을 쓰지 않는다.

## 10. 검증과 측정 계획

위험·변경 영역에 맞는 검증을 먼저 실행하고, 새로운 실패나 계약 변화가 있을 때 확대한다. 단순 문서·표현 정리마다 전체 빌드/전체 시험을 반복하지 않는다.

| 층 | 기준 |
| --- | --- |
| 결함 재현 | 사용자 조건과 실제 데이터에서 수정 전 실패·수정 후 성공. 재현률·타이밍·중단/취소·대조군을 기록 |
| 계약 시험 | 의미 있는 상태·수명·권한 불변식. 구현을 그대로 복사한 단언이나 통과를 위한 timeout/품질 완화는 피함 |
| authored 통합 | 실제 BP·장비·Ability/콤보·Master/몸체 ABP·Notify 사용. 테스트가 생략한 입력/네트워크 경계는 명시 |
| 원격·예측 | Standalone, 소유 클라이언트·simulated proxy·dedicated server 역할을 구분. latency/loss·이벤트 순서·late join·교체·중단 |
| 품질·시간 | 관련 30/60/120Hz·불규칙 dt·화면 복귀·LOD·회전 반전·다음 행동. 발/손의 실제 접촉·포즈·root/캡슐 및 동영상 증거 |
| 장시간·부하 | 1/50/100/200 단계 중 필요한 비교, 반복 장착·스킬·파괴·PIE/맵 수명. leak·peak memory·GC·queue/lease/token 회복 |
| 패키지·서버 | 변경이 에셋 로딩·모듈/soft ref·Cook 의존성을 바꾸면 Game/관련 package/server 경로를 검사. Editor 성공만으로 완료하지 않음 |
| 관측 정확성 | expected/actual, build·source/asset hash, 명령·CVar·RHI·표본 수, warnings/errors/ensures·미실행·작업 환경 기록 |

CPU 비교는 같은 인원·BP/장비·follower·동작·ABA budget·Hz·준비/측정 길이·RHI로 수행한다. 비계측 반복은 전후 교대 실행과 편차·p50/p95/max·긴 tail을 확인하고, trace는 병목 분리용 별도 실행으로 기록한다. 공격 dispatch·observer 시간이 무엇을 포함하는지, montage 수가 hit window를 대체하지 않는다는 점도 확인한다. CPU timer를 중첩 합산하거나 worker 누적 시간을 전체 프레임 시간처럼 더하지 않는다.

GPU·렌더 FPS는 실제 RHI 조건의 별도 검증이다. 시각 품질을 낮춰 개선된 경우 변경한 품질·보호 범위·평가 횟수를 함께 보고한다. 성능 변화가 반복 편차 안이면 향상으로 확정하지 않는다. 백그라운드 앱·열/전원·DDC·shader/PSO·trace overhead 같은 실행 차이를 기록하고, 사용자가 실행 중인 앱을 임의 종료하지 않는다.

사용 가능한 기존 도구는 `Scripts/Validation/Measure-AuthoredAnimation.ps1`(v2, WarmupTicks/MeasuredTicks/NoDDCCleanup), `Measure-Network.ps1`, `Export-DTrace.ps1`, `Measure-Mass.ps1`, `Measure-Navigation.ps1`, `Measure-CrowdE.ps1`, `Measure-CookedCombat.ps1`, `Measure-CookedEffects.ps1`, `Validate-CookedUI.ps1`, `Validate-MMOArchitecture.py` 등이다. **먼저 해당 script의 현재 인자·guard·assert를 읽고** 이름만으로 적합성을 판단하지 않는다. `Validate-MMOArchitecture.py`는 기본 검증과 `--write`의 생성물 갱신을 구분한다. 자동화 filter가 정확히 원하는 leaf test를 실행했는지 report로 확인한다.

## 11. 진행 순서와 좋은 구조의 판단 기준

1. **기준선·범위:** Git/프로세스/문서/소스/실제 에셋을 확인하고 기존 작업을 보존한다. 시스템·owner·실행 단계·주요 의존성·검증 커버리지 지도를 작성한다.
2. **우선 결함:** R→RMB를 재현·추적한다. 서버 권한·파괴/수명·기존 움직임을 깨는 결함이 발견되면 영향도에 따라 우선순위를 조정한다.
3. **구조 감사:** 위험 패턴 검색과 주요 경계 상세 검토를 구분한다. 미사용/중복·부적절한 예외·측정 누락·관측의 거짓 통과를 증거로 목록화한다.
4. **단계별 개선:** 기능 변경과 책임 분리·데이터 이행·성능 변경을 가능한 한 리뷰 가능한 단위로 나눈다. 한 번에 전체를 교체하거나 작은 결함에 범용 framework를 추가하지 않는다.
5. **검증·비교:** 기존 정상 동작·새 실패 경로·원격·병렬·품질·성능을 변경 위험에 맞게 검사한다. 무효화·해제·restore·rollback 경로도 확인한다.
6. **기록·정리:** 원인·최종 구현·효과·실패 시도·남은 위험/미구현/미검증을 구분한다. 기준 문서·제작 가이드·README/인계/검증 원자료 연결을 갱신한다.

좋은 구조는 파일 수 감소나 새 클래스 수가 기준이 아니다. 상태와 수명의 owner가 명확하고, 정상/실패/취소 경로가 일관되며, 데이터로 확장할 수 있고, 실제 자원 비용과 회귀를 관측할 수 있어야 한다. 추상화·dispatcher·전역 service·generic cache는 해결하는 중복/비용과 새로운 의존·무효화·디버깅 비용이 설명될 때 도입한다.

## 12. 결과물과 완료 판단

다음 작업은 다음을 남긴다.

- **현재 구조 지도:** 모듈 의존, 주요 상태 owner, GT/worker/네트워크 실행 단계, 실제 데이터·에셋 연결. 문서와 다른 부분·미검토 범위 표시.
- **발견 목록:** ID·영향도·유형(확인된 결함/잠재 위험/개선 제안)·재현/근거·영향 경로·수정·검증·미해결 이유.
- **구현과 이행:** 제거한 코드의 참조 근거, 데이터/에셋 변경, compatibility·default·restore·fallback·취소 처리. 최종안에서 쓰지 않는 임시 상태·중복 경로 정리.
- **검증 보고:** 정확히 실행한 test/fixture·조건·성공/경고/실패/미실행·원자료·commit 또는 소스/에셋 해시. 기능·사용자 체감·성능·원격을 별도로 표시.
- **성능 보고:** 전후 표본·편차·p95/max·단계별 CPU/GPU/메모리/네트워크, 품질·평가 횟수, 원인 미확정 증가와 개선되지 않은 항목.
- **문서와 후속:** 최신 계약/작성법·README/카탈로그·재현 절차를 갱신하고 남은 작업을 구체적으로 인계한다. 설계도만 작성한 상태를 구현 완료로 보고하지 않는다.

기존 전체 시험의 요구 CVar·시간 조건을 먼저 확인한다. 전체 감사의 완료는 범위별 증거와 남은 한계를 정리한 상태이며 **모든 미래 버그의 부재를 증명한 상태가 아니다.** 자료 부족·도구 제한·실서비스 미구현을 숨기거나 추정 결과로 채우지 않는다.

문서 정리는 `node Scripts/Documentation/Update-DocumentCatalog.mjs`, `node Scripts/Validation/Validate-DocLinks.mjs`, `git diff --check`로 확인한다. 과거 CSV/JSON·실패 원본을 임의 변경하지 않는다.

## 13. 다음 채팅에 복사할 요청문

아래 요청문과 이 파일의 절대 경로를 함께 전달한다. 이 블록은 사용자가 다음 채팅에 전달할 작업 요청 초안이며, 현재 문서 작성 작업에서 전체 개선을 이미 수행했다고 뜻하지 않는다.

```text
C:\Users\I\Documents\GitHub\Project_J\Docs\Handoffs\WholeProjectAudit_2026-10-13\Project_J_Whole_Project_Audit_Handoff_2026-10-13.md

이 인계 문서를 읽고 Project_J 전체 구조·동작·병렬 처리·네트워크·성능의 감사와 개선을 진행해줘.
책임/규모/중복/미사용 코드에 한정하지 말고 상태·데이터·실제 에셋·수명·취소·재진입·확장·제작·검증까지 살펴봐.
내가 예로 든 항목과 문서의 목록을 범위 제한으로 취급하지 말고, 실제 시스템 목록·의존성·실행 흐름에서 누락된 영역과 공통 결함도 찾아서 조사해줘.
지금 발견한 버그는 R스킬 이후 우클릭 콤보가 안 되는 것이야. 우선 실제 데이터에서 재현하고, 다른 숨은 결함과 최적화 누락도 근거를 가지고 찾아줘.

먼저 Git 상태와 기존 미커밋 리타깃 작업을 보존하고, Docs·Source·Config·uproject/Build.cs로 현재 구조를 파악해줘.
Unreal MCP를 활용해서 필요한 실제 BP/ABP/DA/DT/Chooser/IA/IMC 연결과 런타임 상태를 대조해줘.
이번 조사·개선에 필요한 MCP·컴퓨터 사용·로그 수집·빌드/재빌드·코드/에셋 수정과 필요한 저장을 허용해.
허용한 범위를 반복 확인하지 말고, 접근 제한을 우회하거나 관련 없는 사용자 작업을 덮어쓰지 말고 진행해줘.
AGENTS.md의 직접 UBT 사용·프로세스 겹침 금지·빌드 중단 금지 규칙을 지켜줘.

현재 잘되는 OTM/Strafe, Run/Sprint, 단발/MM 복귀·Land·측면 후보·전투·장비·리타깃을 보존해줘.
단발 지속 곡선 기본값 0.35초·100cm·3도·85%는 임의 변경하지 말아줘.
확인된 결함·잠재 위험·제안을 구분하고, 특정 키/클립 예외·강제 reset·타이머 덧붙이기로 증상을 가리지 말고 원천 계약에서 해결해줘.
기존 병렬 시스템을 확인하고 GT/worker·스냅샷·GC·task 취소/완료·서버 권위/예측과 충돌 없게 구현해줘.

200명 비교에서 일부 CPU 증가가 남아 있으므로 해결 완료로 가정하지 말고, 같은 조건의 반복·교대 비교와 단계별 trace로 조사해줘.
NullRHI CPU·렌더 FPS/GPU·200개 표시 캐릭터·실제 원격 접속 수를 구분해줘.
기능별로 리뷰 가능한 단위로 개선하고 새 실패 경로를 검증에 추가하며, 필요한 통합·원격·품질·부하·Cook 검증을 완료해줘.
MCP 미지원/연결 실패나 자동화 통과를 전체 무결성으로 해석하지 말고, 검토·검증하지 못한 부분을 명시해줘.

구조 지도·발견 목록·최종 변경 이유·검증/성능 원자료·남은 문제를 한국어 문서로 남기고 관련 기준 문서/README/카탈로그도 갱신해줘.
구현과 검증까지 진행하고, 커밋·push·병합·컴퓨터 종료는 별도의 최신 요청 범위로 남겨줘.
```
