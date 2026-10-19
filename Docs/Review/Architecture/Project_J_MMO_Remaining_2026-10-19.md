# MMO 아키텍처 잔여 작업 — 현재 단일 상태표

구현·검증 범위는 `Project_J_Finish_Remaining_Work_Prompt.txt`다. 원본 ZIP 6문서의 설계 의견, 앞 절편의112개 회귀와 이번 실제 구현/검증을 구별했다. **현재 지원하는 내부 실행 범위의 A 잔여는 0개이며, 외부 항목 C는 차단 상태다. 모든 MMO 아키텍처나 인증된 네트워크 단계A 전체가 완료됐다는 뜻이 아니다.** 아래 검증 단계에서는 commit/push/PR/배포·실제 사용자 DB 전환을 하지 않았다. 이후 공유 커밋은 이 검증을 새로 실행한 결과로 세지 않는다.

이번 문서 갱신은 아래 기존 검증을 재사용하며 새로운 native 실행 결과로 세지 않는다. [공유 검증 요약](Project_J_MMO_Validation_2026-10-19.json)에117개 명단/상태, 빌드 종료 코드, 반복·복구·이관·패키지 결과, 입력/바이너리 지문과 정리량을 선별해 남겼다. `Saved/...` 링크는 이 PC의 ignore된 로컬 원자료이며 새 checkout에 포함되지 않는다.

HEAD `45f55444c9ca764386b8867a1063d7ee6025a5f3`와 기존 미커밋 변경을 보존했다. 기준은 `Saved/MMOArchitectureFinish_20261019/Baseline.json`, 최종 입력/출력은 [VerifiedFinalResults.json](../../../Saved/MMOArchitectureFinish_20261019/VerifiedFinalResults.json)과 [RuntimeInputFingerprints.json](../../../Saved/MMOArchitectureFinish_20261019/RuntimeInputFingerprints.json)이다. 실제 소스 변경 목록은 [ChangedSourceFiles.json](../../../Saved/MMOArchitectureFinish_20261019/ChangedSourceFiles.json)이며 기존 repository/권위 컴포넌트/요청 UI와 focused 회귀를 확장했다. 별도 저장 서비스나 모듈 의존성은 추가하지 않았다. 이번 입력880개와 Editor DLL17개가 검증 중 변하지 않았고 원래 Content 14409개의 경로/크기/mtime 변경·누락은0이다. 추적 에셋14310개는 모두 존재하며 LFS pointer placeholder0이다. 앞 절편에서 승인 복구한41개는 이번에 다시 이동·복원하지 않았다.

## 원본 ZIP 전체 판정

A=기존 기능/이번 저장 절편의 실제 미완료를 이번에 해결, B=기존 구현을 현재 근거로 확인, C=외부 서비스/환경/권한 차단, D=현재 없는 콘텐츠의 도입 조건이다. 한 항목 안의 내부 기반과 외부 운영/미래 콘텐츠를 분리해 기재했다. 기본 파티/길드의 운영 영속 미연결을 단순히 D로 바꾸지 않았다.

