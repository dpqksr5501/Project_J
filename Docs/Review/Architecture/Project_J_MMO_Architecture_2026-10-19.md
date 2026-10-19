# MMORPG 아키텍처 검토와 현재 실행 경계

**앞 단계 검증 이력:** 로컬 Progression/Quest 체크포인트의 Editor/Game·native110·멀티플레이·Cook/Stage·패키지 재실행과 교차 복원을 확인했다. 당시 자동 journal 복구는 실패했고 별도 Server target은 설치 엔진에서 차단됐다. 아래 프로세스 수·지문·정리량도 당시 시점의 기록이다. 현재 작업 완료 여부는 다음 링크의 잔여 상태표를 따른다.

현재 후속 요청의 단일 잔여 판정은 [남은 아키텍처 작업](Project_J_MMO_Remaining_2026-10-19.md)에서 갱신한다. 이 문서의 앞 단계·후속 단계 빌드/110개 시험·정리 수치는 당시 입력의 **검증 이력**이며, 새 변경의 최종 결과가 아니다.

## 입력·보존·범위

`Project_J_MMO_Architecture_6850ed45.zip`의 README, 종합, backend, runtime, social, operations 6개 문서를 모두 읽고 현재 작업 사본과 대조했다. ZIP은 `6850ed4556b5df0468d6809fd190950e1b1a4c82`의 설계 평가다. 이번 기준 HEAD는 `4fe5a732fe42cef6a3c54f7db20996c4cceccd0f`, main, UE 5.8 / Win64다. Editor·Game·Server target이 있다. 아래의 계약·프로토타입·미연결·미구현은 구현 완료와 구분한다.

시작 시 tracked 삭제 5,126건, 수정 2,448건, untracked 파일 2,245건이 있었다. 환경 에셋 이동·월드 문서·생성 스크립트와 기존 테스트 변경을 포함한다. reset/clean/stash/commit/push/PR/배포를 하지 않는다. 기존 파일 상태는 `Saved/MMOArchitecture_20261019/InitialGitStatus.txt`, 사용자 소스·문서 지문은 `PreservedUserFiles.json`, 자산 메타데이터는 `ContentBeforeValidation.json`에 보존했다. 원본 버그 감사의 [28개 마감](../Audits/Project_J_Source_Audit_6850ed45_Closure_2026-10-19.md)은 과거 검증 근거이며 이번 실행 결과를 대신하지 않는다.

일반적인 설계 선택은 현재 사용 중인 경계의 결함을 우선 고치는 것으로 정했다. 기존 PS/ASC·Avatar·Motion Matching/Chooser/Blend Stack과 안정 ID를 보존한다. 모듈을 대량 분리하거나 카탈로그 기능마다 새 서비스를 만들지 않는다. 운영 인증·DB·경제 규칙·실제 존 이전은 아래 조건을 충족할 때 도입하며 이번 완료에 합산하지 않는다.

작업 중 별도 변경으로 HEAD가 `45f55444c9ca764386b8867a1063d7ee6025a5f3`으로 바뀌었다(13:28 KST, 환경 에셋 통합 커밋). 이 작업에서는 commit/reset/clean을 실행하지 않았다. 이후 시작 시 관측한 `Content/World/MainWorld/Source` 원본 41개가 현재 경로에서 없어졌으며 폴더 수정 시각은 13:30:20 KST, Python 회귀 시작은 13:30:22 KST다. 사용자가 **다른 작업에서 이동·정리 중**이라고 확인했으므로 해당 변경을 보존하고 원래 경로를 임의 복원하지 않는다. 따라서 전체 Content가 시작 상태와 동일하다고 보고하지 않는다. 별도 커밋에 포함된 기존 테스트 4개를 포함해 EditorBuild3 직후 컴파일 입력 676개와 Source 지문이 일치했다. 이후 같은 테스트 파일의 복제 인덱스와 월드 수명 구성을 수정하고 EditorBuild5 및 GameBuild1을 완료했다. 최종 Source 676개는 EditorBuild5 입력과 일치하고, 보존 대상으로 기록한 기존 사용자 소스/문서 99개도 일치한다.

## 이번 변경과 선택 이유

| 변경 | 권위·소비 경로와 대안 판단 |
| --- | --- |
| 장비 상세/공개 복제 분리 | `EquipmentArray`는 owner-only, `PublicEquipmentArray`는 skip-owner다. 공개 항목에는 슬롯과 정의만 있고 인스턴스 GUID·수량·잠금·가방 순서가 없다. 서버의 기존 장착/해제 commit에서 투영을 먼저 갱신하며 독립 writer/API를 만들지 않는다. Runtime은 같은 조회/이벤트 계약으로 외형을 소비하고 서버만 GAS를 부여한다. FastArray가 자체 delta 순서·late join snapshot을 소유하므로 별도 timer/revision 동기화 상태를 중복 추가하지 않는다. 공개 정의는 정적 제작 데이터이며 개별 소유 아이템은 아니다. |
| PS 수명의 공통 명령 예산 | Core의 `CommandBudgetComponent`를 PS에 두고 장비·가방·소모품·퀘스트·SSR 요청 입장에서 소비한다. Avatar가 교체돼도 예산이 유지된다. 도메인의 20/s 또는 SSR 32/s 제한은 보존한다. 직접 서버 mutation은 소비하지 않는다. 기본 총 80/s는 튜닝값이며 처리량 실측이 아니다. 모든 RPC·계정·IP·분산 quota를 구현했다는 뜻은 아니다. 이동/표현 RPC를 같은 예산에 넣어 정상 고주기 이동이 전투를 굶기게 하지 않는다. |
| 가방/소모품 요청 ID와 명령 결합 | 최근 64개 receipt가 결과뿐 아니라 원래 요청을 보존한다. 같은 ID/같은 요청은 원래 결과, 같은 ID/다른 요청은 Invalid다. 이미 보존된 결과를 새 payload로 바꾸지 않는다. 세션 캐시는 내구성 경제 receipt가 아니며 eviction 이후 영구 멱등성은 보장하지 않는다. |
| 소셜 접속 교체 후 쓰기 철회 | 명시적 서버 Bind가 새 PS를 선택하고 이전 PS의 투영/변경 자격을 철회한다. 늦은 Leave/Create/Restore/Unbind가 새 접속의 상태를 바꾸지 못한다. 새 PS가 나간 뒤에도 옛 PS가 자동으로 권한을 회복하지 않는다. 약한 참조를 정리해 폐기 객체를 붙잡지 않는다. 이는 프로세스 내부 소셜 binding이며 인증·캐릭터 소유권·다중 존 fencing과 다르다. |
| Gateway 크기/route/intent 경계 | 발송 전 route와 UTF-8 바이트 수를 검증한다. 응답은 엔진 HTTP 수신 stream에서 한도를 검사한 뒤 저장하며 초과 chunk/부분 업무 응답은 반환하지 않는다. worker는 바이트만 다루고 완료는 GT다. 발송된 mutation의 응답 초과/유실은 UnknownOutcome과 같은 키 reconciliation 의무를 유지한다. 통신 성공을 업무 commit 성공으로 바꾸지 않는다. |

기존 반영 클래스·필드·BP 호출 이름을 이동/삭제하지 않았다. owner 장비 조회와 관찰자 외형 조회의 역할은 유지한다. 관찰자의 `GetEquippedItemInstance`는 상세 정보 없음으로 실패한다. 복제 layout 변경이므로 서로 다른 빌드의 서버/클라이언트를 섞어 검증하지 않는다. 자산 재저장과 저장 데이터 migration은 이 변경에 필요하지 않다. 테스트 전용 native socket은 Editor 테스트에만 포함하며 실제 시작 경로는 생성하지 않는다.

