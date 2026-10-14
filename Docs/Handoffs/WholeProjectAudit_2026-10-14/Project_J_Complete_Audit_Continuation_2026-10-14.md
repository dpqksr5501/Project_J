# Project_J 전체 감사·개선 마무리 인계 — 2026-10-14

## 목적과 현재 판정

사용자는 남은 감사와 전체 범위 누락 점검을 **다른 채팅에서 하나의 계속 작업으로 끝까지 수행**하려 한다. 이 문서는 그 실행 지시와 기준점이다. 코드 규모·중복·미사용, MM 또는 R 버그, 성능 시험만으로 범위를 제한하지 않는다. 실제 시스템 목록·호출·권위·상태 소유권·관련 에셋·제작·검증·운영 경계에서 새로운 누락과 공통 결함도 찾아 개선한다.

**현재 전체 정밀 감사는 미완료다.** 이미 구현·검증한 변경은 이어받고, 아직 모든 함수·헤더·실패 경로·관련 에셋의 상세 검토를 마친 것은 아니다. 아래 작업 목록도 범위의 상한이 아니다. 자동화 성공, Cook 성공, 파일을 읽은 횟수 또는 MCP 성공을 전체 무결성으로 바꾸어 표현하지 않는다.

이번 인계 작성에서는 현재 Git/프로세스·보존 해시·최신 결과·알려진 후보의 소스 위치를 다시 확인하고 목록을 만들었다. 새 production 코드나 에셋을 수정하지 않았다. 전체 감사 완료 문서가 아니라 남은 실행을 위한 문서다.

## 기준점·커밋·권한

| 항목 | 기준 |
| --- | --- |
| 작업 디렉터리 | `C:\Users\I\Documents\GitHub\Project_J` |
| 엔진 | UE 5.8.3, `C:\Program Files\Epic Games\UE_5.8` |
| 브랜치 | `codex/runtime-retarget-profiles` |
| 인계 커밋 직전 HEAD | `f1bbd48ac4a37eab6ed7a33d025f1de2c270427c` |
| 인계 전 Git short 상태 | 203개 항목. 디렉터리 요약을 포함하므로 실제 파일 수와 다름 |
| 새 인계 기준점 식별 | 커밋 제목 **`전체 프로젝트 감사 개선·검증 기록과 후속 마무리 인계 정리`**. 성공 여부와 해시는 실제 `git log -1` 및 상태로 확인한다. 이 문서는 그 커밋에 함께 들어가므로 자기 자신의 해시를 미리 쓰지 않는다 |
| 현재 실행 상태 | 인계 작성 시 Unreal/UBT/dotnet/MSBuild/ShaderCompileWorker/LiveCoding/Project_J 실행 없음. 새 채팅에서는 다시 실제 상태를 확인 |

사용자는 이 대화에서 MCP·컴퓨터 사용·로그 수집·빌드/재빌드·관련 코드/에셋 수정·필요한 저장을 허용했다. 이어갈 때 아래 새 요청문에 그 허용을 명시한다. 접근 제한은 우회하지 않고 관련 없는 사용자 작업은 덮어쓰지 않는다. 사용자는 **2026-10-14 최신 메시지에서 먼저 상세한 한국어 커밋을 하도록 요청했다.** 이전 결과의 “커밋하지 않음”은 해당 시점의 사실이다. 이번 요청은 현재 변경과 인계를 기준점으로 커밋하는 데 적용하며, push·병합·컴퓨터 종료와 이후 새 변경의 추가 커밋은 별도 최신 요청 범위로 남긴다.

커밋을 하면 기준선은 “미커밋 변경”에서 “인계 커밋”으로 바뀐다. 새 채팅에서 기존 변경을 새 개선으로 중복 계산하거나 되돌리지 않는다. 인계 커밋 이후 수정은 별도 diff/검증 버전으로 기록한다. 이전 HEAD의 attack DA는 일반 바이너리였고 현재 `.gitattributes`에 따라 새 커밋에서 LFS pointer로 표현될 수 있다. pointer 크기와 실제 로컬 에셋 크기를 혼동하지 말고 로컬 SHA·LFS OID로 실제 내용을 대조한다. LFS 커밋은 업로드를 뜻하지 않는다.