| 원본 항목 | 판정 | 현재 결과·남은 조건 |
| --- | --- | --- |
| README·종합 감사/설계 구분 | B | 원본은6850ed45의 감사/설계 의견. 현재 HEAD와 미커밋 변경을 보존하고 최종117 고유 회귀로 재검증. |
| 종합 추가 구조·단계A: 인증된 단일 존 | A/C | 현재 Windows 단독 저장 절편의 A는 완료. 인증·네트워크/Shipping 저장·기본 파티/길드 membership의 운영 영속화는 아래 C이며 원본 단계A 전체 완료가 아니다. |
| 종합 수정1·O1 명령 입장/보안 | B/C | PS 공통 예산·domain rate·RPC 부정 입력 회귀 유지. 실제 계정/IP/GM 권한은 인증 서비스와 공개 운영 정책이 필요. |
| 종합 수정2·B4 경제 변경 | A | 기존 XP/claim/Add/stack/remove/use/bag/equip/unequip과 class/advancement를 영속 확정 후 전체 상태 설치·게시로 연결. |
| 종합 수정3·R1 입력/grant 출처 | B | source lease·AbilitySet identity·PS/Avatar 철회·GAS actor-info revision 유지. 실제 복원/늦은 완료와 전투 통신 회귀. |
| 종합 수정4 불변 애니메이션 명령 | B | AirReselection/one-shot owner·rate 계약 보존. 관련 native·SSR/cue/rate 검증. |
| 종합 수정5·R5 표현 예산 | B | 저작 budget과 gameplay pose 보호 유지. 기존 자산·네트워크 검증으로 확인, 전면 ECS/새 예산 manager 미도입. |
| 종합 수정6·R3 비동기 수명 | B | NPC/target worker token·revision·age·retirement·join 유지. UObject는 GT에서만 접근. |
| 종합 수정7 월드 제작 입력 | B | 앞 절편에서 승인 복구한41개를 이번에는 변경하지 않음. 월드 도구17개 시험·MainWorld 실제 로드·의존·Cook/package 검증. |
| 종합 모듈/public/editor·event bus | B | 8모듈 DAG 유지. 기존 SQLiteCore·AssetManager·권위 컴포넌트 재사용. 로컬 MessageRouter를 durable bus로 재명명하지 않음. |
| 종합 단계B~D·부하/병렬 규모 | D/C | 현재 시험은 실제2clients+NPC100, CCU100 지원 주장이 아님. 10/50/100/200 연결은 운영 환경과 측정 예산을 정한 뒤 확장. |
| B1 로그인·소유권·재접속 | C | 개발 이름의 결정적 GUID는 자격 증명이 아니다. issuer/audience/키 공급·소유권 권위·HTTPS 입장/재조회 서비스가 필요. |
| B2 자동 journal 복구·백업 | B | 기존 프로젝트 전용 Win32 rollback VFS 유지. 실제 adapter의24개 중단/복구 프로세스와 native lock/CAS/backup 복원 확인. |
| B2 장기 저장 한도·큐 | A | 체크포인트 최신1, 현재 세션 명령 순번창64, 같은 transaction의 state/receipt/retirement. 이전4096 누적 정지는 실제 consumer에서 제거. |
| B2/O4 구버전 이관 | A | 원본 read-only 검사·누락 목록·dry-run·정의 검증·별도 파일·비교·rollback·중복/부분 실패 부정 시험 구현. container1→2와 snapshot1→2를 구분. |
| B3 Gateway/업무 서비스 | B/C | HTTP byte/timeout/UnknownOutcome 경계 native 회귀 유지. GatewayUrl은 비어 있고 서비스 DTO·인증·재조회 endpoint가 없어 운영 연결 차단. |
| B4 아이템/보상 실제 consumer | A/C | 현재 개발 단독 consumer는 완료. 같은 구현을 다중 서버/Shipping 경제의 권위 저장으로 활성화하려면 인증·저장 서비스 계약이 필요. |
| B5 실제 존/채널 이전 | C | envelope·loopback·상태 머신은 실제 목적지 spawn/영속 소유권 전환이 아니다. directory/예약/ticket/목적지 endpoint/Server 환경 필요. |
| B6 조정·drain·운영 | B/C | 현재 gate32·binding32·명령1·checkpoint mailbox1·CAS/lease·정상 drain 검증. 분산 lock/운영자 권한/감사 서비스는 운영 연결 이후. |
| R1 PS/ASC/Avatar·장비 | B/A | 지속 데이터는 PS, 표현/handle은 runtime. 로드 첫 알림 전 P/Q/bag/slots 전체 설치와 입장 중 mutation 거부도 회귀로 확인. |
| R2 Iris/관계 관심 대상 | B/D | 기본 grid와 owner/public 수신은 현재 통신 검증. 원거리 파티 요약 소비자가 생길 때 custom 관계 정책 등록을 도입. |
| R4 Mass hybrid | B/D | 기존 등록/route/worker→GT 수명은 유지. 실제 야외 비전투 콘텐츠가 없으므로 새 샘플 actor를 생산 기능처럼 연결하지 않음. |
| R6 Encounter·필드 활성화 | B/D | 현재 권위 timer·registry 해제·ground 준비 분리를 유지. 필드 휴면/보상 권리 scheduler는 실제 콘텐츠와 활성화 예산 이후. |
| S1 파티 기본 membership | B/C | 현재 trusted memory create/join/leave·8인·리더 승계/접속 철회는 검증. 운영 membership 재시작 복원은 stable 인증 ID·권위 backend/DTO/저장 endpoint가 없어 차단. |
| S1 초대/kick/UI | D | 실제 초대 소비자가 생길 때 revision·일회성 invitation·만료/권한 계약을 추가. 현재 기본 membership의 영속 미연결과 별개. |
| S2 길드 기본 membership | B/C | 현재 memory100인·snapshot/tombstone/퇴역 binding은 유지. 운영 영속 membership은 인증·권위 서비스가 필요하며 완료로 세지 않음. |
| S2 길드 역할·창고 | D | 현재 창고가 없다. 역할/승계/offline 정책과 실제 item transfer consumer를 도입할 때 같은 transaction으로 권한/소유권을 연결. |
| S3 친구/차단/presence | B/D | 현재 OnlinePlayers는 PS binding. 친구 요청·초대/귓속말 UI가 생길 때 공개 범위/TTL을 적용. |
| S4 채팅/신고 | D | 현재 채널/신고 서비스가 없다. 첫 실제 소비자에서 membership·MessageId·차단·제재·보존 정책을 구현. |
| S5 퀘스트 | A/D | 기존 단일 목표·실제 travel·claim은 저장 경계 연결 완료. 복합 ObjectiveId/EventId/history는 복합 콘텐츠 도입 이후. |
| S6 XP/직업/전직 | A/D | 현재 int64 XP·class·순서 있는 advancement·definition/rules·GAS grant 연결 완료. 스킬포인트/리셋/계정 업적은 현재 없는 소비자. |
| S7 아이템/외형 | A/D | 현재 소유 instance/definition/version/count/order/slot/가방 여부와 owner-only/public 투영을 확인. 귀속·염색·entitlement는 실제 기능 도입 이후. |
| S8 재화/창고/메일/거래/경매 | D | 카탈로그 타입은 서비스가 아니다. 현재 Wallet/escrow/mail/market consumer가 없어 신규 콘텐츠를 임의 확장하지 않음. |
| S9 드롭/채집/제작 | B/D | 기존 hit/spawn revision은 유지. loot 권리/표/기여·recipe 서비스는 실제 콘텐츠 정책 이후. |
| S10 공격대/던전/공성/주거 | D | 현재 소비자가 없으며 실제 콘텐츠·권리·reset/입장 정책과 단일 존 기반을 갖춘 뒤 도입. |
| O2 관측·출처 | B/A | source/config/script880·Editor DLL17·Game/package 지문, 고유 roster·actual CSV/로그·phase별 입력/결과 보존. 실측과 synthetic를 구별. |
| O3 테스트/CI·Editor/Game/Cook/package/Server | A/C | 현재 로컬 fail-closed 실행 gate와 Windows Editor/Game·자산·MainWorld Cook/Stage/package/restart 확인. Server target 지원 엔진과 외부 CI runner/격리 checkout 연결은 환경·권한 차단. |
| O5 현지화/liveops | B/D/C | FText/DA/Config 유지. 출시 언어·font/locale/pseudo-long·운영 flag/rollback은 해당 제품/운영 소비자가 생길 때 별도 검증. |
| O6 배포/복구 | B/C | 현재 단독 shutdown/backup/복구는 검증. 외부 readiness/secrets/deploy·두 실제 서버 복구는 운영 환경/권한이 필요. 배포하지 않음. |
| 마감·용량·기존 변경 | A | 정상 native 종료 후 이번 소유 임시 캐시/패키지/trace만 정리. 원래 Content와 다른 작업·현재 빌드·공유 DDC/Git/LFS 보존. |

