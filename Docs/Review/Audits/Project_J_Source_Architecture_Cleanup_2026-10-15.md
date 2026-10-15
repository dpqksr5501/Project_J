# 소스·아키텍처 정리 — 2026-10-15 범위 변경

후속 PC 범위의 [실제 소스·BP·DA 정리와 최신 검증](Project_J_PC_Structure_Cleanup_2026-10-15.md)을 함께 읽는다. 아래는 `f8ebd8a5` 이전 정리의 당시 범위·근거이며 후속 변경의 검증으로 승격하지 않는다.

**현재 기준점:** 기존 전체 시스템의 명시 native 본문·header/inline·주요 caller/상태 소유권 근거를 통합하고, 지목됐던 본문 공백을 추가 대조했다. 아래12묶음 구조 정리를 반영했고 최종 Editor 빌드·집중57개(55clean/동일기존warning2)·실제 UI BeginPlay 화면 생성 실패→EndPlay 경계1개가 통과했다. 전수 BP/환경 및 보류한 성능·운영·품질 감사 완료를 뜻하지 않는다.

사용자의 최신 요청에 따라 우선 목표를 반복 버그 수정/리팩터링으로 복잡해진 **소스와 아키텍처 정리**로 변경한다. 종전 전체 성능·운영·품질 감사의 모든 문제 해결은 이 작업의 완료 조건이 아니다. 이전 기록·검증·코드·사용자 변경을 보존하고 실제 호출과 상태 소유권을 기준으로 판단한다.

## 현재 작업과 완료 조건

- 전체 시스템의 실제 caller·header/inline·상태 권위/소유권을 기존 읽기 범위와 대조한다. 명시 범위 밖/관련 BP·데이터 미확인은 유지하고 파일 연결 수로 전수 완료를 주장하지 않는다.
- 중복 구현, 불필요 분기/상태/예외, 미사용 private 코드, 책임 혼재와 확장 구조에서 근거가 있는 부분을 직접 정리한다. 파일 분할·새 추상화 자체를 개선으로 집계하지 않는다.
- public/BP reflected entry·serialized/default 필드·server/prediction·GT/worker/snapshot/GC·취소/physical 완료 계약은 호출/에셋 근거가 없으면 제거하지 않는다.
- 코드 정리 안전성에 필요한 지정 BP/반영 호출·데이터 연결만 최소 조회한다. 변경마다 빌드와 영향 기능의 집중 회귀를 한다. 통합·Cook은 변경 위험상 필요할 때만 수행한다.
- 결과는 **실제 구조 정리 / 검토 후 유지한 이유 / 남은 위험·접근 미확인**으로 나눈다. 모든 예전 결함 후보를 수정해야만 끝나는 작업으로 확대하지 않는다.

## 보류한 범위

CPU/GPU 원인 조사, 반복/교대 성능 비교와 단계 trace, 대규모 부하·원격·시각 품질 전수 검증을 보류한다. 이전 CPU 증가와 렌더 차이는 해결됐다고 하지 않는다. NullRHI world/processCPU·렌더FPS/GPU·표시200·실제socket2를 구분한 기존 원자료와 위험은 [종전 감사](Project_J_Continuation_Audit_2026-10-15.md) 및 `ContinuationWorkQueues.json`에 보존한다. 오래된 RA13 binary 복원 스크립트를 실행하지 않는다.

## 기준점과 진행 방식

HEAD는 `855a1c8e111c94a5dffdd08ad05c68bdea2accec`, branch는 `codex/runtime-retarget-profiles`이며 추가 commit/push/merge/종료 요청은 없다. 사용자 인계 trailing-blank diff와 Content/기준13파일을 보존했다. CA17–18 전체389·Game/Cook/package/remote 결과는 이전 통합 기준점이며 이후 수정의 결과로 승격하지 않는다.

범위 변경 당시 진행하던 native MM/Chooser 안전성 묶음은 normal build와 영향 집중 회귀로 마무리했다. 그 뒤 읽기 전용 병렬 담당은 기존 소스 대조를 재사용해 정리 후보를 좁히며, 주 작업자만 코드/에셋 수정·MCP·빌드/검증을 수행한다. direct UBT·프로세스 중복 금지·실행 중 빌드/검증 정상 종료를 지킨다.