`AGENTS.md`와 사용자 직접 지시를 먼저 읽는다. Unreal C++은 **직접 UnrealBuildTool.exe를 사용하고 정상 완료를 기다린다.** UBT/dotnet/Editor/LiveCoding/MSBuild/ShaderCompileWorker 실행 중 새 빌드를 시작하거나 실행 중 빌드를 중단하지 않는다. dotnet 예외 창이 나오면 첫 UBT/compiler 오류와 Windows Application Event Log부터 확인한다. 허용 범위를 반복 질문하지 않는다.

## 처음 읽을 문서와 원자료

커밋 준비 중 원자료의 패치/로그/스냅샷 공백이 일반 소스 공백 검사에 걸리는 것을 확인했다. 원자료를 수정하지 않고 `.gitattributes`에서 기존 Evidence/DeepEvidence/ReauditEvidence 폴더에만 `-text`를 적용해 Git checkout의 줄바꿈 변환으로 기록 바이트와 해시가 달라지는 것을 방지한다. PNG 등의 기존 LFS filter는 유지한다. 일반 코드/문서 검사는 별도로 수행한다. 고정된 소스3개(InventoryWidgets.cpp, SkillButton.cpp, WeaponPresentationComponent.Diagnostics.cpp)의 기존 끝 빈 줄도 이번 인계에서 임의 포맷팅하지 않는다. 전체 raw-evidence 공백 검사가 clean이라고 주장하지 않는다.

1. [현재 변경·검증 결과](../../Review/Audits/Project_J_Reaudit_Verification_2026-10-14.md): 최신 RA27 판정의 첫 진입점.
2. [실행·상태·모듈 지도](../../Review/Audits/Project_J_Reaudit_Map_2026-10-14.md), [상세 결과와 남은 문제](../../Review/Audits/Project_J_Reaudit_Remaining_2026-10-14.md).
3. [재감사 변경 기록](../../Review/Audits/Project_J_Reaudit_2026-10-14.md), [앞선 심화 지도](../../Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md), [앞선 심화 감사](../../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md), [구조 개선](../../Review/Audits/Project_J_Structural_Improvement_2026-10-13.md).
4. [이번 이어받기 목록](ContinuationCoverageInventory.json): 소스607개(생산/제작458·시험/fixture149), Content 패키지 파일2738개, 추가 설정/스크립트/descriptor44개. 목록 자체는 검토 완료나 runtime 도달성 증명이 아니다.
5. [배포 가능한 원자료 manifest](../../Review/Audits/ReauditEvidence_2026-10-14/Manifest.json): 기존 묶음803파일·78,427,499bytes, 최종 해시 불일치0. 인계 새 문서/목록은 이 기존 불변 묶음 밖에 있다.

로컬 주요 원자료는 `Saved/Validation/Reaudit_20261014`에 있다. `RA27ChangeUnits.json`(기능별25단위), `RA27SourceChangeManifest.json`/`.patch`, `SourceReviewLedger.json`, `AdditionalReviewScopes.json`, `ReviewSummary.json`, `RemovedNativeReferenceProof.json`을 사용한다. 패치/해시와 실제 현재 파일을 비교한다. 작은 portable 자료와 full binary/trace 자료를 구분한다. 큰 trace42개는 `LargeLocalRawEvidence.json`의 경로·해시로 찾고, 다른 컴퓨터에서 Saved 자료가 없으면 있다고 가정하지 않는다.

**시점 우선순위:** 최신 RA27 receipt > 해당 실행 manifest/소스·바이너리 해시 > 시점별 결과 문서 > 오래된 진행 메모. `ContinueState.json`에는 RA13 등 과거 next_actions/대기 상태가 남아 있으므로 그것을 실행 큐로 사용하지 않는다. 기존 상세 문서의 RA23–27 “진행/대기”는 뒤의 최신 확인 상태로 닫혔다. 새로운 진행 장부를 만들고 기존 이력을 삭제하지 않는다.

## 이미 실제 개선한 부분 — 반복하지 말 것

앞선 전체 감사/구조 개선/DS 작업은 R 도중 RMB 입력의 공통 buffered 실행 계약, GAS grant·장비/인벤토리 재진입, 애니메이션 수요·layer lease·UI 분리 등을 포함한다. “R 종료 후 새 공격”만 확인한 것으로 축소하지 않는다. 실제 입력→GAS→몽타주→notify→다음 능력 흐름과 공격 DA10개 교정이 근거다. 자세한 단위는 앞선 보고서에 있다.