## ZIP 전체 항목 판정

상태의 의미: **수정**은 이번 코드 변경 대상, **유지**는 현재 경로를 확인한 구현, **미연결**은 동작하는 일부 코드/계약과 실제 게임 연결 사이의 경계, **유보**는 현재 사용처가 없거나 별도 제품·운영 단계에서 도입할 기능이다. 미연결·유보·환경 차단은 완료에 합산하지 않는다. 검증 결과는 다음 절에 별도로 적는다.

### 종합 문서·README

| 원문 위치 | 현재 근거·판정 | 비용·도입 조건·검증 |
| --- | --- | --- |
| README의 과거 감사/설계 제안 구분 | **유지.** 원본 버그 감사 수정과 ZIP 권고를 분리한다. SourceAudit 1·2·3 및 기존 28항목 기록을 회귀 기준으로 사용한다. | 과거 테스트 수를 이번 통과 수로 재사용하지 않는다. |
| 추가 구조: 인증된 단일 존 영속 절편 | **일부 구현/미완료.** 기본 네트워크 경로는 prototype GUID다. 후속 작업은 기존 Repository 계약의 SQLite provider와 명시적으로 선택한 단독 실행의 Progression/Quest 체크포인트 소비자를 연결한다. | 아래 후속 우선순위를 따른다. 기존 컴포넌트가 유일한 실시간 writer이고, 저장 스냅샷은 새 입장 시 한 번 복원한다. 보상 성공 응답 전 DB 커밋·계정 인증·인벤토리 복원은 여전히 별도 구현 대상이며 SQLite 추가만으로 전체 절편 완료라고 하지 않는다. |
| 수정 1: 명령 공통 정책 | **수정.** 장비 구형/신형 RPC의 같은 admission은 이미 해결돼 유지. 이번 공통 PS 예산은 실제 5개 경로가 소비한다. | SharedCommandBudget, 기존 EquipmentNetwork·SSR 회귀. 인증·IP/계정별 예산은 외부 접속 전 별도 필요. |
| 수정 2: 여러 컴포넌트 commit | **유지/유보.** Inventory/Equipment lock 및 reentry·퇴장 방어와 quest claim/XP 지급 경계를 확인했다. 이번 receipt payload binding 보강. | 메모리 commit과 내구성 commit 구분. InventoryCommandIdentity·기존 소비/장비/보상 수명 회귀. 서로 다른 저장소 saga는 실제 경계가 생긴 뒤. |
| 수정 3: 입력·효과 출처 | **유지.** 입력 source lease, AbilitySet grant identity, 이전 Avatar 해제 방어가 존재한다. | SharedSkillHold·QueuedInputOrigin·EquipmentGrantIdentity·RetainedAvatarEquipment. bool 통합/PS로 모든 효과 일괄 이동 안 함. |
| 수정 4: 불변 애니메이션 명령 | **유지.** 후속 AirReselectionCommand·원샷 command ownership과 play rate 수정이 이미 있다. | 원래 파이프라인을 보존하고 SourceAudit2/3 회귀. 방향/시작 clock을 새 병렬 상태로 복제하지 않는다. |
| 수정 5: 효력 있는 예산 설정 하나 | **유지.** 후속 BudgetAuthoringContract와 authoring 소비 계약이 이미 정리됐다. | SourceAudit3.BudgetAuthoringContract. 기존 자산 migration 없는 필드 대량 삭제 안 함. |
| 수정 6: 비동기 수명 | **유지.** scorer snapshot→worker→GT generation/owner/world 검사, visual lease, NPCPath 취소 계수 보존. | 기존 NPC/target scoring/retirement 회귀. 전역 scheduler 추가 비용은 실제 경합/기아 측정 후 판단. |
| 수정 7: 검증 실패 전파 | **유지 + 입력 실패 기록.** validation wrapper, provenance, CSV 누락, closure, owned package save는 후속 감사에서 고쳤다. 현재 월드 plan 입력 부재로 기존 4개 시험은 실패한다. | mock wrapper/provenance와 Python 회귀를 이번 소스에서 재실행. 누락 입력을 성공 처리하지 않는다. |
| 모듈: Core-only MMO·의존성 방향 | **유지.** 8개 프로젝트 모듈의 DAG를 확인. MMO는 Core만, Core→GAS→Mount/Character→조립 방향. | Validate-MMOArchitecture.py. 새 HTTP 바이트 처리는 조립 계층에 두고 MMO/Core에 HTTP를 넣지 않는다. |
| 모듈: public/private·runtime/editor/test | **유지.** public 반영 타입·애니메이션/validation public 헤더 소비 때문에 의존성 개수만으로 제거하지 않는다. 실제 socket fixture 의존성은 Editor 조건으로 제한한다. | 전체 Editor/Game target. Character 분할은 측정된 컴파일 파급과 실제 독립 책임이 생길 때; reflected 클래스 대량 이동 안 함. |
| 모듈: gameplay pose·이벤트 버스 | **유지.** GameplayPose/서버 notify·socket을 보존. MessageRouter는 로컬 알림, Repository/DomainEvent는 별개 계약이다. | 실제 SSR/소유자·proxy 회귀. durable event bus로 로컬 router를 재명명하지 않는다. |
| 단계 A~D·병렬/네트워크 원칙 | **유보 순서 확정.** 단일 영속 절편→다중 사용자/부하→명시적 존 이동/경제→선택 콘텐츠/운영 순서다. | 실제 10/50/100/200 연결·DB 부하는 미실행. Task 수·NPC 수·몸체 수를 CCU 실측으로 표시하지 않는다. |

### architecture_backend.md

| 절 | 현재 근거·판정 | 다음 조건·검증 |
| --- | --- | --- |
| B1 로그인·소유권·재접속 | **로컬 개발 경로 추가/운영 유보.** 기본 GameMode는 prototype GUID다. 명시적 단독 실행은 이름에서 안정 ID를 만들고 상태 로드 후 spawn을 허용한다. 로컬 중복 binding은 거부하며 저장 시 session revision으로 이전 writer를 거절한다. | 로컬 이름은 인증이 아니다. 외부 인증 provider·ticket TTL·캐릭터 roster·두 번째 네트워크 접속 정책이 있어야 완성 가능. 위조 소유자/만료 시험을 첫 외부 플레이 전 수행한다. 계정·credential을 이번에 생성하지 않는다. |
| B2 저장·revision·복구 | **후속 로컬 구현/운영 유보.** `Project_JMMO/Repository` 검증을 공유하는 SQLite adapter와 실제 Progression/Quest 체크포인트 호출자가 있다. state+receipt 및 session+profile은 각각 원자 transaction이다. | 아래 후속 실행 결과를 따른다. 알 수 없는 DB/저장 schema와 정의 fingerprint 불일치는 reset/migration하지 않고 거부한다. 운영 데이터 migration, 경제 commit-before-publish, 외부 DB 운영은 미완료다. |
| B3 Gateway·비동기 | **수정 + 미연결.** 실제 HTTP·timeout·tracker 종료 수명과 크기 제한은 구현. Config URL은 비어 있고 IServicePort 도메인 adapter와 인증/reconciliation 소비자는 없다. | 이번 실제 로컬 HTTP의 small/oversize/503·mutation 불확실성 시험 통과. 운영 HTTPS·인증·DTO·bounded retry/backoff/deadline은 서비스 연결 단계. combat tick에 HTTP를 넣지 않는다. |
| B4 인벤토리·보상·경제 | **런타임 유지/일부 수정, 영속 경제 유보.** owner-only Inventory, Quest Claim→XP, revision/guard가 있다. 캐시 요청 payload를 결합했다. Wallet/Trade 이름은 구현이 아니다. | 현재 중복/실패/퇴장 검증을 유지. 재화/아이템/claim 원자 저장·원장·보상 이유·정정 거래는 durable reward 소비자 도입 때 함께 구현. |
| B5 handover·존 이동 | **전송 계약/유보.** HandoverManager의 admission·TTL·CRC·apply receipt·transport 교체/재진입 방어와 loopback은 실제 구현. CompleteTransfer 상태 전이는 목적지 spawn/권한 fencing/클라이언트 reconnect가 아니다. | source freeze→destination reservation/restore→epoch 전환→reconnect→cleanup 각 실패점과 최대 1 writer를 실제 2서버에서 검증. CRC를 인증으로 사용하지 않음. 이번에 실제 존 이전 성공으로 표시하지 않는다. |
| B6 조정·운영 | **유지/유보.** AggregateGate/RequestTracker는 프로세스 내부 예약이며 durable lock이 아니다. 기존 trace와 큐 상한·이번 거절 카운터를 사용한다. | 다중 서버에는 DB CAS/unique/fencing 필요. queue/unknown/conflict/latency 지표, drain·GM 권한·감사는 외부 사용 단계에서 실제 소비자와 도입. |