## 장기 저장 정책과 실제 consumer

기존 권위 컴포넌트와 concrete SQLite adapter를 재사용했다. 경제 명령은 상태+lease CAS+receipt+high-water+정리를 **한 transaction**에 기록한 뒤, 현재 binding에서 모든 P/Q/items/slots 값을 설치하고 GAS/알림을 게시한다. 초기 로드도 첫 알림 전에 전체 상태를 설치하며 그동안 mutation admission은 닫힌다. Add는 즉시 유효한 instance를 반환하지 않고 `OnItemAdded`가 확정을 알린다. UI Pending·같은 키 재조회·응답 유실·늦은 binding·퇴역/재결합된 ASC 대상의 소모품 효과 계약을 유지했다.

체크포인트는 최신 상태와 자신의 결과1개를 교체하며 동일 상태는 쓰지 않는다. 입장 시 세션 결과1개, 경제 명령은 현재 세션의 마지막64 **순번 범위**만 정확한 intent/result 조회를 지원한다. 런타임이 96-bit GUID 접두 세션과 uint32 단조 순번으로 키를 발급하고 발급되지 않은 키는 거부한다. 오래된 순번/세션은 `Expired`이며 CAS를 새로 붙여도 신규 지급으로 수락하지 않는다. 사용하지 않은 순번도 뒤의 성공 high-water보다 오래되면 만료된다. 순번 소진은 wrap하지 않고 새 binding이 필요하다. 재접속/재시작/백업 복원은 새 세션으로 시작하며 이전 요청은 만료, 확정된 전체 상태는 복원한다. 무기한 과거 결과 조회를 약속하지 않는다.

