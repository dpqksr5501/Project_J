# PC 소스 누락 대조와 DT·Chooser·IA·IMC 구조 검토 — R19

**후속 Mirror 판단:** [2026-10-15 도입 보류 결정](../../Animation/Planning/Mirror_Data_Adoption_Decision_2026-10-15.md)에 따라 현재 locomotion을 유지한다. 아래 MDT 설치 미입증은 미러 도입 필요성을 뜻하지 않으며, 후속 조회와 미러 적용 후 품질 검증을 구분한다. R19·R20의 커밋 이후 main 통합은 [별도 기록](Project_J_Main_Integration_2026-10-15.md)을 따른다.

**후속 실제 재현:** 사용자의 Strafe 대각 점프·이동 착지 보고로 [R20 sample 보정 분기 결함](Project_J_Strafe_Diagonal_Warp_Repair_2026-10-15.md)을 추가 재현했다. 아래 선택 존재/구조 대조는 해당 분기의 초기화·포즈 동작을 검증하지 못했다. R19를 전체 runtime 동작 완료 근거로 사용하지 않는다.

기존 소스·아키텍처 정리와 BP·DA 검토를 재사용하고, 남았던 데이터 연결과 소스 범위 누락을 대조했다. **Stand 착지의 대각선 선택 누락을 재현해 데이터 계약에서 수정했다.** 아래 명시 범위의 구조 대조와 영향 회귀를 수행했으며, 모든 함수의 모든 실행 조합·모든 에셋 품질·환경을 검증했다는 뜻은 아니다. 성능 조사는 계속 보류한다.

기준 커밋은 실제 Git에서 확인한 `e5e534e81ad909145fc34130f2c2d420b244049a`다. R19 검증 당시 변경은 미커밋이었고, 이후 사용자의 최신 요청으로 R20과 함께 커밋한다. 사용자 인계 파일 변경은 SHA256 `9465b0717e3a5152e06831deffaa20fb70d62f7251ae45f9c9ae3b3253b0c5f2` 그대로 보존하고 이번 커밋에서 제외한다. push·병합·컴퓨터 종료는 하지 않는다. 읽기 전용 담당 3명은 범위·입력·애니메이션을 병렬 대조했고 수정·MCP·에디터·빌드·시험·저장은 주 작업자만 수행했다.

## 실제 수정과 이유

| 대상 | 재현·원인 | 변경·보존 |
|---|---|---|
| `CHT_Player_Strafe_Land_Stand` | 공중 입력 해제 후 Stand 착지가 latch돼도 residual velocity 또는 저속 이전 snapshot은 8방향을 유지한다. 기존 표에는 cardinal 16조합만 있어 대각선에서 결과가 없었다. 실제 `BeginLandingState → GT snapshot → authored root → 선택`의 64조합 중 대각선 32조합이 실패했다. | 원본 19행을 보존하고 정확한 대각 조건 16행을 추가했다. FL/FR→F, BL/BR→B는 기존 FallOff·Land_Run과 제작 기준의 alias 계약이다. 원본 F/B 8행의 결과·Light/Heavy·좌우 발·StartTime .5/1·BlendTime .3·UseMM=false를 복사했다. 입력 방향·서버 의미 상태·worker snapshot을 접거나 reset하지 않았다. |
| 읽기 전용 authored 감사 도구 | 기존 export는 `/Game/Animation_Logic` root·PSD·schema 범위였고, `NestedChoosers=[]`만으로는 traversal 결과가 가리키는 내부 표를 읽을 수 없었다. | 선택적 `ChoosersOnly` 모드에서 `/Game` Chooser와 result/fallback의 Asset/Evaluate/Nested table, metadata child를 visited set으로 추적한다. context·출력 type·dirty·행·컬럼을 읽고 기존 출력 파일을 덮지 않는다. 기존 기본 PSD/schema 범위는 유지한다. 에셋 수정·저장 기능은 없다. |
| 기존 LandingReturnContext 회귀 | 방향이 F인 정상 착지만으로는 위 데이터 공백을 잡지 못했다. | 기존 시험에 8방향×양발×Light/Heavy×잔여속도/저속 이전방향=64조합을 추가했다. fresh 재생 소유자와 실제 producer·root를 사용하며 방향/impact weight 보존·Land 도달·선택 성공을 확인한다. Foot은 선택 입력으로 주며 실제 보행 발 생성·정확 clip/재생·포즈·원격 전달 검증으로 확대하지 않는다. |