### architecture_runtime.md

| 절 | 현재 근거·판정 | 다음 조건·검증 |
| --- | --- | --- |
| R1 PS·ASC·Avatar·장비 | **수정/유지.** PS의 장기 상태, Pawn Runtime의 장비 grant/외형 lease, 이전 Avatar 방어를 유지. 상세 장비 owner-only와 읽기 전용 공개 FastArray를 분리했다. | actual owner/observer payload·late snapshot·장착/제거와 Avatar 수명 회귀. 사망에도 유지할 패시브/쿨다운 정책은 미정이며 전부 PS로 옮기지 않는다. PS 100Hz도 측정 없이 수치만 낮추지 않는다. |
| R2 Iris 정책과 실제 연결 | **기본 Iris 유지, 관계 정책 미연결.** DefaultEngine의 IrisNetDriverConfigs/런타임 switch와 Character SetupIrisSupport, 엔진 grid는 존재. Distance/Combat UObject는 진단·시험 정책 계산기다. | 이번 실제 FastArray 수신을 확인하되 custom Iris 등록 성공으로 세지 않는다. 원거리 파티/길드 요약 소비자가 생길 때 공간 Pawn AOI와 별도 구현. 길드 전원 Pawn 전체 복제/RepGraph 중첩을 추가하지 않는다. |
| R3 병렬 snapshot·GT 적용 | **유지.** NPCDecision/TargetScoring의 token/revision/age/team/권한 검사, 결과 먼저 drain, 상한; NPCPath 취소 작업을 실제 완료까지 계수. | 전투/거래 변경을 worker로 옮기지 않는다. enqueue→apply 지연·snapshot/GT 비용·기아 측정은 NPC 규모를 늘릴 때 실행. |
| R4 Mass | **하이브리드 구현, 게임 연결 유보.** RegisterNPC/SetRoute와 전환/route snapshot/worker join/GT 반영은 있고 caller는 시험에 집중된다. Actor를 유지한다. | 실제 야외 비전투 배회 콘텐츠가 정해지면 activation/path/register/proximity/combat/unregister를 한 종류부터 연결. 모든 NPC에 임의로 켜거나 actorless MMO로 부르지 않는다. O(N×observer) 최적화는 실제 프로파일 후. |
| R5 서버 pose·표현 예산 | **유지.** 시각 coordinator의 DS 제외, GameplayPose lease, 서버 trace/notify 보호와 visual 실패 독립 gameplay 적용. | 실제 SSR 회귀. 대체 arc/capsule 판정은 공격 밀집에서 서버 pose 병목이 입증되고 공격별 동등성/취소/예측이 검증될 때만. timer로 기존 notify를 대체하지 않음. |
| R6 월드 활성화·관찰자 | **유지/정책 유보.** WorldEncounter authority timer, asset load와 ground 준비 분리, registry/EndPlay 해제가 있다. Active NPC를 거리만으로 지우는 정책은 없다. | 보스 전투/보상 권리를 임의 변경하지 않는다. 일반 필드 휴면/유휴 정책과 encounter 수가 확정되면 registry 공간 스케줄을 도입. camera/network/AI observer를 한 의미로 합치지 않는다. |

### architecture_social.md

| 절 | 현재 근거·판정 | 다음 조건·검증 |
| --- | --- | --- |
| S1 파티 | **메모리 구현 유지/접속 철회 수정.** create/join/leave·8인·리더 승계·그룹 revision·PS 투영이 있다. Join은 trusted server API이며 사용자 초대 검증 RPC가 아니다. | 초대 UI 기능을 만들 때 actor identity/revision/일회성 invitation·만료/거절/kick/delegate/구독을 실제 경로와 추가. offline 유예/해산은 제품 정책. |
| S2 길드·권한·창고 | **메모리 구현 유지/나머지 유보.** 100인·snapshot·동일 revision 충돌·tombstone이 실제 구현. | 길드 실제 플레이 전 역할/승계/영속 membership 확정. 창고는 permission revision+limit+item transfer 같은 transaction의 소비자. 아직 창고 구현 없음. |
| S3 친구·차단·presence | **카탈로그/유보.** OnlinePlayers는 로컬 PS binding이며 친구/presence 서비스가 아니다. | 계정/캐릭터 관계와 공개 범위를 정하고 친구 요청·차단·TTL projection을 실제 초대/귓속말에서 소비할 때 추가. |
| S4 채팅·신고 | **카탈로그/유보.** 메시지 라우팅/채널 authorization/서버 증거 보관 구현 없음. | 첫 지역/파티 채팅과 함께 MessageId·membership revision·차단·크기/rate·제재를 구현. Actor 전체 복제 배열로 채팅을 만들지 않는다. |
| S5 퀘스트 | **단일 목표 런타임 유지, 복합 모델 유보.** owner-only·128상태·count clamp·claim 재진입/XP rollback·snapshot. 여행은 0.5초 샘플이며 10m 이상은 teleport 취급한다. | 두 번째 복합 퀘스트에서 안정 ObjectiveId/version·EventId 중복·active/history 분리. 빠른 탈것 누락은 거리 누적 제품 기준과 함께 바꿔야 하며 이번에 규칙을 임의 변경하지 않는다. 영속 claim은 B2/B4와 함께. |
| S6 XP·레벨·전직·업적 | **런타임 유지/확장 유보.** int64 XP·cap·정의와 ordered advancement history·grant reconstruction·v1/v2 migration이 있다. | 저장 provider에 definition version·receipt를 연결한 뒤 스킬 포인트/리셋/업적을 실제 필요 순서대로 추가. AccountMilestones owner는 현재 소비자가 없는 metadata 후보이며 무관한 catalog 의미를 임의 변경하지 않음. |
| S7 아이템·외형 | **런타임 유지/상세 복제 수정.** definition/instance·owner-only bag·stack/lock·bag order와 CombatStyle/PresentationProfile 분리. | durable DTO에는 UObject 대신 stable definition/version/instance/owner/revision 필요. 귀속/escrow/염색/계정 entitlement는 실제 거래·스킨 기능에서 별도 의미로 도입. bIsLocked에 모든 예약 의미를 합치지 않음. |
| S8 재화·창고·우편·거래·경매 | **카탈로그/유보.** 일반 Repository 구매 시험을 실제 경제 서비스로 세지 않는다. | reward receipt/원장→개인 container atomic move→system mail claim/expiry→양측 동의·reservation/escrow→market 순서. 각 기능이 별도 AddItem 지급 writer를 만들지 않도록 B4 소비자로 연결. |
| S9 드롭·채집·제작 | **카탈로그/유보.** Encounter OnResolved·SpawnRevision은 있으나 자동 보상 없음. | 서버 확정 participation/kill EventId→versioned entitlement→RewardClaim→공통 지급. 이후 node incarnation/재료 소비·산출 같은 transaction과 저장 완료 시각. actor timer를 대량 추가하지 않음. |
| S10 공격대·던전·공성·주거 | **카탈로그/유보.** Group/Encounter만 재사용 기반. | 선택한 콘텐츠에 GroupKind/InstanceId/lockout/전쟁 참가 epoch/plot permission을 독립 수명으로 도입. 현재 빈 subsystem 추가 비용을 지불하지 않는다. |