timestamp TTL을 사용하지 않아 시계 역행/미래 timestamp에 admission/정리가 좌우되지 않는다. wall-clock을 바꾸는 시험은 하지 않았고, 순번 경계·세션 교체·별도 프로세스·오래된 backup 복원·동시/중단 경계로 정책을 검증했다. 영구 GUID 기록의 무제한 보존, 단순 시간순 삭제, 한도 증대는 선택하지 않았다. DB container는 `user_version=2`로 올려 이전 version1 writer도 거부하게 했다. 기존 records/legacy receipt 바이트와 application ID는 보존하며 일반 `IRepository`의 영구 opaque-key 계약을 임의 축약하지 않는다.

records1024는 보관 프로필 수의 admission 한도다(완전 profile+lease 약512개). 플레이 횟수마다 record를 만들지 않고, 퇴역 캐릭터는 자동 삭제하지 않는다. active binding32, I/O gate32, binding당 command1과 최신 checkpoint mailbox1, 소스 DB 잠금 안의 backup8 제한을 적용했다. 한도 뒤에는 명시적으로 거부하고 기존 백업을 삭제하지 않는다. 실제 공간/I/O 실패는 저장 실패로 표시하고 원형/유효 receipt를 보존한다. `SQLITE_FULL`은 native page quota로 주입해 검사했으며 사용자 디스크를 실제로 가득 채우지는 않았다. SQLite free pages를 재사용하고 매번 파일을 교체/초기화하지 않는다.

기존 `InitializeClass`/`ApplyAdvancement`와 ById wrapper는 정의·순서·요구 조건·style 준비를 검사하고 같은 사전 커밋으로 연결했다. 변경하지 않은 class GAS handle은 유지하고 additive/replace advancement grant만 필요한 만큼 반영한다. bool은 프로필 모드에서 비동기 admission이며 최종 값은 확정 후 `OnChanged`로 관찰한다. 동기 `SetLevel`/`TrySetCharacterLevel`은 개발/hand-over override이며 프로필에서는 저장 우회를 거부한다. 정상 XP→레벨 상승이 공식 경로다. 실제 hand-over caller는 `Project_JHandoverManager`→`TryApplyHandoverSnapshot`이며 실제 네트워크 존 이전은 C다. 비프로필 NPC/개발 동작은 유지한다. 체력/마나/cooldown·runtime handle은 현재 transient 정책이며 누락된 지속 필드로 세지 않는다.

실측은 다음과 같다. 동일 상태 호출1199회는 새 저장을 만들지 않았다. 제어 이동20초의 실제0.5초 여행 sampler는 진행39회, 각10초의 XP/stack 변경은 각각20회였고, 총50.010초 구간에 profile revision79 증가·records2·receipts46이었다. 마지막 목표 충족+claim은 revision2/receipt1 증가·보상XP7이며 추가 record는0이었다. 이는 제어된 시험의 호출량으로 자연 플레이의 한계 시간을 뜻하지 않는다. 별도 고속 native 반복16384회에서는 records2/receipts65/DB380928바이트가 유지됐고, 실제 인벤토리 경로 반복도12289회를 완료했다. 실행 시간·프로세스별 차이와 원문은 VerifiedFinalResults.measurements 및 각 Write/Read 로그를 따른다.

## 실제 이관 도구