같은 clip을 쓰더라도 조건이 서로 다른 필수 행은 중복 로직이 아니다. 이번 수정은 runtime 분기·상태·timer·특정 키/클립 예외를 추가하지 않았다. 일회성 제작 CPP는 백업 후 Source에서 제거했고 최종 DLL에서도 제외했다. 테스트·prototype·snapshot·비활성 제작 행은 삭제하지 않았다.

## 시스템별 범위 장부

| 시스템 | 실제 대조와 판정 | 남은 검증·경계 |
|---|---|---|
| native 소스·아키텍처 | [CR13–34 명시 범위](Project_J_Continuation_System_Review_2026-10-15.md)·AdditionalReadScopes·ReadOnlyReviewClosure 및 R17/R18의 본문/header/inline/caller/상태 소유권 근거를 연결했다. Mount, Backend/Handover/MMO, Quest/Social/UI, Inventory/Equipment/GAS, Animation, NPC/Mass/Path, Editor/Core/Controller, Experiments/시험의 후속 폐쇄 근거를 재사용했다. 이번 재대조에서 새로운 production 본문 공백이나 추가 확정 중복 로직은 찾지 못했다. | 경로 연결 수를 모든 함수 의미·모든 호출 조합 검토 완료의 증거로 삼지 않는다. 기존 기록의 지정 범위를 넘어선 보증은 하지 않는다. |
| Config·fixture | Engine111·Game35·Input86·GameplayTags108·Mass5·EditorPerProjectUserSettings2행 및 DefaultEditor1–10/14–15를 읽었다. Mass global processor·AssetManager/cook root·UI literal lookup·EnhancedInput의 실제 소비를 연결했다. fixture README39행과 CSV3개 caller를 대조했다. | DefaultEditor11–13 약58KB preview bag은 이름·엔진 에셋 경로만 파싱했다. 모든 PostProcess 값 의미/출력과 CSV 원표본 전체 의미/원로그/시각 품질은 미검토다. |
| 주 애니메이션 Chooser | 20개 전체의 행·조건·우선순위·결과·출력·fallback·context와 native root/GT snapshot/worker 재생 소비를 대조했다. 원본268행은 228활성/40비활성이며 최종284행이다. Stand 외19개와 Stand 원본19행·4컬럼의 기존 셀/설정을 정확히 보존했다. | 실제 화면의 대각 alias 발 접촉/자세 및 독립 다중클라이언트 재현은 이번 논리 회귀와 구분한다. 기존 OTM/Strafe·Run/Sprint·단발/MM 복귀·Land/측면·전투/장비/리타깃 근거를 재사용했다. |
| Traversal 예제 | CMC/Mover root2개·8행과 내부 Mantles/Vaults/Hurdles 6개·68행을 읽었다. 겹치는 숫자 범위·양발 행은 PoseHistory 기반 PoseSearch cost 선택에 참여한다. CMC/Mover의 montage·sampling·local PSD/context 차이가 있어 합칠 근거가 없다. | 운영 설치가 입증되지 않았다. GUID context binding·Mode1/2/10·producer·PoseHistory·내부 PSD6개 준비와 MontageStartTime 소비의 실행 연결은 미검증이다. Hurdles 일부 Run Min25/다른 Run250은 authored 비대칭 후보로 보존하며 오타로 단정하지 않는다. |
| IA·IMC | 현재 IA18·IMC2의 공개값, modifier12·Jump trigger2 본문을 읽었다. Boolean13/Axis2D3/Axis1D2와 native Get 타입, swizzle/negate·radial deadzone .2..1·threshold .5를 대조했다. 실제 매핑은 deprecated `mappings=[]`가 아닌 `defaultKeyMappings`의 26+1행이다. | 새 입력 충돌은 확정하지 못했다. 모든 물리 입력장치·OS focus·렌더 UI 조합의 전수 실행 시험은 아니다. identity scalar는 튜닝 지점일 수 있어 삭제하지 않았다. |
| 실제 BP 입력 연결 | Controller는 Default IMC와 PC MouseLook IMC를 각각 lease한다. GreatSword의 Look/MouseLook와 MoveIntent4는 서로 다른 IA다. Router는 DA3chord/QRT3만 소비하고 inline chords는 비었다. BP_Player는 선택 combat/semantic 입력 없이 Axis fallback을 유지한다. Wyvern5개 action은 별도 pawn 바인딩과 맞는다. | BP_Player의 선택 입력 None은 결함으로 채우지 않았다. lease/press receipt/바인딩 revision·취소/완료·ASC prediction와 서버 raw-input 검증은 기존 source 회귀/검토 근거를 재사용한다. |
| DT·Mirror | 전체 registry DT 계열은 MDT1개다. 현재87행을 전부 읽었고 중앙 self11+좌우38쌍, 누락·비대칭0이다. X축·root motion·FullSkeleton·SK_UEFN 연결을 유지했다. 활성 PSS6개의 mirrorDataTable=None 기존값과 generic BlendStack getter를 구분했다. | 이 MDT의 runtime mirror 설치와 최종 mirrored pose/root motion 품질은 입증하지 못했다. 직접 참조0을 삭제 근거로 삼지 않는다. |
| BP·DA 기존 범위 | [R18](Project_J_PC_Asset_Review_Closure_2026-10-15.md)의 BP33·고유 graph111, native DA44·GE2의 실제값/소유권 기록을 재사용했다. 현재 조회한 BP/CDO·DA root와 소스 소비를 추가 연결했다. | 모든 imported asset·새 외부/동적 콘텐츠 속성/실행을 전수 검증한 것으로 확대하지 않는다. |