### architecture_operations.md

| 절 | 현재 근거·판정 | 다음 조건·검증 |
| --- | --- | --- |
| O1 보안·admission | **수정/추가 경계 유보.** 실제 서버 권한·도메인 rate와 새 PS aggregate budget, byte/route/intent 검증. | identity/auth·endpoint별 권한·production TLS/credential·IP quota·GM 감사는 구현 안 됨. 현재 코드를 외부 인증 완료로 부르지 않음. actual HTTP와 malformed/duplicate/lifetime tests. |
| O2 관측·성능 증거 | **후속 수정 유지.** Source-Provenance가 HEAD+deleted/untracked/config/plugin+binary hashes를 비교. CSV missing은 표본 0/null로 구분. 새 rejection count 추가. | 동일 출처 비교와 누락 통계를 이번에 재검증. latency/p95/CCU/soak·대시보드 도입은 실제 부하 시험부터; 여기서 수용량 수치 만들지 않음. |
| O3 CI·빌드·Cook gate | **wrapper 수정 유지/현재 입력 실패 구분.** native exit·0개/미완료 거절과 MainWorld MapsToCook/WorldDefinition scan은 후속 반영. standalone Server는 이번 direct UBT에서도 설치 엔진의 지원 제한으로 exit 6을 확인했다. | 이번 target/소스 해시·시험 roster/결과를 새로 기록한다. 외부 CI 구성·Shipping release·대규모 asset 이동은 하지 않음. 사용자 월드 작업을 패키지 완성으로 재인증하지 않는다. |
| O4 에셋 버전·migration·제작 | **유지/일부 미검증.** touched package save·closure fail-closed·progression v1/v2는 이미 구현. 현재 MainWorld Source plan은 부재. | unknown version/partial save·missing dependency 시험. 실제 DB schema/content mismatch·삭제 definition migration·crash resume는 durable 사용자 데이터 전에 필요. 원본 자산을 대량 재저장하지 않는다. |
| O5 현지화·구성·liveops | **부분 구현/유보.** UI NSLOCTEXT/FText 숫자와 Config/DA가 있다. gather/compile/cooked locale·안전한 live config provider는 미입증. | 대상 언어 모집 전 pseudo-long UI/폰트/locale Cook, 운영 전 필요한 최소 쓰기 중단 flag부터 version/range/effective-time/rollback. 모든 값을 hot reload시키지 않음. |
| O6 배포·내구성·복구 | **미구현/유보.** Server target·패키징 도구·메모리 계약은 있으며 DB backup/drain/readiness/secret 주입의 통합 경로는 없다. | 실제 durable provider·불확실 결과 조회·동일 transaction receipt·restore 훈련 이후 제한 공개. 별도 엔진/서비스 설치·유료 인프라·배포·credential 생성은 이번 작업에 포함하지 않음. |

## 중복과 유지한 경계

- **통합:** 여러 클라이언트 값 변경/판정 진입점의 총량 예산을 PS 하나로 모았다. 기존 도메인 rate는 비용·규칙이 달라 유지한다. Gateway의 두 응답 API는 원래 공통 DispatchTrackedRequest를 계속 사용한다.
- **독립 책임 유지:** private 장비 진실과 public 외형 투영, Inventory 원본과 UI read snapshot, PS progression과 Avatar grant, 서버 검증과 client prediction, GT snapshot과 worker 값, social membership과 online presence.
- **정리:** 결과만 저장하던 inventory receipt를 원래 명령과 결합했다. 이전 소셜 접속의 writer 자격을 철회했다. 신규 runtime 상태를 갱신하는 독립 public writer·timer·전체 상태 reset은 없다.
- **유지/미확정:** 기존 BP_Player/ABP_Player·시험 fixture·제작 자산·다른 경로의 fallback은 미참조만으로 삭제하지 않는다. 실제 Mass/Iris custom adapter의 미래 소비자는 미연결로 남긴다.

## 검증 기록

원자료 root는 `Saved/MMOArchitecture_20261019/`다. 로컬 원자료는 Git에 자동 포함되지 않는다. 과거 감사의 성공 수치를 이번 실행으로 재사용하지 않았다.

| 이번 실제 실행 | 결과와 범위 |
| --- | --- |
| EditorBuild4 / EditorBuild5 | direct UBT 전체 target 각각 exit 0, 420 actions, 280.43초 / 286.89초. 최종 테스트 수명 수정까지 포함한 기준은 EditorBuild5다. |
| GameBuild1 | direct UBT `Project_J Win64 Development`, exit 0, 354 actions, 217.36초. socket fixture 의존성이 Editor 조건에만 존재하는 실행 target을 검증했다. |
| ServerBuild1 | direct UBT `Project_JServer Win64 Development`, exit 6. 최초 오류는 `Server targets are not currently supported from this engine distribution.`이며 컴파일 전의 설치 엔진 지원 제한이다. 경로·PCH·엔진 파일을 바꾸거나 과거 바이너리를 복원하지 않았다. |
| Focused3 | 새 native 6개 전부 성공: 5 success + 1 success with warnings, 실패/미완료/누락/예상 밖 시험 0. 실제 loopback TCP/HTTP의 정상 응답·응답 초과·503 및 mutation UnknownOutcome을 포함한다. |
| Integration1 | 관련 96개(새 6개 포함)가 93 success + 3 success with warnings, 실패/미완료/누락/예상 밖 시험 0. 정확한 예상 roster와 actual index를 비교했다. |
| 실제 privacy/AOI/FastArray | 별도 DS 호스트 + socket 클라이언트 2개 + NPC 100개. 5단계 장착/제거·복제 범위·종료 검사를 통과하고 세 process exit 0, timeout 0. 관찰자의 private 장비·인벤토리 수신이 비어 있는지 실제 수신 경로에서 검사한다. 100 NPC를 100 CCU 실측으로 표시하지 않는다. |
| 실제 전투 네트워크 | Baseline, LagLoss(75ms/변동 25ms/손실 10%/중복 5%), ReorderLoss(순서 변경/손실 10%/중복 15%) 각각 DS + 2클라이언트의 SSR·cue 복구·실제 몽타주 rate 시험 통과. 각 조건 3개 exit 0, timeout 0. |
| 자산 읽기 | GameMode CDO의 PS/GS/Pawn/Controller, 캐릭터 BP, Master ABP, 장비/프로필/퀘스트/월드 정의 등 선택한 7개 자산 정상 로드. registry와 장비 스타일·표현 연결을 조회했으며 저장하지 않았다. 전체 그래프/시각 품질 전수 검증은 아니다. |
| native DataValidation | 장비 정의 1, 퀘스트 1, 이동 프로필 1, 몽타주 91 = 94개. 모두 passed, 실패/skip/unable/warnings 0. |
| Windows Cook | 명시적 `/Engine/Maps/Entry` 및 기존 AssetManager/AlwaysCook 의존성으로 exit 0. summary 0 errors / 1 warning. 별도 output/UserDir이며 기존 Config와 월드 원본은 수정하지 않았다. 전체 MainWorld 제작·플레이 완료 인증으로 해석하지 않는다. |
| Stage/Pak/IoStore | 위 Cook 출력만 사용, 추가 컴파일 없이 exit 0, `BUILD SUCCESSFUL`. 기존 성공한 packaging wrapper를 재사용했다. |
| 실제 패키지 실행 | staged Development Game exe가 NullRHI에서 Entry 맵을 로드하고 정상 quit, exit 0. 소스/설치 에디터 실행과 구분한 packaged smoke다. |
| 도구·문서 | 205항목/20영역·8모듈 DAG exit 0. 기존 Python 16개 중 12 success, 이동 중인 월드 입력 부재 4 error. validation wrapper 11개 모의/출처 비교 6개 시나리오 통과 기록도 보존하며 native 실행과 구분한다. 수정 문서 8개 링크 broken 0, 범위 diff check exit 0. 전체 문서는 기존/별도 작업의 broken links 58개와 부재 local archives 12개로 exit 1이며 무관한 링크를 수정하지 않았다. `FinalDocumentChecks.json`에 기록한다. |