DB container version과 JSON snapshot format은 별개다. 현재는 container2/전체 snapshot2/record schema1이다. snapshot1은 소유 아이템 정보가 없으므로 자동 빈 가방/기본 무기를 만들지 않는다. `Project_JProfileMigration` commandlet은 원본을 read-only로 검사하고 nonempty sidecar·손상·알 수 없는 형식을 거부한다. dry-run은 정의/규칙/ownership을 정상 복원과 같은 검증으로 확인하고 파일을 만들지 않는다. 변환은 고유 staging→전체 비교→덮어쓰기 없는 새 결과이며 부분 실패는 원본/기존 결과를 보존한다. 동일 입력/결과의 재실행은 idempotent, 다른 결과는 충돌이다. rollback은 보존 원본의 provenance를 확인한 별도 복사본이며 live 파일을 전환하지 않는다. container1/full snapshot2는 소유물을 재입력하지 않고 새 파일로 container2에 변환한다.

```powershell
UnrealEditor-Cmd.exe Project_J.uproject -run=Project_JProfileMigration -Source=Original.sqlite -Inspect -unattended -NullRHI
UnrealEditor-Cmd.exe Project_J.uproject -run=Project_JProfileMigration -Source=Original.sqlite -Ownership=Ownership.json -DryRun -unattended -NullRHI
UnrealEditor-Cmd.exe Project_J.uproject -run=Project_JProfileMigration -Source=Original.sqlite -Ownership=Ownership.json -Destination=New.sqlite -unattended -NullRHI
UnrealEditor-Cmd.exe Project_J.uproject -run=Project_JProfileMigration -Source=Original.sqlite -Migrated=New.sqlite -Destination=Rollback.sqlite -RollbackCopy -unattended -NullRHI
```

Ownership은 누락된 각 character의 완전한 snapshot2 객체 배열(입력 상한16MiB/512객체)이다. account/character/rules/P/Q는 원본과 같아야 하며 items에 확정된 definition/version·instance GUID·level/count·bag/order·slot을 명시한다. JSON order는 BagOrder+1(0은 INDEX_NONE)이다. container만 바꾸는 경우 Ownership을 생략한다. inspect는 구조/누락 목록, dry-run은 실제 정의/복원 호환성까지 검사한다. 알 수 없는 필드를 버려서 통과시키지 않는다.

프로젝트 `Saved/LocalProfiles/Profiles.sqlite`와 `%LOCALAPPDATA%/Project_J/Saved/LocalProfiles/Profiles.sqlite`에는 실제 사용자 DB가 없었다. **사용자 DB 이관/전환은 대상 없음**이며 시험 fixture만 변환했다. 실제 전환은 대상·backup·영향 확인과 별도 승인이 필요하다. 이전 writer로 rollback하려면 해당 container와 호환되는 바이너리/원형을 함께 선택해야 한다.

## 최종 검증

최종 통합은117 고유 항목, 성공114·경고 포함 성공3·실패/미실행0이다. 집중 실행과 fault process를 합산해 항목 수를 부풀리지 않았다. 최종 소스와 다른 앞 시도의 실패/이전112개는 최종 성공 근거가 아니다. 의도적 부정 시험의 warning/error, 시작 시 엔진 UnifiedError self-test 진단과 실제 automation index를 구별한다.

| 실행 | 실제 근거 |
| --- | --- |
| Editor/Game | direct UBT 정상 종료, 실행 중 중단/중복 build 없음. 최종 SourceHashes/Arguments/Process 보존 |
| 실제 인벤토리 장기 | 기존 setter→정상 issuer→실제 subsystem→native DB 12289회, 응답 유실2회·재입장·만료 키 거부. 별도 Write/Read 프로세스에서도 반복 |
| 저장소 반복 | full codec/native writer16384회, record 한도2가 찬 상태에서도 저장 계속, receipts65·page plateau·메모리/큐 bound 확인. 별도 Write/Read 및 backup 복원 |
| 실제 호출 빈도 | 50초 제어 이동의 진짜0.5초 travel sampler와 XP/item/보상 API. accelerated 반복을 자연 플레이 시간/처리량으로 부르지 않음. 수치는 VerifiedFinalResults.measurements |
| 복구 | 일반 writer12+정리 경계 writer12 프로세스. 정상0 16회·격리 self-exit99 8회, DB+journal 복구 전 원형/해시와 state/receipt/floor 원자성 확인 |
| 이관 | native fixture와 CLI16회: 정상·dry-run·unknown definition/version·중복 instance/character·잘못된 slot·부분 실패·중복 실행·충돌·손상/unknown schema·sidecar·rollback·container-only upgrade |
| 기존 수명 | Pending/정확 키·lost ack·늦은 completion·퇴역/ASC ABA·정상 shutdown drain·로드 첫 알림의 전체 상태와 admission 거부 |
| 소비자 | native 읽기 전용 BP audit 10 assets/18 graphs/8 function nodes, 저장0. C++/Config/실제 resolver와 함께 판단 |
| 네트워크 | Editor-host dedicated mode+실제2clients+NPC100의 owner/public AOI/FastArray, LagLoss/ReorderLoss의 실제 SSR/cue/rate. 운영 dedicated binary/CCU 증거 아님 |
| 자산/월드/배포형 | 관련 native asset94개 통과(장비/퀘스트/locomotion 각1·montage91), Python 도구17개 시험 통과·MainWorld20장소/의존3581 확인·Windows Cook/Stage, 실제 package MainWorld 진입과 저장/재실행·Editor↔package 교차 |