이번 RA01–27는 RA 이전의 사용자 변경 포함 기준선 대비 **텍스트64개(생산57·시험/fixture4·검증 script3), 기능별 소스25단위**다. 그 이전 DS/SI/리타깃 변경을64개에 중복 합산하지 않는다.

| 개선 | 실제 변경과 유지한 계약 |
| --- | --- |
| UI/미니맵/인벤토리/저장 | actual ASC delegate lease를 게시 전에 확보, FieldNotify 재진입/GC 대응; source revision 공통 판정; UIIconRequest 세대/취소 재사용; reconstruct/release와 final flush 정리 |
| 전투 시각·탑승 | Niagara 이전 map을 분리한 뒤 callback, 공격 instance/tag/mount revision 재확인, 공통 retirement. old callback이 새 세션을 지우지 않음 |
| 이동·지면 접촉 | 공통 제작/runtime finite 계약, CMC5값 tuple 전체 검증 후 게시. GroundContact 비정상 endpoint는 physics query 전에 거절 |
| follower/작은 중복 | 역할 resolver 통합·이름 예외/약한 캐시 제거; five-state 판정은 기존 StateController 함수로 통합; Super만 부르는 ModularMesh override 제거 |
| 종료·play 재사용 | Mass active step 이후 저장소/processor 해제, 실제4 mismatch 수정;10 owner의 새 BeginPlay admission 재개. old token/generation을 부활시키지 않음 |
| GT callback GC | Mass, TargetScoring, VisualAsset, NPCPath, NPCDecision, PresentationBudget의 동기 callback 범위만 실행 참가자 보유. worker snapshot·취소/완료·module join 유지. 모든 강제GC 조합의 재현을 주장하지 않음 |
| MM/GAS 미사용 | 비반영 native cadence getter3개, 호출 없는 early-transition Begin/End/depth/OR, 호출 없는 direct activation loop 제거. 현재 held clip/gait/snapshot revision·실제 pressed/released 경로 유지 |
| 실제 빈 에셋 | 참조/내용을 대조한 빈 장비 DA2개 삭제, 원본 backup 보존.130 DataAsset 개별 valid. dynamic 외부 문자열 참조 부재까지 증명한 것은 아님 |
| 검증 도구 | native process handle을 Wait 전에 확보; host별 DisablePlugins/DisablePython; PoseSearch 비교를 UObject 복제/변경에서 값 배열로 변경하여 검증 부작용 제거 |

삭제한 DA는 `Content/DataAssetSets/Weapons/DA_Player_EquipWeapon.uasset`, `Content/DataAssetSets/Animation_Profiles/Equip/DA_Equip_Player.uasset`이다. backup은 `EmptyEquipmentAssetBackup`, 근거는 `EmptyEquipmentCleanupBefore.json`과 DataAsset validation 요약이다. 자동으로 되살리지 않는다.

유지한 유사 상태도 중요하다. GT command hold/consumed, physical moving-turn readiness, general-turn 후보와 완료 selection feedback, proxy pending/consumed, node별 applied DB, post-selection acknowledgement는 시점·권위가 서로 다르다. “변수 이름이 비슷함”만으로 합치지 않는다. reflection/BP/override/공개 확장 port는 grep 호출0만으로 삭제하지 않는다. MMO port/catalog/loopback은 설치된 live backend가 아니다.

## 검증 기준점과 남은 성능 사실