### 실패 원인과 재검증

초기 EditorBuild1은 UHT 매개변수 이름 누락, EditorBuild2는 테스트의 `FSocket::GetAddress` 반환형 오용으로 실패했다. 이를 고친 EditorBuild3 전체 target은 성공했다. 모든 빌드는 정상 종료까지 기다렸으며 중단·동시 빌드·cache 일괄 삭제를 하지 않았다.

최초 Focused1은 NetDriver 없는 시험 월드에서 복제 인덱스를 초기화하지 않고 lifetime props를 조회한 fixture 결함으로 native exit 3이었다. `UClass::SetUpRuntimeReplicationData()`로 고친 뒤 EditorBuild4가 성공했다. Focused2에서는 장비/경계/HTTP/요청 재사용 4개가 실제 통과한 다음, 시작하지 않은 컴포넌트에 EndPlay를 직접 호출한 SharedCommandBudget fixture가 `bHasBegunPlay` assertion으로 exit 3이었다. 시험 월드의 실제 BeginPlay와 `State->Destroy()` 종료 경로로 수정하고 EditorBuild5 → Focused3 → Integration1을 통과했다. 런타임 조건이나 assertion을 약화하지 않았다.

중간 재시도는 `Automatic approval review failed: Selected model is at capacity`로 명령 실행 전에 차단된 이력이 있다. 이후 승인 경로가 통과해 이번의 실제 실행과 정리를 완료했다. 승인 오류를 PowerShell 따옴표나 dotnet 예외로 오판해 프로젝트 경로를 바꾸지 않았다.

### 경고와 원자료

Integration1의 경고 있는 시험은 MouseChordLifecycle/RetainedAvatarEquipment의 제작 프로필 없는 native fixture, GatewayRealHTTP의 의도적인 초과 응답 거절(libcurl write failure)이다. Cook의 경고는 기존 GameplayCueNotifyPaths 미설정으로 `/Game` 전체를 검색하는 fallback이다. 자산 검증 경고는 0이며 native 시험 외 엔진 Toolset 테스트 모듈의 시작 로그는 보고서 시험 결과와 구분한다. 모든 로그가 무경고라고 주장하지 않는다.

`EditorBuild5`/`GameBuild1`/`ServerBuild1`의 Arguments·Process·SourceHashes, Focused3/Integration1의 index·Process, `ExpectedIntegrationTests.json`, `VerifiedNativeResults.json`, `AssetInspection.json`, `AssetValidationSummary.json`, 네트워크 각 실행의 manifest·verification·서버/클라이언트 receipts와 로그를 보존했다. 정확한 direct UBT 인자는 각 Arguments.json에 있다. `-WaitMutex -NoHotReloadFromIDE -NoUBA -UBARootDir=<run>/UBA -DisableUnity -MaxParallelActions=4`를 재사용했다.

최종 HEAD는 `45f55444c9ca764386b8867a1063d7ee6025a5f3`이며 이 작업에서 commit/push/PR/배포를 하지 않았다. `FinalConsistency.json`에서 Source 676개 변경 0, 기존 사용자 파일 99개 변경 0, 재개 전 Content 14,368개와 비교한 추가/변경/삭제 0을 확인했다. 최초의 이동 중 월드 원본 41개는 재개 기준에서 이미 부재했으며 임의 복원하지 않았다.

- 최종 Editor DLL SHA-256: `88F2DD9D41BD7E92DCB363D9C7B5790C999EA126CACB80D6CCFD1EFBB25CDE00`
- 최종 Game exe 및 packaged smoke SHA-256: `C0321F014B666F0621E5FC4FAEA7786F1257B8AD26FFC5DD171C5C91EE0D0CA8`

### 변경 파일

런타임 변경은 Core의 `Components/Project_JCommandBudgetComponent.{h,cpp}`, Character의 `Components/Project_JEquipmentManagerComponent.{h,cpp}`·`Project_JInventoryComponent.{h,cpp}`·`Project_JCombatHitValidationComponent.cpp`, 조립 계층의 `Game/Project_JPlayerState.cpp`·`Project_JQuestComponent.cpp`, `Social/Project_JSocialSubsystem.{h,cpp}`, `Backend/Project_JBackendConnection.h`·`Project_JGatewaySubsystem.{h,cpp}`·`Project_JGatewayResponseBuffer.h`다. Editor socket fixture 의존성은 `Project_J.Build.cs`의 Editor 조건에만 추가했다.

테스트는 `Project_J/Private/Tests/Project_JMMOArchitectureTests.cpp`·`Project_JMMOArchitectureReceiver.h`·`Project_JGatewayHTTPTests.cpp` 및 기존 `Testing/Project_JNetworkLoadFixture.cpp`의 관찰자 private 장비 수신 거절 검사다. 이 보고와 Networking/ProjectContext/MMO Foundation/Deferred 시스템의 기준 문서·검토 목차·문서 카탈로그를 갱신했다. 기존 사용자 변경 99개는 수정하지 않았다.

## 용량 증가 원인과 정리 결과

초기 C 드라이브 여유 공간은 77,346,467,840 bytes, 중간에는 약 55~56GB까지 감소했고, 최종 별도 조회에서는 76,940,386,304 bytes다. 동시에 다른 작업이 실행되었으므로 전체 차이를 이번 빌드/삭제의 효과로 귀속하지 않는다.

초기 관측에서 `.git/lfs` 신규 2,256개 / 26,149,185,296 bytes가 증가했다. 그중 objects 2,203개 / 20,055,438,501 bytes는 당시 현재 인덱스가 모두 참조했다. Git status/UBT adaptive working-set의 LFS clean과 동시간대 에셋 커밋을 고려해야 하며, 이 작업은 Git 원본/현재 참조 객체를 삭제하지 않았다. 당시 tmp 후보 53개 / 6,093,746,795 bytes는 **최종 정리 전 재조회에서 이미 모두 부재**했다. 제거 주체와 실제 회수량을 이 작업의 성과로 표시하지 않는다. `StorageBeforeFinalCleanupObservation.json`과 `LfsTmpCleanupResult.json`에 삭제 0으로 기록했다.