경고가 포함된 기존3개 항목은 index의 실제 메시지를 유지했다. assert/skip을 약화하지 않았다. 첫 시도의 배열 부정 fixture 자체 사용 오류를 복사 후 검사로 고쳤고, 의도된 warning의 리터럴 괄호를 regex로 정확히 검사하도록 수정했다. CLI의 in-bag+slot 불일치는 정의 검사6보다 앞선 구조 검사4에서 거부하므로 wrapper의 기대 코드를4로 정정해 별도 `_Final2` 결과에서16개를 재실행했다. 첫 Blueprint audit은 검사는 끝났지만 NullRHI의 가상 Slate 창에서 Quit/입력이 처리되지 않아 idle로 남았다. 자동 승인 검토의 거부 뒤 사용자가 명시 승인한 PID31708 하나만 강제 종료했고(-1), `BlueprintConsumers2`의 유한 Python commandlet에서 같은 native audit·동일 결과·정상0 종료를 다시 확인했다. [해당 종료 근거](../../../Saved/MMOArchitectureFinish_20261019/OwnedInspectionTermination.json)와 앞 실패 로그를 보존했다. 빌드를 중단하거나 다른 프로세스를 종료하지 않았다. 모든 기능 assertion과 기대 발생 횟수는 유지했다. 물리 전원 차단·디스크 고장·다른 플랫폼/WAL·수동 시각 품질·실제 운영 서비스는 검증하지 않았다.

## 재검증과 원자료 범위

재실행 전 UE5.8·플러그인·툴체인·LFS 원본을 확인한다. 각 실행은 새 이름과 격리 User 경로를 사용하며 Unreal/UBT/dotnet/LiveCoding/MSBuild/ShaderCompileWorker가 남아 있으면 기다린다. C++ 변경 시 설치 엔진의 direct `UnrealBuildTool.exe`로 대상 빌드를 정상 종료한 뒤 실행한다. 기존 래퍼의 엔진 경로는 실제 설치 위치와 대조한다. 경로가 다르면 같은 인자로 실제 엔진 실행 파일을 사용하며 문서의 로컬 원자료 경로를 새 PC의 필수 경로로 취급하지 않는다.

전체 automation 명단은 검증 요약의 `integration.tests[].name`이다. 프로젝트 루트에서 기존 실행 래퍼에 이 명단을 전달하고 보고서의 고유 명단까지 비교할 수 있다. 아래는 재실행 예시이며 이번 문서 갱신 중 실행한 명령이 아니다.

```powershell
$spec = Get-Content 'Docs/Review/Architecture/Project_J_MMO_Validation_2026-10-19.json' -Raw | ConvertFrom-Json
$expected = @($spec.integration.tests | ForEach-Object name)
$runName = 'MMO_Recheck_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
& './Scripts/Audit/Run-ContinuationTests.ps1' -Name $runName -Tests ($expected -join '+')
$report = Get-Content "Saved/Validation/Continuation_20261014/$runName/index.json" -Raw | ConvertFrom-Json
$actual = @($report.tests | ForEach-Object fullTestPath)
if ($actual.Count -ne $expected.Count -or @($actual | Sort-Object -Unique).Count -ne $expected.Count -or
    @(Compare-Object $expected $actual).Count) { throw 'Automation roster mismatch' }
```

경고는 개수만 맞추거나 통째로 무시하지 않고 실제 항목/원인과 비교한다. 이 automation 예시는 별도 Write/Read·writer 중단·실제 네트워크·패키지 검증 전체를 자동 재현하는 명령이 아니다. 별도 프로세스의 retention은 각 시험의 `-ProjectJRetentionPhase=Write/Read`, 실제 인벤토리는 `-ProjectJGameRetentionPhase=Write/Read`를 사용하고 같은 격리 User 경로를 공유해야 한다. 장애 시험은 사용자 DB 대신 새 fixture에서만 수행한다.