최종 Chooser는 **registry package22개, 내부 포함 고유 table28개·360행(활성320/비활성40), dirty0**이다. 이전 `327행`은 PSD53/schema6의 없는 rows 값을 함께 집계한 잘못된 수치이며 주 Chooser20개 원본은268행이다. `21개`로 추정했던 root 수도22개로 정정한다. 본문 대조와 실제 실행 완료를 구분한다.

## 상태 소유권과 확장 구조 지도

| 입력/데이터 → 소비 | 권위·책임 | 유지한 이유 |
|---|---|---|
| Controller → LocalPlayer IMC lease → 현재 pawn binding | context는 owner set/외부 설치 여부로 보존, binding은 receipt/revision으로 퇴역 | possession 교체·늦은 release가 다른 소유자의 입력을 지우지 않는다. |
| InputMapping DA → Router → Execution/ASC → 서버 검증 | raw input/command 변환과 예측·피해 권위를 분리 | DA와 inline fallback을 동시에 실행하거나 BP에 서버 상태 writer를 늘리지 않는다. |
| Character profile → Locomotion DA policy → StateController root | 실제 root는 `motionMatchingSearchPolicy.stateControllerAnimationChooserTable` | ABP의 `motionMatchingChooserTable=None`은 별도 PSD typed migration fallback이다. 서로 대체·통합하지 않는다. |
| CMC/입력/복제 의미 → GT immutable snapshot → Chooser → Unified Locomotion | 이동/착지 의미와 presentation 수명·worker 포즈 소비를 분리 | Stand는 입력 의도, Strafe sector는 물리/이전 이동 방향이다. 이 둘을 합쳐 선택 공백을 가리지 않는다. |
| 프로필·태그·linked layer → 장비/탈것/표현 | 공통 lease와 snapshot 소비 재사용 | MMORPG 콘텐츠 추가 시 BP 복제·분기 Enum·새 추상화만 늘리지 않는다. 새 서비스/콘텐츠 구현은 별도 작업이다. |