이번 검증은 실행별 User 캐시, Cook 출력, Stage 패키지/PDB와 진단 trace를 추가로 만들었다. native 검증·패키지 실행이 종료된 뒤 아래의 소유 범위만 정리했다.

| 실제 정리 | 근거·결과 |
| --- | --- |
| 격리 network 에셋 2개 | 새 `Content/__SourceAuditTests__/MMOArchitecture_20261019`의 몽타주/스타일 복사본만 SHA-256/길이를 비교한 뒤 삭제. 원본 제작 에셋 보존. |
| 격리 Saved 임시 경로 29곳 | 실행별 User 디렉터리 24곳과 Cooked/Package 출력 2곳, trace 3개. 소유 root·절대 경로·reparse point를 검사하고 파일별 지문을 기록한 뒤 삭제. 미완료 target 0. |
| 삭제 파일 논리 합계 | **2,919,766,085 bytes (약 2.92GB)**. 하드링크/공유 데이터/열린 handle/동시 작업 여부에 따른 실제 물리 회수량과 같은 수치라고 주장하지 않는다. |
| 드라이브 실측 | 삭제 호출 직전 76,238,237,696 → 직후 76,237,987,840 bytes로 즉시 증가가 관측되지 않았다. 후속 독립 `DriveInfo`/Win32_LogicalDisk 조회는 모두 76,940,386,304 bytes였다. 이 전체 변화를 우리 삭제량으로 환산하지 않는다. |
| 현재 프로세스 | 전체 Windows 조회에서 UnrealEditor/게임/UBT/dotnet/LiveCoding/MSBuild/ShaderCompileWorker/Uba 0개. 검증 프로세스는 정상 종료했고 별도 GUI 에디터를 새로 띄우지 않았다. 기존 Git/Git LFS 프로세스 7개는 유지했다. |

첫 정리 시도는 기존 Git 작업을 발견해 삭제 전 중지됐다. Git 저장소 내부 정리는 제외하고 `.gitignore`에서 제외된 소유 Saved 출력만 대상으로 확인한 다음 정리했다. Source/Config/Content/current Binaries/Intermediate/DDC와 다른 작업 폴더는 정리 대상에 넣지 않았다. 현재 빌드·공유 캐시는 다음 개발에 재사용하는 자원이며 속도나 용량만으로 일괄 삭제하지 않았다.

원본 입력 6문서, 실패/성공 로그, 정확한 인자·소스 해시·검증 index/receipts, 작은 package manifests와 `RetiredOutputFileHashes.json`을 남겼다. 정리 계획/실제 결과는 `OwnedOutputCleanupPlan.json`/`OwnedOutputCleanupResult.json`, 최종 프로세스·여유 공간·남은 경로 0은 `FinalProcessAndStorage.json`에 있다. 삭제된 전체 trace 재분석이나 격리 패키지 재실행에는 재캡처 또는 Cook/Stage가 필요하다.

### 남은 환경·다른 작업 경계

1. **별도 Server target:** 현재 설치 엔진이 지원하지 않는다. 지원되는 소스 빌드/서버용 엔진 환경을 별도로 준비한 뒤 검증할 조건이며 이번에 엔진을 교체/설치하지 않았다. 실제 DS 호스트 네트워크 회귀 성공과 독립 Server 배포 가능성을 구분한다.
2. **WorldPlanTests 4개:** 다른 작업이 이동 중인 `Content/World/MainWorld/Source/MainWorld.plan.json`을 기존 시험이 요구한다. 새 위치/소비 계약을 해당 작업이 확정할 때 재개한다. 누락 입력을 성공 처리하거나 원본을 복원하지 않았다.
3. **확장·시각 품질:** 운영 인증/영속 DB/경제/실제 존 이전과 전체 월드·시각 품질·대규모 CCU는 아래 기능 도입 조건과 별도다. 이번 native/소규모 통신 성공을 구현되지 않은 기능의 완료로 합산하지 않는다.

## 후속 구현: 로컬 저장과 남은 우선순위

사용자의 후속 요청에 따라 `45f55444c9ca764386b8867a1063d7ee6025a5f3`과 당시의 변경 상태를 다시 기록했다. 근거 root는 `Saved/MMOArchitectureContinuation_20261019`다. 앞 절의 96개 시험·패키지 실행은 이전 단계의 결과이며 새 코드의 검증으로 대신하지 않는다.

| 순위 | 이번 처리와 다음 완료 조건 |
| --- | --- |
| 1 로컬 단일 캐릭터 저장·재입장 | 기존 Progression/Quest 스냅샷과 Class/Advancement ID resolver를 재사용한다. 설치 엔진의 SQLiteCore 플러그인을 프로젝트에서 활성화하고, 조립 계층에 worker-only Repository adapter와 opt-in GameInstance consumer를 추가한다. 정상 재실행·중복·충돌·백업 복원·수명을 검증한다. 중단된 journal의 자동 복구는 설치 엔진 VFS 한계로 미완료이며 아래의 차단·오프라인 복구를 구분한다. |
| 2 장애에 견디는 보상·인벤토리 | 다음 내부 구현 우선순위다. 아래 저장 VFS/운영 DB의 복구 계약을 충족하고, 기존 아이템 정의 ID/version resolver와 전체 인벤토리·장비 복원 계약을 확인한 뒤, 기존 claim/GrantExperience의 사전 검증·commit-before-publish를 같은 경제 명령에 연결해야 한다. 단순 저장 observer는 이 요구를 충족하지 않는다. 원장/정정/보상 이유와 결과 조회도 함께 필요하다. 이 내부 기능 미구현을 전부 외부 환경 탓으로 분류하지 않는다. |
| 3 인증된 네트워크 입장 | 로그인 제공자, 검증할 issuer/audience/키, 계정–캐릭터 소유권 저장소, 서버 endpoint·TLS/비밀 공급 경로가 현재 없다. 프로젝트에는 기본 Gateway URL도 비어 있다. 운영 소유권을 임의로 발급하거나 로컬 이름을 인증으로 확대하지 않는다. 실제 서비스/인증 환경이 필요한 경계다. |
| 4 존·채널 이전 | 단일 존의 1~3 이후다. 현재 설치 엔진의 Server target 차단, 서버 디렉터리·목적지 endpoint·예약/티켓 제공자 부재가 구체적 외부 경계다. 로컬 host 통신 검증과 두 배포 서버 간 권한 이전을 구분한다. |
| 5 소셜·경제 확장·규모 | 초대/권한/영속 그룹, 거래·우편·창고는 실제 기능 정책과 2~4의 소비 경로에 맞춰 추가한다. 10/50/100/200 실제 연결과 DB 부하 환경은 아직 없다. NPC 수를 CCU로 세지 않는다. |

### 로컬 저장의 사용과 경계

개발 Editor/Game을 **단독 실행**하면서 `-ProjectJLocalProfile=Developer_1`을 명시한다. 이름은 ASCII 영숫자·`_`·`-`의 1~32자이며 대소문자를 정규화한다. 파일명은 사용자 문자열을 연결하지 않고 `FPaths::ProjectSavedDir()/LocalProfiles/Profiles.sqlite` 하나를 사용한다. 패키지와 `-UserDir` 지정은 사용자별 Saved 위치를 사용하므로 항상 저장소의 Saved 폴더라는 뜻은 아니다. Shipping과 네트워크 플레이는 이 입장 경로를 허용하지 않는다. 플래그 없는 기존 멀티플레이는 그대로다.