| 검증 | 최신 실제 결과 | 승격해서 주장하지 말 것 |
| --- | --- | --- |
| Editor/native | 직접 UBT266.99초; WholeRefinement2 총352(332 clean/20 warning), 실패/미실행/inProcess0,665.34초,exit0. 재감사20 clean | 모든 함수·에셋·경고 무해성 |
| PoseSearch | ForwardIndexFixed1–3 새 프로세스1 clean씩, 전체 raw corrupted-index0; Cycle70 index assets/5288poses, Forward14entries | 모든 PSD/Chooser/clip 품질 |
| production package | GameBuild4 203.57초, Cook3 0error/기존1warning, Packaged3 정상. RA27와 RA25 차이는 Editor시험1+network wrapper1뿐 | production 소스가 바뀐 뒤에도 이전 package를 새 검증으로 계산 |
| 기능/UI | CookedDamage3, Reaudit_Combat3 실제 연계3회; Reaudit_UI3 두 해상도 동적 부하 성공 | 모든 장비/몸체·접근성·입력 장치 |
| 실제 소켓 | Network10–12, 실행별 실제localhost2socket+NPC200,lag50ms/loss20%; 세process exit0,Python startup0; 각12 R/target/end 묶음 | 실제200접속·WAN·native Server binary·live backend |
| 실제 렌더 | D3D12 offscreen200표시 body, 동시200montage167frames 평균27.1149ms/36.88FPS,GPU10.6211ms; 복귀219frames15.8822ms/62.96FPS | NullRHI CPU 또는200socket, 가까운 손발/cloth 품질 |
| native Server | direct UBT 정상 exit6,0.80초. 첫 오류 “Server targets are not currently supported from this engine distribution.” | Editor-Cmd server-mode를 독립 Server 배포 성공으로 표기 |

RA13 full binary A/B/B/A64행에서 CPU 평균 증가가 일부 남았다. 별도16 trace region은 반대 방향이고 ABA/MM 실제 수행량도 다르다. 원인은 미확정이다. 최신 Render3는 Render2보다 낮은FPS다. 교대 비교·열·스케줄링·실제 작업량 통제 없이 수정의 회귀나 개선으로 단정하지 않는다. CPU·렌더 성능 해결 완료가 아니다. UI3 GT 평균/p95는1280×720 0.5737/0.8108ms,1920×1080 0.5900/0.8532ms이며 교대 성능 개선 증명은 아니다.

## 새 채팅 시작 절차와 반복 방지

1. 현재 `git status`, branch/HEAD/log와 실행 프로세스를 확인한다. 인계 커밋에서 사용자 후속 변경이 있으면 보존하고 차이를 장부에 추가한다. reset/clean/stash/강제 checkout으로 기준선을 만들지 않는다.
2. 소스650·Editor9·보존13파일을 `RA27SourceHashes.json`, `RA27EditorBinaryHashes.json`, `PreviousAttackAssetHashesFinal.json`과 비교한다. 기존 verifier는 `Verify-CurrentPreservation.py`. 불일치는 곧 손상이 아니며 이후 사용자 변경 여부를 먼저 조사한다.
3. `ContinuationCoverageInventory.json`과 기존 review scopes를 병합해 새 범위 장부를 만든다. 지금607개 모두 whole-file 완료 주장은 false다. 읽은 구현 본문141개와75 named contract 파일을 새로 발견한 것처럼 반복 계산하지 않는다. 부족한 함수/헤더/실패 경로를 구체적으로 좁혀 읽는다.
4. 변경마다 사용자/이전 구현/현재 추가 수정의 출처를 기록한다. 검토만 하고 유지하는 계약도 근거를 남긴다. 상태 소유권과 실제 호출이 바뀌지 않는 파일 분할을 아키텍처 개선으로 과장하지 않는다.
5. 근거 있는 문제부터 기능별로 재현→원천 계약 수정→집중 회귀→필요한 통합 검증을 수행한다. 새 변경/실패/불확실성이 없는 동일 전체 빌드·시험 반복은 피한다.
6. run 이름/출력 디렉터리/소스 및 binary snapshot은 새 버전으로 만든다. RA13/19/25/27 및803파일 portable 묶음은 불변이다. 새 원자료 묶음은 새 폴더를 쓴다.

**중요한 실행 함정:** 기존 `Run-BalancedCPU.ps1`은 finally에서 RA13의 오래된9개 Editor artifact를 복원한다. 최신 변경본에 그대로 실행하지 않는다. 새 교대 harness는 시작 시점의9개 완전한 binary set·modules와 소스 버전을 고정하고 정상 종료 후 그 최신 set을 복원·해시 검증해야 한다. DLL 일부만 교체하거나 실행 중 파일을 교체하지 않는다.