지속 곡선 **0.35초·100cm·3도·85%**와 무기·리타깃·전투 데이터는 변경하지 않았다. source/config636개 경로는 R18 hash 목록과 신규/삭제0이며, 차이는 감사 도구 CPP와 기존 회귀 CPP2개뿐이다. 이번 선택 에셋51개 hash에서 차이는 Stand1개뿐이다. 이 hash 대조는 보존/누락 감시 근거이며 정밀 의미 검토를 대신하지 않는다.

## 검증과 원자료

- directUBT 전체 Editor 최종 `R19_FinalSourceBuild3` 성공(컴파일1+link2+metadata1, 12.69초). 최종 source와 DLL을 연결했다. 제작/검증 프로세스는 정상 종료를 기다렸고 겹치지 않았다.
- 수정 전 `R19_StandLandRepro1`: 시험1실패, 선택 assertion32실패. 같은 fixture의 cardinal32는 성공했다. 독립 원격 세션 재현이라고 부르지 않는다.
- 수정 후 `R19_StandLandFinal2`: 집중9개 clean 성공, 실패/미실행0. LandingReturnContext64조합·OneShotModeContinuity·ReturnedChooserTraversal·StrafeDirection3·StrafePivot3을 포함한다. Final1 이후 heavy/Land 도달 assertion만 보강해 재실행했으며 18개 고유 시험으로 합산하지 않는다.
- `R19_StandLandCook1`: Windows 기본 map+strafe Chooser 명시 Cook, exit0, 실제1361·증분skip0·platformskip7·total1368. 변경한 Stand가 ReferencedSet에 포함됐다. 기존 GameplayCueNotifyPaths 경고는 남겼다. Game 실행/패키지/원격 검증으로 승격하지 않는다.
- 초기 도구 CPP의 auto pointer 추론 오류(C3535)는 수정 후 빌드했다. 새 임시 CPP가 첫 증분 빌드에서 action0으로 누락됐고, 제거 후에는 캐시가 삭제된 CPP를 참조해 C1083으로 실패했다. 해당 Editor `Makefile.bin` 하나를 백업 이동해 재생성했고 Build2의3개 link/metadata action으로 정상 종료한 뒤 위 최종 assertion 보강을 빌드했다. 최종 DLL에 일회성 제작 시험은 없다. 기존 obj/PCH를 전체 삭제하거나 오래된 binary를 복원하지 않았다.

원본 MCP request/response·세션·로그·빌드/시험/Cook·일회성 제작 코드는 ignored `Saved/Validation`에 보존한다. 영구 문서에는 [R19 요약·범위/hash](PCStructureEvidence_2026-10-15/Reconciliation/Summary.json)와 [원자료 SHA/위치](PCStructureEvidence_2026-10-15/Reconciliation/ReceiptIndex.json)만 남긴다. 이전 R17/R18 회귀·Cook을 이번 새 수정의 재실행으로 합산하지 않는다.

## 남은 문제

현재 확인된 잔여 소스/데이터 구조 대조 축은 위 표에 기록했다. 새 확정 중복 로직은 없고 재현한 착지 결함은 수정·회귀·Cook을 마쳤다. **미검토 항목은 Config preview bag의 전체 의미와 CSV 원표본 의미, 실행 미검증은 traversal 설치/MDT 사용 및 대각 alias 시각·독립 원격 경계다.** 전체 무누락·전수 품질 완료라고 말하지 않는다.

CPU 증가·렌더 차이, NullRHI CPU와 렌더 FPS/GPU 비교, 200표시/실제 socket 부하, 대규모 원격·실서비스 adapter·전수 시각/오디오 품질 조사는 최신 요청대로 보류한다. 기존 성능 문제가 해결됐다고 하지 않으며 RA13 binary 복원 스크립트를 실행하지 않았다.