GameMode는 로드 완료 전 Pawn 시작을 보류한다. 기존 컴포넌트가 유일한 실시간 writer이며 observer가 진행도와 퀘스트를 하나의 값으로 캡처한다. 새 입장 때 기존 RestoreSnapshot으로 한 번 복원하고 GAS grant도 기존 경로에서 재구성한다. 기본 자동 수락 퀘스트는 미진행·미청구·unpossessed 상태에서만 교체할 수 있다. 알려지지 않은 정의 ID/버전·규칙은 기본값으로 덮어쓰지 않는다.

I/O는 게임 스레드 밖에서 실행한다. 최대 32개의 실제 작업 예약과 캐릭터당 현재 저장 1개+최신 대기 체크포인트 1개를 사용한다. identity/owner/world/binding 수명을 완료 때 확인하고, logout은 추가 수락을 막되 수락한 값은 drain한다. GameInstance 종료는 native 작업 완료를 기다린다. 저장 세션과 프로필 CAS를 같은 트랜잭션으로 검사한다. 이전 세션이 늦게 저장하면 둘 다 변경되지 않는다. ambiguous commit은 원래 명령/키 그대로 한 번만 재시도하며 실패를 신규 성공으로 바꾸지 않는다.

현재 한 DB의 상한은 records 1,024 / 성공 receipt 4,096이다. 상한에서 실패하고 영수증을 버리지 않는다. 운영용 무기한 저장/영수증 보존 정책이나 자동 migration이 아니다. 상태 변경은 메모리에서 먼저 발생하므로 **마지막 확인된 체크포인트 이후 변경은 장애로 잃을 수 있다.** Inventory/Equipment/Wallet/그룹/좌표는 이 프로필에 포함하지 않았다. 로그의 저장 성공, 메모리 보상 성공, 경제 내구성 성공을 구분한다.

SQLite는 기존 엔진에 포함된 것을 사용한다. `journal_mode=DELETE`, `synchronous=EXTRA`와 integrity check를 확인하고, 알 수 없는 application/user version이나 손상 파일은 거부한다. 설치 엔진의 `SQLiteEmbeddedPlatform.cpp`/`SQLiteCore.Build.cs`를 확인하면 `unreal-fs` VFS는 DB의 동시 writable handle과 세밀한 파일 잠금을 지원하지 않는다. 따라서 정규화된 DB 경로의 OS named mutex를 최대 1초 기다리고, **한 작업의 open→transaction→close 전체**를 보호한다. 프로세스 내부 AggregateGate만으로 DB 공유가 해결됐다고 하지 않는다. DB CAS/receipt 검사는 이 보호 구간 안에서 그대로 수행하며 다른 머신의 저장소나 인증을 제공하지 않는다. 외부 도구의 열린 핸들·잠금 실패는 unavailable로 거부한다.