범위 변경 시점 예상은 약 **4~8 작업시간**이다. 이는 코드 정리와 필요 BP 연결·집중 검증의 예상이며, 보류한 성능/운영/품질 문제 전체 해결 시간은 포함하지 않는다. 오늘23시에는 새 작업을 멈추고 실행 중 빌드/검증은 정상 종료 후 이어갈 상태를 남긴다.

## 분류 장부

| 분류 | 현재 근거와 상태 |
|---|---|
| 기존 실제 구조 정리 재사용 | CA16 resolver 동일 owned set의 gather→재순회를 단일 순회로 정리, CA18 방향 bool/누적 sequence를 bounded held-order 하나로 정리. 기존 검증을 반복 집계하지 않음 |
| 범위 변경 전 진행 수정 | CA19 mount 전환/restore/phase·controller current owner 경계, 집중24개 23clean/기존warning1 통과. MM/Chooser 경계도 집중 회귀 통과 |
| 새 구조 정리 | 아래12묶음 실제 반영·세 담당 교차 검토·Editor/집중57+UI수명1 통과 |
| 유지 계약 | reflected BP entry/serialized legacy fields, PS 권위와 avatar 표현 lease, logical 취소와 physical 완료, fallback와 제작 정책은 이유를 개별 기록 |
| 남은 위험 | 기존 실행 미재현 후보·에셋/환경 미확인, CPU/렌더 미해결은 별도 보류. 코드 정리의 안전성에 직접 영향하는 위험만 이번에 추가 확인 |

OTM/Strafe·Run/Sprint·단발/MM 복귀·Land·측면 후보·전투/장비·retarget 및 지속 곡선 .35초/100cm/3도/85%를 보존한다. 새 force reset/timer/특정 키·클립 예외로 증상을 가리지 않는다.

## 전체 시스템별 구조 대조와 유지 결정

| 시스템 | 실제 호출/상태 소유권 기준 | 정리·유지 판단 |
|---|---|---|
| Player·Progression·GAS | PS persistent ASC/장비·level 권위→avatar 초기화·runtime projection→input/ability | Player 생성자의 더 이상 실행되지 않는 로컬 ASC/장비 초기화와 미사용 private bool 제거. Base의 NPC-local 지원·PS projection/restore·avatar current epoch는 유지 |
| 입력·전투·UI | input origin/held-order→execution alias receipt→ASC spec/prediction→ability end; UI quickslot은 자기 press receipt만 소유 | CA18 단일 방향 상태 재사용. UI는 callback 전에 이전 receipt를 분리하고 실제 EndPlay 첫 callback 전에 기존 종료 barrier 게시. press의 inactive activation과 release generic event를 같은 분기로 합치지 않음 |
| Inventory·Equipment | inventory bag/lock/FastArray 권위→manager commit→runtime grants·visual lease→owner UI | Consume을 기존 Add→Set mutator로 통일. 서버 내부 신뢰 호출·correlated 요청·legacy void RPC의 정책 차이는 임의 통합하지 않고 위험으로 유지 |
| HUD·ViewModel | local UI status/서버 pending receipt·model subscription→screen 표시 | 화면 재초기화 전에 이전 model split 구독 해제. source revision/entry·pending 요청은 별도 수명이라 유지. HUD 직접 status 작성3경로는 Model.SetLocalStatus로 귀속하고 pending 서버 상태를 보존 |
| 탑승·비행 | common interaction→eligibility→actor rider/possession·flight phase, rider component→원래 ASC tag/layer | private eligibility 전달만 제거하고 공통 승인 직접 호출. helper 미사용 인자 제거. collision/cue·CMC/phase·health compatibility mirror는 다른 책임이므로 유지 |
| NPC·AI·Mass·Path | copied GT capture→bounded worker→GT current revision/MoveId/ability, Mass lease→단일 CMC writer | state/enabled/owned attack/revision과 logical cancel/physical completion은 역할이 달라 유지. 기존 CA15/17 수명 수정 재사용, 성능/운영 설치 조사는 보류 |
| Animation·MM·retarget | GT owner/cache→frozen proxy snapshot/GC pins→worker search/pose→GT feedback | Retarget 중복 기본값 제거·header 단일 기준, 무동작 SprintLanding helper 제거. authored timer/phase acknowledgement·empty BP TIP entry·property-access mirrors는 유지 |
| Core·network·async | physical task slot/논리 pending·message snapshot·asset handle·input mapping lease | pool 중복 검사만 제거. 공개 registry/custom replication helper를 native caller 수만으로 삭제하지 않음. 실제 Iris 설치·서비스 port와 정책 정의를 구분 |
| Backend·handover·Quest·Social | packet/attempt/request tracker→checked apply/owner commit·XP 지급→projection | Quest 중복 재진입 검사 제거. checked result와 legacy void/bool wrapper는 공개 의미가 달라 유지. 사회/퀘스트 authority와 UI pending을 하나로 합치지 않음 |
| 제작·Editor·실험 | runtime/Editor/UncookedOnly 모듈 경계·validator·명시 migration/save/compile·disabled plugin | Max attribute 중복 진단 제거. public graph/serialized legacy 필드 유지. transaction rollback·순차 save·disabled GPU poll 등 기존 위험은 별도 기록; 전수 해결로 확대하지 않음 |