`Run-EditorBuild.ps1`, `Run-Tests.ps1`, `Run-GameBuild.ps1`, `Run-Cook.ps1`, `Run-Stage.ps1`는 Saved의 실제 내용을 먼저 확인하고 새 run 이름을 사용한다. 사용자의 저장/game config는 실행별 UserDir로 분리한다. 시험 자체의 잘못된 World/LocalPlayer/processor root가 제품 수명 결함처럼 보였던 과거 fixture 실패를 반복하지 않는다. report clean이어도 **시험 종료 뒤까지 raw log**를 검사한다.

## 알려진 남은 후보 — 재현과 수정 단위

| 우선 조사 | 실제 시작점·증거 | 원하는 해결/검증 |
| --- | --- | --- |
| 제작 도구 실패 원자성 | `Source/Project_JAnimationNodes/Private/Animation/Project_JStrafeUpperBodyFacingEditorLibrary.cpp`, InstallLowerBodyInMasterForReview는 upper 설치 후 lower topology 확인. 현재 인계에서도 순서를 재확인 | 실제 Master 경로 제한은 유지. read-only preflight와 mutation의 책임을 분리하고 하나의 전체 성공/실패 계약을 설계. transient graph/적절한 내부 seam으로 duplicate/missingpin/connection/compile failure와 재실행 시험. 실패 전후 nodes/links/dirty/생성 상태를 비교; transaction Cancel을 자동 rollback으로 가정하지 않음. 실제 에셋 파손은 아직 재현하지 않음 |
| 동일 정책 follower 교체 | `Project_JCharacterAnimInstance.Budget.cpp::ApplyOptimizationPolicy`는 coordinator 미소유 fallback에서 policy bool changed 조건으로 tick 적용 | 같은 policy·새 follower, 초기Hidden/disabled, source/body교체·load/attach·coordinator enable/disable를 실제 인스턴스로 재현. policy값뿐 아니라 적용 대상/수명을 계약에 포함. coordinator와 이중 write를 만들지 않고 매frame blanket reset도 금지 |
| malformed 숫자 | WeaponPresentation::BeginIndependentMotion/SetIndependentMotionPosition와 WeaponMotion helper·notify/preview, OneShotInputResponse 및 제작 validators | NaN/Inf/time순서/범위/quaternion·transform/overflow/blend-duration/IKalpha 등 의미별 계약 대조. 정렬 전에 finite 검증, 부분 게시 차단, preview/runtime/authoring 일치. 현재 모든 에셋이 잘못됐다고 단정하지 않음. 기본값 임의 변경 금지 |
| 공유 notify 수명·동시 시작 비용 | ComboWindow/MeleeHit/WeaponMotion/GroundContact/CombatPresentationCue의 per-mesh/token weak table Begin 순회 | trace로 방문량·등록/정리·GT ability/weapon spawn/notify 비용 분리. overlap/reentry/end누락/interruption/mesh destruction/worldshutdown/GC를 재현. 수명/토큰을 합치거나 다른mesh를 지우지 않음. O(n²) 후보를 측정 없이 CPU 원인으로 확정하지 않음 |
| CPU 증가와 렌더 차이 | 기존64행ABBA·16regions 및 Render1–3 raw/phase | 최신 수정본과 명시적 기준본의 같은 조건 반복/교대, warmup/순서/품질/ABA/실제MMcalls/montage수를 맞춤. GTpre/MM/post/retarget/ABA/rigidbody+notify/ability/weaponspawn+workerwait 단계. inclusive 합산과 worker CPU를 wall critical path로 오해하지 않음 |
| GameplayCue 검색 | Cook 기존 NotifyPaths 미지정 경고 | 실제cue asset registry·tag/provider·hard/soft closure와 cooked검색 대조 후 범위 결정. 임의경로 지정으로 경고만 없애지 않음 |
| callback GC 확장 | RA20/21 current contracts와 native regression | 아직 직접 시험하지 않은 decision/presentation·interface callback 등에서 worldcleanup/GC/cancel/reentry/latecompletion 조사. fixture 자체 root가 제품 lease 증명을 대신하지 않게 설계 |

이 표 밖의 결함을 계속 찾는다. 확인되지 않은 후보를 먼저 대규모 refactor하지 않는다. 현재 잘 동작하는 공통 계약이 있다면 그것을 사용하는 방향으로 정리하고 새로운 추상화·cache·manager는 소유권/일관성 개선 근거가 있을 때만 추가한다.