[SQLite synchronous 설명](https://www.sqlite.org/pragma.html#pragma_synchronous)과 [트랜잭션 설명](https://www.sqlite.org/lang_transaction.html)은 VFS 지원도 전제로 한다. 특히 설치 엔진 `unreal-fs`의 Delete 함수에는 directory sync가 TODO이므로 `EXTRA` 값 조회만으로 디렉터리 동기화가 보장됐다고 하지 않는다. 이 단계는 프로세스 종료·재시작 및 interrupted transaction 복구를 검증하며, 물리 전원 차단·스토리지 고장까지 보장/시험하지 않는다. 운영 경제의 엄격한 내구성은 적절한 OS VFS/외부 DB와 배포 환경에서 별도 충족해야 한다.

실제 중단 시험에서 18,960 bytes의 hot journal이 남았고, 설치 엔진 wrapper의 자동 복구가 실패했다. `unreal-fs`의 CheckReservedLock은 실제 OS의 SQLite lock 의미를 제공하지 않는다. 이를 성공으로 완화하지 않고, adapter가 **비어 있지 않은 `-journal`/`-wal`이 있으면 DB를 열기 전 unavailable로 거부**하도록 보강했다. 파일은 지우거나 초기화하지 않는다. 정상 OS VFS를 지원하는 adapter/운영 DB 환경을 선택·연결하는 일은 남아 있으며 현 설치 엔진에서 자동 장애 복구가 완성된 것은 아니다.

단독 실행 콘솔의 `ProjectJ.LocalProfile.Backup`은 새 GUID 경로 `Saved/LocalProfiles/Backups/*.sqlite`에 일관된 복사본을 만들고 application/version/integrity를 검증한다. 기존 백업은 덮어쓰지 않는다. 이는 백업 파일의 전원 차단 내구성 인증이 아니다. 복원은 에디터·게임·저장 작업을 정상 종료한 다음 현재 DB를 별도 보존하고, 검증된 백업을 `Profiles.sqlite`에 복사하는 수동 절차다. DB에 journal이 남았다면 **DB와 journal 쌍을 함께 보존**하고, 모든 writer가 닫힌 상태에서 표준 OS VFS를 사용하는 SQLite 도구로 오프라인 복구·integrity check를 수행한 뒤 백업한다. Unreal wrapper로 다시 여는 것만으로 자동 복구되는 환경이라고 가정하지 않는다. 코드는 Python이나 외부 프로그램을 실행하지 않으며 사용자 DB를 자동 삭제/교체하지 않는다. 시험은 기존 번들 Python의 표준 SQLite 3.53.1로 격리 DB를 수동 복구해 원래 상태·영수증 수·lease가 모두 일치하는 것을 확인하고, 그 뒤 native 재입장을 확인한다. 중단 원본 파일 쌍은 별도 보존했다.

### 후속 검증

최종 입력은 `EditorBuild6/SourceHashes.json`의 Source 683개다. 아래 경로의 상대 기준은 `Saved/MMOArchitectureContinuation_20261019`이며, 이전 단계의 같은 이름 로그와 구분한다.

| 검증 | 실제 결과 |
| --- | --- |
| Editor direct UBT | `EditorBuild6`: exit 0, 424 actions, 277.65초. |
| Game direct UBT | `GameBuild1`: exit 0, 358 actions, 212.78초. |
| 별도 Server target | `ServerBuild1`: exit 6, 최초 오류 `Server targets are not currently supported from this engine distribution.` 엔진 교체/다운로드를 하지 않았다. |
| 새 저장 집중 시험 | `Persistence4`: 3 success, warnings/failure/notRun 0. 실제 SQLite atomic commit/replay·키 재사용·conflict·tombstone·capacity·두 adapter의 CAS·백업 재개방/영수증 복원·unknown/corrupt/잔여 journal 보존·정수 정밀도·실제 PS 재입장·중복 binding/identity 철회·종료 drain을 확인했다. |
| 관련 native 회귀 | `Integration1`: **110개 = 107 success + 3 successWithWarnings**, failed/notRun/inProcess/missing/unexpected 0. `ExpectedIntegrationTests.json`과 실제 index를 대조했다. 집중 3개는 이 110개에 포함되며 별도 신규 통과 수로 합산하지 않는다. |
| 중단·오프라인 복구·재입장 | `ProfileWrite` 성공 후 격리 Python writer만 커밋 전에 self-exit 99. hot journal 18,960 bytes. `ProfileRead`의 엔진 자동 복구는 실패했다. 중단 원본 쌍을 보존하고 표준 SQLite로 수동 복구한 뒤 `ProfileReadRecovered` exit 0. `OfflineRecovery.json`은 원래 상태 해시/receipt 수/lease 일치, `CrashRecovery.json`은 재입장 세션+동일 체크포인트만 추가됐음을 확인한다. 최종 코드에는 잔여 journal 사전 거부가 추가돼 있다. |
| 실제 기본 캐릭터 | `StandaloneFirst/Second`: 실제 BP GameMode와 GreatSword Pawn 입장·정상 시간 종료·재실행, 동일 AccountId/CharacterId·class/level·payload 유지, 저장 revision 1→2. 별도 fixture PS만의 성공으로 대신하지 않았다. |
| 기본 네트워크 | `Saved/Validation/GroupD_20260910/MMOContinuation_20261019_Default`: 실제 DS 호스트+클라이언트 2개+NPC 100개. AOI/FastArray 5단계와 정상 퇴장 성공, 세 native exit 0, timeout 0. 로컬 프로필 플래그 없이 기존 경로를 시험했다. |
| Windows Cook/Stage | `Saved/Validation/Continuation_20261014/MMOContinuation_20261019_EntryCook`/`EntryStage`: explicit Entry + 설정된 기본 에셋/AlwaysCook closure, 1,369 packages, Cook exit 0 / errors 0 / 기존 warning 1. Pak/IoStore Stage/Package exit 0. |
| 실제 패키지 | `PackagedFirst/Second`: 같은 패키지의 실제 실행·재실행 exit 0, 동일 개발 ID·직업·진행 상태, revision 1→2. staged exe SHA-256은 현재 Game exe와 일치한다. |
| 에디터↔패키지 저장 교차 사용 | `PackagedCrossMode`/`EditorCrossMode`: 같은 격리 저장 파일을 읽고 정상 종료, revision 2→3→4. FName 표시 대소문자 GreatSword/Greatsword는 달라도 기존 ID 동등성·정규화한 전체 상태 해시는 유지된다. `CrossMode*.json`에 원래 값과 비교 결과가 있다. |
| 모듈·카탈로그·보존 | `Validate-MMOArchitecture.py` 통과: 205개 계약/20영역 및 module DAG, MMO Core-only 유지. `PreservationVerification.json`: 기존 Source/Config 입력 672개 unchanged, 의도 수정 13개+신규 소유 7개, 예상 밖 변경 0. Source 683개가 최종 빌드 해시와 일치한다. Content 14,368개 메타데이터 변화/신규 0이며 이동 중인 월드 원본은 복원하지 않았다. |

회귀의 경고 3개는 기존 MouseChordLifecycle/RetainedAvatarEquipment native fixture의 제작 프로필 부재와 GatewayRealHTTP의 의도적 초과 응답 거절이다. Cook 경고는 기존 GameplayCueNotifyPaths 미설정 fallback이다. Editor-Cmd `-game` 시작 로그의 엔진 Experimental Toolset Python reflection 오류와 자동화 시작 시 엔진 Toolset 테스트의 Condition failed는 native 시험 index·게임 상태 검증과 구분한다. 모든 로그가 무오류라고 보고하지 않는다. 기본 네트워크 검증은 기존 스크립트의 Editor Toolset 비활성 옵션을 사용했고 startupPythonErrors는 0이다.

중간 실패도 보존했다. 첫 빌드는 잘못된 헤더 경로/자동 포인터 형식으로 실패했고 수정했다. `Persistence1`은 백업 검증 연결의 Close 누락 assertion, `Persistence2`는 중복 시험 데이터를 만드는 TArray 자기 참조 assertion이었다. 실제 연결 수명을 수정하고 시험 값을 복사했으며 assertion을 약화하지 않았다. `SQLiteProbe1`의 동시 open I/O 오류는 엔진 VFS 조사와 OS 파일 게이트/작업별 close로 해결했다. 자동 hot-journal 복구 실패는 위와 같이 성공으로 바꾸지 않았다. 실행 중인 UBT/Unreal 프로세스를 임의로 중단하지 않았다.

현재 Editor DLL SHA-256은 `BD71F9A40CAF75BC68CBB8F01841E6DA436CC6D3A0DD76BCFB57EF725B99A581`, Game/패키지 exe는 `ABC17267367027A71E40BB8427867BB5238942E3883EBA85D7878424CAC9346B`다. Engine SQLiteCore DLL 지문도 `RuntimeHashes.json`에 보존했다. 현재 바이너리 지문은 임시 출력 정리 후에도 일치한다.

### 후속 용량 정리와 완료 경계

이번 후속 검증의 주요 임시 증가는 실행별 User의 약 162MB AssetRegistry/cache, Cook/User의 파생 출력, Cooked 데이터와 Stage 패키지/심볼, 네트워크 trace다. 전역 프로세스 종료·소유 root·절대 경로·reparse point·파일별 SHA-256을 확인하고 **19개 경로 / 337개 파일 / 논리 3,223,320,795 bytes**를 정리했다. 시험 DB·journal 40개는 총 630,846 bytes의 해시 일치 사본으로 남겼고, 중단 당시 원본 DB+journal 쌍, manifest 4개, 로그/index/receipts/인자/소스 지문도 보존했다. `OwnedCleanupResult.json`의 남은 정리 대상은 0이다. 실제 사용자 LocalProfiles, Source/Config/Content, 현재 Binaries/Intermediate, 전역 DDC, Git 저장소 및 다른 작업 파일은 정리 대상으로 삼지 않았다.

논리 삭제량을 물리 회수량과 같다고 하지 않는다. 정리 호출 전/직후 여유는 141,073,104,896→141,073,448,960 bytes였고, 후속 DriveInfo/Win32_LogicalDisk는 모두 141,614,485,504 bytes였다. 다른 작업·공유/하드링크·지연 해제 때문에 전체 차이를 이 삭제의 효과로 환산하지 않는다. 최종 전역 조회에서 에디터·게임·UBT/dotnet/LiveCoding/MSBuild/ShaderCompileWorker/Uba는 0개다. Shared Zen/trace 같은 서비스는 임의 종료하지 않았다.

검증용 Cook/Stage/trace 전체는 정리했으므로 해당 패키지 재실행이나 trace 재분석에는 재생성이 필요하다. 원본 소스와 현재 빌드 산출물, 작은 검증 근거는 남아 있다.

이번 후속은 기존 파일 13개와 신규 소유 파일 7개, 관련 기준 문서 4개를 변경했다. 새 backend 3쌍과 집중 native 시험은 실제 GameMode/PlayerState 소비자에 연결돼 있다. 기존 live Progression/Quest가 상태를 소유하고 snapshot/lease/I/O mailbox는 저장 경계와 수명을 소유한다. 기존 MemoryRepository·기본 prototype 입장·소셜·handover 계약은 서로 다른 사용 역할이 있어 유지한다. commit/push/PR/배포는 하지 않았다.

남은 것은 위 우선순위의 운영 저장 복구/강한 가치 변경·인벤토리·인증·소셜/존 확장이다. 이동 중인 월드 원본을 요구하는 Python 시험 4개, 실제 운영 인증 endpoint·소유권/티켓 서비스, Server 배포 엔진 환경도 여전히 별도 경계다. 이번 로컬 체크포인트를 운영 MMORPG 전체 완료로 표시하지 않는다.

## 사용자 플레이와 다음 확장

1. 두 클라이언트에서 장비 장착/교체/해제와 respawn 후 원격 무기 외형을 확인한다.
2. 가방 split/merge·소모품·퀘스트 보상을 연속 조작했을 때 UI 응답과 장비 유지감을 확인한다.
3. 연속 공격 속도·착지·전투 취소의 자연스러움과 타격감은 기술 회귀와 별도로 확인한다.

다음 영속 기능은 **정의 ID/version resolver→인증된 계정/캐릭터/세션→단일 writer→상태+receipt 원자 저장→GT generation 검증→owner/public projection** 순서로 연결한다. 실제 source/DB schema·콘텐츠 compatibility와 crash/restart/응답 유실을 함께 검증한다. 그 전의 그룹 snapshot·Repository 테스트·handover 완료 enum을 운영 MMORPG 완성으로 표시하지 않는다.