이 표는 실제 본문/호출 대조와 유지 결정을 설명한다. `AdditionalReadScopes.json`의 정확 manifest와 기존 CR 단위를 근거로 사용하며, 장부 미연결0이나 자동화 Success를 모든 함수/에셋/환경 완료 판정으로 사용하지 않는다. 변경한 공개/직렬화 이름·기본값은 보존했고, 삭제 대상은 비반영 private 구현·무효 상태·동치 분기다. BP 참조 근거가 없는 reflected entry는 삭제 대상에서 제외했다.

### 첫 구조 정리 묶음의 교차 검토

읽기 전용 세 담당이 현재 diff를 재대조했다. Player constructor null 전제, pool 조건의 정수 동치, Max attribute 승인 집합, Retarget 16값의 완전 일치, Quest guard의 callback 없는 함의 관계, Consume의 int64/authority/lock/publication 공통 경로, 이전 HUD model 구독 해제를 확인했다. 제품 동작 계약 차이는 발견하지 못했다. 새 시험의 delegate 형식·데이터 준비·LocalPredicted 전제는 시험 자체 교정이며 제품 결함으로 집계하지 않는다. 실제 회귀 결과는 아래 최종 receipt에 별도 추가한다.

## 실제 정리한 구조와 최종 검증

| 정리 묶음 | 결과와 근거 |
|---|---|
| Player 소유권 잔재·Retarget 기본값·Sprint no-op | 실행되지 않는 로컬 ASC/장비 초기화와 미사용 private bool 제거. Retarget16개 중복 대입→header 단일 기본값. no-op SprintLanding helper 제거. reflected BP TIP entry·재초기화·landing 완료는 유지 |
| 공통 변경/승인 경로 | Consume→기존 Add→Set으로 통일. Quest 중복 재진입 분기, pool 정수 중복 검사, Max attribute 중복 진단 제거. int64/lock/authority·delegate snapshot과 승인 집합 보존 |
| Mount/Flight 전달 계층 | private eligibility wrapper와 미사용 tag helper 인자만 제거. 공개 승인·K2/OnRep 의미·phase/CMC·collision/cue·health mirror는 유지 |
| UI 입력·종료 | 단일/전체 해제가 원래 map/tag/pawn receipt를 callback 전에 소비한다. 실제 GAS 종료 callback의 같은 slot 새tag 입력을 이전 정리가 지우지 않는다. 실제 EndPlay가 신규 입력을 받지 않도록 기존 bEnding을 첫 callback 전에 게시하고 operation pin 유지 |
| HUD/ViewModel | 이전 model split 구독을 교체 전에 반환. 세 안내/취소 status writer를 Model에 귀속해 pending 서버 상태를 보존. unrelated subscriber·새 model·BP read/OnChanged 계약 유지 |
| 생성 데이터/확장 구조 | MMO JSON→inl/MD205개 일치 및 module DAG default validator 통과. generated 파일을 독립 작성 권위로 바꾸거나 runtime JSON 로딩/새 manager를 추가하지 않음 |