## 전체 범위 누락 점검 — 실제 시스템에서 확장

| 시스템/경계 | 상세 확인할 축과 관련 에셋 |
| --- | --- |
| 엔진/모듈/설정 |8project modules·targets·Build.cs·plugin enable/loadingphase/editor guards·startup/shutdown/CVar/config defaults. disabled experiments는 미사용과 별개로 제작 도구 범위에 표시. platform/Shipping/Server/cookability 의존성과 순환 확인 |
| Game/입력/Avatar | GameMode/State/Controller/PlayerState/LocalPlayer·possess/unpossess/respawn/travel·ASC Avatar actor info·IMC/IA/mapping lease/chord/press/release/device/focus/UI capture. 실제 BP defaults/SCS/ConstructionScript overrides 대조 |
| 이동/MM/리타깃 | CMC predicted savedmove/server replay·GTcommand/proxy worker/evaluate·Chooser row precedence/null output·PSD/PSS/normalization/index coverage·trajectory/history/warping/land/turn·source/body/IK/rigidbody/cloth. 동일값을 가진 다른 수명/권위 상태 구분 |
| 전투/GAS/장비 | input intent/ability grant-remove/attack instance/notify-window/hitduplication/targetscoring/tag/cue·equipment transactions/inventory revision/mesh socket/weapon motion/손접촉·실제DA/GA/GE/montage/data binding. cancel/end/death/unequip/late server correction |
| 캐릭터/스킬/퀘스트/사회 | progression/attribute/effect/status·trusted quest objective/reward·party/guild/social authority와 PS projection·DT/catalog/profile/IDduplicates/versioning·owner-only/private/public replication·malformed save/reentry/partial failure |
| NPC/Mass/탑승 | activation/decision/action/path/scoring/AI controller/NavMesh·rider/mount authority/possession/route/demote-promote·copied workers/canceljoin/epoch/storage/control restoration. 실제NPC/mountBP·profiles/navmap. 기존200 CPU fixture는Mass opt-in 사용 증거가 아님 |
| UI/오디오/VFX/품질 | viewmodel revision/delegate/widget reconstruct/icon load/settings/save/input routing·UI profile/texture/font/localization/accessibility·Foley/cue/attenuation/pool/mixer·Niagara/material/LOD/bounds/visibility. local split/player 소유권·close-up 손발/의상/cloth와body/weapon조합 |
| 네트워크/backend/MMO | replication/FastArray/AOI/Iris 실제등록/RPC validation/rate budgets·server authority/prediction/reconcile·request outcome vs transport success/idempotency/timeout/cancel/retry/backpressure·repository/handover persistence/disconnect/reconnect·실제adapter vs extension port 구분 |
| 제작/시험/배포 | editor install/migration/restore receipt/preflight/undo/dirty/save/compile·fixture isolation/원자료·asset manager/soft reference/cookmaps/external actor/pluginclosure·missing/redirector/packagedload·모든 변경 실패 복구. Cook통과와CookAllunused closure 구분 |

Content 최상위에는 애니메이션/캐릭터/DataAsset/Input/UI/Audio/Mount/NPC/Level 외에도 variants, developers, externalactors/objects, materials/effects가 있다. 디렉터리 이름만으로 관련 없음/삭제 대상으로 판정하지 않는다. 시작map·GameMode·PrimaryAsset/catalog·실제BP/ABP/DA/DT/Chooser/IA/IMC에서 참조를 따라가고 source/config의 soft string·dynamic runtime 참조와 합친다. 현재도달, extension/authoring, sample/disabled, 불명으로 분류하며 모든 분류에 근거를 남긴다. 플랫폼 제외나 engine/generated 파일은 제외 이유를 기록한다. 런타임 로그만으로 제작 에셋을 누락시키지 않는다.

MCP가 실제 binary asset 연결 확인에 필요하면 공개 도구로 좁게 조회한다. 대량 inventory/schema 조회가 필요하면 먼저 간단히 알린다. 이미 성공한 schema를 반복 나열하지 말고 실제 changed graph/row/default를 대조한다. BP/CDO/template/runtimeinstance는 서로 다른 증거다. private 필드 접근 거절은 Python reflection 등으로 우회하지 않는다. 공식 BlueprintTools.get_parent와 공개 native API 등 독립 허용 경로가 있으면 그 범위로 검증한다. MCP 연결 실패/지원부재는 성공으로도 asset부재로도 해석하지 않는다. 기존 Session3은 정상 종료되어 token을 재사용하지 않는다.