네트워크는 `Scripts/Validation/Measure-Network.ps1`의2clients/NPC100과 `Scripts/Tests/Run-SourceAuditNetwork.ps1`의 LagLoss/ReorderLoss를 구별한다. Cook은 `Scripts/Audit/Run-ContinuationPackaging.ps1`에 MainWorld+Entry를 명시한 뒤 해당 출력으로 Stage한다. 소유 시험 montage는 전용 경로에 준비·해시 기록하고 Cook 전에 퇴역한다. 실제 package의 저장·재실행·MainWorld 진입·Editor↔package 교차 검사는 별도 실행 근거가 필요하다.

이 PC의 원자료는 위 `Saved/MMOArchitectureFinish_20261019`와 실행별 `Saved/Validation`/`Saved/Audit6850`에 남아 있다. 정리한 User·Cook·Stage/package·trace의 원래 경로는 다시 존재하지 않을 수 있다. 보존 DB/sidecar/manifest의 위치와 SHA256은 `RetainedFiles.json`, 삭제 범위/크기는 `CleanupPlan.json`/`CleanupResult.json`을 따른다. 보관 요약만으로 퇴역한 전체 패키지나 trace를 재분석할 수 있다고 주장하지 않는다. 실제 사용자 DB 전환은 여전히 별도 대상·백업·승인이 필요하다.

## 외부 차단과 종료

필요한 외부 입력은 HTTPS API/업무 DTO와 UnknownOutcome 재조회 계약, token issuer/audience/서명 키 공급·회전과 계정–캐릭터/그룹 소유권 권위, 실제 서버 directory/목적지 예약·일회성 ticket/epoch 전환, Server target 지원 엔진이다. 외부 CI에는 고정 Windows/UE5.8·툴체인·LFS 접근이 있는 runner, 격리 checkout·로그/산출물 보존·저장소 gate 권한이 필요하다. 로컬 wrapper 검증을 외부 CI 연결 완료로 세지 않았다. 현재 `GatewayUrl=`이고 인증/운영 저장 서비스가 없으며 설치 배포본은 `Server targets are not currently supported from this engine distribution.`으로 종료한다. 새 서비스/엔진 설치·비용/계정/보안/배포 변경은 진행하지 않았다.

Unreal/UBT/dotnet/LiveCoding/MSBuild/ShaderCompileWorker/게임 프로세스0을 확인했다. 이번 소유 임시 파일의 논리 크기 약5.63GB를 정리했고 실제 드라이브 여유 공간 전후는 [CleanupResult.json](../../../Saved/MMOArchitectureFinish_20261019/CleanupResult.json)에 별도로 기록했다. 주요 증가 원인은 격리 User 캐시·Cook·Stage/package·trace였다. DB/journal/로그/입력·출력 지문과 작은 재현 자산은 보관했고 현재 Binaries/Intermediate·공유 DDC·Git/LFS·다른 작업 파일은 유지했다. 임시 network Content2개는 hash 확인 후 Cook 전에 제거했다. 패키지 실행 파일/캐시 원형이 퇴역한 경로와 보관 매핑은 cleanup manifest를 따른다.

| 이번 증가 원인 | 삭제한 논리 크기 |
| --- | --- |
| 이번 빌드 진단용 임시 출력 | 98 bytes |
| 격리 실행 User 캐시·시험 출력 | 1.782GB |
| 일회성 수정·중복 staging | 88,871 bytes |
| 네트워크 trace | 0.018GB |
| Cook 파생 출력 | 0.536GB |
| Stage 패키지·디버그 심볼 복사본 | 3.291GB |

삭제 직전부터 직후까지 드라이브 여유 공간의 관측 증가는 3.17GB였다. 이는 다른 프로세스의 디스크 활동에도 영향을 받는 관측값이며 논리 파일 크기와 구별한다. 보존 사본 114개/36.36MB는 별도 남겼다. [원인별 집계](../../../Saved/MMOArchitectureFinish_20261019/CleanupSizeByCause.json)와 [보존 매핑](../../../Saved/MMOArchitectureFinish_20261019/RetainedFiles.json)을 보관했다.