`R15_StructureCleanupFinalBuild1`은 full Editor direct UBT4actions/8.90초 정상 종료다. 큰 header 묶음의147actions/320.92초는 새 시험 delegate 타입 오류로 정상 실패했고, 그 후 같은 full target4actions/10.78초로 잔여를 완료했다. 마지막 UI 소유권 묶음21actions/42.58초와 종료 순서7actions/13.91초도 정상 종료했다. 빌드/검증을 중단하거나 겹치지 않았다. 기본값·단순 분기 정리는 동작 집합을 정적으로 교차 확인하고 관련 기존 회귀를 재사용했다.

`R15_StructureCleanupFocused2`는57개=55clean/기존warning2, 실패/미실행/진행0·exit0이다. 기존 전체389 중 영향48개+신규9개이며 나머지341개는 이 집중 실행 범위 밖이다. 전체 회귀 미실행을 누락/실패나 전체 통과로 해석하지 않는다. 경고는 `TwoHandIKTransitionAndCurve`의 빈socket, `PossessionAndBlockedExit`의 native profile 없음 각1개로 기존 원문·multiset이 동일하고 새 warning은 없다. 추가 `R15_UIScreenBoundaryFocused1`1개는 실제 UI BeginPlay의 screen creation failure→종료 구독 반환을 확인했다. 합계58개의 서로 다른 영향 시험이며 단발/MM·지속 곡선 관련 기존26개 성공 근거도 재사용한다.

원본 UI 단일/전체 해제의 새 receipt 소실과 실제 EndPlay 신규입력 잔류는 각각 실제 GAS callback으로 재현했다. UI fixture는 persistent PS ASC/current avatar·LocalPredicted controller를 확인하고 실제 component lifecycle을 사용한다. screen/catalog 초기화 없는 상태의 owner 경계와 별도 screen-failure 사례다. 다른 pawn·실제 profile catalog 교체/GC·원격 전체 완료가 아니다. Nullable/Chooser 원본의 AV/무한루프를 실행하지 않았고 source/engine one-hop 근거와 수정 후 native fresh-feedback·self/cycle/null/정상chain/64/65 경계를 시험했다. 초기 abstract spawn·normalization membership·controller 준비/서명 오류는 제품 실패로 집계하지 않는다.

최종 `R15_StructureCleanupCompiled2`/`R15_StructureCleanupVerification1`은 runtime Source/Config/Plugin/descriptor649개와 Editor9개 hash/파일집합 차이0, 보존13 mismatch0, 기존 사용자 인계 diff 일치, Content tracked/untracked 차이0, actual HEAD/branch 유지·호스트 관련 프로세스0을 확인했다. SourceHashes720에는 검증 도구 Scripts71개도 포함되며 runtime649와 구분한다. 추가 commit/push/merge/컴퓨터 종료는 하지 않았다.

Game/Cook/package·remote의 마지막 전체 배포 기준점은 CA17–18이다. 이번에는 모듈/반영 이름·기본값·에셋/Config/packet schema를 바꾸지 않고 local owner 경계를 정리했으므로 Editor와 영향 회귀를 검증 기준으로 삼았다. 새 Game/Cook/대규모 원격·품질 실행을 이 결과에 합치지 않는다. 필요한 추가 배포 검증은 해당 변경 위험/배포 시점에 판단한다.

원자료는 [최종 검증 receipt](ContinuationEvidence_2026-10-14/Receipts/Saved/Validation/Continuation_20261014/R15_StructureCleanupVerification1/StructureCleanupVerification.json), [실제 정리/유지/남은 위험](ContinuationEvidence_2026-10-14/Receipts/Saved/Validation/Continuation_20261014/R15_StructureCleanupVerification1/StructureCleanupSummary.json), [읽기 범위 마감 근거](ContinuationEvidence_2026-10-14/Receipts/Saved/Validation/Continuation_20261014/R15_StructureCleanupVerification1/ReadOnlyReviewClosure.json)와 정확 manifest에 연결한다. 구체적 unresolved source 후보는 기존 `ContinuationWorkQueues.json`에 보존하며 이번 목표의 무제한 버그 전수 해결로 확대하지 않았다.