## 검토 장부·완료 기준

소스 파일마다 읽은 함수/헤더/inline/조건부 컴파일 범위, actual caller와 reflection/BP entry, 상태의 작성자/독자/권위/수명, 관련 에셋과 실패 경로, 근거 위치/해시/실행 receipt를 기록한다. 여러 함수가 같은 계약이면 그룹 검토가 가능하지만 그룹에 포함된 범위를 명시한다. 파일 수607나2738을 검토완료 수로 출력하지 않는다.

에셋별로 object path/type·parent/CDO/runtime override·referencer/softdependency·소유 시스템·제작 validator·Cook/package load·품질 검토·미지원 항목을 기록한다. 기존130 DA validator와 이번 MCP8 성공 등은 해당 항목의 증거로 이어받고 전체 asset graph를 완료 처리하지 않는다.

발견별 표준 행은 `ID / confirmed defect·potential risk·proposal / trigger / source+asset evidence / owner+contract / actual change or retained reason / focused failures / integration version / raw evidence / residual limit`다. 상태는 **실제 수정·검증**, **범위 명시 검토 후 유지**, **미완료 검토**, **환경/접근 때문에 미검증**, **범위 판정/제안**으로 구분한다. 미검증과 미착수를 섞지 않고 차단 사유·남은 확인 방법을 적는다.

완료 보고 전 다음을 대조한다.

- 현재 시스템 목록과 소스/설정/descriptor/에셋 inventory의 미분류·상세미검토 항목을 찾아 닫거나 구체적 제한으로 기록한다. 새로 발견된 시스템/분기는 장부와 테스트 범위에 추가한다.
- 확인된 결함은 기능별 원천 계약에서 해결하고, 기존 정상 흐름과 새 실패 경로의 회귀를 연결한다. 강제reset/특정키·클립예외/새timer로 증상만 가리지 않는다.
- worker UObject 접근·snapshot GC rooting·task cancel/actualcompletion/module join·authority/prediction·reentry/partialfailure가 새 수정으로 깨지지 않았는지 확인한다.
- 변경 위험에 맞는 directUBT/집중native/필요한 전체native·실제에셋·Game/Cook/package·remote·render/quality/load를 완료한다. 결과가 같은 소스/asset/binary 버전인지 확인한다.
- CPU 증가 등 열린 측정 문제는 원인을 해결하거나 조사 증거와 잔존 영향/환경 제한을 남긴다. 원인 미확정을 해결로 쓰지 않는다.
- README/관련 기준·제작 문서/구조지도/발견 장부/최종변경 이유/원자료 manifest/문서 catalog를 최신 상태에 맞춘다. 실제 수정·유지·미검증을 각각 집계한다.

live HTTPS backend/실제지속저장/분산handover/WAN/200socket/nativeServer 운영은 현재 검증되지 않았다. 설치 엔진의 Server 지원 제한을 우회하거나 유료/외부 운영 인프라를 임의 구축하지 않는다. 환경이 없어 실행할 수 없는 영역은 계약/실패 fixture를 조사하고 실제환경 미검증을 남긴다. 따라서 전체 범위 감사를 정리하더라도 모든 배포환경의 무결성 보장은 아니다. 검토 자체가 남아 있으면 전수 정밀 감사 완료라고 하지 않는다.

## 보존 항목과 새 결과 기록

Body ABP SHA256 `27283D5F8A7F9F5B67FD493EF0D5CCBCCC4B75CE640E2264B3B02D739A995D9E`, RTG SHA256 `74E3E616BC099FFBB1104B8EDB18FE79ED59569D8D4B324CD680C22D38FE9982`, 기존10월13일 인계 SHA256 `DB19C60AE025D12FB2C5EBB2EDB084A113F543CD7D881AA4A81F2225BE174315`를 인계 시 재확인했다. 공격DA10개도 기존 교정 이후 해시와 일치했다. 소스650·Editor9 mismatch0. 기존 인계의 오래된 HEAD/커밋 지시는 이 문서의 최신 사용자 요청과 실제 Git 상태보다 우선하지 않는다.

OTM/Strafe, Run/Sprint, 단발/MM 복귀·Land·측면 후보·전투·장비·리타깃을 보존한다. 지속 곡선 **0.35초·100cm·3도·85%**를 임의 변경하지 않는다. 보존 대상의 의도적 추가 개선이 필요하면 현재 해시를 새 기준으로 무조건 덮지 말고 원인·전후·검증을 기록한다.

다음 결과는 새 날짜/버전 문서와 run으로 작성한다. 실제 새 소스/에셋 변경, 검토 후 유지, 아직 미검증/미완료 범위를 별도 표로 보고한다. 기존 실패 로그·첫 compiler 오류·unsupported MCP 응답·warning은 삭제하지 않는다. portable 원자료에는 Config token·개인 SaveGame·ephemeral MCP token·비관련 사용자 자료를 넣지 않는다. 최종 Git status와 실행 프로세스·LFS 실제 내용도 확인한다.

## 새 채팅에 붙여 넣을 요청문

```text
C:\Users\I\Documents\GitHub\Project_J\Docs\Handoffs\WholeProjectAudit_2026-10-14\Project_J_Complete_Audit_Continuation_2026-10-14.md

이 인계와 최신 검증·상태 지도·상세 장부·ContinuationCoverageInventory.json을 읽고, 이전 작업을 보존하면서 남은 전체 감사와 개선, 전체 범위 누락 점검을 하나의 계속 작업으로 끝까지 진행해줘.
현재 Git/프로세스/인계 커밋/해시부터 확인하고, 이미 구현·검증한 일을 반복하지 말아줘. 모든 시스템의 소스 함수·헤더·inline·실제 호출·상태 권위/소유권·관련 BP/ABP/DA/DT/Chooser/IA/IMC와 제작/검증/배포 경계를 상세히 대조해줘. 파일 목록이나 자동화 성공을 전수 정밀 완료로 해석하지 말아줘.
남은 후보 목록에 제한하지 말고 실제 시스템·의존성·실행·실패 흐름에서 새 누락/공통 결함을 조사해줘. 근거 있는 중복·불필요 로직·이중 상태·예외·확장 구조 문제는 직접 정리하고, 유지할 이유가 있는 계약도 기록해줘. 기능별 재현·수정·회귀·필요한 통합/원격/품질/부하/Cook 검증까지 수행해줘.
이 작업에 필요한 Unreal MCP·컴퓨터 사용·로그 수집·빌드/재빌드·관련 코드/에셋 수정·필요한 저장을 허용해. 허용 범위를 반복 확인하지 말고 접근 제한을 우회하거나 관련 없는 사용자 작업을 덮어쓰지 말아줘. AGENTS.md의 directUBT·프로세스 겹침 금지·빌드 중단 금지를 지켜줘.
OTM/Strafe·Run/Sprint·단발/MM 복귀·Land·측면 후보·전투·장비·리타깃과 지속 곡선0.35초·100cm·3도·85%를 보존해줘. 강제reset·timer·특정키/클립 예외로 증상을 가리지 말고 GT/worker·snapshot·GC·취소/완료·서버 권위/예측과 맞는 원천 계약에서 해결해줘.
CPU 증가와 렌더 차이는 아직 해결되지 않았어. 같은 조건의 반복/교대 비교와 단계별trace로 조사하고 NullRHI CPU·렌더FPS/GPU·200표시캐릭터·실제socket수를 구분해줘. 오래된 RA13 binary를 복원하는 Run-BalancedCPU.ps1을 최신본에 그대로 실행하지 말아줘.
한국어로 전체 범위 장부·구조지도·발견/최종변경 이유·검증/성능원자료·남은문제를 남기고 관련 기준/제작 문서·README·catalog도 갱신해줘. 실제 수정·범위 명시 검토완료·미완료 검토·환경/접근 미검증을 분리해줘. 전체 검토가 남아 있으면 완료라고 말하지 말아줘.
인계 전에 요청한 기준점 커밋은 실제 Git에서 확인해줘. 이후 추가 커밋·push·병합·컴퓨터 종료는 별도 최신 요청으로 남겨줘.
```
