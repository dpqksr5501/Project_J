# MMORPG 아키텍처 구현·검증 작업 — 2026-10-26

사용자가 승인한 MMORPG 기반 고도화의 작업 기록이다. [전체 감사](../../Review/Audits/Project_J_MMO_Foundation_Audit_2026-10-26.md)의 발견과 [205개 확장 계약](../Extensions/MMO_Content_Catalog.md)을 재사용한다. 최종 범위와 종료 판정은 바로 아래의 최신 요청을 따른다. 기능 목록과 실행 구현·서비스 연결·부하 검증 완료는 각각 구분한다.

## 최신 사용자 범위 — 필수 마감 완료

2026-11-04 사용자는 약5일의 작업 시간을 지적하며 **현재 기반의 정말 필수적인 작업만** 요청했다. 이 최신 요청이 아래 장기 기능 지도와 이전 확장 계획보다 우선한다. 과거 전체 고도화의24–40시간 추정도 범위 축소 전의 계획값이며 현재 필수 마감 ETA로 반복하지 않는다.

현재 마감 대상은 실제167의 canonical prediction/서버 판정 실패와, 기존 구현에서 확인된 치명적 실행·상태/저장·복제·수명 결함이다. 수정한 실제 caller와 실패/취소/종료를 필요한 집중 회귀·Editor/Game 빌드로 확인한다. 같은 근거의 전수 감사나 이미 통과한 전체 묶음을 다시 시작하지 않는다. 필수 결함이 새로 확인되면 구체적 trigger와 영향만 근거로 포함한다.

미래 콘텐츠를 위한 거래·시즌·하우징 등 실행 기능의 추가, 선택적 고도화/별도 성능 실험, 물리/Mass·strict Iris 재등록 같은 대규모 엔진 실험은 현재 마감에서 보류한다. 기존 책임·등록·확장 계약과 이전 실패/음성 결과는 보존한다. 운영 provider/소스 엔진 미정의 실제 배포·연동과 전체 규모 성능은 미입증으로 남기며 이를 완료 보장으로 바꾸지 않는다. 새 manager·metadata 경로·queue·timer는 현재 결함 해결에 필요함이 확인되지 않으면 추가하지 않는다.

완료 판정은 현재 기능의 필수 수정 적용, 해당 원래 trigger와 집중 회귀의 성공, 최종 소스/실행물·자산 연결 보존, 남은 환경 의존/보류 경계와 재개 지점의 명확한 인계다. 아래167의 실제 결과로 이 필수 범위를 마감했다. 사용자의 “이것만 하면 일단 아키텍처 작업은 끝”이라는 합의에 따라 새 고도화 단계를 자동 추가하지 않는다. 이 판정은 전체 MMORPG 콘텐츠나 대규모 실서비스 성능의 완성을 뜻하지 않는다. 아래 과거 단계의 active·다음 작업 표기는 당시 기록이다.

## 필수 기반 마감과 예측 전투 실행 시계 수정 — 2026-11-04·167차

166에서 확인한 source packet 시각 불일치를 실제 생산 경로에서 수정했다. 기존 각 역할의 BeginAttackNode 시각과 같은 activation의 node visit을 기존 hit 요청에 함께 운반한다. target rewind는 원래 Query를 유지하고 source packet만 `server node start + (Query - client node start)`로 조회한다. client start는 신뢰된 사실이 아닌 제한된 보정 주장이다. 서버는 현재 key/visit·공간 세대·열린 native packet·시간 범위와 실제 owner connection 지연의 상한을 확인하고 최초 유효 clock을 geometry 이전에 고정한다. 교체·End·공간 reset 뒤 이전 clock은 재사용하지 않는다. 두 RPC는 한 command budget·검증·효과 본문을 공유하며 직렬화 변경은 protocol13으로 구분한다.

실제 전용 서버+두 NullRHI 클라이언트의 기존 moving recipient·고정75ms/loss0 원래 trigger에서 Seq1/key4가 `SpecSubmitted`로 수락됐다. Query44.4251065010234는 실제 target history 내부, source query44.61363332557525는 서버 packet44.609166506677866을 선택했다. authority 접촉0인 상태에서 GE1·HP1000→990·자연 종료/정리를 확인했고, 원 Server.utrace의 같은 thread에서 SSRRequest⊇ApplyEffect⊇ApplySpec 세 scope가 중첩된다. 허용 반경·head tolerance·이동 경로·권위 명중을 바꿔 통과시키지 않았다. 이는 이 실행 시계 불일치의 원인 수정이며 모든 지연·기하·실제 렌더 시각의 보장은 아니다.

첫 strict 보고서는 확장된 accepted 로그를 기존 정규식이 읽지 못해 false였다. 원자료를 보존하고 현재 reader의 같은 functional gate를 부작용 없이 재실행해 별도 `RevalidatedWire167_1.json`으로 통과를 기록했다. 임의 suffix·비유한 시각·잘못된 mapping·다른 요청 결합을 거절하는 순수 파서27개도 통과했다. 기존 runner의 trace 요구는 유지한다.

관련 native는 최초 실행의 유효6개와 최종 단일 시계 사례1개, 고유7개 성공·테스트 경고0이다. 새 fixture의 동기 world tick과 초기화 전 이동 정지 전제는 실제 실패 로그를 근거로 수정했다. 자연 engine frame마다 기존9개 tick을 실행하며 초기화/BeginPlay 뒤 첫 tick 전에 기존 정지를 적용한다. 최종9개 실제 history/capsule 진단과 성공을 보존했다. 실패한 Native167_1·167_2를 삭제하거나 성공으로 바꾸지 않았고 생산 코드는 wire 뒤 불변이다.

최종 Editor367/Game126 full direct UBT0, Source/설정/스크립트1274·runtime38+host1이다.166 대비 Source6·Script1·새파일/Config0·보호28asset 변경0, native 전후와 Game 이후 Source·테스트한 Editor runtime을 대조했다. wire 뒤 변경은 테스트 CPP와 reader뿐이므로 최종 전체 바이너리가 당시 wire와 같다고 주장하지 않는다. 근거는 `Saved/ArchitectureBatch167ExecutionClock/Verification167.json` SHA29186b2e0778d8ead2010d61ab79eb3cf0b483abf2fee9958188e99f0b94d48f, `FinalWorkspace167.json` SHA01a175a5d479440d3037bfc536da35c62c7610e8e39f3275728ab233992a7f1f다.

현재 필수 아키텍처 작업은 여기서 종료한다. 운영 인증/DB/호스팅·서버 영속 provider, 다중 소유자 경제·인스턴스/전역 시즌 등 실제 서비스 실행, 소스 엔진 의존 실험과 대표 콘텐츠 규모의 성능·시각 품질·soak/release는 보류다. 향후에는 선택한 서비스나 콘텐츠의 실제 요구가 생길 때 기존 계약을 연결하며, 아래 장기 지도를 자동 재개 지시로 사용하지 않는다. NPC non-Motion-Matching 정책과 기존 에셋·확장 지점은 유지했다.

마감 정리는 종료한167 세션의 전용 OS Temp 안에서 확인한 생성 registry/shader cache12곳·499,616,460bytes(약476MiB)만 제거했다. engine/build process 부재·정확한 세션 경로·reparse 부재·파일 미사용을 확인했다. 원자료·실패 실행·profile/DB·Git/LFS·프로젝트 빌드/DDC/UBA 캐시와 분류하지 않은 임시 하위 경로는 보존했다. `Cleanup167.json`이 근거이며 이 정리는 자원 증가의 root-cause fix가 아니다.

## 실제 예측 전투의 packet 시각 거절 특정 — 2026-11-04·166차

151·162·157의 기존 근거와 실제 소비 경로를 재사용했다. melee의 local TargetData는 notify→Melee ability→전용 `ServerRequestSSRHit`로 이어지며157 ASC cached-payload gate와 다른 경로다.151의 authority 선행 GE/`DuplicateTarget`은 정상 중복 방지로 유지한다. 이를 수정하거나 합격 증거로 바꾸지 않았다.

새 조건은 기존 고정 native 이동 경로의 PacketLag75/loss0 한 가지다. owner의 실제 role2/local 입력·handle2/key4·몽타주·접촉6, authority native packet/자연 종료·접촉0·GE0에서 `PacketUnavailable`을 관측했다. 기존 rejected log에 정확한 시각·보유 범위만 추가한 동일 트리거 재현은 접촉5와 같은 거절이었다. 두 실행 모두 native0×3·정리 정상이나 strict network 결과는 실패이며 새 수락 증거가 아니다. 다른 타이밍/반경/권위 정책을 탐색하지 않았다.

진단 재현의 Seq1/key4는 query42.582311500280966, attack start42.012281108647585, history5, oldest42.678044106811285/newest42.811192706227303이었다. query가 active start 이후임에도 최초 packet보다95.7326ms 이전이어서 기존50ms head guard를 통과하지 못한다. 따라서 현 실패는 기하 검사 전의 source-packet 선택 경계로 좁혀졌다. target history/LOS의 실제 교차·몽타주/NP/렌더 시각 정렬과 accepted gameplay SSR은 아직 미입증이다. 시간 보정 상수·범위 확대·권위 피해 억제로 가리지 않는다.

공개 Mover/NP API로 presentation state와 current sim ms를 읽을 수 있지만, interpolation/endpoint-hold·fixed copy-forward·별도 smoothing 및 조회 순서를 구분해야 한다. 확인한 공개 표면에 NP 원점/진행률을 서버 world seconds로 바로 바꾸는 API는 없으며 sim-ms/1000을 제출 시각으로 삼지 않는다. 이 API 검토가 모든 엔진 내부 가능성의 부재를 입증하는 것은 아니다.

변경은 `Project_JCombatHitValidationComponent.cpp`의 기존 Verbose rejection payload1곳뿐이다. Editor364/Game125 full direct UBT0, Source1274/runtime38+host1·보호28asset 변경0·Script/Config0·각 wire 전후/최종 Source·Editor runtime 동일성을 확인했다. Game 이후 변경 runtime은 Project_J.exe다. `Saved/ArchitectureBatch166AutonomousSSR/Verification166.json` SHA3e707a21d0b402ba351dc8317df82dcb74ed5602f75a077a6af261ab7b531f05, `FinalWorkspace166.json` SHA92bc925448b9658cc33b705ffc4676a07b9789efa34aa74280a40d1994adc752를 따른다. 이는 관측성 보강과 원래 trigger 진단이며 root-cause fix·workaround·containment나 성능 개선 인증이 아니다. 새 전체 회귀/Cook/Shipping은 하지 않았다.

다음167은 target query time과 source 실행/몽타주 단계의 서버 검증 가능한 결합을 먼저 확정한다. 두 시각을 독립적으로 받아 임의의 과거 blade/target을 조합하는 초안은 채택하지 않는다. 실제 contact 값 생산·same key/node/spatial/window·서버 보유 packet과 activation-clock binding·허용 skew/재전송/종료를 함께 검토하고 필요한 실제 consumer만 구현한다. 반복 wire는 이 계약의 변경 후 원래 트리거에 한정한다. 전체 목표는 active, 다음 필요 빌드는 Editor365/Game126이며 운영 provider·A03/A07·전체 부하/품질 잔여는 그대로다.

## 아이템·XP·완료의 공통 보상 확정 — 2026-11-04·165차

`UProject_JQuestDefinition.RewardItems`의 선택1..8개 고유 item ID/version/count/level을 실제 Claim에 연결했다. 새 콘텐츠는 canonical 정의 등록과 보상 데이터로 같은 실행을 사용한다. 최대 스택은 아이템 정의에서 가져오며 Claim에서 동기 로딩하지 않는다. 빈 배열은 기존 XP-only 실행과 legacy/compound/history-scope 해시 bytes를 유지한다. 제작의 같은 private packer를 호출하는 얇은 `PlanGrant`와 기존 item projection 추출을 사용하며 별도 inventory writer·registry·manager는 없다.

GT에서 잠금/정의 값을 캡처하고 영속 worker는 receipt 조회 후 miss에서만 포장·GUID 생성을 수행한다. 기존 Format4 core+변경 완료 page+lease+receipt의 원자 커밋과 byte/row 예산을 유지한다. 가방 부족·직렬화 초과·XP overflow에서 보상 전체를 보존하고 lost-ack/replay가 ID를 다시 만들지 않는다. native 경로도 비장착 transient lock을 유지하고 일반 가방·XP·완료를 callback-free 설치 후 알린다. 다른 현재 quest의 잘못된 진행도 설치 전 기존 validator로 거절한다.

마지막 extensible ASC getter 뒤 checked progression installer가 정확한 binding/PS/참가자·현재 규칙·가방/장비를 다시 확인한다. native→persistent writer 정책 변경도 거절한다. publication은 같은 본문에 값싼 owner/revision fence를 전달해 실제 변경 행마다 검사한다. 지급 전 신원 교체는 미설치, 지급 후 교체는 후속 알림 중단이며 확정 결과는 원래 profile 재열기로 복구한다. 공개 API를 통한 재진입·ASC 교체/GC·uninitialized XP0·잠금·완료 재청구를 실제 consumer에서 확인했다. 이 보상 경로의 검증을 AbilitySet 내부 모든 spec의 새 세션 취소 보증으로 확대하지 않는다.

최종 Editor363/Game124 full direct UBT0, 관련 고유12개 사례는165_1의 유효6+165_2의 유효5+165_3의 수정 후1이며 테스트 경고0이다. 최초359의 컴파일 실패(dynamic delegate/private DB 접근), native165_1의 진단 hook 준비 시점 assert와165_2의 기본 quest 빈 목록 전제 실패를 보존했다. 테스트는 기존 dynamic receiver와 DB 경로만 설정하는 test friend를 사용한다. 초기 checkpoint의 실제 pending을 기다리고 로드 전 pristine snapshot(current1/history0/revision0/items0)을 보존해 검사했다. 생산16파일·나머지 실제 테스트는 세 실행 사이 불변이고 해당 test CPP의 setup/실패 전제만 보강했다. 전체 성공 묶음을 반복하지 않았다.

Source22파일(생산16·test/support6, 신규6)·1274항목/runtime38+host1, Config/Script0·보호28asset 변경0이다. 각 native 전후 지문과 최종 Source·Editor runtime이 일치하고 Game 이후 변경 runtime은 `Project_J.exe`뿐이다. `Saved/ArchitectureBatch165QuestRewards/Verification165.json` SHA0be975ff4fc1037426d6c4a21d9b6325e4d49b7ff9222d7bc882a9628b14c4d9, `FinalWorkspace165.json` SHA0233a0a52ab7d7e1619743675374c7e6a54618e740a280447576cfa8f410d429를 따른다. 새 socket 보상·운영 서버 저장·인증·분산 writer·반복/계정/시즌 규칙·부하/FPS/CCU·Cook/Shipping은 별도다. NPC non-MM과 기존65536 완료/4096 item/64KiB record/256KiB transaction 예산은 유지한다.

다음166은 기존151·162·157 근거를 재사용해 실제 autonomous 입력/GAS/몽타주의 accepted SSR 미완료를 이어받는다. authority 선행 피해의 정상 DuplicateTarget과 통제된 RPC 기하 성공을 accepted gameplay SSR로 바꾸지 않는다. 엔진/운영 provider 미정 잔여를 제외하거나 새 선택 기능으로 종료점을 늘리지 않으며 전체 목표는 active다. 다음 필요한 빌드는 Editor364/Game125다.

## 완료 페이지의 실제 소유자 전용 통신 — 2026-11-04·164차

163의 새 조회 경로를 기존 AuditNetwork의 opt-in `QuestHistoryOnly`에 연결했다. 실제 native PC/PS·기본 QuestComponent를 두 socket에서 사용한다. 서버만 검증한65개 완료/활성1 상태를 준비하고, 클라이언트는 public RequestCompletedPage로32/32/1을 수신한다. 다른 PS와 기본 컴포넌트가 실제 존재함을 양성 대조로 두고 foreign summary GUID·진행·page가 모두 비공개 기본 상태임을 함께 확인했다. slot1의 own empty page는 valid summary/nonce의 실제 응답으로 확인하며 단순 기본값0을 성공으로 세지 않는다.

서버의 실제 immutable Append/Install/Publish로66개가 된 뒤 owner summary revision 변경·이전1개 page의 Stale·새 nonce 재조회32개를 확인한다. 성공한 생산 수신기의 page view에도 실제 수신 Request/RequestedOrdinal을 남겨 현재/이전 응답의 상관관계를 관측할 수 있게 했다. Pending의 정확한 취소는 마지막 확인 page/nonce를 보존한다. 수신 승인·보상·저장 계약을 바꾸는 새 writer는 없다.

기존 진단 controller와 runner에 case80만 opt-in으로 추가했다. authority/client각6행의 실제 native 객체·NetMode·요약 revision·응답 nonce·ID 순서를 strict parser로 대조한다. ACK 이후0.55초의 실제 tick 간격으로 기존0.5초 승인 정책을 지켰고, 미매핑 control ref의 제한 재전달은8초 관측/20초 phase/140초 전체 기한을 연장하지 않는다. 기존 warning/session/NPC verifier 본문은 동일하며 합성 parser12검사·AST0을 확인했다. client state/page 주입·새 UCLASS·relevancy override·예산 retune는 없다.

**검증:** Editor358/Game123 full direct UBT0(18.52/21.16초), `NativeBatch164_1` 관련2개 Success·경고/실패0/native0이다. 기존163의17개 성공은 반복하지 않았다. `QuestHistory164_Baseline1`과 `QuestHistory164_LagLoss1`은 각각 실제 전용 Editor 서버+2RHI client의 native0×3·timeout0, server6/client각6행과 정상 정리를 통과했다. 후자는 실제75±25ms·10%손실·5%중복의 native 설정도 대조했다. native2와 두 통신 실행의 Source/runtime 전체 지문이 같고 최종 Source·Editor 바이너리도 같다. Game 뒤 바뀐 runtime은 Project_J.exe뿐이다.

Source/Script6파일(신규 private inl1)·1268항목/프로젝트 runtime38+host1·Config/보호28asset 변경0이다. 근거 `Saved/ArchitectureBatch164QuestHistoryWire/Verification164.json` SHA `39d97a6263434f89575ddd1b7ec181100bad7cfafab3644f14aac0b588bbaac0`, `FinalWorkspace164.json` SHA `a4ca1623b2567762ac9be21f23679d29d5e36748767465f7a6111962034c7063`. 읽기 페이지/요약의 소유자 통신 근거이며 네트워크 영속 보상·모든 queued RPC 접속 세대/순서·UI 픽셀·CCU/FPS·운영 provider·새 Cook/Shipping 증명이 아니다.

다음165는 XP-only Claim의 아이템 bundle을 기존 inventory packing/원자 commit에 함께 연결하는 필수 실행 경계다. 현재 PackOutputs를 craft/claim에 재사용하고 persistent/nonpersistent 양쪽의 all-install→notify 계약을 검토하는 Saved 설계 단계이며 아직 Source 구현으로 세지 않는다. 전체 목표는 active다.

## 활성 퀘스트와 영속 완료 이력 분리 — 2026-11-04·163차

161–162에서 확인한 수락 제한의 원인은 활성 진행과 보상 완료 행을 같은128 상태에 계속 보관하는 것이었다. 현재 상세128개와 immutable 완료 페이지64ID×최대1024개(명시 예산65536ID)를 분리했다. 성공한 Claim은 완료 사실을 추가하고 상세를 제거한다. 새 수락은 완료 ID를 재사용하지 않으며 현재 상세의 향후 완료 공간도 예약한다. 반복/계정/시즌별 완료 규칙을 새로 만든 것은 아니다. 최초의1024 평생 한도·전체 완료 FastArray 초안은 채택하지 않았다.

값 snapshot은 공유 포인터를 복사하고, 일반 아이템/XP/진행 변경은 완료 이력을 복사·직렬화하지 않는다. Append는 최대64개 tail과 최대1024개 page 포인터만 교체하고 sealed page를 재사용한다. 권위 없는 full capture는 명시적인 unhydrated 상태이며 클라이언트는 현재 States·완료 요약·조회 결과를 사용한다. 현재 ID/동일 immutable history의 검증은 기존 scope를 재사용하고, 신규 ID·바뀐 history·cold restore는 교집합을 확인한다.

저장 Format4는 기존64KiB hot core에 count/root만 기록하고 별도 페이지를 기존 repository의 lease→core→변경 page 순서 CAS와 영수증으로 함께 확정한다. 기존 per-record64KiB/transaction256KiB·64mutation 한도는 유지한다. 정상 hot write는 history page I/O0이고 첫 load는 같은 SQLite read transaction의 core/page·digest와 실제 revision을 검증한다. 알려진 immutable prefix는 재사용하며 실패 시 부분 snapshot을 게시하지 않는다. 새 저장 서비스/스케줄러는 없다. 개발 LocalProfile만 record budget4096을 명시하며 generic 기본1024와 Standalone guard를 유지한다. 이 값은 운영 캐릭터 수용량이 아니다.

레거시1–3 문서는 원래 형식·규칙을 먼저 검증한다. load만으로 claimed 행을 지우지 않고 다음 적법한 변경에서 이관하며, 기존 raw claimed 행을 보존한 coalesced progress도 허용한다. 이미 이관한 완료 사실은 제거된 옛 정의를 다시 요구하지 않는다. 그러나 이관 전 정의/보상 검증에 필요한 자료가 없으면 자동 reset하지 않는다. offline mixed DB Inspect/Plan/Create는 페이지를 보존한다. 제작 시작/환급도 Format4와 같은 history view를 유지하고 metadata-only profile은 거절한다.

owner-only 요약(count+FGuid revision), 최신 nonce/character/session/source를 확인하는32ID 조회, 수동 재시도 가능한 실제 journal 완료 탭을 연결했다. 현재 행128/완료 label32를 재사용하고 이전 source의 버튼·요청·delegate를 정확히 해제한다. 일반 UI Refresh는 조회를 자동 반복하지 않는다. 조회하지 않은 ID는 클라이언트에서 Unknown이며 완료 전체를 join/Claim마다 복제하지 않는다. wire 변경으로 realtime protocol11→12를 올렸고 기존8/직전11의 실제 엔진 checksum·replay 비호환을 native로 확인했다.

**검증:** Editor357/Game122 full direct UBT0(65.00/39.69초), `NativeBatch163_1` 관련17개 전부 Success·경고/실패0·native0이다. 실제 legacy128→새 수락/129번째 완료와 XP,65536개 bulk 값·sealed pointer/실패 출력 보존·constant hot core, SQLite 원자 page/core/receipt·누락/변조·stale revision·raw legacy progress·offline 이관, 기존 실제 재접속/보상·제작·UI source/GC/페이지를 확인했다. native 전후 Source/runtime와 Game 뒤 최종 Source·Editor 바이너리는 일치한다. Source28파일(신규5)·1267항목/프로젝트 runtime38+host1, Config/보호28asset 변경0이다. 근거 `Saved/ArchitectureBatch163QuestHistory/Verification163.json` SHA `a32a3a2b8fd25756865a53e1be9cc6baff6e9fbae9ce3f22974298e6bf336483`, `FinalWorkspace163.json` SHA `57ed35b001e057907fb9c6a56d2c1ead8d8040e59a49ebc3950ea9e2e594c974`.

이는 수명 상태 혼합의 구조 수정과 해당 로컬 통합 마감이다. 새 page RPC/owner privacy의 실제2socket 전달은164의 다음 집중 범위이며 아직 성공으로 세지 않는다. Game은 Development이고 새 Cook/Stage/Shipping·운영 인증/저장·CCU/FPS/메모리 전체 용량은 별도다.164 검증 후보는 Saved에서만 준비 중이며163의 검증 입력은 바꾸지 않았다. 전체 목표는 active다.

## 퀘스트 저널 재사용과 서버 접촉 질의 — 2026-11-04·161–162차

현재 Quest→UI OnChanged→native HUD 소비가 매번 전체 행·텍스트·버튼을 생성하던 경로를 현재 source/QuestId별 재사용으로 바꿨다. 같은 행·label·command button·button content와 순서를 유지하고, obsolete ID는 즉시 분리한다. cache는 기존 상태 계약128을 넘거나 ever-seen 이력으로 커지지 않는다. 기존 generic command button의 TextBlock content도 재사용한다. 화면/source/owner 교체·종료에서는 HUD command 참조를 먼저 비우고 disable하며, 전체 old binding detach 뒤 exact old screen을 정리한다. 다른 live UI가 이미 소유한 화면은 이전 owner가 지울 수 없다. snapshot/revision·강한 pin으로 콜백/GC 뒤 새 출력을 보호한다. 새 VM·queue·timer·manager·UCLASS는 없다. 행 생성/slot padding·문구·추적/보상·지도 계약은 유지했다.

실제 native widget/OnClicked에서 반복12회 행·label·button·text·panel slot identity, native Slate offset9, 순서 변경·제거, 한 번의 tracking publication과 실제 XP17 claim, same-ID 새 source/owner의 이전 button 무효화, 이전 UI cleanup 이후 새 rows 보존, GC·128→1·NativeDestruct를 확인했다. 초기 테스트는 Slate를 만들기 전 UScrollBox getter가0을 돌려 실패했다. 엔진 소스로 원인을 확인해 테스트에 TakeWidget과 실제9 양성 대조만 추가했다. 원래 보존 assertion·생산 코드는 그대로고 실패 자료도 보존했다. 이 결과는 widget-object 재사용·수명 근거이며 실제 viewport 픽셀/접근성·focus 행렬·CPU/ms/FPS/heap 절감 측정은 아니다.

서버 SSR의 no-Rewind fallback이 expanded segment AABB와 actor bounds 겹침만으로 확정하던 경로는 실제 native sphere sweep의 requested actor 접촉을 요구하도록 수정했다. live-pose/canonical 두 notify producer와 SSR은 동일 private query 정책(ECC_Pawn·sphere·owner ignore·physical material·face metadata·native OutHits)을 재사용한다. owner/metadata용 FCollisionQueryParams는 dispatch/request당 한 번 만들고 각 구간에 const ref로 전달해 해당 설정을 구간마다 반복하지 않는다. capsule 전용 대체로 기존 Pawn primitive 지원을 좁히거나 client hint를 판정 geometry로 쓰지 않는다.

선택된 packet이 있으면 모든 실제 접촉 후보의 Visibility LOS도 그 server segment origin에서 검사한다. 기존 legacy admission은 client hint가 아닌 **최신 server trace origin**을 사용했으며 선택된 과거 packet과 달라질 수 있었다. packet 미발견의 기존 live admission·기본 검증·요청 예산/순번/중복·authority 직접 경로·history interpolation·canonical 제작 geometry는 보존했다. 실제 physics control에서 coarse 대각선 상자만 겹치는 경우 GE0/Health 보존, 실제 접촉의 GE/Health·힌트 독립·dedup, 선택된 old origin 차단/latest server origin 허용 및 반대 방향을 확인했다. no-Rewind는 현재 물리이고 LOS는 현재 visibility다. 잘못된 coarse 검사를 정확한 질의로 바꾼 만큼 해당 fallback의 비용이 늘 수 있으며 성능 절감으로 보고하지 않는다.

Editor356/Game121 full direct UBT0이다. Native161162_1의 유효7개와 UI 준비만 고친 Native161162_2의1개, 고유8사례가 최종 관련 결과 기준 경고/실패0이다. 생산9파일·기존 회귀 본문은 유지했고 유효7개를 반복하지 않았다. 기존 `SSRGeometry162_Baseline1` 실제 서버+2socket의6 SSR사례와 정확한 float64 timestamp 왕복도 native0×3/timeout0·Source/runtime 전후 동일성으로 통과했다. 이는 controlled inherited RPC/health 경로이며 A09 실제 autonomous 입력/GAS/몽타주의 accepted SSR이 아니다. 최종 Source와 wire의 Editor 바이너리는 Game 빌드 뒤에도 대조했다.

Source11파일(신규 private query h/cpp2),1262항목·프로젝트 runtime38+별도 host1, Config/보호28asset 변경0이다. `Saved/ArchitectureBatch161QuestProjection/Verification161.json` SHA19fe2afb16809b69a2ba6a95d771bc77abcd92dee32a1f1ae939b8aaff02d630, `Saved/ArchitectureBatch162SSRGeometry/Verification162.json` SHAa6703c2ba400750f51a41334fa4ebbf1697a6fddd21d26d76fcff6618b489dd8, 공통 `FinalWorkspace161162.json` SHAcf6287e6a8236bf3191a087f9408993f4ecd585c582b0e803c31d793bbf4d7c9가 근거다. 저장/quest XP/craft escrow/session detach의 기존 원자성은 재사용했고 새 provider/server profile manager를 만들지 않았다. 새 Cook/Stage/Shipping·규모/장기 성능·A03/A07/A09·운영 저장/분산 권위는 별도이며 목표는 active다. 다음 필요 빌드는 Editor357/Game122다.

**후속 필수 진행 규모:** `QuestComponent.cpp:250`의 PlanAcceptance와`:259`의 Accept는 claimed를 포함한 전체 States128에서 거절한다.`:383` 이후 Claim은 bClaimed를 게시하고 행을 남기며 snapshot은 States만 갖는다. 따라서 현재128은 단순 활성 퀘스트 예산이 아니다. 방대한 누적 진행을 위해 활성/표현 상태와 compact 완료 이력을 구분하고 기존 claim·재수락 방지·snapshot/receipt·이관·복제·저널 소비를 함께 보강해야 한다. cap만 늘리거나 완료 행을 지워 재보상을 허용하는 방식은 완료 조건이 아니다.161의 UI cache는 현행 계약에 맞춘 재사용이며 이 후속의 대체물이 아니다.

## 공유 명령 응답 승인과 NPC 동적 복제 복구 — 2026-11-04·159–160차

**실제 생산 변경:** 공유 PS 명령 예산이 gameplay work를 거절한 뒤에도 Inventory use/bag와 correlated equipment가 요청마다 Reliable 실패 응답을 만들던 경계를 기존 crafting 정책에 맞췄다. 승인 실패에서는 새 응답·영수증·변경을 만들지 않는다. 장비의 private executor는 caller의 stack-local 승인 결과만 돌려주며 기존 direct/legacy 결과와 단일 과금을 보존한다. 승인된 도메인20/s RateLimited, persistent Pending·이미 수락한 최종 완료는 계속 전달한다. wrapper의 강한 self pin과 기존 publish-liveness 검사를 사용하며 새 상태·queue·timer·RPC는 없다. 기존 UI의5초 source/request timeout과 durable unknown 처리·재시도 정책은 바꾸지 않았다. 거절 후 응답 생성의 원인 수정이며 native 수신/channel 한도·실제 byte/latency/메모리/FPS 효과를 측정한 것은 아니다.

**명령 검증:** 실제 공유 예산을 소진한 후 세 핸들러24호출에서 추가 완료 응답·use/bag 영수증·아이템/장비/Health 변경0을 확인했다. 독립 owner의 단일 과금과 정상 오류 응답, 공유 승인을 받은 로컬20/s 거절 응답, 직접 executor의 RateLimited, 예산 소진 뒤 이미 수락한 persistent Pending의 최종 callback을 확인했다. 마지막 것은 기존 persistence port의 callback 계약 stub이며 DB/실제 durable 상태 설치 증거가 아니다. Native160_1의 기존3사례는 성공했고 신규 제어군만 실패했다. 빈 instance GUID가 공개 RequestUseItem에서 RPC 이전에 거절됐으므로 해당 테스트 입력 두 곳만 유효한 미소유 GUID로 고쳤다. Native160_2의 신규1사례가 경고/실패0/native0으로 통과했으며 유효3개를 반복하지 않았다. 생산4파일과 기존 회귀 본문은 유지했다.

**실제 원격 소비:** 기존 AuditNetwork에 별도 opt-in `NPCAssemblyOnly` case70을 연결했다. asset/공격 grant 없는 실제 native NPC와 빈 activation 정의로 생산 helper를 사용한다. slot0가 먼저 NPC/pair0을 관측하고 helper 생성+수동 warning의 active 상태를 ACK한 후 slot1을 한 번 늦게 접속시킨다. 두 클라이언트의 native 등록·BeginPlay·복제 pair1과20선 RHI marker, 명시 EndAttack/Stop 후 actor 수명 보유, 동일 객체/컴포넌트 재시작·새 lease, 정상 Actor/component와 helper가 생성한 controller의 퇴역을 확인한다. 수동 경고는 fixture가 소유하므로 helper Stop이 외부 경고를 종료한다고 세지 않는다. client 생성·private property 주입·관련성 변경·강제 LOD·Iris Stop/Start는 없다. component ID는 프로세스 안에서만 비교한다.

정상 `NPCAssembly159_Baseline2`와75±25ms 지연·10%손실·5%중복 `NPCAssembly159_LagLoss1`은 각각 native0×3·timeout0, 실제 NetDriver 설정, server6/client0=6·late1=5행이 통과했다. late1의 최초 active tuple RPC는0인데 실제 marker를 복구했고, 재시작 tuple RPC는 양쪽1로 기록돼 logging/transport 양성 대조도 유지한다. 최초 slot0 RPC는 component 준비보다 앞설 수 있어 수신 수를 강제하지 않는다. 두 조건은 동일 Source/runtime이고 Game 빌드 뒤에도 Source와 wire가 사용한 Editor 바이너리가 유지됐다. 이 프로토콜의 순수 parser18개는 허위 통과 방지 검사이며 게임 실행 근거와 구분한다.

최초 `NPCAssembly159_Baseline1`은 초기 RPC의 actor 참조를 확보하지 못해 실패했고2프로세스만 정상 drain했다. 이후 ControlV3에서 기존 generic 준비 경로처럼 미확정0/1단계에 정확한 object-ref control을1초 간격으로 제한 재전달한다. 이미 기록한 단계는 재기록하지 않고8초 관측·20초 phase 기한은 연장하지 않는다. 실제 Baseline2에서 최초 param null/replica0, 약1초 뒤 같은 RPC의 실제 NPC 참조 유효/replica1이 관측됐다. 카메라/관련성/생산 복제의 결함으로 단정하거나 값을 바꾼 수정이 아니다. Editor351의 local Pending 이름 충돌·TObjectPtr 순회 추론 오류도 정확한 이름/타입으로 수정하고 원자료를 보존했다. 빌드는 중단하지 않았다.

최종 Editor354/Game120 full direct UBT0이다. 합계 Source/Script10파일(생산 경계4·검증/소비자6, 신규 inl1),1260항목·프로젝트 runtime38+별도 엔진 host1, Config/보호28asset 변경0이다. `Saved/ArchitectureBatch159NPCRemote/Verification159.json` SHA f441d56f911fb57dea6ac76df70931ba6ce972c7f8b468300665f8920cbf3424, `Saved/ArchitectureBatch160CommandReplyAdmission/Verification160.json` SHA1bb154ad1ce777ef243335e0378beb7b30cfed540906cee18b1f480b03c301ce, 공통 `FinalWorkspace159160.json` SHA47f94d99796cb2ddb2720af770f53caf7f8ca24e2daadc8ba0c44fcd0ec827de를 따른다. 새 Cook/Stage/Shipping·원격 실제 애니메이션/피해·경고 픽셀/피해 이전 도착/모든 audience·A07·규모 성능·운영 연동은 완료로 세지 않는다. 공급자와 소스 엔진은 미정이고 전체 목표는 active다. 다음 필요 빌드는 Editor355/Game121이며 같은159/160 근거를 다시 시작하지 않는다.

## 전체 준비도 점검 후 NPC 전투 조립 보강 — 2026-11-04·158차

[전체 준비도·재사용 범위 점검](../../Review/Audits/Project_J_MMO_Architecture_Readiness_2026-11-04.md)의 생산/검증 fixture 차이를 실제 진입점에서 닫았다. NPC `StartCombatActions`는 Action/Scoring/Activation뿐 아니라 HitValidation·Presentation도 grant 전에 준비한다. 기존 exact-one 등록·begun·replicated 인스턴스와 tags/tick/BasePresentationSet을 보존하고, 잘못된 중복/미등록 구성은 대신 덮어쓰지 않는다. 등록/능력 callback 뒤 request·controller·ASC pointer/revision·owner/world와 외부 실행 claim을 재검사한다. 실패는 이 호출이 만든 unclaimed 자원만 회수하고, 성공한 두 tickless 컴포넌트는 actor 수명 동안 Stop/Start에 재사용한다. 새 manager·Tick·강제 profile 로드·NPC 모션매칭은 없다. 기존 능력의 exact End/Stop 계약을 유지했다.

실제 Nav/RenderedCrowd 소비자의 수동 HitValidation 보충을 제거했다. 기존 callback fixture로 lazy 생성/재시작·설정 보존·중복/미등록 거절·등록 후 외부 claim·grant 중 destroy/GC와 같은 ASC의 ActorInfo 교체를 확인했다. Editor350/Game119 full direct UBT0이다. 최초 Native158_1의5사례는 유효 성공이고, 유일한 실패는 변경하지 않은 저장 NPC 궤적의 엄격한 재생성 일치 검사였다. 공개 read-only preview로 관련 NPC·플레이어 두 경로 모두34개 키·시간·바인딩·윈도우가 정확히 같고 최대 좌표 차이0, stamp만 다른 것을 확인했다.153의 non-Transient reflected Purpose 추가와 전체 notify metadata 해시는 이 오래된 제작 지문의 원인을 뒷받침하지만 개별 hash byte를 분리한 증명은 아니다.

원본 두 파일을 백업하고 frozen preview에 맞는 기존 공개 Bake execute/save로 파생 데이터를 갱신했다. 판정·시간·socket·소스 asset·hash 계약이나 비교 tolerance는 바꾸지 않았다. 새 프로세스 Native158_2에서 원래 strict 전체 struct 비교를 포함한 실패1사례가 성공·경고0이고, 등록된 선택 Data Validation도2/2·skip/unable/경고0·입력24개 불변이다. 보호28입력 중 이 두 파생 공격만 변경됐다. 기존 RHI 실제 실행(NPCAssembly158_1)은 Player1+non-MM NPC1+별도 passive recipient1에서 helper 조립·실제 montage·GE1/HP10·자연 종료·예산 복귀·actor/mesh 정리·dirty 보존을 통과했다. 해당 실행은 지문 갱신 전 입력이며 수치 geometry는 이후에도 동일하다.5개 성공 사례는 반복하지 않았다.

담당 생산2+검증 소비자3, Source/Scripts1259·프로젝트 runtime38개(+별도 엔진 host1개)·Config0이다. 각 실행 전후 지문·최종 Source와 재사용한 Editor 바이너리를 대조했다. 로컬 `Saved/ArchitectureBatch158NPCCombatAssembly/Verification158.json` SHA c026e33b9f84c25e108e2554134314ed19b1a206dd1edc643d6abc417547f653, FinalWorkspace158 SHA131414f8c929418605c169726573d37dd524ff8d76c1fcb3be972d447423127b가 근거다. 원본은 같은 묶음의 `AssetOriginal/Content`에 보존했다. 이후 편집이 있으면 무조건 덮어쓰는 rollback을 하지 않는다. 미적용 C++ stamp 진단 후보·최초 실패도 보존했다.

**남은 경계:** 실제 원격 NPC dynamic component 생성/지속 경고 복구, A03/A07/accepted SSR, 실제 서버 저장·인증·분산 권위·도메인별 typed transaction/운영 통합, 대표 혼합 성능/품질은 별도다. 새 Cook/Stage/Shipping은 하지 않았다. 임의 asset import만으로 rig·graph·판정·HLOD/네트워크 계약이 자동 완성되지 않으며 기존 제작/등록 규칙을 따른다. 전체 목표는 active이고 아래157의 유효 근거·잔여를 재사용한다. 다음 빌드가 실제 필요하면 Editor351/Game120이며 이미 통과한 같은 입력을 다시 시작하지 않는다.

## 현재 로컬 마감과 환경 의존 잔여 — 2026-11-04·157차

전체 기능군/신규 에셋 자동 적용 여부에 대한 최신 종합 판정은 [준비도·재사용 범위 점검](../../Review/Audits/Project_J_MMO_Architecture_Readiness_2026-11-04.md)을 따른다.157의 국소 마감은 전체 MMORPG 아키텍처·성능/확장성 완료가 아니다. 현재 Source/Config와 기존 검증을 대조했으며 새로운 엔진/부하 실행이나 생산 변경은 하지 않았다. 공통 정책은 연결 조건을 만족하는 콘텐츠에 재사용된다. 미연결 typed domain/운영 소비, NPC capability 조립과 제작/호환 검증, 기존 엔진/시간 경계 및 실제 혼합 규모·품질은 남는다.

### 157차 GAS 데이터 실행 승인과 수명

standalone TargetData/Cancelled/GenericEvent/VectorEvent의 미사용 handle/key가 native 선형 cache에 누적될 수 있던 진입 경계를 기존 ASC에서 보강했다. 현재 세션·owner/avatar/revision, 실제 native 활성 인스턴스와 key를 확인하기 전에는 cache를 만들지 않는다. 새 client 활성화는 확장 callback 전에 실행 슬롯을 예약한다. `ClientPayloadExecutionLimit` 기본128은 예약/활성/종료 중 작업을 기준으로 새 client 요청을 승인하는 headroom이며, 실제 서버 시작 작업은 한도를 넘어도 추적·수신한다. 전역128·동접 수용량·GPU/FPS 상한을 뜻하지 않는다. 기존156의 shared80/s와 배치 단일 승인은 유지하고, 이미 승인된 데이터·취소·release·native End에 새 transient 예산 거절을 추가하지 않았다. 거절된 배치의 target data가 기존 같은-key 실행에 전달되는 경로도 막았다.

실행 기록은 실제 native Notify에서만 게시한다. native cache의 handle+Current 별칭과 full key를 구분하고, 서버 생성 키가 임시 요청 예약과 같은 Current를 쓰는 경우에는 자기 Reserved bookkeeping만 제외한다. 외부 payload 자체로 기록을 만들거나 키의 숫자 증가·재사용 시각을 추정하지 않는다. 종료 callback의 역순 실행과 InstancedPerExecution의 IsActive 의미를 고려해, header-only `UProject_JGameplayAbility`가 자신의 보호된 native 활성·종료 상태를 읽도록 했다. 별도 상태·End override·manager·timer·wire protocol은 없다. 기존 전투4개 C++ 부모와 테스트 Ability를 이 기반에 연결했고, payload 진입에서 임시 instance 배열 복사도 제거했다.

native Clear가 pool로 옮기기 전에 payload를 Consume하고, 실제 NotifyAbilityEnded의 Super가 끝난 뒤 캡처한 serial만 반납한다. 수신 대상 변경은 revision/changing guard를 먼저 선점하고, payload 해제가 새 recipient를 설치해도 이전 Init/Clear가 덮어쓰지 않는다. 이전 기록·listener를 먼저 분리한 뒤 정리하며 새 native trusted 실행의 게시를 잃지 않는다. native free-list 잔류와 실제 shared payload 해제를 구분하고 OS 메모리 반환을 주장하지 않는다. native client activation은 원래 시작 전에 ConsumeAll을 호출하므로, 보호할 계약은 활성화 후 task 바인딩 전 데이터다. 미래·비활성 키를 보존하기 위한 새 ACK/tombstone은 필요하지 않았다.

**제작 계약:** client cache-payload를 사용하는 능력은 instanced `UProject_JGameplayAbility`와 LocalPredicted/ServerInitiated 정책을 사용한다. `AbilitySet.Ability`의 일반 `TSubclassOf<UGameplayAbility>`와 native 활성화 계약은 유지했다. 외부 기반 클래스·NonInstanced·ServerOnly/LocalOnly의 payload 지원이 자동 생기는 것은 아니며 이 경계는 fail closed다. 실제 `/Game/GAs`의 Spellblade/CombatToggle/NPC 두 BP, 총4개를 정확히 로드하여 새 기반 상속과 공개 API의 메모리 내 컴파일을 확인했다. native0·4/4·BS_UP_TO_DATE, 저장0·네 파일 SHA 불변이다. NPC 비모션매칭은 유지한다.

**검증:** Editor349/Game118 full direct UBT0이다. Native157_1은9성공/1실패였고, 실패한 fixture의 정상 시작 시점 로컬 제어군을 수정한 Native157_3은 해당1묶음 성공·경고/실패0/native0이다. 재사용한3개 신규 사례 본문과 생산7파일이 그대로임을 비교했으며 기존 성공9개를 다시 실행하지 않았다. 실제 동시 실행·종료 중 late data·payload weak 수명·재진입/파괴자·동기 종료 배치를 확인했다. `TrustedSession157` 전용 Editor 서버+2실제RHI클라이언트도 native0×3/timeout0·server10/client각5행·세션 전이/권한 회수/정리 통과다. 이 socket 검사는 세션·Sprint 호환이며 네 payload RPC의 모든 실제 wire 순서나 accepted SSR을 증명하지는 않는다. native3/BP/socket의 Source/runtime과 Game 빌드 뒤 최종 Source가 일치한다. 담당 생산7+테스트2, Source/Scripts1259/runtime39, Config/에셋0이다. `Saved/ArchitectureBatch157GASPayloadAdmission/Verification157.json` SHA c5aa78f72ebff37c202f85689c37de297ea5997e5f109441905d55309b92e468, FinalWorkspace157.json SHA caef62661d74be167f9479db35fbf07ca270e0089a1dc469e87bef581bc8dc89를 따른다.

Editor346의 protected 접근 오류는 지원되는 실제 하위 클래스 getter로 고쳤다. Native157_1의 비로컬 LocalPred 직접 실행 거절과 Native157_2의 Standalone ServerInitiated 동기 루프백 중복 활성화 ensure도 보존했다. production reset·private 접근·native 역할/키·CDO 강제 변경은 하지 않았다. RPC 역직렬화/임의 payload 크기, 모든 접속 세대의 wire 구분, 글로벌 부하·품질·운영 연동과 새 Cook/Stage/Shipping은 여전히 별도다. 이 근거만으로 전체 목표를 완료하지 않는다.

### 153–156차 재사용 근거

사용자가 목표를 다시 active로 하고 서버 세션·캐릭터 소유권과 필수 경고의 기본안 설계·구현을 위임했다. 153차는 검증 전 명령·생성을 보류하는 서버 진입, 재접속 세대와 이전 입력·GAS 호출권 회수, 필수 경고 분류·기본 표시·복구·종료를 기존 모듈에 구현하고 로컬 검증을 마쳤다. 기존151의 월드·전투 통합 근거는 재사용했다. 소스 엔진·운영 DB/인증/호스팅은 계속 미정이며 실제 공급자 검증이나 분산 권위 인계 완료로 세지 않는다. 이전 blocked는 당시 이력이며 현재 목표는 active다.

155차는 기존 AuditNetwork의 격리 `-TrustedSessionOnly` 경로로 실제 전용 Editor 서버+2클라이언트의 세션 진입·동시 접속 간 권한 이전을 검증했다. 서버가 생성한 임시 계정/캐릭터로 native Begin/Complete를 호출하고, 처음에는 신원/Pawn/명령을 보류하며 승인 후에만 기존 native player를 조립했다. 기존 production Sprint를 두 소유자가 각각 실제 client→server RPC로 활성화했다. 이전 접속의 PS/ASC 채널을 유지한 채 유효한 기존 grant로 요청하여, 변경하지 않은 생산 권한 predicate의 실제 서버 호출·거절1회와 새 소유자의 지속 활성 상태를 확인했다. 늦은 완료·이전 revoke와 다음 정상 tick에서 실행한 captured 작업은 적용되지 않았고, 정확한 최종 revoke·관측자/소유 grant 정리가 통과했다. `TrustedSession155_Baseline3`의 server10행·client각5행·native0×3·timeout0·동일 Source/runtime, 최종 Editor344/Game116 full direct UBT0이다. 근거는 `Saved/ArchitectureBatch155TrustedSessionNetwork/Verification155.json`이다.

초기 두 실행의 phase2 실패는 PC.Pawn과 PlayerState의 nonrep PawnPrivate 캐시를 같은 제어권으로 취급한 검사 오류였다. 실제 진단에서 PC.Pawn=null, 이전 Pawn의 Controller/PlayerState=null, ASC owner/avatar=null을 확인했다. 최종 조건은 실제 제어·ASC 퇴역과 gameplay getter 거절을 요구하고, 정확히 그 detached 이전 Pawn의 native 역참조만 허용한다. 생산 상태 reset·private 필드 쓰기·ClientRestart·시간 우회는 추가하지 않았다. 최초 UHT 이름 충돌/반환형 빌드 실패와 두 실패 실행도 보존했다. 155는 기존 fixture Source2개/runner1개만 변경했고 생산 세션·GAS 코드를 바꾸지 않았다. 외부 credential 검증, 끊어진 socket의 재접속/DB 복원, 같은 접속의 모든 지연 RPC wire epoch, 이동 품질/규모/Shipping 완료로 확대하지 않는다.

156차에서 GAS의 공용 요청 예산 누락을 실제 소스에 보강했다. 기존 활성 세션의 native activation/batch는 shared80/s를 소비하지 않았고 전투 의도는 별도30/s 제한만 사용했다. 이제 일반 client 활성화·의도 요청은 기존 PlayerState 예산을 각각1회 소비하고 배치는 handle/key/owner/avatar/ActorInfoRevision에 묶인 stack-local 단일 사용 승인으로1회만 소비한다. 재진입은 이 승인을 재사용하지 않으며, callback 뒤 다른 recipient에 이전 batch의 TargetData/End가 전달되지 않도록 exact batch만 검사한다. 소진된 배치는 새 활성화/target data를 전달하지 않되 기존 native End 검증은 보존한다. 일반 종료·취소·release와 신뢰 서버의 직접 TryActivate는 과금하지 않는다. 새 manager·CVar·timer·wire 프로토콜이나 기존 intent30/s/순번/시간 규칙 변경은 없다.

Editor345/Game117 full direct UBT0, NullRHI 집중6사례 succeeded6/경고·실패·미실행0/native0이다. 새 `ProjectJ.MMO.CommandBudget.GASClientEntry` 안에서 공유 계수·배치1credit·재진입·소진 시 native End·callback recipient 교체의4개 묶음을 검사했고 기존 세션/공유예산/입력 수명 회귀를 함께 확인했다. 같은 소스로 실제 `TrustedSession156_Baseline1` 전용 서버+2socket도 native0×3·timeout0·권한 전이/이전 호출 거절/정리 통과다. 실행 내/실행 간 Source/runtime 동일성과 최종 빌드 뒤 Source 동일성을 확인했다. 담당 생산GAS Source2/새 테스트1개, Source/Scripts1256/runtime39이며 Config/에셋0이다. Git working-tree CRLF와 Saved LF 후보의 byte SHA 차이는 정규화 텍스트 동일성으로 확인하고 실제 빌드한 live SHA를 별도 기록했다. 근거는 `Saved/ArchitectureBatch156GASCommandBudget/Verification156.json`이다. 154 경고·기존 부하·Cook/Stage를 반복하지 않았으며 전체 FPS/CCU/AAA 완료로 세지 않는다.

156 당시의 후속 cache 누락은 위157에서 보강했다. `PayloadResourceReview.md`의 원래 확인과 근거는 보존한다. 당시 전제 중 activation 전 payload 보존은 native ConsumeAll 순서를 추가 확인하여 실제 task 바인딩 전 계약으로 정정했다. 공급자·소스 엔진 선택은 계속 미정이다.

154차는 남아 있던 경고의 실제 통신 경계를 기존 AuditNetwork fixture의 `-WarningOnly` 경로로 연결했다. 실제 전용 Editor 서버와 RHI 클라이언트2개에서 정상 조건과75±25ms 지연·10% 손실·5% 중복 설정을 각각 통과했다. 설정은 CLI 요청만이 아니라 세 native NetDriver의 값으로 확인했다. 매 실행의 native0×3·timeout0·명시 slot0/1·Source/runtime 전후 동일성을 확인했으며 두 조건 사이에도 같은 소스/바이너리를 사용했다. 최종 Editor339/Game115 full direct UBT0이고 빌드 후 Source도 동일하다. 담당 Source3개와 기존 runner1개를 변경했으며 Config/에셋은 수정하지 않았다. 근거는 `Saved/ArchitectureBatch154WarningWire/Verification154.json`과 `Saved/Audit6850/Round3/Network/WarningWire154_{Baseline1,LagLoss1}`이다.

이 경로는 실제 생산 BeginAttack/BeginCueLease/EndCueLease/EndAttack만 사용하며 복제 property·순서·role·tick을 직접 덮어쓰지 않는다. 서버 한 frame 안의 짧은 시작/종료를 실제 reliable RPC 입구4개와 대조하고, 현재 유지되는 경고는 두 클라이언트의 native20선·등록/RHI 상태·root 부착·Tick0·profile null·Niagara/pending0으로 확인했다. 각 종료는 다음 자연 PlayerTick까지 empty/marker0을 확인했다. 기본 비활성 Verbose 진단은 도착/입구 eligibility를 기록할 뿐 표시 성공으로 세지 않으며 기존 순서·적용 정책을 유지한다. 두 조건에서 ineligible RPC는0이어서 wire상 역전/무효화가 관측됐다는 주장은 하지 않는다. 짧은 경고의 픽셀 표시나 피해 이전 도착, 화면 밖 HUD, late join/AOI 재진입, 실제 audience/피해 형상, GPU·규모·Shipping 품질은 별도 잔여다. 이전13사례와 SSR·몽타주·탈것·부하/Cook을 반복하지 않았다. 새 검증기의11개 합성 검사는 누락/중복 RPC·잘못된 RHI/role/설정·종료 순서의 거짓 통과를 방지하는 검사이며 게임플레이 증거와 구분한다. Editor338의 이름 가림 컴파일 실패도 보존했다.

153차의 구현 계약은 다음과 같다.

- 서버 세션: 검증된 계정 주체에서만 native Begin을 호출하고, 비동기 캐릭터 소유권 검증이 성공한 정확한 시도만 Complete한다. 계정당 현재 캐릭터 하나, 캐릭터당 현재 writer 하나를 호스트 안에서 유지한다. 이전 시도의 완료·로그아웃은 새 세션을 변경하지 않는다. 현재 프로토타입과 Standalone 저장을 운영 인증으로 바꾸지 않는다.
- 로그인 연결: GameMode의 `bRequireTrustedSessions`를 켜면 PostLogin 이후 검증을 기다리고 캐릭터 생성·명령을 보류한다. 기본값 false는 기존 프로토타입 호환이며 운영 인증이 아니다. 최초 연결은 신원·Pawn이 없는 PlayerState만 허용한다. 동일 신원의 세대 교체와 다른 캐릭터의 새 상태 생성을 구분하여 기존 인벤토리·성장 상태를 다른 신원에 넘기지 않는다. `MaxTrustedSessions`는 시작 시 적용하는 active/pending 계정 상한이며 기본128은 개발 기본값이다. 실제 수용 성능을 뜻하지 않고 활성 상태의 즉석 재설정·퇴거를 하지 않는다.
- 검증기 연결 순서: 서버가 계정 주체를 검증한 뒤 `BeginTrustedSession` → 제한된 비동기 소유권 확인 → 게임 스레드에서 정확한 시도의 `CompleteTrustedSession` → 성공한 현재 세션의 `HandleStartingNewPlayer`를 호출한다. 실패·취소는 해당 시도만 취소한다. 전역/서버 간 소유권과 검증기의 deadline·저장 복원은 실제 공급자 연결 시 구현해야 하며 임의의 클라이언트 ID/성공 bool을 신뢰 경로에 노출하지 않는다.
- 필수 경고: 타이밍을 생산하는 NotifyState에서 의미를 분류한다. 기본 주의 신호는 cosmetic Niagara의 로드·생성 예산과 분리하고, 하나의 owner에 표시 하나를 공유한다. 복제 상태와 경고 전용 신뢰 이벤트는 같은 순서 검증·복구 소비자를 사용한다. 실제 피해 영역·화면 밖 HUD·네트워크 지연보다 앞선 표시 보장은 콘텐츠/통신 계약으로 별도 검증해야 한다.
- 제작 계약: 필수 경고의 유효한 태그·양수 구간·서버 실행·확정 발생을 부모 애니메이션 에셋에서 검사한다. 확률·LOD·요청 필터로 필수 producer를 끄는 설정은 실패로 보고한다. 일반 cosmetic 제작 설정은 유지한다.

최종 Editor337 full direct UBT0(7.02초), Game114 full0(181.32초)이다. Editor335/336의 정상 완료도 보존했다. Native153_2는 실제 RHI에서 세션·GAS 활성화/배치 거절·공유 예산·기존 저장·cue 수명 등12개가 성공하고 새 경고 fixture 한 건만 실패했다. 정상 BeginPlay가 생성한 로컬 AI 컨트롤러를 테스트 조건에서 해제하고, 실제 remote budget 분기를 확인한 Native153_3의 경고 한 건이 native0/경고0으로 통과했다. 성공한12개를 반복하지 않았으며 이후 Source 변경이 그 테스트 파일 하나뿐임을 대조했다. 각 실행 내 Source/runtime 지문과 최종 빌드 후 Source 동일성을 확인했다. 초기 Editor 태그 등록/NotValidated 기대값/직접 EndPlay 오류와 두 실패 실행을 보존했다. 담당 Source25개를 변경했으며 Config/에셋은 수정하지 않았다. 근거는 `Saved/ArchitectureBatch153ServerSessions/Verification153.json`과 각 agent의 원본·후보·수정본 기록이다.

경고의 기본 신호는 owner root에 붙은 native component 하나와 고정 local20개 선이다. 렌더 프록시는 복사한 값과 native transform만 사용하고 UObject를 읽지 않는다. 에셋 로드·Niagara 예산·컴포넌트 Tick 없이 정상 이동 transform을 따른다. optional Niagara의 기존 생성 예산·EffectType·pool·컬링은 유지했다. 기본 신호는 relevant actor의 주의 표시이며 실제 피해 형상·화면 밖 HUD·모든 픽셀 가시성·피해 이전 도착 보장이 아니다. 새 reliable 경고 전송의 실제 socket/손실 부하와 native Niagara 컬링 공존은 이번 직접 월드 검사로 입증하지 않았다. 세션 검사는 native PostLogin·possession·GAS 서버 활성화/배치까지이며 외부 인증/네트워크 저장은 미연결이다. 같은 PC의 이미 큐에 든 RPC에 새 wire epoch를 추가한 것도 아니다.

기존 감사·146/149/150과151의 실제 통합을 재사용했다. 이전7–12시간/중심10시간은147/148 시점 추정이며 현재 잔여 ETA로 반복하지 않는다. **전체 MMORPG 수용·운영·품질 완료를 뜻하지 않으며 환경 의존 필수 작업은 미완료다.** 전체 완료시각은 그 입력이 정해지기 전까지 미확정이다. 같은 SSR 장면의 수락이나 새 선택 기능으로 종료점을 자동 연장하지 않는다.

| 구분 | 현재 상태·다음 입력 |
| --- | --- |
| 로컬 실행·수명·제작·배포 통합 |157 Editor349/Game118 full0·payload 실행 수명 집중9+수정1사례·실제BP4개 상속/컴파일·2socket 세션 호환 통과.156 공유예산,155 세션 전이/거절/정리,154 경고 정상/지연손실,153/151 근거 재사용.157 Source의 새 Cook/Stage/Shipping을 수행한 것은 아님. |
| 엔진 지원 수명·실행 | A07 strict 동일Actor 재등록/Iris 참조와 A03 Mover 물리 저빈도/Mass 전환은 선택 소스 엔진·지원API가 필요. Server Target 정의는 실제 독립전용서버 빌드 완료와 다름. |
| 운영 연동 |153 호스트 내 세션/캐릭터 소유권 기본안과155 실제 두 접속 간 권한 전이·이전 요청/작업 거절을 검증. 서버 배치·DB·외부 인증 검증기·실제 다중서버 인계/장애 복구는 provider와 운영 환경 미정. 임시 trusted fixture를 운영 인증/재접속 저장 복원으로 세지 않음. |
| 실제 자원·품질·추가 증거 | 고속월드/HLOD/중요경고/규모 수용은 실제콘텐츠·품질·hardware 입력 필요.106 메모리 증가는149 자료로 귀속불가; 다음 독립적으로 필요한 실행에만 최소귀속 계측 결합. accepted autonomous SSR은151의정상DuplicateTarget으로 닫지 않으며 실제소비시간/호출중첩 증거가 미완료. |

사용자는1~2인개발이며 서버를 아직 정하기 어렵다고 밝혔다. **권장 방향은 로컬 단일 게임서버 중심으로 개발하고 기존 worker-only SQLite/IRepository 계약을 향후 저장 연결에 재사용하는 것**이다. 현재 UProject_JLocalProfileSubsystem은 header의Standalone 개발 checkpoint 계약과 cpp OpenProfile의 NM_Standalone 검사(544·575)를 유지한다. 따라서 현재 멀티플레이 서버 테스트와 SQLite 프로필 저장은 별도 경로이며 네트워크 저장이 이미 연결된 것이 아니다. 네트워크 저장에는 검증된 account/character 소유권·세션을 발급하는 진입과 서버 권위 저장 연결이 필요하다. 이 입력 없이 Standalone 가드를 완화하거나 prototype ID를 운영 인증으로 사용하지 않는다. 운영 DB·실제 인증·호스팅은 계속 미정이며 새 공급자 설치/비용 지출·엔진 다운로드를 시작하지 않았다. 필요한 백엔드가 생기면 기능별 책임은 나누되 프로그램 수는 작게 시작하고, 측정된 부하·장애 경계가 생길 때 프로세스를 분리한다. 이 권장은 사용자 운영환경 확정이나 SQLite의 분산/전원장애 안전성 승인과 구분한다.

152차의 좁은 재확인에서는 새 Source/asset/Native 실행 없이 위 저장 진입과 A12 기존 producer만 대조했다. 현재 combat presentation은 cosmetic CueTag→profile→owner local/viewed·bUseRemoteSpawnBudget 계약이다. 해당flag=false도 spawn-attempt budget만 우회하며 native pre-cull/EffectType count·distance 정책은 유지된다. 기존99/133/142/150의 trail/자원/GPU 근거를 필수 위험 경고 보호로 세지 않는다. 실제 Warning AttackTag/CueTag/profile/System·필수 audience·cold-load/overload시 가시성/종료 계약이 없는 상태라 새enum/manager/전역컬링예외를 추가하지 않았다. 근거는 Saved/ArchitectureBatch150PresentationClosure/MandatoryWarningRevalidation.md에 있다. 이는151의 검증 범위를 줄이는 것이 아니라 미연결/미입증 경계를 구체화한 것이다.

## 일정 정정과 필수 마감 기준 — 2026-11-02

103차에 기록한 현 환경의 필수 잔여40–80시간을130차 완료 뒤에도 재산정 없이 현재 ETA처럼 반복한 보고를 철회한다. 과거55–65%/40–80시간과 아래20시간 목표는 당시 기록이며 현재 완료 예측이나 새 마감 약속이 아니다. 전체 완료시각은 아직 근거가 부족해 미확정이다. 추정이 없다는 것을 새 임의 시간으로 가리지 않는다.

131차 통합을 마쳤으며 이후에는 위 감사/아래 단계3·4의 남은 필수 실행·성능·수명·배포 경계를 우선한다. 선택적인 새 기능·추상화·검증 행렬을 계속 추가해 종료점을 늘리지 않는다. 필수 결함과 미완료 요구는 제외하지 않으며, 기존 근거로 이미 검증된 동일 입력을 이유 없이 반복하지 않는다.

완료 판단은 기존 원래 범위를 유지한다. 로컬에서 구현·검증 가능한 잔여, 소스 엔진/서비스/게임 규칙·품질 입력이 필요한 잔여, 선택적 확장을 구분해 보고한다. 환경 의존 항목을 완료로 바꾸거나, 국소 통과를 전체 AAA/MMORPG 완료로 세지 않는다. 새 ETA는 현재 잔여 작업의 의존성·예상량과 실제 소요에 근거할 때만 제시하고, 근거 없는 고정 시각 완료를 보장하지 않는다.

사용자 후속 요청에 따라 남은 큰 작업을 재구성한 **현 환경의 필수 마감 잠정 추정은18–30시간(중심24시간)**이다. 전체 운영 서비스/모든 콘텐츠/환경 의존 항목의 완료시각이 아니다. 아래16–26시간에 수정 여유2–4시간을 더했다. 이는 실행 계획의 추정이며 확정 납기나 모든 미해결 기술 문제의 해결 보증으로 쓰지 않는다.

| 필수 마감 묶음 | 예상 잔여 | 판정 |
| --- | --- | --- |
| 131차 전투 실행·준비·소유권 통합 | 완료(잔여0) | Editor262/Game94 및 실제 producer·그룹 GC pin·정확한 해제와 기존 이동/어그로24개 native 검증 완료 |
| 복제 수명·스트리밍 미완료 경계 | 6–10시간 | 기존 A07 실패와 A10 실제 수명/준비/자원 경계를 재사용해 수정·입증 또는 필요한 기술 의존성을 특정 |
| 혼합 부하·자원 예산·성능 | 6–10시간 | A03/A06/A08/A09/A12의 실제 소비·주요 비용을 대표 입력으로 판단; 무제한 비교 행렬이나 품질 누락으로 수치 맞추기 없음 |
| 최종 통합·배포·마감 정리 | 3–4시간 | 최종 입력으로 필요한 검증/배포와 코드·데이터 연결을 닫고 남은 외부 의존성을 정확히 구분 |

각 묶음을 닫을 때 남은 작업량을 갱신하고 같은 수치를 계속 현재 ETA로 재사용하지 않는다. 이미 통과한 동일 입력의 반복이나 선택 기능 확장으로 이 목록을 자동 연장하지 않는다. 실제 필수 결함/기술 의존성으로 추정이 달라지면 원인·남은 조건·일정 영향을 명시한다. 소스 엔진/운영 DB·인증/게임 규칙 결정이 필요한 항목은 별도로 남으며, 이 로컬 묶음이 끝나도 그 항목을 전체 완료로 바꾸지 않는다.

131차 마감 후 현재 목록은 기본15–24시간+수정 여유2–4시간, **잠정 잔여17–28시간(중심 약23시간)**이다. 경과시간을 기계적으로 뺀 값이 아니라 위 통합 묶음의 완료로 작업량1–2시간을 제거했다. A07 strict93은 기존 실패/현재 코드/지원 API 근거를 재사용해 선택 엔진의 참조 무효화·재양자화 수명 계약 필요를 특정했으며 전체 해결로 세지 않는다. 이 한 gate를 정상 AOI 시험 반복이나 가짜 client 참조 복원으로 대신하지 않고 A10 등 독립 마감을 계속한다.

138차 현재 아래의 단계별 기록까지 진행했다. 위17–28시간은131차 시점의 작업량 추정이며 현재 ETA로 재사용하지 않는다. NPC 비모션매칭의 실제 Mover/전투 연결을 마쳤지만 실제 NPC 군중의 표현·처리 예산, 혼합 GPU/VRAM, A08 자산 의존성과 A10 준비 경계는 남는다. 환경 의존 A07/서비스 항목도 그대로 구분하며 새 완료시각을 임의로 보장하지 않는다.

## 148 측정 소비자 마감·147 통합 진행 — 2026-11-03

새 전수 감사나 부하 캡처 없이143의 봉인 trace를 재사용해 실제 동시 공격 전후19frame만 내보냈다. 측정된 canonical notify 소비자는89호출/양수6frame·union4.3742ms/exclusive1.1895ms, 최대frame1.4842ms이며 계측된 GT root scope-covered wall 대비0.218%다. 자식 Sweep/LOS/GE의 중첩을 별도로 확인했으며 추가 최적화·병렬화 패치를 정당화할 병목 근거가 없다. BoneFinalized root-anchor capture/Begin/admission은 분리 계측되지 않아 전체canonical/A09 마감이나 FPS/CCU/GPU/누수 개선으로 세지 않는다. Source/예산/품질 변경·새 캡처0이며 `Saved/ArchitectureBatch148ClosurePlan/Verification148.json`·`Closure148.md`를 따른다.

147은 실제 Player compatible skeleton의 RAW name mapping/native retarget와 같은 socket UObject alias 수용·별도 same-name 충돌 거절을 구현하고 기존 NPC keys/stamp를 보존했다. 실제Player34키 제작·원본22asset 불변·새 reference4개·strict6valid/입력27/경고0을 확인했다. fixture의 유일 idle spec 가정을 native 직업/장비 grant 공존·실제 활성 handle/예측key/현재combo 관측으로 고쳤고 정확 recipient Actor 참조·소유 source별 정리를 통합했다. 생산 grant/InputRouter는 유지했다.

Editor326/Game111 full0, 공통5map Cook3187+platform skip7/오류·경고0, Stage native0/동일 admission, Game111과 같은 inner exe의 packaged listen+remote2client/native0×3/timeout0까지 통과했다. 다중구간 최대5/GE1/Health1000→990/자연종료/원gear복원·새source퇴역을 확인했고 covered-history GeometryUnconfirmed는 거절 관측으로만 기록했다. 실제 accepted SSR·전체A09·FPS/CCU·GPU·Shipping·운영 provider 검증으로 확대하지 않는다. Source10/runtime4 변경·보호22+새4asset, FinalWorkspace147 SHA3a9f0493…ca21dff2 및 `Saved/ArchitectureBatch147CanonicalRemote/Verification147.json`을 따른다. 판정·예산·시간 규칙은 유지하면서 부족한 관측을 보강했고 동일200군중/퀘스트/제작 시험은 재사용했다. 전체 목표는 active다.

## 150 GPU 관측 마감·151 선택 통신 경계 진행

150은 기존143의 봉인 trace/148의 정확19frame만 재사용했다. native GPU queues의 strict raw Id/Type/Name·display metadata·thread 동일성과 clipping/union/exclusive가 통과하고, 독립 endpoint sweep도 일치했다. 실제 Niagara GPU Ribbons19양수 event·family64.5627ms를 확인했으며 Compute wall envelope2.0064415s를GPU utilization로 해석하지 않는다. per-owner draw/VRAM/인과FX비용/mandatory-warning/FPS는 미검증이다. Source/품질/예산 변경·새부하 캡처0이며 `Saved/ArchitectureBatch150PresentationClosure/Verification150.json`을 따른다.

151은 현재 Source/Scripts5파일을 보강해 existing ThirdPerson Nav에서 fixed crossing recipient를 normal StartNavigationTo→Mover→실제 LMB로 관측하는 opt-in 사례를 준비했다. 초기 링크의 직접 NavigationSystem 의존성 누락은 Editor328 full0으로 해결했다. 실제151_1은 native0×3이나 Stage0 ready timeout이며 전투·Nav 입력 전이라 unqualified다. Editor330 full0 뒤151_2의 기존 timeout 진단은 두 실제 Avatar의 fallback 원점·Falling/floor 없음·landing0을 확인했다. Nav/공격 전 잘못된 초기 배치 준비가 계속되므로 생성 전 native 충돌 배치 admission을 좁게 수정한다. 정확한 collider 귀속은 미확정이며 임의 위치/시간/형상 변경이나 static147 반복으로 대체하지 않는다. 후속 Editor331/332 full0 뒤151_3/4는 초기 native placement를 즉시 거절했고 모두 native0×3/timeout0이다.151_4의 초기 Nav 투영은 통과했지만 실제 blocking collider 귀속은 미확정이다. SpawnAdmission2는 지면 검증 뒤 PlayerState 게시를 보장하고, 초기2슬롯의 Nav-foot 기반 생성 중심만 도입했으며 아직 이동·LMB 통합 통과가 아니다. 151_5는 실제 capsule42/96의 blocker1을 확인했고 선택 actor metadata는 원점 내부의 SM_Cube13(boundsZ0–200)을 확인했다. 조회 component enum 오류는 partial로 보존했다. 기존 AOI도 X0/Y0로 돌아오므로 초기XY만 옮겨 덮지 않고 작은 정상 Nav 검증 맵의 기존 제작 API를 확인한다. Editor333/Game112 full0, Source1245/runtime39·변경5·보호26asset/원map 불변이며 CheckpointWorkspace151은 최종 완료 지문이 아니다. 새non-WP testmap을 정상Editor post-tick 준비→nativeBuild→실제투영→대상만save로 제작했고, 실제R/RMB동선을 수용한Nav범위만 보강했다.151_7은 native0×3/timeout0·실제11.47m 도착/cleanup·raw5/5·서버GE1/Health1000→990·자연종료1이 통과했다. RPC DuplicateTarget은 정상중복차단이며 수락으로 세지 않는다. Editor334/Game113 full0·Source/Scripts7변경·새testmap1/원26asset+ThirdPerson보존이다. 다음은 새Navasset의선택/Cook·패키지로드만좁게확인한다. accepted autonomous SSR은 여전히 열린 통신 경계다. 전체 목표는 active이며 기존 범위를 줄이지 않는다.

## 기존 진행 기록

사용자는 단계마다 작업을 종료하지 않고 전체 미완료 구현·연결·검증을 계속하도록 명시했다. 이에 지속 목표를 설정했고, 단계별 성공은 중간 검증으로 기록한다. 제품 규칙·실제 콘텐츠·미정 서비스 때문에 아직 구현할 수 없는 경계도 완료로 세지 않는다. 이후 사용자가 용량 문제를 직접 처리해 당시 정리는 제외했다.2026-10-29 사용자가 다시 안전 정리 후 작업 재개를 요청해49차에서 종료된 산출물의 한정 정리를 수행하고 생성 저장 경로를 보강했다. 이는 소스·에셋·Git/LFS·현재 빌드 캐시 전체 삭제 권한으로 확대하지 않는다.

2026-10-27 10:54 KST에 받은20시간 목표는 기존 전체 범위·품질을 유지한 실행 목표이며, 목표 시각은2026-10-28 06:54 KST다. 강제 중단·임의 완료 판정이나 필수 항목의 제외를 뜻하지 않는다. 미정인 인증/DB/서버·목표 규모에 의존하는 필수 구현/검증은 실제 의존성으로 남기고, 독립 구현과 큰 병목 작업을 계속한다.

2026-10-29의61차 뒤 일시정지에서2026-10-30 재개해64차까지 통합했고, 다른 Unreal 작업을 위해65차 설계 지점에서 안전 중단했다.2026-10-31 사용자가 다시 재개·목표 active를 명시해 작업을 이어간다. 시작 조회는41% 사용/59% 남음이었다. 처음 요청한 남은 사용량30% 기준 중단은 같은 날 최신 사용자 지시로 명시적으로 취소됐으므로 전체 목표를 계속 진행한다. 이전40% 조건과 PC 종료도 계속 취소 상태다. 아래 안전 일시정지와30% 조건은 당시 기록으로 보존하며 현재 실행 조건으로 사용하지 않는다.

## 구현 기준

- 지속 상태와 현재 Avatar, 권위 계산과 클라이언트 표현, 서버 시간과 UTC 운영 일정을 구분한다. 각 상태의 writer는 하나로 유지한다.
- 입력/명령 승인 → GT 값 수집 → 필요한 크기의 batch → worker 순수 계산 → revision/수명 검증 → 권위 적용 → 복제·저장 → 관측의 실제 연결을 만든다.
- 모든 큐에는 수량·보관 메모리·최대 age·공정성·포화 정책과 종료 처리가 필요하다. 취소된 consumer와 아직 실행 중인 native 작업의 예약 수명을 구분한다.
- 기존 GAS·Mover·AssetManager·Iris·repository·grant/입력 lease를 먼저 활용한다. 새 모듈·서비스·pool·캐시·framework는 실제 소비와 검증을 연결할 때 추가한다.
- 전투 정확도·보상/아이템 보존·개인정보·위험 경고의 가시성을 보호한다. 표현 품질과 지연 가능한 갱신은 따로 예산을 둔다.
- 새 구현은 기존 데이터·저장·BP·실제 호출자·실패·취소·종료 경로를 함께 확인한다. 스레드 수 증가나 파일 분할을 성능 개선으로 보고하지 않는다.

## 성능 목표와 입증 방법

최소/권장 하드웨어·server tick·전체 CCU·동시 공격 수와 확정 품질 정책은 미정이다. 사용자가 제시한 WQHD·에디터100FPS급·최대 약200개체·달리기 약5배 이상 탈것은 [추가 성능 목표와 실행 범위](#추가-성능-목표와-실행-범위--2026-11-01)의 잠정 기준으로 관리한다. 임의의 수치를 지원 보증으로 적지 않으며, 해당 변경에 필요한 대표 조건의 기준선과 아래 축으로 예산을 판단한다.

2026-11-02 사용자 명확화: **100FPS는 최대한 최적화하자는 방향을 표현한 수치이며, 200개체·모든 조건에서 넘어야 하는 하드 합격선이나 작업 종료 조건이 아니다.** 실제 병목·자원 낭비·확장 시 비용을 줄이는 효과와 화면/전투 품질·유지보수 비용으로 개선을 판단한다. 숫자를 맞추기 위한 무제한 재튜닝·설정 행렬은 진행하지 않는다. 네이티브 비교는 실제 render 입력 해상도와 FSR/해상도 업스케일/FG 미사용 여부·품질 설정을 함께 확인하고, 출력 PNG 크기만으로 네이티브 렌더를 주장하지 않는다. 최하 품질이나 표현 누락으로 만든 수치를 목표 달성으로 세지 않는다.

| 축 | 수집/판정 단위 |
| --- | --- |
| 클라이언트 CPU | GT·render·RHI·worker의 총 작업량과 frame critical path, P50/P95/P99, 긴 frame의 원인 |
| 서버 | simulation/명령·충돌·AI·복제·저장 준비 시간, tick 초과·명령 대기·상태 age |
| GPU·표현 | pose/Cloth/VFX·재질·그림자·overdraw·draw 비용, VRAM·resident texture/geometry |
| 메모리·수명 | actor/component/ASC·snapshot·queue의 현재/peak, allocation rate·GC·해제 스파이크·누수 |
| 로딩 | IO·압축 해제·로드·component 등록·collision/Nav·GPU 준비와 이동 도착의 관계 |
| 네트워크 | 실제 연결별 bytes/sec·object 수·RPC·reliability backlog·loss·correction·관심 영역 재진입 |
| 저장·서비스 | 명령/receipt 지연·성공/unknown outcome·거절·복구·backup/restore·schema/rules 호환 |
| 포화 | 큐 깊이·가장 오래 기다린 작업·drop/reject/defer·중요 작업 보호·품질 복귀 |

로컬 actor200, 여러 실제 clients, 여러 권위 서버는 서로 다른 시험이다. Editor/NullRHI, packaged RHI, dedicated server 결과도 따로 보존한다. 작은 계약 회귀는 자주, 혼합 부하는 주요 실행 변경 때, 장기/장애 시험은 통합 경계에서 수행한다.

## 구현 순서와 현재 작업

### 1. 수명·요청·커밋 단위

현재 구현한 변경과 실제 소비 계약은 아래와 같다. 실행 결과와 남은 경계는 검증 절에서 구분한다.

| 변경 | 실제 소비 경로와 유지할 계약 |
| --- | --- |
| Mover GC와 runtime 정리 분리 | liaison EndPlay/Uninitialize/replace는 정상 Uninit을 유지. GC는 참조만 해제하고 파괴 중인 FSM에서 기본 mode를 만들지 않는다. 프로젝트 plugin patch로 추적하며 설치 엔진은 수정하지 않음. |
| 탈것 요청 승인 | 탑승·하차·이륙 RPC가 기존 PS command budget과 domain 간격을 소비. 탑승 중에는 현재 mount의 PS로 해석. 서버 강제 하차·사망/종료 복구는 기존 trusted 호출을 유지. |
| 커밋 bag/equipment delta | 같은 item instance/slot의 FastArray identity 보존. 변화 항목만 dirty, private level 변경은 public 외형을 dirty하지 않음. 전체 권위 상태를 설치한 뒤 알리는 계약 유지. |
| 저장 값·직렬화 | GT는 definition/권위·진행 규칙·fingerprint·값 snapshot을 소유. checkpoint는 최신1 mailbox에서 합친 뒤 worker에서 JSON encode와 bounded decode를 검증한다.88차 제작의 순수 계획과 durable 명령 Encode는 기존 DB worker에서 receipt 확인 뒤 수행한다. receipt 재조회는 현재 계획이 무효여도 기존 확정 결과를 복구할 수 있게 유지. 완료 decode·정의 해석·원자적 상태 설치는 GT에 남는다. |

### 2. 권위 월드 이벤트 실행

기존 WorldEncounter actor가 phase/NPC/cooldown을 계속 소유하고 WorldEventRegistry가 실행 자원을 조정한다. actor마다 실행 timer·플레이어 전수 검색·무제한 async 준비를 만들지 않는다.

- 중앙 공정 순회, tick당 방문128·update8·spawn 적용2, GT soft budget1ms.
- observer snapshot 최대4096, 0.5초 재사용, 기존 공간 index를1km cell로 재사용. NPC 전투의 기본10m cell은 유지한다. observer 범위가 불완전하면 activation을 보류하고 overflow를 계측한다.
- native preparation 예약 최대8, consumer가 퇴장해도 실제 load 완료까지 예약을 보존. 완료 callback은 ready 표시만 하고 실제 spawn은 중앙 apply budget에서 수행한다.
- 107차는 준비 중 최대8개 요청에서 기존1초 wake로 수요를 재확인한다. 완전한 관측에서의 부재·닫힌 일정은 consumer를 취소하고, 관측 미확정은 원30초 한도 안에서 보류한다. 완료 전후에는 요청 당시 NPC 클래스와 정확한 owner/token/생성 revision을 확인한다. 이미 Active인 Encounter의 종료 규칙은 유지한다.
- 준비30초 timeout은 consumer 실패로 처리하되 아직 진행 중인 load 예약을 조기 재사용하지 않는다. world teardown 때 handle/weak owner를 정리한다.
- 선택 UTC 시작/종료 기간을 서버 admission에 사용한다. 시작은 포함·종료는 제외하며 잘못된 기간은 거절한다. 이미 진행 중인 보스는 종료 시각 이후에도 정상 해결할 수 있다. 재시작 간 cooldown/보상 권리의 영속성은 후속 서비스 작업이다.
- `ProjectJ.WorldEvents.Stats`와 trace로 처리량·대기·슬롯·observer overflow를 읽는다. 이는 인증된 LiveOps 서비스나 다중 서버 일정을 구현했다는 의미가 아니다.

### 3. 실시간 처리·복제·자원

| 감사 항목 | 다음 구현/검증 기준 |
| --- | --- |
| NPC 준비/worker A06 |85/92/94/100차는 snapshot 셀별 할당·등록부 복사·ready 이동·index/record 복사를 줄였다.102차의0.5초 age와127차의 bounded target 수집 재개·원age/완성 index 이관, row/revision·callback 전 예약 퇴역·GC·96 producer 공정성을 유지한다. 관측자 index는 실측상 느려 채택하지 않았다.134차 실제 플레이어200/NPC판단64·대상128의 혼합 입력에서 계산 wall과 전달 지연을 분리했다. NPC 자체는 MM을 쓰지 않으며 그 입력은 값 기반 판단 부하다. 물리/cloth 대기와 native Flying 예측의 GT 비용을 확인했고135차 owning worker를 실제로 연결·검증했다. 혼합 프레임 이득이 입증되지 않아 Ground-only 기본값을 보존하고 Flying은 기존 제어값2의 명시 검증으로 제한한다. 개별 callback·지속 churn·전체 비용·추가 혼합 회귀는 남는다. |
| Mover/Mass A03 | 기존 보호 가드와101/120/124/125의 실제 이동·조회 격리·같은-frame animation worker 근거를 유지한다.130차는 일반 NPC의 finite destination→공유 path→NavMover/Mover 실제 변위·ASC/취소/동일 Actor 전투 승격을 생산 helper와 선택 Encounter에 연결했다. 물리 simulation 빈도·BP fallback은 유지하며 point intent는 현재 Mass suspension을 거절한다. authoritative 물리/Mass 표현 이관 또는 지원되는 Mover 저빈도 정책·혼합 부하 비교는 남는다. trajectory worker와 새 실행 adapter를 simulation/Mass 전환 완료나 과거 CMC 근거로 바꾸지 않음. |
| Iris 관계 A07 | 실제 파티/audience/owner group 근거와123차 정상 Grid AOI 이탈·native remote Destroyed/매핑 해제·같은 서버 handle의 새 client incarnation·참조/소유자 상태 복원을 유지한다(정상 및 lag/loss/역순).93차 strict live RemovedFromWorld 재등록의 외부 이전 Mount 참조 실패는 별도 보존한다. old remote ACK/history 전체·실제 travel·운영 기본 cutover와 관계별 수신 비용은 남는다. |
| 정의 로드 A08 | registry의 Config/Primary dedup과 gameplay metadata 검증을 유지하고41차의 pure VFX headless 요청을 제거했다. 전체 메시/에셋 hard closure와 gameplay/cosmetic bundle 필요성은 남으며 server pose/notify는 보존. |
| 전투 pose A09 | gameplay pose/SSR·표현 분리를 유지하며89차 동일 native sweep의 임시 배열을 제거했다. 실제 metadata/geometry·콜백 중 teleport와 local storage를 검증했으나 capsule fixture가 복잡 메시/전체 pose 정확도·비용 인증은 아니다. 보호 actor 수/시간·bone·trajectory의 실제 혼합 부하는 남는다. |
| 스트리밍 A10 |95차 authority 목적지 permit·96 실제 Portal·97 원격 포화/인계에 이어106은 동일PID의 두 public 취소/재생성에서 선택 native level tail 종료를 확인했다. 논리 permit 반환 뒤에도 실제 load가 남는 것을 구분한다. default256은 호환값이며 합산 physical I/O·resident/GPU/Nav·고속/동시 이동·연속 원경·CCU와 미정 서비스는 남는다. UE 완료 bool을 단독 readiness로 쓰지 않는다. |
| 반복 콘텐츠 A11 | 연산별 정의 index·이동 표본 일괄 적용·동일 커밋 알림 생략에 이어24차 AND2–8 stable ObjectiveId·schema 호환·채집/제작과 동일 커밋 진행·실제 HUD/owner-only 소켓을 연결했다.128차는 기존 Snapshot.Decode로 정상 Open/migration의 closed-shape 검사를 통합해 미지원 필드의 저장 손실을 막았다.129차는 최신5map Development 공통 패키지에서 실제 새 프로세스의 pending job 복원/수령과 GatherCraft/owner-only 소켓을 확인했다. 공유 목표·offline 게임 규칙·대량 FastArray 이관은 남는다. 중앙 encounter 실행은 위 단계2에 연결. |
| 표현/UI A12 |86차 owner 상태 효과 UI는 GAS 변화로 갱신하고 표시 중 finite countdown만 timer를 사용한다. 기존 dirty 갱신/설정 저장을 재사용한다.98차 생성 시도 예산과99차 project-owned trail EffectType/초기128 정책,126차 shared notify의 정확한 재진입·PostGC lease와 제작 검증을 유지한다.133차는 실제 saved128 정책에132개 고유 remote component의 수량 컬링/복귀·culled 취소·32풀 잔류/초과100파괴를 검증했다.136차 native leader flag 채택은 거절했고137차 physics cache는 이득이 없어 기본0이다.153차 필수 경고 의미·기본 신호·수명·부모 에셋 검증과154차 cold-profile 실제 socket/RHI·정상/지연손실 전달·종료를 검증했다. late join/AOI·피해 영역/audience/HUD 품질·GPU/VRAM·혼합 표현 비용과 bounds 기본 채택은 남는다. |

### 4. 기능 확장과 서비스 연결

아래는 전체 범위다. 실제 게임 규칙·데이터·consumer 없이 빈 interface를 대량 추가하거나 완료 항목으로 늘리지 않는다.

| 기능 묶음 | 현재 확인한 실행 범위 | 남은 구현·확정 계약 |
| --- | --- | --- |
| 이동·입력·카메라·탈것 | Mover 입력/예측·점프/착지, CMC 탈것/비행·possession·부착/복구 | 빠른 도착 준비, 수영/넉백/플랫폼, 좌석/탑승 전투와 실제 혼합 부하 |
| 전투·능력치·AI·펫 | GAS·근접 판정/SSR, NPC decision/action·값 worker·비동기 path | 광역/투사체·중첩 규칙·어그로. 소유 아이템 기반 비전투 동료 추적은17차 통합·검증 중이며 펫 전투/성장 전체 구현으로 세지 않음 |
| 성장·직업·스킬·퀘스트 | int64 XP·class/advancement grant, legacy/travel·claim, AND2–8 복합 목표와 생활 동일 커밋·실제 journal/owner 복제 | 공유/offline·계정 업적·명시 규칙 migration, 복합 목표의 다음 공통 배포 검증 |
| 아이템·드롭·제작·생활 | instance/가방/장비/소비·로컬 영속 커밋, 즉시 제작·bounded/no-Tick 채집 예약과 실제 제작대/노드/UI·복합 진행.112차는 로컬 장시간 제작의 escrow/환급·UTC 재접속·수령을 같은 profile 커밋에 연결 | 드롭·공유/운영 장시간 작업·운영 영속 자원 예약·생산 운영 경제. 로컬 profile 커밋과 월드 cooldown은 하나의 DB transaction이 아님. 새 작업의 실제 socket/UI·패키지 검증도 별도 |
| 경제·거래소·우편·창고 | 현재 단독 개발 adapter의 CAS·receipt·복구·단일 profile 커밋 | 운영 자산 원장·양측 이전·재조회·정산·감사·실제 거래/우편/창고 서비스 |
| 파티·공격대·길드·소셜 | trusted memory party/guild membership·leader·snapshot/tombstone | 운영 영속 membership·초대/역할·공격대·친구/차단/presence·채팅/신고. 장기 group ID/퇴역 계약도 미완료 |
| PvE·PvP·공성·하우징·수집 | 현재 권위 encounter/NPC/UTC 기간·cooldown 실행 | 참가/기여/보상·전투 phase·승패/시즌·배치 권한/저장량·계정 공유를 실제 소비와 연결 |
| 월드·표현·제작 | WorldDefinition/clock·예측 WP source·현재 표현 budget/제작 도구 | 실제 도착 collision/Nav/GPU readiness·원경 HLOD·자원 재생·VFX/voice 예산·통합 맵 품질/부하 |
| 계정·세션·서비스 | prototype 호환과153 trusted PostLogin 보류·호스트 내 캐릭터 단일 writer·재접속 세대·이전 입력/GAS 차단·시작 용량 설정 | 실제 인증/소유권 검증기·네트워크 저장 복원·서버 간 권위·DTO/HTTPS·timeout 후 조회. 공급자 미정 |
| 서버·이관·운영 | coordination gate·handover state/envelope/loopback | 실제 zone/instance writer·directory·목적지 spawn/예약/ticket·fencing·drain·장애 격리·운영 감사 |

인증 공급자·운영 저장/서비스·목적지 Server 환경·배포 자격 정보는 현재 연결돼 있지 않다. 기존 prototype ID·memory repository·handover loopback을 운영 서비스로 활성화하지 않는다. 서비스가 필요한 단계에서 실제 endpoint/인증 계약을 연결하고, 그 전까지 독립적으로 검증 가능한 데이터·실행·수명 작업을 계속한다.

사용자는 이번 작업 중 Unreal 소스 엔진·서버/DB·인증 서비스가 아직 미정이라고 답했다. 현재 범위는 로컬 실행·불변식·자원/부하·실제 소규모 통신을 검증하는 기반이며, 운영 endpoint·분산 authority·인증을 임의로 선정하거나 연결 완료로 계산하지 않는다.

## 완료 판정

각 변경에는 실제 caller, 상태 writer, GT/worker 경계, replication/storage 단위, admission/포화, cancel/replace/logout/travel/teardown, 콘텐츠 연결과 focused regression이 있어야 한다. 성능 개선 수치는 같은 조건의 before/after가 있을 때만 적는다.

전체 목표를 이 단계의 native 통과나 event scheduler 추가만으로 완료 처리하지 않는다. 현재 구현은 수명·요청 승인·커밋/저장·작업 크기·권위 이벤트 실행을 연결한 첫 기반 단계다. 위 단계3·4의 실제 콘텐츠/월드/서비스 작업은 남아 있다.

### 2026-10-28 범위 확인과 작업량 추정

사용자가 대량의 게임 콘텐츠 제작과 전체 MMORPG를 수용하는 아키텍처 구축을 명확히 구분했다. 현재 범위는 후자와 이를 입증하는 최소 실제 기능·콘텐츠 연결이다. 새 직업·지역·퀘스트 등을 모두 제작하는 일은 이 완료율에 포함하지 않는다. 아래는 기존 구현·실행 근거와 위 미완료 표를 재사용한 계획 추정이며, 새 전수 감사·카탈로그 항목 수·테스트 개수로 계산한 값이 아니다.

| 영역 | 예상 전체 작업량 가중치 | 해당 영역 완료 추정 |
| --- | --- | --- |
| 공통 실행·소유권·병렬 작업·취소·자원 수명 | 20% | 85% |
| 게임플레이·콘텐츠 시스템의 실행 기반 | 20% | 65% |
| 월드·스트리밍·표현·자원 예산 | 15% | 60% |
| 실시간 복제·예측·관심 영역·서버 경계 | 20% | 40% |
| 저장·인증·세션·운영·장애 복구 | 15% | 30% |
| 통합 성능·회귀·제작 검증·배포 | 10% | 65% |

가중합은58%로, 보고 정밀도는 **완료 약60%·잔여 약40%, 완료 범위55–65%**다. 미검증 코드를 통합하거나 테스트를 추가한 것만으로 즉시 수치를 올리지 않는다. 남은 큰 작업은 원격 이동 strict 품질과 실제 복제 수명/손실, 시스템 간 정상·취소·교체/재접속 계약, Nav/GPU/원경 준비·포화 정책, 실제 provider/서버의 저장·중복 요청·복구·인계, 혼합 부하와 필요한 배포 target 검증이다. 공급자·서버 환경이 미정인 경계는 로컬 prototype 성공으로 완료 처리하지 않는다.

완료 조건은 새 MMORPG 콘텐츠가 공통 기반을 재사용하고 권위·병렬 실행·복제·저장·자원 예산·실패 복구가 실제 소비 경로에서 검증되는 것이다. 실제2연결/200캐릭터는200명 동시 접속 수용량의 증명이 아니다. 콘텐츠 전체 제작을 추가한다면 인력·분량에 따라 수년 단위의 별도 제작 계획이 필요하며 현재 아키텍처 잔여 시간으로 합산하지 않는다.

## 1차 실행 검증

실행 환경은 Windows11, Ryzen7 9800X3D, 설치 UE5.8.4다. 네이티브/소켓 시험은 Editor Development와 NullRHI를 사용했다. [검증 요약·대상 소스/바이너리 지문](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)에 실제 테스트명·실행 인수·계산 비교·스레드·통신 조건·실패 후 수정 이력을 보존했다. raw 로그·trace·임시 실행기는 `Saved/Validation/MMOFoundation_20261026`, 통신 시험은 `Saved/Validation/GroupD_20260910/MMOFoundation_20261026_*`에 보존한다.

| 검증 | 결과와 실제 의미 |
| --- | --- |
| Editor 직접 UBT | 1차 코드 `EditorBuild7` 성공,347.25초. GC·기능 실행을 검증한 아래 native binary와 연결. |
| Game 직접 UBT | `GameBuild1` 성공,438 actions·258.15초. Cook/packaged 실행·Server target 검증과는 별도다. |
| native 집중/기존 회귀 | `NativeMain4`:135/135, 오류·경고·누락0,7.557초. 기존 GC 실패 묶음과 새5개 테스트 포함. |
| 실제 저장 consumer/복구 | `NativePersistence2`:10/10, 오류·경고·누락0,411.730초. 실제 인벤토리12,288명령 확정, 합성 native16,384커밋, DB 재개방·복구·schema·장애/늦은 완료·중복 요청 확인. 합성 반복은2 record·65 receipt·380,928bytes를 유지. |
| 월드 이벤트 | 실제 async 완료·ground collision·NPC4개 spawn/정리, 취소된 owner의 늦은 완료, 별도32개 encounter의 실행/예약 상한, UTC/preview/종료 경계 통과. 운영 보스·원격 다수 검증은 아님. |
| 소켓/Iris/FastArray | server1+실제 client2를 정상 조건과75ms 지연·10% 손실 조건에서 각각 통과. 모든 프로세스 exit0·timeout0.100개 static AOI probe와 private/public FastArray의5단계·정상 disconnect를 검증하며100개 실제 AI를 뜻하지 않는다. 설치 Editor host의 `-game -server` 실행이며 production Server target 빌드와 구분한다. |
| 도구/정적 계약 | Python48/48,205개 카탈로그·20 domain·module DAG 통과. 카탈로그 수는 기능 구현 수가 아니다. 문서281개·로컬 링크3163개·오류0, 기존 미보관 로컬 archive 링크12개는 별도 집계. 생성 카탈로그 갱신·diff 검사 통과. |
| 에셋 보존 | 감사 때 선택한 BP/DA6개의 현재 SHA256이 감사 전과 일치. 이번 구현에서 에셋·프로젝트 Config를 변경하지 않음. |

### 같은 바이너리의 계산 비교

`ProjectJ.TargetScoring.ParallelMinCandidates`를 이전 동작1(A)·기본4096(B)으로 바꿔 **A/B/B/A** 순서의 독립 프로세스4개에서 실행했다. 프로젝트 Editor DLL들의 실행 전/후 SHA256이 일치한다. 기존 benchmark의 mode2(비동기 root + 후보 분할 요청), 각 case/run12표본이며 아래 값은 실행2회의 P50을 다시 중앙값으로 묶은 계산 시간이다.

| 후보 수 | A:1(µs) | B:4096(µs) | 해석 |
| --- | --- | --- | --- |
|256|12.100|5.849|작은 worker 작업을 추가 분할하지 않아 계산 비용 감소|
|4096|54.648|54.600|두 설정 모두 분할, 거의 같은 범위|
|16384|170.302|158.150|두 설정 모두 분할, 변동 범위의 관측값이며 새 알고리즘 이득으로 계산하지 않음|

12표본의 보고 P95는 마지막 순서 통계값이므로 큰 표본의 tail 추정으로 해석하지 않는다. mode2의 GT 전달 P50은 약16.6ms로 유지됐다. 계산 µs 감소를 frame/FPS 개선이나 대규모 수용량으로 환산하지 않는다. GT snapshot 준비·혼합 worker 경쟁은 후속 부하 검증이 필요하다.

### 실제 직렬화 스레드

`LocalProfileLifecycle`을 CPU trace로 실행하고 [Unreal Insights export](https://dev.epicgames.com/documentation/unreal-engine/using-the-timers-and-counters-tabs-in-unreal-insights-for-unreal-engine?lang=en-US)와 현재 엔진의 export 구현으로 확인했다. checkpoint encode3회·command encode21회·DB commit18회 모두 Background Worker, capture/checkpoint43회·command21회·apply22회 모두 GameThread였다. 완료 decode·정의 해석·원자적 상태 설치는 여전히 GT 계약이다. 이 trace는 실행 위치의 증거이며 직렬화 전/후 latency나 전체 frame 개선 수치는 아니다.

첫 Insights 실행은 인수 전체를 감싼 따옴표 때문에 분석이 시작되지 않았다. 그 실행의 idle 분석기만 정리하고 인수 전달을 수정한 `Insights2` export는 정상 완료·종료했다. 빌드·Editor·테스트를 중단하거나 결과를 누락하지 않았다.

## 2차 실행 기반 — 권위 조회·퀘스트·자원 대기

사용자가 남은 전체 아키텍처를 단계별로 계속 진행하도록 요청한 후의 후속 작업이다. 전투·AI·이동·성장·퀘스트·복제·UI/외형 로딩이 공유하는 비용과 상태 연결부터 개선한다. 서버/DB/인증 미정 상태는 유지한다.

| 경로 | 변경과 보호하는 계약 |
| --- | --- |
| 공간 존재 판정 | 기존 WorldEvent observer snapshot을 그대로 사용하되 boolean activation에 필요 없는 전체 후보 배열·정렬을 제거. Core `HasAny`는 첫 정확한3D match에서 종료. 전체 후보를 요구하는 NPC scoring query의 안정 ID 정렬·inline 순회는 유지. |
| NPC 관측자 거리 | 현재512명 상한의 vectorized linear scan을 유지. index가 방문 수를 줄여도 build/lookup 비용이 더 컸으므로 후보 구현·CVar·무소비 runtime API를 생산 소스에서 제거했다. coverage 불완전/urgent/잘못된 policy의 Near 계약과 즉시 promotion·demotion hysteresis 유지. |
| 권위 target 검사 | 정렬된 기존 target registry가 유일한 원천. membership/team map은 registry revision이 바뀐 뒤 첫 검사에서만 재생성하는 파생 cache. IsDead 같은 외부 호출 이후 현재 revision을 읽어 이탈·진영 변경·destroy·world retirement를 반영. |
| 퀘스트 정의 | objective/restore/commit 검증과 저장 fingerprint의 반복 정의 검색을 연산별 index로 바꿔 D×Q 반복을 D+Q로 축소. 중복 ID는 여전히 모호함으로 거절. 매 연산 새 view이므로 제작/테스트의 definition 교체·규칙 변경을 stale cache로 숨기지 않음. fingerprint byte 순서·schema·BP 필드 타입 유지. |
| 이동 표본 | 현재 활성 travel 목표의 event만 생성하고 모든 관련 목표를 적용한 뒤 한 번 Publish. unaccepted travel 정의가 다른 interaction event를 생성하지 않음. 서버 horizontal speed와 실제 world sample time에 비례한 허용 거리,1m 오차 여유·1.5배 계수·구간1km 상한·2초 초과 gap 재기준화. 탑승/Avatar 교체 때 이전 pawn의 잔여 거리 폐기. 이동 권위/anti-cheat의 대체물이 아님. |
| 커밋/추적 알림 | 동일 quest snapshot 설치는 변경 없음으로 보고, 일반 durable edit에서 quest가 바뀌지 않았으면 quest 알림 생략. 초기 profile load는 초기 표시 알림 유지. 실제 tracking 변경만 Publish. 모든 권위 상태 설치 후 첫 알림이라는 순서 유지. |
| 원격 회귀 | 기존 socket fixture에 선택 `-Quests`를 추가. 실제 owner의 진행·tracking 갱신과 다른 player의 quest 데이터 비공개를 기존5단계에서 검사. transient fixture 정의만 만들며 생산 DA/BP는 저장하지 않음. 이 모드를 사용한 전체 bytes는 이전 fixture와 같은 payload가 아님. |
| 시각 에셋 residency | 기존 loader의 Pending lease 수로 실행 여부를 구분. 요청/실제 async 완료는 즉시 tick 자격을 깨우고, 전달이 끝난 resident-only 상태는0.5초 cadence의 weak-owner 점검으로 전환. 점검이 시작되면128방문·1ms soft budget의 연속 tick으로 한 순회를 마친다. token의 pin·명시 Release·native 완료 전 예약 보존·world retirement를 유지. 새로운 cache/loader를 추가하지 않음. |

`LastTickObserverCandidateVisits`, `ProjectJ_NPCDecision_TargetMembership`, `ProjectJ_TargetSpatial_Presence`, `ProjectJ_QuestDefinitionIndex/Objective/Travel`과 VisualAsset의 `ExecutedTicks/LastTickLeaseVisits`로 현재 비용을 관측한다. 시각 에셋의 새 유지 관리 cadence는 CPU 작업 사이의 soft budget이며 강제 wall-clock 퇴역 보증이 아니다. 명시 Release는 즉시 처리된다.

### 채택하지 않은 관측자 index

`EditorBuild8`·`Phase2Main1`은 후보 포함139/139, 경고/오류0으로 통과했다. 그러나512관측자×128query,32표본, 같은 binary에서 interleaved A/B·B/A로 비교한 계산+build P50은 다음과 같았다.

| 분포 | 기존 linear(µs) | 후보 index+build(µs) | 방문 수:기존→후보 |
| --- | --- | --- | --- |
| 희소 |40.498|65.099|65536→128|
| 조밀 |37.700|176.299|65536→65536|
| 중간 간격 |37.700|43.903|65536→4095|

방문 수 감소를 CPU 개선으로 잘못 보고하지 않았다. 후보 소스7개와 지문은 로컬 `Saved/Validation/MMOFoundation_20261026/ObserverPrototype`에 보존했으며 최종 생산 경로는 해당 index를 사용하지 않는다. 이 측정은 게임 frame·낮은 코어 CPU·혼합 worker 부하의 결과가 아니다.

### 최종 경로의 검증

- `EditorBuild9` 직접 UBT 성공:539 actions·348.38초.
- `Phase2FinalMain1`:140/140, 경고/오류/누락0,8.146초. 최종 `HasAny`·quest·membership·resident 유지 관리와 기존 수명/취소 회귀 포함.
- `NativePersistence3`:10/10, 경고/오류/누락0,393.970초. 실제 인벤토리12,288명령·합성 native16,384커밋·재개방/복구·규칙 변경·중복 요청·지연/실패 소비자 유지. 이번 단계의 서로 다른 native 시험은 총150개다.
- resident192 lease의 실제 async 완료·pin을 유지한 상태에서100회 유휴 호출의 실제 tick 실행0회를 확인. 새 consumer는 같은 group을 재사용했고, 파괴된 owner는 방문128 상한의2회 점검으로 모두 회수. due-time을 시험에서 주입했으므로 실제0.5초 wall-clock 회수나 장시간 memory/FPS 입증은 아니다.
- `GameBuild2` 직접 UBT 성공:441 actions·252.80초. 설치 Editor host로 수행한 소켓 검증과 Game target 컴파일을 구분하며 Cook/packaged RHI·production Server 산출물 검증으로 세지 않는다.
- server1+실제 client2의 선택 quest fixture는 정상 환경과 `PktLag=75/PktLoss=10` 설정에서 각각 통과. owner 진행·tracking 갱신, foreign quest 데이터 비공개, 기존 AOI/FastArray5단계·정상 disconnect를 확인. 모두 exit0·timeout0. 설정 지연을 측정 RTT로 해석하지 않는다.
- Python48/48·205개 카탈로그/20 domain/DAG·문서281개/로컬 링크3165개/오류0 통과. 기존 미보관 local archive 링크12개는 별도 집계. 감사 때 선택한 에셋6개의 SHA256 보존·Content/Config 변경 없음.
- [같은 검증 요약](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `continuations[0]`에2차 source39개 지문·각 실행·비교/제외 근거를 보존했다.1차 기록과 실패 근거는 덮어쓰지 않았다.

### 존재 판정 비교

최종 binary의 전체 후보 query와 `HasAny`를 같은 이미 생성된 공간 snapshot에서 interleaved A/B·B/A, warmup2회 뒤32표본으로 비교했다. 별도 프로세스 `PresenceRepeat1`도1/1, 경고/오류0으로 통과했다. Editor DLL의 비교 전/후 지문이 일치한다. 아래는 반복 실행의 보고 P50이며 **128회 조회 합계**다.

| 값 분포·관측자 수 | 전체 query(µs) | HasAny(µs) | 후보 방문 수 |
| --- | --- | --- | --- |
| 밀집512 |1154.501|9.201|65536→128|
| 희소512 |33.401|9.902|465→128|
| 무매치512 |29.501|14.603|0→0|
| 밀집4096 |11120.699|9.201|524288→128|
| 희소4096 |37.700|10.502|465→128|
| 무매치4096 |29.501|14.599|0→0|

공통 snapshot 생성 비용은 두 비교 모두 제외했고, 전체 query 기준은 배열 capacity까지 재사용해 보수적으로 비교했다. 실제 event scheduler의 tick당 update 상한은8이며,128회 합계를 한 frame의 절감량으로 보고하지 않는다. 값4096개를 실제 player4096명·AI·GPU·전체 game-frame·네트워크 수용량으로 환산하지 않는다. NPC 관측자 최근접 검색의 실패한 index 후보와 이 boolean 소비자의 개선은 서로 다른 결과다.

## 3차 진행 — 정의 갱신·월드 준비·서버 시간

아래 변경은 직접 Editor/Game UBT와 native·socket·실제 맵 시작 경로를 검증했다. 각 검증의 실제 범위와 미완료 경계는 구분한다.

| 경로 | 구현과 남은 경계 |
| --- | --- |
| 정의 registry | Config와 AssetManager의 현 입력을 모아 한 번 검증·게시한다. Config/Primary 중복 경로는 정상 결합하고 Config 자체 중복·모호한 ID·잘못된 전직 graph는 거절한다. 새 map으로 교체하므로 제거된 정의가 남지 않으며 실패 시 이전 membership을 유지한다. class/전직 합계1024·item1024 상한과 resolve/load/시간 계측을 추가. 기존 asset 객체 내부의 런타임 수정을 rollback하거나 hard closure를 제거하는 기능은 아니다. |
| 월드 준비 source | WorldState가 늦게 생기거나 Definition이 나중에 연결되어도 재해석한다. 부재 시 검색은1초 간격, 유효 owner는 weak cache로 재사용. 종료·비로컬·dedicated·잘못된 수치는 거절한다. 현재/예측점 사이를 반지름 이내 간격의 sphere 최대8개로 덮고 정지 시1개만 생성한다. 기존 정상 BeginPlay 후 component tuning도 유지한다. |
| 제작 검증 | 기존 WorldDefinition에 streaming 거리/반지름/예측 시간·clock 수치의 Data Validation을 연결한다. 현재 DA_MainWorld도 native 시험에서 읽고 검증하며 에셋을 저장하지 않는다. |
| 서버 시간 | WorldState가 clock 길이·초기 시각을 값으로 복제한다. 클라이언트 authoring Definition 유무에 관계없이 같은 설정과 동기화된 server time으로 계산한다. 기존 ClockStart·환경 복제는 유지. UTC 운영 일정·영속 월드 시간 서비스와는 별도다. |
| 실제 원격 소비 | 기존 socket fixture에 선택 `-WorldClock`을 추가. 서버에만 transient authoring Definition을 두고 실제 client가 복제된 clock 설정과 rain을 관측하는지 검사한다. |

첫 `EditorBuild10`은 성공했고 `Phase3Main1`은145개 중144개 통과·1개 실패였다. 실패는 새 late-definition 시험에서 LocalPlayer 생성 전 GameViewport 초기화가 빠져 발생한 engine ensure였다. viewport 초기화 후 `EditorBuild11`·`Phase3Main2`에서는 기능145개가 통과했지만 입력 없는 LocalPlayer 초기화 경고1개가 남았다. 엔진 source 계약을 대조한 최종 fixture는 streaming 소비에 필요한 실제 local controller·pawn을 사용하고, 불필요한 LocalPlayer/viewport 생성은 제거한다. 실제 local player의 원격 복제는 socket 시험이 담당한다. Definition 없는 owner를 먼저 cache한 뒤 실제 값이 연결되는 경우와 기존 component tuning 유지 검사는 유지한다. 오류를 숨기거나 생산 경로에 우회 fallback을 넣지 않았다. 수정된 소스의 직접 UBT·집중/저장·socket·Game 검증을 이어간다.

정의 hard closure의 gameplay/cosmetic 분리, 실제 맵의 collision/Nav/GPU 도착 준비와 HLOD·급회전/teleport 검증, 운영 Server·DB·인증 연결은 여전히 남아 있다.

### 3차 최종 검증

- `EditorBuild12`: 직접 UBT 성공,542 actions·361.06초. `GameBuild3`: 직접 UBT 성공,444 actions·262.31초. 모든 빌드·시험 프로세스의 정상 종료 후 다음 실행을 시작했다.
- `Phase3Main3`:145/145, 오류·경고·누락0,8.151초. 최종 late-definition fixture, 실제 DA_MainWorld 검증, 연속 준비 기하·서버 clock 값·원자적 registry 갱신과 기존 회귀 포함.
- `NativePersistence4`:10/10, 오류·경고·누락0,408.679초. 실제 인벤토리12,288명령127.434초, 합성 native16,384커밋225.608초와 bounded record/receipt 유지, 재개방·복구·schema/규칙·중복·지연/실패 소비자 확인. 서로 다른 native 시험은 총155개다.
- 정상 및 `PktLag=75/PktLoss=10` 조건의 server1+실제 client2가 모두5단계·정상 disconnect를 통과했다. 서버에만 존재하는 authoring Definition 없이 client가 clock600초/초기19시·강우0.4를 읽고, owner quest 갱신·타인의 private quest 비공개를 확인. 모든 프로세스 exit0·timeout0. fixture 제어 payload가 바뀌었으므로 이전 실행과 byte 절감 비교로 사용하지 않는다.
- `WorldSmoke1`: 실제 `/Game/World/MainWorld/Maps/L_MainWorld` 시작 후 WorldState1개·DA_MainWorld/장소20개·PrimaryAsset 등록·World Partition 연결을 확인, exit0. Editor `-game` NullRHI 시작 검사이며 이동·도착 준비·원경 시각·Cook/출하 판정이 아니다.
- 위 맵 시작 로그에는 editor widget factory, HLOD builder 제작 설정 클래스, PoseSearch async index 준비 경고가 있었다. HLOD builder 설정은 엔진의 `WITH_EDITORONLY_DATA`와 Editor module에 속한다. uncooked 로드 순서와 실제 저장/패키징 경계를 확인할 후속 항목이며, 경고 없는 실행이나 HLOD 합격으로 기록하지 않는다. production에 Editor module을 강제 연결하지 않았다.
- Python48/48·205 catalog/20 domain/DAG·문서281개/로컬 링크3165개/오류0 통과. 기존 absent archive12개 별도 집계. 선택 에셋6개 SHA256 보존, Content/Config 변경 없음.
- [기존 검증 요약](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `continuations[1]`에 source51개·native 실행·원격/맵 receipt·바이너리 지문과 첫 두 fixture 실패/경고 이력을 추가했다. 이전 단계 기록은 보존했다.

## 4차 진행 — UI 조회 snapshot과 콜백 수명

실제 PlayerUI의 퀵슬롯 갱신은 슬롯마다 같은 GAS 목록과 조건·cooldown을 재조회했다. 조건과 cooldown은 native/Blueprint 확장 호출이므로 그 사이 Screen·ASC·ActorInfo/Avatar가 교체되거나 종료될 수 있다. 현재 보강은 갱신별 값과 소유자를 고정하고 현재 슬롯이 소비하는 tag만 모아 GAS 목록을 한 번 순회한다. 한 spec이 여러 슬롯/tag에 대응해도 조건·cooldown은 한 번 조회한다. 실제 스킬 실행은 기존 입력·GAS 권위를 유지한다.

조회 사이와 GAS lock 해제 후, HUD 적용 후 현재 revision·owner·source·ActorInfo를 검사한다. 교체된 결과는 게시하거나 countdown timer를 다시 걸지 않고 기존 next-tick refresh에 합친다. reentry는 중첩 조회를 실행하지 않는다. 영속적인 ability cache나 새 manager를 만들지 않으며, 다음 갱신은 현재 spec과 조건을 다시 읽는다. 기존0.2초 cooldown 갱신과 cooldown 없는 상태의 무폴링은 유지한다.

`ProjectJ_UIQuickSlots` trace와 CSV의 방문·평가·폐기 수로 측정할 수 있다. 실제 GAS spec·HUD button을 사용하는 새 회귀는 중복 슬롯, source 해제/GC, ActorInfo 변경, 재진입, EndPlay/GC, screen/profile 교체, Avatar 파괴와 능력 제거를 확인한다. 현재는 빌드·검증 중이며 CPU/FPS 개선 수치나 완료로 계산하지 않는다.

`EditorBuild13`은543 actions·356.26초로 성공했다. 첫 `Phase4Main1`은163개 중162개 통과·1개 실패, 경고0이었다. 새 시험이 Actor 초기화 전에 지정한 Avatar를 engine ASC InitializeComponent가 owner로 바꿔 파괴 조건이 실제 조회 대상에 적용되지 않았다. fixture는 world/component 초기화 후 실제 ActorInfo를 지정·확인하도록 수정했다. 생산 경로는 기존 ASC의 ActorInfoRevision도 확인해 같은 owner/Avatar로 재바인딩한 세대까지 구분한다. 최종 집중 시험과 후속 Game 컴파일 전에는 완료로 세지 않는다.

다음 저장 실행 경계는 DTO JSON 파싱이다. codec의 실제 필드가 ID·수치·배열뿐인지 확인했고, raw 저장 bytes와 receipt 재조회는 보존해야 한다. 값 파싱은 기존 worker의 gate 예약을 반납하기 전에 끝내고, 불변 결과만 GT 완료에 전달하는 방향으로 후속 구현·수명·복구·trace 검증을 진행한다. 정의 해석과 권위 상태 설치를 worker에 옮기는 계획은 아니다.

수정된 `EditorBuild14`는543 actions·349.99초로 성공했고 `Phase4Main2`는163/163, 오류·경고·누락0,8.470초였다. 실제 Avatar를 확인한 파괴/GC와 같은 Actor 재바인딩까지 통과했다. 같은 Editor DLL 지문과 Insights의 UI scope19개/전부 GT를 확인했다. scope에는 의도적인 GC·종료·재진입과 early return이 섞여 있으므로 평균 비용이나 FPS 이득으로 보고하지 않는다. [기존 증거](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `continuations[2]`는 Editor 검증 완료·Game/신규 packaged 검증 대기로 명시했다. 기존 UI runtime smoke가 cooked data를 요구하므로 오래된 Shipping/패키지 결과로 대체하지 않는다.

## 5차 진행 — worker 값 파싱과 실제 회수 경계

기존 LocalProfile worker가 저장 읽기와 확정된 receipt의 payload를 기존 codec으로 해석한다. JSON64KiB·형식/범위·schema 검증은 유지하며 결과는 `TSharedPtr<const Snapshot, ThreadSafe>`로 전달한다. DTO에는 GUID·FName·수치·배열만 있고, UObject·정의 해석·규칙 fingerprint 대조·권위 상태 설치·알림은 GT에 남는다. 새 worker 풀이나 큐를 만들지 않았다.

파싱은 기존 gate 예약을 돌려주기 전에 끝낸다. raw 읽기 bytes는 초기 unchanged checkpoint 비교를 위해 보존하고 그 소유권을 기존 LastQueued로 이동한다. 소비자가 교체·종료되어도 기존 세대 검사와 물리 작업 예약 계약을 유지한다. 손상된 JSON·옛 format·알 수 없는 결과·receipt replay·규칙 변경·완료 지연 시험으로 검증한다. `ProjectJ_ProfileDecodeValues` trace로 실제 실행 스레드를 확인하며, 파싱 전/후 frame latency 입증과는 구분한다.

UI의 실제 GAS 잠금 해제 경계도 보강했다. 개별 pending removal과 잠금 해제 후 목록 크기 변경을 확인해 회수된 능력의 결과를 게시하지 않는다. 실제 `ClearAbility`/`ClearAllAbilities`를 조건 조회 안에서 실행하는 집중 경우를 기존 snapshot 시험에 추가했다. 생산 경로가 사용하는 기존 spec dirty refresh와 현재 ASC 세대 검사는 유지한다.

`EditorBuild15` 직접 UBT는543 actions·354.04초로 성공했다. 수정된 native163개·저장10개·별도 lifecycle trace·Game target을 순차 검증 중이다. 신규 Game·패키지와 복구 검증이 끝나기 전에는 이 단계를 완료로 세지 않는다.

### 5차 코드·실행 경계 검증

- `Phase5Main1`:163/163, 오류·경고·누락0,8.424초. 실제 ClearAbility/ClearAllAbilities의 잠금 중 회수까지 포함한다.
- `NativePersistence5`:10/10, 오류·경고·누락0,403.625초. 실제 인벤토리12,288명령126.595초, 합성 native16,384커밋과 bounded 저장 유지·복구/중복/지연/규칙 변경을 확인했다. 별도 lifecycle trace 시험도1/1 통과했고, 서로 다른 native 시험은 총173개다.
- 실제 lifecycle의 Insights export에서 값 decode21회·checkpoint encode3회·command encode21회·DB commit18회 모두 Background Worker, capture25/21회·command apply22회 모두 GameThread였다. 이 결과는 실행 위치의 증거이며 frame/FPS·지연 개선의 A/B 측정은 아니다.
- `GameBuild4`: 직접 UBT 성공,445 actions·262.99초. source57개 지문이 위 검증 동안 유지됐다. [동일 증거](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `continuations[3]`에 각 실행과 trace를 추가했다.
- 최신 Game 실행 파일의 새 Windows Development Cook·stage와 기본/동적 UI의 두 해상도 검수를 진행한다. 명시 ThirdPerson/Entry map과 기존 AlwaysCook closure를 사용하며 MainWorld/HLOD 전체 패키지 검증으로 세지 않는다. stage 실행 파일은 현재 직접 UBT 산출물과 SHA256을 대조한다. 기존 패키지·Cook·검증 근거와 Content/Config를 보존한다.

## 6차 진행 — 실제 Cook의 이동 모드 archetype 결함

5차 Game 이후 새 `MMOFoundation_20261026_UICook1`이 exit3으로 실패했다. 최초 오류는 ThirdPerson map 저장 중 `/Game/NPCs/BP_NPC_Test` CDO의 private Walking mode를 다른 package의 인스턴스 archetype으로 참조한 것이다. UBT/dotnet 예외나 빌드 경로 문제로 분류하지 않았다. 실패 Cook이 정상 종료한 뒤 source를 조사하고 기존 native 검증과 분리했다.

저장 없는 좁은 Editor 조회에서 NPC·base player·GreatSword CDO와 실제 ThirdPerson NPC의 mode 클래스/outer를 확인했다. 각 인스턴스 outer는 올바르며, 엔진 `SavePackageUtilities`가 요구하는 public archetype export 플래그가 문제다. 해당 규칙은 class archetype이 다른 package의 인스턴스로부터 참조되기 때문이라고 엔진 source가 명시한다.

Project Mover의 로드 경계에서 자신이 소유한 template mode archetype/default subobject의 public export를 정규화하고, 기존 avatar 제작 호출도 같은 함수를 사용한다. 객체·클래스·튜닝·BP graph를 교체하거나 런타임 인스턴스에 public/standalone/root 플래그를 부여하지 않는다. Source 에셋 저장 없이 Cook export를 정상화할 수 있는지 검증한다. 기존 template metadata를 복구하는 migration이며, 잘못된 참조를 무시하는 Cook 옵션이나 별도 동작 fallback을 추가하지 않는다.

새 native 회귀는 private template의 복구·idempotency·런타임 private 소유 유지, 실제 gameplay BP3개의 template public import와 서로 다른 두 인스턴스의 mode 소유권을 확인한다. Editor 빌드 후164개 집중 회귀·새 Game 빌드·새 Cook/stage/UI 검수를 순서대로 진행한다. 현재 이 결함의 최종 수정/패키지 검증은 완료로 세지 않는다.

`EditorBuild16`은544 actions·359.52초로 성공했고 `Phase6Main1`은164/164, 오류·경고·누락0,8.565초로 통과했다. NPC BP와 ThirdPerson map의 조회 전 SHA256이 유지되고 Content/Config 변경도 없다. 새 Game5와 Cook2·stage2·실제 UI 실행 결과는 별도 확정 전이다. Python48/48·205 catalog/20 domain/DAG와 문서281개/로컬 링크3169개/오류0도 확인했다. 기존 absent archive12개는 별도 집계한다.

### 6차 Cook과 실제 패키지 결과

`GameBuild5`는446 actions·260.69초로 성공했다. 새 Cook2는1,676개 package 처리 후 exit0, stage2도 exit0/39.13초로 성공했고 Game5 실행 파일 SHA256과 일치했다. private mode archetype 저장 오류는 재현되지 않았다.

그러나 fresh packaged1280×720 실행은 UI/source-boundary smoke가 success1·cooked1, 프로세스 exit0이었어도 로드 오류4개 때문에 검수에 실패했다. native NPC/player CDO의 `CommonLegacyMovementSettings_0` template import를 찾지 못했고, 이 실패가 base player·GreatSword·배치 NPC로 이어졌다. UI fixture 성공만으로 전체 로드 로그의 실패를 무시하지 않았다. 실패 `UIBaseline`과 Cook1/2·stage2를 보존한다.

## 7차 진행 — native 설정 identity와 실제 실행 복원

프로젝트의 이전 PostInitProperties는 engine RefreshSharedSettings를 호출해 공통 설정을 동적 객체로 만들었다. Editor/Cook에서 저장된 native CDO 하위 객체를 packaged loader가 같은 native default subobject로 복원할 수 없는 경계를 확인했다.

Project Mover 생성자에서 기존 직렬화 이름 `CommonLegacyMovementSettings_0`의 진짜 default subobject를 만든다. 프로젝트 Mover plugin의 protected bootstrap은 owned default-subobject/설정 interface를 검사해 기존 private SharedSettings 목록에 등록한다. runtime 목록은 기존 RefreshSharedSettings가 계속 관리한다. 별도 설정 목록·두 번째 writer·fallback을 추가하지 않고 동적 PostInitProperties 생성은 제거했다. plugin 변경 근거는 기존 patch 문서에 추가했다.

native 회귀는 실제 NPC/player 기본 설정의 이름·default-subobject/public 플래그·outer와 두 actor 간 mutable 설정 분리를 확인한다.165개 집중 회귀·새 Game6·Cook3/stage3·새 UI baseline/dynamic 두 해상도와 packaged 실제 이동/전환/착지를 순차 검증한다. 현재는 빌드 중이며 오류 없는 패키지 결과를 확인하기 전까지 완료로 세지 않는다.

`EditorBuild17` 직접 UBT는544 actions·366.34초로 성공했다. `Phase7Main1`은165/165, 오류·경고·누락0,8.603초로 통과했다. 실제 native 기본 객체의 default-subobject 조회와 BP/Actor별 mutable 설정 분리를 확인했으며, Game6와 실제 packaged 로드 결과는 아직 검증 중이다.

### 7차 최종 로컬 검증과 미완료 관측 경계

- `GameBuild6` 직접 UBT 성공:446 actions·266.10초.63개 source 지문을 유지했다.
- 새 Cook3/1,676 packages·stage3 모두 exit0. stage3의 실행 파일 SHA256은 현재 Game6와 일치한다. native template 누락 오류4개는 새 packaged 실행에서 사라졌다.
- 기본 HUD와1,000항목·24 buffs·창4개·10 quickslots·5Hz/50단계 동적 UI가1280×720/1920×1080에서 각각 통과했다.4실행 모두 exit0·로그 오류0, 실제 source-boundary와 cooked 조건을 확인했다. startup 포함 실행 시간을 UI 비용/FPS 개선으로 해석하지 않는다.
- 같은 packaged binary의 비계측 실제 이동은 exit0·로그 오류0, 이동53.736m·반전17·착지2·local quality9,523표본을 확인했다.1개 실제 Avatar의 회귀이며 CCU/원격 다수 검증이 아니다.
- trace를 켠 첫 이동 실행은 기능 success1·이동53.789m/반전17/착지2였지만 shutdown 후 exit777003이었다. 엔진 enum은 CrashReporterCrashed이며, 확인한 위치의 새 crash context/Windows Application 항목은 없었다. 같은 binary 비계측 실행의 정상 종료만으로 원인을 단정하지 않는다. 이 trace 실행은 프로세스 통과나 완전한 성능 검증으로 세지 않고 원인 미확정 후속으로 유지한다. 중간 보고의 정상 종료 표현도 실제 exit 확인 후 정정했다.
- 선택 에셋 기존6개와 NPC BP/ThirdPerson map2개의 SHA256 보존, Content/Config 변경 없음. 실패 Cook1·초기 packaged UI·trace 종료 실패와 성공 후속을 [동일 증거](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `continuations[4]`에 함께 보존했다.4/5차의 과거 source/native 기록도 유지하고 최신 packaged 검증과 연결했다.

이 단계는 template/설정 초기화·패키징·UI·단일 이동의 실제 통합 경계를 복구했다. 전체 목표는 계속 활성 상태이며 실제 서버/전체 콘텐츠/원경·도착 준비/혼합 부하와 위 trace 종료 문제를 완료로 세지 않는다.

## 8차 진행 — 이동 불연속과 관측 자원의 실제 경계

사용자가 서브에이전트 병렬 작업을 승인해 전투·네트워크·추적 수명을 독립 검토한다. 수정 파일을 나누고 native 실행과 빌드는 한 주체가 순차 수행한다. 기능 목록이나 진단 helper 존재를 실제 실행 연결로 세지 않는다.

일반 `TeleportTo`·Mover 순간이동·handover 이후 SSR capsule과 근접 notify의 이전 위치가 서로 다른 공간을 보간/휩쓸 수 있는 경로를 확인했다. 실제 성공 경계에서 위치 이력과 공격 공간 세대를 함께 갱신하고, 현재 공격·hit window는 유지하는 수정을 진행한다. 실패/시험 teleport와 정상 고속 이동은 별도로 보호한다. 완료 후 실제 성공·실패·callback 중 이동·이후 정상 공격 회귀로 판정한다.

기존 `ProjectJ.CaptureMoverBuffers`의120초 상한만으로는 고FPS·많은 actor에서 문자열·weak component 목록·발견 작업의 비용이 제한되지 않는다. 기존 CSV 소비를 유지하면서 메모리·발견/추적 작업을 제한하고, 중도 종료와 부분 기록을 명시하는 보강을 진행한다. 프레임별 파일 IO나 새로운 worker/pool을 추가하지 않는다.

7차 packaged trace 실패는 동일 실행 파일을 Win32 debugger로 두 번 관찰했다. 두 실행은 종료 직전 breakpoint를 debugger에서 처리한 뒤 exit0이었으므로 정상 수정 결과로 세지 않는다. disassembly의 실제 실패는 `TLockFreeFixedSizeAllocator<65536,64,FThreadSafeCounter>` 소멸의 `GetNumUsed() == 0` 검사이며, `FPageAllocator` 소멸 경로의64KiB MemStack 페이지가 남아 있었다. 최초 nearest-symbol의 `FPreloadFileRegistry` 이름은 실제 소멸 함수와 일치하지 않아 원인으로 사용하지 않는다. 미반납 페이지의 소유자는 아직 미확정이다. 같은 binary의 trace channel/이름 이벤트 조건을 분리해 확인하며, 검사 무시·강제 allocator 비우기·임의 engine shutdown 호출은 추가하지 않는다.

같은 Stage3/Game6 실행 파일에서 `-statnamedevents`를 제외한 `cpu,frame,bookmark`는 기능 success1·로그 오류0이어도 exit777003/52.635초였다. `gpu,frame,bookmark`는 기능 success1·로그 오류0·exit0/52.710초였다. 두 실행은 debugger 없이 실제 종료까지 기다렸다. CPU 추적 경로로 재현 조건을 좁힌 것이며, 원인 소유자 규명이나 성능 개선의 A/B 결과는 아니다.

캡처 보강은 유효 CSV 문자16MiB 미만(Windows TCHAR)·실제 retained allocation18MiB 상한, component1,024·world8·발견 actor 슬롯8,192/level128을 사용한다. 저장 시 변환·IO 임시 메모리와 프로세스 전체 peak는 이 retained 값에 포함하지 않는다. 상한 도달을 부분 캡처로 기록하고 실제 CSV 행수와 저장 결과를 receipt에 남긴다. Python 검증기는 새 receipt를 필수로 읽고 partial/저장 실패/행수 불일치/자원 상한 모순을 거절한다. 과거 CSV에 완료 receipt를 소급 생성하지 않는다. 새 경계 회귀를 포함한 Python58/58과 문서281개/로컬 링크3,172개/오류0을 확인했다.

73개 변경 source를 고정하고 `EditorBuild18` 직접 UBT 및168개 native 회귀·실제 Mover capture 소비 socket을 순차 검증한다. 아직 빌드 중이므로 위 C++ 변경을 검증 완료로 세지 않는다. 전체 목표, 실제 서비스/콘텐츠/대규모 혼합 부하의 남은 경계와 전체 완료 후 안전 정리 조건은 유지한다.

### 8차 실행 결과와 후속 검증

`EditorBuild18` 직접 UBT는545 actions·361.68초로 성공했다. `Phase8Main1`은168개 중167개 통과·1개 실패/경고 집계0였다. 새 `MoverCaptureBounds` fixture가 abstract `UNetworkPredictionComponent`를 직접 생성해 UObject ensure를 발생시켰다. cap/행수/메모리·해제 단언은 수행됐지만 전체 시험은 실패로 보존했다. 실패를 무시하지 않고 구체 `UMoverNetworkPredictionLiaisonComponent`를 사용하는 시험으로 수정했으며,9차 빌드·재실행에 포함한다. 전투 불연속 두 새 회귀는 위 실행에서 통과했다.

같은 Editor18 바이너리로 실제 server1/client2·NPC fixture100의 Animation/MoverBuffers/Quest/WorldClock5단계가 모두 exit0·timeout0으로 통과했다. 두 client 캡처는 실제 NetworkPrediction component2개를 관찰했고 각각 world361/363행·actor722/725행, peak retained16,798,856bytes였다. 새 완료 receipt·저장·행수·상한·초기화 후 공급/보간을 검사했다. client1의 캡처 전 NP 경고1개는 보존하고 측정 구간은 진단0이었다. NPC fixture100개를 실제 Mover100개/실제 연결100개로 세지 않는다.

정상 Editor의 저장 없는 MainWorld 비교에서 HLOD 설정 class가 맵 로드 전에 등록되고 두 layer 설정이 정상 해석되는 것을 확인했다. WorldState1개·정의20 places·PoseSearch cache 완료 로그7개도 관찰했다. 최초 helper의 Python ScriptName 오류는 수정했다. 후속2/3차는 runtime hash의 snake-case 조회 실패와 protected-property 접근 제한 때문에 전체 receipt가 partial/false이며, 실제 프로세스는 exit0·선택4개 에셋 SHA 유지다. 보호를 우회하거나 그 값을 미검증 상태에서 정상으로 채우지 않았다. 이 결과는 기존 `-game` 로드 순서 경고와 구분하며 runtime readiness·원경 시각/Cook 합격이 아니다.

[누적 증거](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `2026-10-26-8`에 위 성공/실패·실제 소스·독립 trace 조건을 함께 보존했다.

## 9차 진행 — 장기 실행의 절대 시각 정밀도

UE5.8의 World/GameState 시각은 double인데 프로젝트 전투·입력 RPC와 이력이 float로 축소했다. 절대 시각524,288초(약6.07일)의 float 간격은62.5ms,1,048,576초(약12.14일)는125ms다. SSR는 동일 timestamp를 치환하므로30Hz 실제 이력이 합쳐지고, 짧은 입력 간격과50ms 요청 허용/순간이동 경계도 손실될 수 있다.

기존 엔진 시각을 RPC·native interface·명령 이력·피격 request/정책·공격/캡슐 ring·검색/차감까지 double로 유지한다. 상대 시간 튜닝·주기·Alpha, 고정 이력 용량·단일 권위·요청 예산·중복 판정은 유지한다. 새 시계/프로세스/저장 schema를 만들지 않는다. 두 RPC의 시각 scalar는32→64bit로 바뀌므로 연결 양쪽 재빌드가 필요하고 구/신 wire 호환을 보장하지 않는다. 필드당4byte 증가는 압축/프레이밍 전 표현 차이이며 실제 연결별 전송량 증감은 별도 측정 대상이다.

기존 native leaf에12일 절대시각의31개30Hz 보존·인접 보간/만료, 실제 생산 clock의 hit window/공간 경계, 미래/과거 허용과 입력 RPC 구현→native interface→명령 이력의20ms 보존을 추가했다. 실제 handover transform 성공 후 progression callback에서 owner가 바뀌고 GC가 일어나 실패로 반환돼도 이미 옛 공격 기하가 폐기됐는지 기존 회귀를 보강했다. 캡처 abstract fixture 수정도 포함한다.

87개 변경 source를 고정해 `EditorBuild19`를 직접 UBT로 진행한다. native·양쪽 새 실행 파일의 socket·Game/BP/Cook 경계는 아직 검증 전이다. prepared Niagara soft-leaf 초안은 별도 검토 단계이며 아직 Source/에셋에 적용하지 않았다.

`EditorBuild19`는356.76초 후 컴파일 실패로 정상 종료했다. 최초 오류는 기존 audit network fixture의 SSR override가 옛 float 시그니처를 유지한 `C3668`이었다. 실제 fixture의 override·client 송신·보관·수신 기록까지 double로 맞췄다. 기존 입력 회귀가 이미 승인한 sequence1을 재사용한 새 시험도2/3으로 수정하고 생산 중복 거절 상태는 유지했다. 실패19 산출물로 native 성공을 보고하지 않는다.

엔진 GameState의 복제 시각·누적 평균은 double이지만 최종 offset은 float였다. 기존 프로젝트 GameState의 OnRep가 Super의 표본 누적/251 재기준화를 재사용하고, 같은0 sentinel·0.5 평활 식의 파생 offset만 double로 보관하도록 보강했다. 새8byte 파생 캐시는 기존 필터 응답을 바꾸지 않으면서 정밀도를 유지하기 위한 값이며, 추가 복제/저장 필드·timer·시계 서비스·권위 writer가 아니다. query는 값만 읽는다. 새 실제 GameState 회귀는12일 offset·초기화/zero·half-step·250/251 경계를 확인한다. 별도 Character 모듈의 큰 시각 producer 시험은 엔진 float offset을 시험 시계로 쓰지 않도록 concrete double-clock fixture를 사용한다.

`Run-SourceAuditNetwork.ps1 -SSROnly`는 기존 기본 full 검사를 유지하면서, 새 필드의 실제 전송을 위한6개 SSR 사례만 명시적으로 실행한다. float32로 정확히 표현되지 않는 server sample을 실제 client 송신과 inherited SSR RPC로 왕복한 뒤 비트 값이 보존되는지 비교한다. cue/이동 이벤트/몽타주 사례를 검증한 것으로 세지 않는다. 예전 audit 전용 montage/style 에셋을 새로 만들지 않고 해당 필드와 실제 피해·중복·공격 교체·종료 계약을 집중 검증하기 위한 선택이다.

92개 source를 다시 고정한 `EditorBuild20`과169개 집중 native·실제 baseline/lag-loss/reorder-loss SSR을 순서대로 진행한다. 현재는 빌드 중이며 Game·BP·Cook/패키지 경계와 전체 미완료 영역도 남아 있다.

### 9차 Editor·실제 통신 결과

- `EditorBuild20`: 직접 UBT 성공,546 actions·351.05초. `Phase9Main1`:169/169, 오류·경고·누락0,8.359초. 캡처 abstract fixture와 큰 시각 producer/입력 sequence 시험을 실제로 바로잡은 최종 실행이다.
- 실제 dedicated Editor server1/client2의 SSR6사례가 Baseline,75ms lag/variance25/loss10/dup5,order1/loss10/dup15 세 조건에서 모두 통과했다. 각 프로세스 exit0·timeout0, 실제 client→inherited SSR RPC의 non-float32 값이 exact equality를 만족했다. 피해1회·중복·서버 선판정·A→B→A·종료 거절도 함께 확인했다.
- 별도 `Phase9Input1`은 실제 raw 입력 RPC·authored R/후속 스킬·애니메이션, quest·clock·Mover capture를 통과했다. 모든 프로세스 exit0. 캡처는 world927/926행·actor1853/1852행, 현재 구간 NP 진단0·상한 내였다. client1의 캡처 전 초기화 경고1개는 보존했다. 이 검사는 원격 입력의 기능 연결이며, native의12일/20ms 정밀도 시험과 구분한다.
- 92개 source 지문이 전 실행에서 유지됐다. [누적 증거](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `2026-10-26-9`를 갱신했다.8차 native 실패도 보존하고 이 최종 회귀와 연결했다.

위 결과는 Editor/작은 실제 통신 경계다. 실제 큰 uptime late-join clock 동기화·Game/BP/Cook/패키지와 전체 MMORPG의 성능/콘텐츠/운영 완료를 뜻하지 않는다.

## 10차 진행 — 실제 realtime wire 버전 admission

현재 UE Hello checksum에는 소스/RPC 시그니처 자체가 자동 포함되지 않는다. 프로젝트는 별도 game network protocol을 초기화하지 않아 앞선 float32→64 변경을 옛 실행 파일과 연결 전에 확실히 구분할 근거가 없었다. Iris descriptor나 InternalAck 전용 compatible checksum을 일반 실시간 연결의 보장으로 사용하지 않는다.

기존 Primary game module Startup에서 UE의 `SetGameNetworkProtocolVersion`과 `SetGameCompatibleNetworkProtocolVersion`에 프로젝트 정수1을 전달한다. 새 GUID·handshake·PreLogin 서비스는 만들지 않고 기존 engine game slot을 사용한다. 제품 버전·저장 schema·향후 backend API와 구분한다. compatible 값은 replay 계약이며 다른 live current 버전의 자동 허용을 뜻하지 않는다. 원치 않는 replay 호환을 광고하지 않도록 두 값을 함께 유지한다.

95개 source를 고정한 `EditorBuild21`, native170개, 같은 버전의 실제 연결과 고정한 구 Game6 패키지의 Hello 거절을 순차 검증한다. 구 실행은 engine `-seconds`의 자체 종료를 활용하며, 체크섬 불일치 로그 한 줄이나 프로세스 exit0만으로 통과시키지 않는다. 실제 server Hello invalid-version·client OutdatedClient·동일 runtime features·Join/RPC 부재·실제 종료를 함께 확인한다. 현재는 빌드 중이다.

Niagara soft-leaf와 기존 shared loader 재사용안은 선택 우선순위·예약 포화·초기 recovery·실제 Niagara 재진입/GC·원격 style 교체·제작 타입 검증을 교차 검토하고 있다. 아직 Source/Content에 반영하지 않은 별도 초안이다.

### 10차 최종 Editor·admission 검증

`EditorBuild21` 직접 UBT는547 actions·356.59초로 성공했다. `Phase10Main1`은170/170, 오류·경고·누락0,8.407초였다. protocol1 server/client의 실제 SSR Baseline도 통과했다.

구 클라이언트 시험의 첫 preflight는 동일 데이터의 live OrderedDictionary와 JSON PSCustomObject 정렬 차이 때문에 실행 전에 거절됐다. 공용 provenance 비교기의 두 정렬에서 실제 path 값을 명시했고, 실제 pwsh의 JSON 왕복/행 순서 수락과 파일 변경·추가·삭제·plugin binary 변경 거절 회귀5개를 추가했다. 전체 Python63/63 통과. 비교 조건을 낮추지 않았고 C++ source95·바이너리는 유지했다. 도구 script 지문이 달라져 새 protocol1 Baseline2를 재수집했으며, 세 프로세스 exit0/실제 SSR을 다시 확인했다.

`RealtimeProtocolOldClient1`은 보존된 Game6/SHA 고정 package와 새 Editor21 server를 실제 연결했다. 같은 ProjectVersion/NetCL/EngineNetworkVersion·양쪽 IrisReplication에서 game protocol1/checksum193569404와 protocol0/checksum3006645529를 확인했다. 서버 Hello invalid-version/Upgrade, client OutdatedClient, Login/Join 미진입을 확인했고 둘 다 engine 자체 종료/exit0·timeout/강제 종료0으로 끝났다. 이 expected network error를 crash나 깨끗한 로그로 바꾸지 않았다. 서버의 Iris loaded-modules-updated 경고는 별도 남긴다. [누적 증거](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의 `2026-10-26-10`에 기록했다.

## 11차 진행 — cosmetic Niagara payload와 기존 loader 연결

기존 cue의 NiagaraSystem 필드명은 유지하고 참조만 soft로 바꾼다. 서버는 cue의 의미·loop/stop 메타데이터를 계속 읽되 payload loader를 요청하지 않는다. 공격 montage/pose/notify·전투 정확도 데이터의 참조는 유지한다. 명시적 Editor 제작 검증에서는 missing/wrong-type 경로까지 확인한다.

client는 기존 world VisualAssetSubsystem의 lease/물리 load·apply 한도를 재사용한다. component는 최대64개 path와 desired loop를 관리하고,128 profile/256 cue 방문 내에서 실제 override 승자를 준비한다. 실제 cue 요청은 사용하지 않는 prewarm/idle lease에 밀리지 않도록 양보받지만, shared world admission 자체의 거절은 여전히 가능하다. 신규 manager/pool·component tick은 없다. 일회성 cue는 놓친 순간을 늦게 재생하지 않고, loop는 현재 asset 세대·attack instance·선택 path·활성 의도가 맞을 때만 적용한다.

선택 변경은 원래처럼 combat cue 정리 후 weapon 교체를 유지한다. 초기 OnRep 후 BeginPlay는 수락한 pending cue를 지우지 않는다. 원격 style-only 변경과 Niagara stop의 동일 tag 새 공격/GC 재진입을 기존 수명 계약으로 연결한다. 선택 변경 시 기존 cue를 퇴역시키는 의미는 유지하며, 별도 batch/늦은 metadata 참조의 완전한 현재 loop 재구성까지 보장한 것으로 세지 않는다. 필수 전투 경고의 가시성이나 전체 Niagara/GPU 동시 비용 예산은 이 cosmetic 경로와 별도다.

101개 변경 source를 고정한 `EditorBuild22`는 RHI fixture의 latent 매크로 인수 안 전처리 지시문에서 실패했다. 정상 종료 후 람다를 매크로 밖으로 옮겼으며, `EditorBuild23` 직접 UBT는547 actions·353.43초로 성공했다. `Phase11Main1`은171/171, 오류·경고·누락0,8.552초였다. 실패한 실행은 보존했다.

실제 profile 조회는 공개 Python에 노출되지 않은 NativeMake 함수 사용으로 첫 실행이 실패했고, 공개 TopLevelAssetPath 생성자와 export_text로 수정했다. `ProfileRead2`에서 실제 profile1개/cue1개와 두 단계의 set/style 참조를 확인했다. 기존 Niagara hard import를 가진 선택 Greatsword profile 하나만 이전 SHA 확인·백업 후 저장했다. `ProfileSave1`은 실제 exit0, cue tag·무기 부착/socket·transform·loop/stop 의미가 동일하고, Niagara package 의존성이 hard에서 soft로 이동했다. 새 SHA는 `48eef06a67133fd66e3ba6936e4d6b3967efc1cb83e6c1fa0b265a250b4994c9`다. BP·set·style은 저장하지 않았다. 이 두 단계 참조 조회를 전체 BP 그래프 사용 증거로 세지 않는다.

`Phase11RHI1`은 새 D3D12/SM6·RX9070XT 프로세스에서 기존 EffectsPoolLifetime1/1을 통과하고 실제 exit0으로 종료했다. authored trail100개×12회/모드와 cold Aura의 공유 요청·중단·현재 loop 적용·동일 tag 재진입/GC 계약을 검사했다. 테스트 오류/경고0·전체 로그 오류0이다. 전체 로그의 MCP 플러그인 이용 안내 경고1개 때문에 엄격 wrapper의 원본 passed는 false이며 이를 덮어쓰지 않는다. 렌더 오프스크린 테스트 world이고 gameplay viewport·GPU frame·시각 품질·CPU/FPS/resident 개선 측정은 아니다.

같은101개 source의 `GameBuild7` 직접 UBT도449 actions·271.03초/exit0으로 성공했다. `ProfileRead3`은 새 프로세스에서 저장 SHA·soft edge를 유지했고, profile 로드 후 Niagara payload가 로드되지 않았음을 확인했다. 이는 선택 profile 하나의 근거이며 전체 character hard closure나 메모리 절감량을 뜻하지 않는다. 실제 BP 컴파일·제작 검증·새 Cook/패키지와 전체 MMORPG의 남은 영역은 계속 미완료다.

## 12차 진행 — 실제 원격 외형 교체·상호작용 진입 경계

교차 검토에서 원격 weapon-profile-only OnRep가 local pair setter와 달리 무기를 먼저 파괴하는 잔여 경로를 확인했다. combat cue 퇴역 후 weapon 교체로 순서를 맞추고, callback 뒤 owner/world/선택 세대를 확인한다. 실제 old weapon 파괴 callback의 pending loop/notify 퇴역과 owner 파괴/GC를 기존 PresentationRecovery 회귀로 검사한다. 무기 생성 함수 자체의 old-destroy·빈 슬롯·spawn 반환 전 재선택은 pinned owner/profile/pose를 확인하고 현재 선택을 다시 읽는다. 한 번의 생성이 superseded되면 기존 bounded/coalescing presentation queue에 최신 선택을 넘긴다. 큐 거절 뒤 retry도 같은 admission으로 돌아가며 local 동기 경로로 한도를 우회하지 않는다.

실제 입력 binding→TryInteract→ServerTryInteract→충돌/반영 호출 경로에는 기존 공통 요청 예산이 빠져 있었다. RPC 진입에 PlayerState shared quota와 기존 mount와 같은0.2초 domain 간격을 연결했다. trusted server의 직접 TryInteract 경로는 유지한다. 기존 ServerSelection에 reflected production RPC의 수락/예산 소진/Avatar 교체/동기 burst/직접 서버 호출을 추가했다. 추가 WorldPartition 준비 source도 기본 PC 및 같은 PC의 다른 component와 velocity/history 이름이 겹치지 않도록 component path로 구분했다.

`EditorBuild24`는 새 fixture의 First/Profile 지역 이름 중복 C4456 두 건으로 실패했다. 실제 exit6과 잔여 프로세스 부재 확인 후 이름만 수정한 `EditorBuild25`는547 actions·353.89초/exit0으로 성공했다. `Phase12Main1`은174개 중173개 성공·1개 실패/실제 exit255였다. 실패는 새 Recovery fixture가 abstract native weapon base를 생성한 것이며 해당 callback 조건은 아직 검증되지 않았다. 이를 숨기지 않고 현재 사용 중인 concrete `BP_Greatsword_Basic`으로 fixture를 바꿨다.

기존 RemoteEquipmentBurst도 유효한 authored mesh/root와 concrete weapon class를 사용하게 했다. 기존 friend에서 실제4096-component 큐 포화·거절·retry Tick·재입장·128-visit FIFO/4-apply 수렴을 확인하도록 확장했고 새 friend/timer는 없다.105개 파일의 `EditorBuild26`은547 actions·352.46초/exit0으로 성공했다. `Phase12Main2`는175개 중174개 성공·1개 실패/실제 exit255였다.300명 생성과4096건 포화는 통과했지만, remote role fixture의 일반 Destroy가 엔진 권한 검사에서 거부되어 owner가 제거되지 않았다. 엔진 LevelActor.cpp의 authority/bNetForce 조건을 확인하고 fixture에서 Destroy(true)의 실제 성공도 검사하게 했다. 생산 코드는 이 후속에서 바꾸지 않았다.

`EditorBuild27`은547 actions·353.39초/exit0으로 성공했다. `Phase12Main3`은175/175, 테스트 오류·경고·누락0,9.877초/실제 exit0이었다.105개 source 지문도 유지됐다. 전체 로그 오류0이며, 엔진 시작 smoke-test 시간 초과 안내와 MCP 이용 안내 경고2개는 별도로 보존한다.300명 수치는 NullRHI의 동일 authored weapon 생성/큐 수렴 근거이며 실제 화면 FPS나 혼합 부하 수용량을 뜻하지 않는다.

실제 소비 BP4와 profile/set/style까지7개를 정확히 선택해 무저장 컴파일·native 제작 검증했다. 첫 helper는 공개 Python에 없는 선택적 parent_class 조회에서 실패했지만 BP4 컴파일 자체는 성공했다. 해당 부가 조회만 제거한 `BPData2`는BP4 UpToDate·검증7 Valid/오류/경고0·실제 exit0, 저장/dirty0·선택 파일 해시 동일이었다. 실제 GameMode의 GameStateClass는 `/Script/Project_J.Project_JGameState`였다. `GameBuild8`·새 Cook/실제 패키지를 이어간다. 중간 성공과 실패를 [누적 증거](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)에 함께 보존한다.

## 13차 실행 구조 묶음 — 2026-10-27

사용자 요청에 따라 세부 타격/개별 예외의 반복 대신 실제 비용 경로 세 개를 함께 변경하고 공통 빌드·대표 부하로 검증한다.

- **NPC 값 파이프라인:** 기존 공유 spatial snapshot에서 weak actor 식별과 immutable worker 후보를 직접 준비한다. 중간 actor/position 배열 두 개와 복사를 제거했다. worker도 후보별 Scores 배열과 후속 전수 reduction 대신 단일 순회·128-candidate chunk의 부분 reduction을 사용한다. 점수 공식·tie·취소·4096 병렬 threshold·작업 admission·Mover 단일 writer와 cadence는 유지한다. 기존 대표 부하12표본/모드의 로컬 비교에서 16,384후보 compute p50은 serial 275.601→208.601µs, candidate-parallel 168.901→84.799µs였다. 전체 frame/FPS·CCU나 반복 A/B 통계 증거로 확대하지 않으며 A03 Mover/Mass 전환 완료로 세지 않는다.
- **연결별 속성 투영:** 실제 remote VM이 소비하는 Health/MaxHealth/Mana/MaxMana는 공개를 유지한다. owner character-sheet와 authoritative 계산의 AttackPower/Defense만 owner-only로 바꿨으며 GAS REPNOTIFY_Always는 유지한다. 기존 public exporter의 BP17/관련package27 무저장 관측과 native 소비자를 확인했다. 유일한 generic hit는 Health/-10 ScalableFloat modifier의 비활성 empty capture였다. 범위 밖 동적 콘텐츠의 무사용 증거로 확대하지 않는다. 기존 socket scenario에서 owner/remote/실제GAS NPC의5단계 결과를 함께 비교하며 realtime protocol은2로 변경한다.
- **역할별 무기 payload:** metadata의 WeaponActorClass 이름은 유지하고 soft class로 변경했다. 기존 공유 visual loader의 한 current lease와 기존 생성 큐를 사용한다. warm 로컬 생성은 즉시, cold 완료는 큐를 통해 적용하고 socket intent를 보존한다. 실패·교체·종료와 actor가 class 수명을 인수하는 경계에서 lease를 반환한다. 서버 pose/notify·판정 metadata는 유지하고 dedicated visual 요청은 만들지 않는다. 선택 profile의 백업/저장과 fresh root-only 관측을 통해 기존 hard edge가 실제로 제거됐는지 확인한다. disk bytes를 RSS/VRAM 절감량으로 환산하지 않는다.

초기 Editor28의 fixture capture 오류와 Main1의 cold class를 준비하지 않은 GC fixture 실패를 보존했다. 113개 source를 고정한 Editor30은 직접 UBT 547 actions·345.89초/exit0, Main2는 기존176개 회귀 모두 통과·9.510초/exit0이다. 별도 fresh cold-class 실행도1/1·exit0이다. 실제 선택 무기 profile1개를 백업 후 같은 metadata를 보존하여 soft class로 저장했고, fresh profile 및 실제 player root-only 로드에서 class payload5개가 로드되지 않음을 확인했다.

실제 서버1+client2의 Audience2는 owner/remote/native GAS NPC의5단계 속성90행을 각 client에서 모두 통과하고 세 프로세스도 정상 종료했다. 함께 수집한 Mover stream의 interpolation 진단 때문에 전체 strict 결과는 false로 보존하며 actor/sim 원인을 분리한다. 이전 Audience1의 NPC 준비 실패 역시 보존했다. 실제BP4·제작 검증7은 무저장/해시 동일·exit0으로 통과했다. 같은 source의 Game9 직접 UBT449 actions·254.83초/exit0, 새 Cook5/Stage5·패키지UI4조건·실제 입력/GAS/몽타주/notify/soft cue/해제3회도 정상 종료했다. Entry/ThirdPerson과 AlwaysCook closure의 근거이며 full MainWorld/HLOD·출하 Server·전체 MMORPG 완성으로 확대하지 않는다.

Audience2 보간 진단은 actual NPC에만 귀속됐고, actor CSV의 presentation view-frame 전진 간격 중앙값은 원거리0.4초·근거리0.1초였다. 이 값은 SDK GetPendingFrame()이 노출한 보간 소비 frame의 전진 간격이며 raw 수신 간격·패킷 도착 시각으로 확정하지 않는다. NPC10Hz poll/기본80m 관심 범위와 명시 static priority를 보존하며 기존 Iris sphere cadence floor를 재사용하는 설정을 후속 적용했다. Source 재빌드 없이 같은 Editor30의 Audience3는 속성·quest/clock·Mover capture 전체 strict pass와 actual exit0×3을 확인했다. AudienceLoss1(75ms/10%)도 속성90행과 actual exit0×3은 통과했지만 저빈도 NPC 보간 진단으로 Mover strict false를 보존한다. 설정 조정 반복이나 진단 숨김 대신 이 손실 경계를 열린 후속으로 두고 다음 공통 구조 묶음을 진행한다. 앞선 strict false와 설정 변경 전 Game9/패키지 근거를 구분한다. 전체 MMORPG 목표·미정 운영 서비스/실제 월드·혼합 부하는 계속 미완료다.

## 14차 공통 실행·표현 예산 묶음 — 진행 중

원격 표현 비용과 월드/진행 상태의 실제 소비 경로를 함께 보강한다. 새 전역 manager나 미정 서비스 adapter를 추가하지 않으며, 호환되는 소스 변경을 묶어 한 번 빌드한다.

- **월드 이벤트 실행:** Disabled 설정은 등록 전에 확정한다. 안정적인 EncounterId 소유권과 runnable 순회 등록을 구분하여 잘못된 제작 설정만 남은 월드가 매 frame 실행되지 않게 한다. 기존 Register를 통한 같은 소유자의 명시적 복구·해제도 정합하게 유지한다. 스폰 지면 query는 기존 static/dynamic 지지를 보존하면서 Pawn 몸체를 지면으로 오인하지 않게 한다. full WP/Nav/GPU readiness 구현과는 구분한다.
- **장식 물리 예산:** 기존 source gameplay pose·손발 접촉·FBIK·평가 주기는 보존한다. 실제 로컬 view 관측과 기존 BoolAlpha를 사용해 명시적으로 확인한 원격 Far의 장식 RigidBody 작업만 조절하는 초안을 준비했다. PA의 실제 simulated bone 출력·socket/핵심 bone 비침범을 검사한 뒤에만 선택 Body BP를 opt-in한다. 기존 CVar 비교와 200명 대표 이동/공격 집합으로 비용·품질을 함께 판단한다. 소스는 공통 묶음에 적용했으며 실제 에셋 opt-in은0개다. 엔진의 실제 alpha/evaluation-gap 복구를 재사용하고 별도 proxy 상태나 강제 reset은 추가하지 않았다. 검증 완료는 아니다.
- **진행·보상 경계:** 실제 퀘스트 보상 callback 수명을 기존 operation pin으로 보호하고, bag-only/동일 receipt 커밋은 progression 게시를 생략하도록 연결했다. 초기 복원과 실제 XP/직업/전직 정의 객체 교체의 GAS grant 갱신은 보존한다. append-only grant 재사용도 기존 active 객체 prefix가 동일할 때만 허용한다. standalone 저장 adapter의 변경을 미정 운영 DB/서비스 성능으로 확대하지 않는다.

133개 source/config/script를 고정한 Editor31 직접 UBT548 actions·359.26초/exit0과177개 공유 회귀60.101초/exit0을 통과했다. 정적 Body/PA 자료는 비침범과 해시 불변을 확인했지만 AssetProof1의 Python 종료 AV/exit3으로 strict false를 보존한다. AssetProof2 commandlet도 정상 script Main0 뒤 같은 Python 종료 AV/exit3을 재현했다. plugin 제외 요청에도 실제 toolset53개가 남아 분리 성공으로 세지 않는다. 실제 에셋 opt-in은0개이며, 같은 Editor31·에셋의 임시 clone200명 OFF/ON은 모두 실제 exit0으로 끝났다. 같은 부하의 frame p50은 이동58.216→58.249ms, 공격76.946→76.550ms, 복귀53.599→53.176ms로 의미 있는 개선을 입증하지 못했다. 공격의 source gameplay pose demand200·로컬 accessory 비활성0은 보존했지만 추가 양보 대상이2–10명에 그쳤다. 이 distance-only 후보는 실제 에셋에 적용하지 않으며, 비용/적용 범위 근거를 보강하기 전 ABBA·canonical 품질 집합을 반복하지 않는다. 정적 비침범 자료와 clean host 종료·최종 품질 승인은 구분하여 미완료 경계를 보존한다. 앞선13차의 정상 통신 strict pass와 지연/손실 보간 strict false를 함께 유지한다. 손실 조건의 진단은 remote player 일부에도 귀속됐으며, 공유 world buffer가 양수여도 개별 actor의 보간 bracket을 보증하지 않았다. 진단 숨김이나 임의의 전송량/지연 증가로 완료 처리하지 않는다.

## 15차 게임플레이·인벤토리·소셜·월드 실행 묶음 — 진행 중

전체 MMORPG 목표를 유지하며14차의 장식 물리 후보만 반복하지 않는다. 기존 소비 경로 네 묶음을 소스에 통합하고 공통 빌드/기존177개 집중 회귀로 검증한다.

- 동일한 정상 수량·정규화된 잠금 상태는 기존 authority/session/정의 검증 뒤 FastArray dirty·알림·새 로컬 저장 명령을 생략한다. 명시적 keyed command의 receipt 검증과 persistent 장비 변경 금지는 유지한다.
- 파티/길드 복원은 중복 AddUnique와 전체 membership 재설치 대신 선형 set delta와 리더 변경분을 투영한다. 전체 순서 있는 authority snapshot·revision·삭제 tombstone은 유지한다. 유일한 FName ID와 tombstone을 단순 lifetime cap으로 막아 장기 서버가 영구 생성 불가가 되는 변경은 채택하지 않는다.
- 외부 mount-item RPC는 PS 공통 quota와0.2초 domain 간격을 소비하고, trusted server/async continuation은 private authority 경로에서 기존 inventory/lock/current-mount 재검증을 한다. NPC activation의 grant→action commit은 기존 ActorInfoRevision을 함께 검증한다. 이미 활성인 NPC lease를 ActorInfo 교체 전에 정리하는 계약까지 해결한 것은 아니다.
- 미래/종료 Dormant UTC 일정은 기존 초기 stagger와 분할 순회를 거쳐1초 heartbeat로 합친 뒤 대기한다. 기존 방문128/update8/spawn2/1ms를 유지하고 UTC는 Tick당1회 읽는다. Preparing의 빈 update·closed schedule의 gameplay quota 소모를 생략하며 준비 callback/timeout·등록 변경은 깨운다.1024 owner·실제 World.Tick 진행과 시계 복구를 기존 대표 회귀에서 확인한다. 새 heap/manager나 가짜 WP/GPU 준비 플래그는 추가하지 않는다.138개 source/config/script를 고정한 Editor32 직접 UBT548 actions·351.65초/exit0과 기존177개 회귀59.944초/exit0을 통과했다.1024 초기 stagger·실제 시간 진행/대기·준비 wake와 실제 profile no-op/수신자 epoch를 기존 묶음에서 확인했다.

## 16차 제작·생활의 실제 실행 연결

레시피 정의→기존 제작대 상호작용/요청 예산→현재 profile의 재료 소비와 결과 지급 단일 커밋→receipt/실패/세션 종료를 연결했다. optional recipe 오류·제거는 제작만 닫고 core class/item 레지스트리와 unrelated profile 복원을 유지하며, 레시피는 character-wide RulesFingerprint와 분리한다. 반복 요청은 현재 재료 검사 전에 확정 receipt를 조회하므로 응답 유실 뒤 같은 요청을 복구할 수 있다. 실시간 계약은3으로 올렸다. Editor33 compile 실패(exit6)는 필요한 private test 접근과 fixture의 명시 type include를 수정했고, 고정한150개 소스의 Editor34 직접 UBT553 actions·345.75초/exit0, 공유178개 회귀60.125초/exit0을 통과했다.

MainWorld·기존 사용자 레벨을 보존하고 새 레시피 `/Game/DataAssetSets/Crafting/DA_PrototypeHealthToMana`와 별도 `/Game/Tests/Crafting/L_CraftingPrototype`만 저장했다. Author1은 저장 전 Python standalone 구조체의 EditDefaultsOnly setter 거절로 논리 실패/실제 exit0이었다. 설치 엔진의 공개 구조체 생성자 계약에 맞춘 Author2는 두 새 package 저장·기존 에셋/소스 해시 불변·실제 exit0을 확인했다. 실제 D3D12 game 실행 Play1은 기존 플레이어 상호작용→health 재료2개 소비/mana1개 지급→단일 profile revision→기존 owner UI 요약 일치를2.342초에 확인하고 exit0으로 끝났다. 이 수치는 제작 처리 지연/프레임 성능 측정이 아닌 fixture 완료 시간이다. Prototype 배합은 편집 가능한 예시 데이터이며 생산 밸런스 승인이 아니다. 로컬 개발 저장 provider와 즉시 제작의 검증을 운영 멀티플레이 경제·전체 생활 콘텐츠 구현으로 확대하지 않는다. 17차의 Game10·Cook6/Stage6에서 동일 제작 맵의 패키지 상호작용·재료/결과·저장·UI 일치와 실제 프로토콜3 통신/구2 거절까지 확인했다. 전체 생활/경제·운영 저장 경계는 계속 남아 있다.

## 17차 소유 아이템 기반 동료 실행 — 로컬 통합 범위 검증

canonical companion item→기존 InventoryVM Activate/Use→PlayerState 세션 component→공유 명령 quota/0.2초 domain→기존 월드 준비8개·frame당 적용2개/1ms→명시 비전투 NPC follow→기존 AI action32 update/128 visit/0.75ms·path64 pending/16 inflight/4 dispatch/8 delivery→Mover 단일 writer를 연결한다. 별도 pet tick·timer·로더·thread pool을 만들지 않는다. 세션당 하나의 runtime lease를 소유하고 소유자/동료 사망·아이템 자격 상실·실제 Avatar 교체·종료 때 해당 lease만 정리한다. 일반 탑승/하차는 ASC의 gameplay rider를 따라가며, NPC에 플레이어 owning connection이나 GAS RPC 권한을 부여하지 않는다. 새로운16파일의 Editor35는 복제 매크로 인자명 오류/exit6을 보존하고, 수정한 Editor36 직접 UBT556 actions·351.21초/exit0을 확인했다. 설치 UE5.8의 CLI -DisableUnity는 descriptor NU와 actual Default PCH 의존성 cache 경로를 다르게 만들어 반복 전수 빌드를 유발했다. 두 TargetRules에 bUseUnityBuild=false를 명시하고 두 빌드 runner에서 해당 flag만 제거해 non-unity·Shared 환경을 유지했다. 엔진 수정·캐시 삭제·바이너리 복원 없이164개 지문을 고정한 Editor37은3.70초, 동일 소스 Editor38은0.81초/모두 no-action·exit0이었다. 이는 불필요 재컴파일 제거의 증거이며 게임 frame 개선 수치가 아니다. 새 동료 DA1개 저장과 보호된 기존 에셋10개/소스 해시 불변·Author1 실제 exit0을 확인했다. 시작 로그 경고2개는 보존하고 strict 전체 log clean으로 세지 않는다. 공유180개는179개 통과/1개 실패를 보존했다. 실제 동료 수명은 통과했고 Nav fixture가 요청한 follow200cm 대신 전투150cm로 도착을 비교한 오류를 수정했다(실제 도착 약175cm). scripted 목표도 두 번째 live Mover의 transform을 우회하지 않도록 Actor+ASC로 교체했으며 실제 추적 NPC/Nav/Mover는 유지했다. Editor39는 해당 cpp1개/전체4 actions·5.85초/exit0이었다. 이후 해당 fixture의 실제 Nav5경로·동료 수명·공유 준비를 묶은7개는6.291초/경고0·실패0·exit0을 통과했다. 변경 없는 나머지179개의 앞선 근거는 재사용한다. Game10 직접 UBT134 actions·107.10초/exit0, 명시 Entry/ThirdPerson/제작 맵의 Cook6·Stage6, 패키지 UI 기본/동적2해상도4실행·실제 제작/저장/UI·전투3cycle를 모두 exit0으로 확인했다. 공식 UnrealPak의 읽기 전용 IoStore 목록에도 새 레시피·제작 맵·동료 DA3개가 포함됐다. 동료의 실제 소환/이동은 authored Native/Editor 근거이며 packaged 소환/원격 audience나 대규모 성능까지 확대하지 않는다.

프로토콜3 실제 server1/client2의 Baseline1은 별도 rate montage 입력을 지정하지 않아 case30에서 기능 실패/actual exit0×3을 보존했다. 명시 SSR-only Baseline2의 실제6개 SSR/health·float64 시각 왕복은 timeout0·actual exit0×3으로 통과했다. 동일 소스/바이너리의 actual protocol3 서버는 보존된 Game9/Stage5 protocol2를 Hello/Upgrade에서 거절했고 Login/Join/관측 Actor 채널·RPC 진입 없음과 실제 exit0×2를 확인했다. 구 바이너리를 복원/덮어쓰지 않았다. 이 통신 근거를 운영 인증·대규모 동접·손실 보간 문제의 해결로 세지 않는다. 펫 전투·성장·수집·운영 영속 unlock은 별도 미완료 범위다.

## 18차 채집 실행·저장 확정과 현재 투영의 분리 — 진행 중

기존 SubmitEdit의 live bool은 저장 확정 뒤 현재 Actor/Profile에 설치·게시할 수 있었는지를 포함하며, owner 종료 뒤에는 통보하지 않는다.3파일의 선택 GT durable outcome observer를 실제 채집 소비자의 선행 기반으로 Source에 통합했다. worker는 값과 원래 저장/receipt 상태만 준비하고 GT task가 native 관측자를 한 번 호출한 뒤 weak subsystem을 다시 조회해 기존 live fencing을 적용한다. 기존 bool/UI API는 유지한다. 현재 세션에서 receipt 부재가 확인돼도 이것은 해당 조회 시점의 증거이며 영구 거절이 아니다. Unknown/Expired는 미지급으로 간주하지 않는다.164개 지문을 고정한 Editor40 직접 UBT20.94초/exit0 뒤 기존 프로필 반복 회귀는 통과했다. 확장 제작 회귀의 I/O 오류 주입은 첫 flush가 COMMIT이면 UnknownOutcome을 반환할 수 있는데 Unavailable만 기대한 fixture 전제를 수정했다. 생산 분류는 유지했고, Editor41의 테스트 cpp1개/4actions·5.88초/exit0 뒤 제작 회귀는0.313초/exit0으로 통과했다. 실제 원래store7=UnknownOutcome·조회receipt3=Invalid→ProvenAbsent·GT 통보1회를 확인했다. 첫 실패/exit255는 보존한다.

채집 Actor가 언로드돼도 NodeId의 pending/cooldown을 유지하는 작고 bounded/no-Tick 월드 소유자와 실제 Interactable→canonical HealthPotion→단일 profile Add/receipt→확정 후 cooldown/UI 소비자를 준비한다. 노드별 Tick·timer와 새 범용 manager를 만들지 않는다. 살아 있는 중복 ID·공간/세션 권한·포화·실패·late callback과 expired cooldown의 제한된 회수를 함께 다룬다. Unknown 예약은 timeout만으로 풀지 않고, 부재가 증명돼도 옛 operation을 terminal/fence한 뒤 재가용 처리한다. profile/receipt는 원자적이어도 world cooldown은 같은 DB 트랜잭션이 아니며 서버/월드 재시작 reset을 운영 영속 경제로 부르지 않는다. 실제 Node/subsystem/native 회귀·명시 playable fixture8파일을 Source에 통합했다. 지원 밖 네트워크 모드의 초기 프롬프트도 닫히도록 replicated Phase는 Disabled로 시작하고 정상 standalone 등록만 Available을 게시한다. git apply의 EOL 정규화로 초기 raw hash 대조가 실패했으나8개 normalized text와 명시 Phase 보강의 일치를 확인했고, 빌드 중 Source 변경은 없었다.171개 raw 지문을 기록한 Editor44 직접 UBT22.28초/exit0, actual Gathering lifecycle·Craft 복구·기존 interaction 선택·공유 quota4개는0.551초/경고0·실패0·actual exit0을 통과했다. 새 owned Gathering map 저장·실제 플레이/UI·packaged 동료·원격 audience는 다음 통합 경계다.

메인 월드는 Source/설정/기존 에셋을 저장하지 않고 현재 D3D12·uncooked Editor-game으로 대표 실행했다(A10StockConsole1 actual exit0·보호3에셋/소스 해시 불변). S00의 실제 MainWorld partition과 기본PC source/추가 WorldStreamingPreparation source2개를 확인했다. 추가 source는 Priority192·Local·Activated·NonBlocking·(-93200,-93200,2348)·radius60000cm였다. 초기 로딩35.45초 뒤 첫 Tick에서 stock deferred 명령이 실행됐고, 초기 문자열 검색은 실제 출력 제목을 놓쳤으므로 재실행하지 않았다. 종료 중 HLOD 표시 결정과 초기 PSO hitch100/precached0을 관찰했지만, 원본 cell unload/원경 품질이나 패키지 PSO 성능의 합격/실패로 확대하지 않는다. 다음엔 기존 선택 GT smoke에 bounded source/cell/collision 조회를 연결하고 실제 도착/이동 소비자와 묶는다. 시간만 늘린 반복·HLOD 전역 토글·Ghost 이동은 하지 않는다.

18차의 새 owned Gathering 맵은 실제 canonical 아이템·기존 UI와 연결해 Author/Play 모두 정상 종료했다. Play에서 두 채집의 수량2·revision2와 동료의 소환/해제/재소환2회를 확인했다. 실제 이동/Nav 검증으로 확대하지 않는다. companion audience의 TObjectPtr 추론 오류(Editor45)와 Game 모듈의 IrisCore 직접 의존 누락(Editor46)은 실패로 보존했다. `.Get()`과 기존 CharacterModule과 같은 `SetupIrisSupport(Target)`으로 고친 Editor47은124 actions·94.38초/exit0이며 엔진을 수정하지 않았다. 새 native·복제 계약은 realtime protocol4로 구분하고 저장 schema는 유지했다. 고정173개 입력의 공유181개는62.191초·경고0·실패0/exit0, 실제 server1/client2의 owner/remote 속성 각90행과 동료7단계/각14행도 actual exit0×3으로 통과했다.100개 static AOI probe를100개 이동 AI나 대규모 동접 성능으로 세지 않는다.

현재 MainWorld의 한 번의 초기 NullRHI snapshot은 실제 WP provider가 소비됨·35개 cell의 제한 내 완전한 조회·완료 상태·실제42/96cm 캡슐과 walkable 지면/비중첩을 확인했다(A10Readiness1, actual exit0). GPU·Nav·이동 도착·HLOD 시각 품질의 증거는 아니다. 같은 입력의 GameBuild11은122 actions·71.58초/exit0이며 MainWorld·제작·채집을 포함한5개 명시 맵의 Cook7/Stage7을 진행한다. 패키지 실행과 구 protocol3의 실제 거절은 아직 다음 검증이다.

18차 Cook7은 MainWorld를 포함한5개 명시 맵에서 error0·warning0/exit0, Stage7도exit0이었다. 현재 Game11과 동일 SHA의 패키지 UI 기본/동적2해상도4실행·실제 채집2커밋/수량2/UI·동료 소환/해제 각2회·제작·전투3cycle 모두 정상 종료했다. 같은 cooked D3D12 MainWorld의 초기 snapshot은36개 셀·유효 provider 소비·완전한 조회·지면/캡슐 준비를 확인했다. PSO Missed 관측을 보존하며 hitch나 프레임 개선으로 바꾸지 않는다. 새 protocol4 SSR-only server1/client2는 receipt3개·timeout0·actual exit0×3이고, 동결한 같은 소스/바이너리의 서버는 보존 Stage6 protocol3를 실제 Hello/Upgrade에서 거절했다. Login/Join/관측 Actor 채널/RPC 진입 없음·actual exit0×2와 원본 바이너리 불변을 확인했다. 전체 운영 인증·혼합 부하·손실 보간·도착/HLOD 품질의 미완료 경계는 유지한다.

## 19차 콘텐츠 규칙 호환과 실행 수명 비용 — 로컬 통합 범위 검증

기존 프로필의 전체 퀘스트 카탈로그 hash 때문에 수락하지 않은 콘텐츠 추가도 저장 복원을 막던 제약을, 저장된 active/claimed ID의 계약으로 좁힌5개 파일을 통합했다. 기존 Format2·worker encode/decode·receipt·owner 투영·UI를 재사용한다. 과거 all-catalog hash는 현재 전체 정의와 정확히 일치할 때만 수락하고 정상 checkpoint에서 scoped 형태로 전환한다. 이미 정의가 달라진 과거 저장을 추측해 열거나 폐기하지 않는다. 새 형태를 구 바이너리로 rollback하는 호환은 보장하지 않는다. 현재 바인딩은 이전 saved-ID 규칙을 먼저 검증한 뒤 새 수락 ID로 범위를 확장하고, 기존 active/claimed 의미 변경은 다음 쓰기 전에 admission을 닫으며 이미 수락한 immutable 작업은 정상 drain한다. 최대128개의 선택 ID 정렬을 전체 경로 O(128)이나 실제 성능 수치로 표현하지 않는다. 현재 정의 index와 검증의 카탈로그 순회는 남는다. 아직 이 묶음의 컴파일·실행 검증 전이다.

NPC의 사망·ASC/컨트롤러 교체 감지를 잃는 단순 idle park는 적용하지 않는다. 실제 NPC·동료 action의 등록/해제는 기존 dense 배열에 최대2048개 weak-key 파생 index와 swap 제거를 연결해 N개 burst의 검색/이동 O(N²)를 예상 O(N)으로 줄였다. 다음 소비자 identity와 callback 중 peer 재시작/self 종료·GC, world-stop 때 callback 전 두 저장소 비우기를 다룬다. 기존 heartbeat·0.1초 cadence·128visit/32update/0.75ms 상한과 path/공격 취소는 유지한다. 소셜은 매 Bind의 RevokedPlayers 전수 청소를 제거하고 private/non-UPROPERTY/nonreplicated PlayerState bool에 같은 revocation을 붙인다. 명시적 Bind만 재허용하며 현재 replacement가 로그아웃해도 살아 있는 옛 세션은 거절한다. 실제 Iris 관계 filter 연결과 장기 group ID/tombstone 퇴역은 이 변경으로 완료되지 않는다.

같은 묶음의 렌더링은 MainWorld 실제 cooked 관측에서 찾은 구체적인 비용을 다룬다. RHI 신규 Missed73개와 상세 FullPSO SkyPass 누락1개는 다른 지표다. 설치 UE5.8의 SkyPass collector와 실제 realtime sky capture의 depth format/flag/load/stencil 차이를 공개 collector에 별도 capture variant로 보완했다. 엔진·셰이더·VF·기존 collector를 교체하지 않고 monolithic/deferred/UE5.8/cube-layout에만 적용하며 main-view와 depth-disabled/MSAA 변형은 건드리지 않는다. 실제 InstancedStaticMeshes usage가 빠져 GT로 작업을 돌린6개 MI도 선택적으로 조사·저장한 뒤 새 Cook에서 확인한다. 아직 콘텐츠 변경 전이며 Source180개를 고정한 Editor48과 관련 native189개 실행을 준비했다. 성능 수치나 모든 PSO/hitch 해결을 미리 주장하지 않는다. 미정 공급자용 빈 서비스나 새 thread pool은 추가하지 않는다.

19차의 고정180개 Source/Config/Script 입력은 Editor48 직접 UBT78 actions·86.12초/exit0, 실제189개 공유 회귀65.664초·경고0·실패0/exit0, Game12 직접 UBT40 actions·53.90초/exit0을 통과했다. 선택6개 MI의 원본 백업·전수 사전검사 후 usage override만 저장했고, 별도 새 Editor Read에서도 usage/override·부모·공개 effective/raw 파라미터와 보호 에셋/소스 불변을 확인했다. Author/Read 모두 실제 exit0이었다. 동일5개 명시 맵의 새 Cook8/Stage8도 exit0이며 현재 Game12와 동일 SHA의 패키지 채집2커밋/UI·동료 소환/해제 각2회·제작을 정상 종료했다. UI 배치나 전투 동작 변경이 없어 이전18차 UI4조건·전투3cycle 근거는 재사용하고 반복하지 않았다.

같은 Game12/Stage8·같은 머티리얼의 D3D12 MainWorld에서 sky 보완 OFF/ON을 각각 새 UserDir로 실행했다. 상세 SkyPass 누락은1→0, RHI 신규 PSO는73→72였고 두 실행 모두 실제 exit0·초기36셀 CPU 준비를 통과했다. 이전 Stage7에서 관측된 선택 MI6개의 `Had to pass SMU back to game thread`도 두 새 실행에서는0건이었다. OS/driver cache는 보존했으며 이 표적 누락 해소를 전체 hitch·FPS·GPU 준비나 원경/고속 이동 완료로 확장하지 않는다. 기록은 기존 [검증 요약](../../Review/Audits/Evidence/MMOFoundation_2026-10-26/ValidationSummary.json)의19차에 둔다. 다음 묶음은 실제 목적지 preparation을 소비하는 이동·이벤트 종료 경계이며 전체 목표는 계속 활성 상태다.

## 20차 월드 이동의 준비 소비와 이벤트 종료 — 진행 중

기존 `WorldEncounter`의 중앙1초/최대8update 사이에 사망 NPC가 파괴되면 성공 `OnResolved` 대신 실패10초 cooldown으로 처리되던 경로를 보강했다. 파괴 callback에서도 같은 권위 `IsDead` 계약을 사용하고, 확장 가능한 life-query 뒤 NPC·SpawnRevision·phase·월드/소유자 수명을 재검증한다. 기존 NPC/token/해결 identity는 외부 callback 전에 분리하며 정상 파괴는 실패 의미를 유지한다. 새 death manager·timer·보상 정책은 만들지 않았다. 생산 cpp1개와 기존 실제4NPC fixture 연결3개 파일을 통합하고 직접 Editor49 UBT9 actions·34.05초/exit0을 확인했다. 관련 기존17개는1.696초·경고0·실패0/exit0이며, dead/healthy destroy·정상 poll·nested destroy와 완료 listener의 ownerDestroy/GC를 검증했다. 불필요한 별도 Cook 없이 포털/파티의 반영·wire 계약이 준비되면 다음 배포 기준점으로 묶는다.

월드 이동은 기존 상호작용·PS 명령 예산→서버 목적지 ID 해석→현재 PC의 streaming source→정상 지면/캡슐 판정→Mover effect의 실제 결과로 연결했다. bool 하나를 준비 완료로 믿지 않고 provider/WP/셀 범위와 서버 streaming-off 정책을 구분한다. Pawn/세션 교체·사망·탈것·원점 이탈·취소/timeout 및 queue 확정 경계를 다룬다. 기본 Mover 성공 delegate에는 request ID가 없으므로 해당 effect가 생성한 native 이벤트의 공개 처리 callback과 실제 도착 검증을 연결했다. 초기 지원은 동일 WP 월드·동기 Mover·정지/지상/비탑승이며 NoWP는 큐 없이 거절한다. 준비 ACK의 quota 거절은 같은 세대의0.5초 재시도로 복구하고, 이미 수신한 결과는 deadline보다 먼저 처리한다. Nav/GPU 준비·분산 서버 이관을 이 CPU gate로 대체하지 않는다.

A07의 실제 소비자는 owner-only 파티 요약→기존 PlayerUIComponent→실제 HUD다. 읽기 전용 Editor 조사에서 현재 BP GameMode/PlayerController의 PlayerUI.ScreenClass가 `/Script/Project_J.Project_JPlayerHUDWidget`임을 확인했다(ConfiguredHUDRead1, actual exit0·소스/보호 에셋 불변·저장0). 현재 native 기본 화면 경로를 실제로 사용하므로 미사용 fallback에만 기능을 붙이는 문제가 없다. 검토한 파티16개·포털6개 파일과 선택 MainWorld 실행 연결을 Source에 통합했다. 파티는 최대8행의 값/이름·레벨·정량 health·로컬 서버 바인딩만 복제하며, Actor/ASC/Account·사적 전투 상태는 보내지 않는다. 기존 PlayerState 수명의 GAS/reset-completion/Pawn lease와0.2초 one-shot coalescing을 쓰며 파티가 없으면 관측/타이머도 없다. 같은 group revision에서 접속 바인딩·metadata가 바뀌는 재진입도 이전 행으로 덮지 않도록 게시 직전에 현재 원천을 다시 읽는다. 일반 Pawn/PlayerState의 실제 Iris relevance 비용 감소로 미리 세지 않는다.

통합 wire는 protocol5다. Editor50 UBT193 actions·178.02초/exit6의 UObject 종료 API/DOREPLIFETIME 인자 오류를 보존하고, 두 cpp만 수정한 Editor51은5.53초/exit0이었다.191개 공유 회귀는190개 통과·포털 fixture1개 초기화 오류/exit255를 보존했다. 정상 World/ASC 초기화로 수정한 Editor52는6.29초/exit0이고 관련5개 기능 검사는 완료됐지만 World EndPlay 누락 경고1개로 strict 실패를 유지한다. 해당 시험 종료 순서만 추가 보강하고 원격/HUD 선택 실행 연결과 다음 빌드로 묶는다. 포털 실제 MainWorld 실행 및 파티 실제 소켓/픽셀 검증은 아직 남아 있다.

정상 EndPlay를 추가한 Editor53 직접 UBT는6.28초/exit0이고 관련5개는0.256초·경고0·실패0/exit0으로 마감했다. 같은 바이너리의 A10PreparedArrival1은 실제 MainWorld에서 반영 ServerTryInteract·공유 예산/정상 선택→생산 Portal→목적지 source→MIP Mover의 요청별 native 결과→지면/캡슐 도착을 완료했다. S00 `(-93200,-93200,2348.75)`에서 실제 S01 `(6800,16800,7897.999)`로 이동하고 추가 source가 정상 Pawn predictor로 복원되는 것도 기록했다. 실제 exit0·Source/보호 에셋 불변이며 NullRHI/standalone이다. 단계 관측을 field 성능이나 클라이언트 socket·Nav/GPU/HLOD 품질로 확대하지 않는다. 원격 파티/HUD 선택 검증7파일과 NoTrace 옵션을 통합한 Editor54도26.23초/exit0이며 실제 server1/client2·PNG 검증은 진행 중이다.

현재 Editor49/동결182개 입력의 기존 Native NavigationLoad100도 CPU trace 없이 실제 CSV로 확인했다. 실제 NPC100·decision/action/Mover·Recast 경로·동적 장애물8개가 겹친 Mixed100의 합성 월드 Tick P95는2.611ms, queue P95는75.151ms·engine-observed15.443ms였다. 종료 후 남은 요청/금지 callback은0이었다. 고정0.05초 시뮬레이션·미렌더링·접속0이며 공격/회피·Pawn 충돌·authored body가 없는 조건이므로 실제 서버 frame/FPS나 개선률로 표현하지 않는다. 현재 병목 기준선과 제한된 queue 수명 근거로 보존하고, 임의로 inflight/dispatch 한도를 올리지 않는다.

동결191개 입력의 실제 파티 소켓 `ArchitectureBatch20PartyAudience1`은 server1/client2의 protocol5·actual exit0×3·timeout0을 확인했다. 실제 BP Pawn의 원래150m cull을 복원해 서로 다른 Pawn이 없는 거리에서도 owner FastArray·현재 HUD와42개 권위 행이 일치했다. 멤버 합류/health burst/탈퇴/재합류·8행/실제 peer 정상 접속 종료 후 unavailable을 확인했고, foreign PS의 private roster는0이었다. 첫 실행 client의 실제 역할은slot1이라 launch 순서를 역할로 가정하지 않았다. NoTrace로 수집한960×540 PNG도 직접 확인해 리더80%·멤버60%·연결 정보 없는6행이 겹치지 않고 표시됨을 확인했다. 추가6행은 실제 접속6명이 아니다. 최종 아트·다중 해상도·세계 렌더링 품질 또는 기존 always-relevant PlayerState/ASC 전체 비용 감소를 입증한 것으로 세지 않는다. 같은 구현의 Game13 배포 기준점과 실제 원격 MainWorld 소유자 이동은 다음 검증이다.

Game13 직접 UBT124 actions·102.28초/exit0과 MainWorld 포함5개 명시 맵의 Cook9/Stage9도 정상 종료했다. 동일 Game13 SHA의 Development 패키지에서 실제 D3D12 S00→S01 준비 이동·지면/캡슐·정상 predictor 복원, 채집2커밋/UI·동료 소환/해제 각2회·제작·전투3cycle 모두 actual exit0이었다. 원경/도착 GPU 품질·실제 대규모 동접을 이 결과로 보장하지 않는다. 새 protocol5 실제 SSR 대조군과 보존된 Stage8 구4의 Hello 거절은 다음 순차 확인이며, 정상 MainWorld 서버/실소유 client 이동은 별도 미완료 범위다.

현재 protocol5의 새 SSR-only server1/client2 대조군과 보존 Stage8 구4의 실제 Hello/Upgrade 거절도 완료했다. 정상 대조군 actual exit0×3, 구4 거절 actual exit0×2·timeout0이며 Login/Join·관측 Actor RPC 진입이 없고 소스/바이너리 불변을 확인했다. 인증 서비스나 수용량의 증거로 확대하지 않는다.

## 21차 실제 실행 비용과 원격 월드 이동 — 진행 중

현재 Game13/Stage9의 실제200명 렌더 기준선은 normal exit0·cooked1·로컬 품질662표본으로 완료했다. 기존 엄격한 Rig/Weapon 소비자의 동일 phase2–7초·capture frame 제외·공격200 montage 필터를 사용했다. 이동 frame P95는66.100ms/GT50.982ms/GPU11.353ms, 전원 공격은81.519/68.678/15.751ms였다. 스레드 시간은 합하지 않으며 GT에는 병렬 작업 대기가 포함될 수 있다. 전체200 cloth와 로컬 LOD/물리 품질을 유지한 한 번의1280×720 기준선이므로 개선률·실제 동접·60FPS 보증이 아니다. 큰 CPU 실행량의 실제 원천을 먼저 확인하고 후속 개선을 선택한다.

정상 MainWorld GameMode와 실제 소유 client의 production 상호작용 RPC를 사용하는 원격 준비 이동 검증은 기존 선택 fixture의 Saved 초안으로 준비했다. 일시적인 source를 놓치는 표본 간격과 광고 geometry만 보고 실제 소비 복원을 오인하는 두 관측 경계를 독립 검토에서 찾아 보강 중이다. 원거리 PlayerState/ASC fanout은 세션 참가·GAS/장비·현재 Avatar의 수명 계약과 연결된다. 설정만 바꿔 전체 PS를 de-relevance하면 클라이언트 세션 해제 의미까지 달라질 수 있으므로 owner 지속 상태와 Avatar 표현의 점진적 분리안을 검토한다. 이 검토 자체를 비용 감소로 세지 않는다.

월드 이벤트의 기존 Owners 항목에 파생 visit slot을 붙여 활성/해제의 반복 전수 검색·배열 이동을 swap 제거로 바꿨다. 살아 있는 다음 consumer identity와 중앙 예산·ID 권위·callback revision fence를 유지한다. 일괄 N개 등록/해제의 예상 평균 비용을 O(N²)에서 O(N)으로 줄인 구조 변경이며 FPS 수치는 아니다. 비활성/재활성·stale owner·이동된 다음 slot·일괄 EndPlay를 기존 BoundedExecution에 연결했다. UHT 진단 CSV 문자열 길이(Editor55)와 시험 변수 shadow(Editor56)는 실패로 보존하고, 같은 CSV 내용의 인접 TEXT 상수/시험 변수 이름만 고친 Editor57은6.62초/exit0이었다. 관련8개 이벤트·동료·월드 준비는2.449초·경고0·실패0/exit0이었다.

정상 BP GameMode의 첫 원격 MainWorld 실행은 실제 전용 서버 PlayerController::BeginPlay의 터치 UI 조회가 Slate CurrentApplication 단언을 일으켜 server exit3로 실패했다(A10PreparedOwnerRemote1). 남은 client는 중단하지 않고 exit0까지 기다렸으며 양 역할 receipt 부재로 strict 실패를 보존했다. 기존 ShouldUseTouchControls 한 곳에 local player/Slate 초기화 guard를 넣어 원인을 수정했고 Editor58 직접 UBT8.74초/exit0을 확인했다. 입력·UI를 강제로 서버에 초기화하거나 diagnostic GameMode로 회피하지 않았다.194개 입력을 고정한 A10PreparedOwnerRemote2는 정상 BP GameMode server/client 모두 actual exit0으로 완료했다. 실제 client TryInteract RPC·복제 Portal·양 역할의 목적지 source 선관측·Mover native 결과와 S01 물리 도착을 확인했다. 정상 predictor 광고 Z7898과 실제 소비 Z7800의98cm 차이는 UE400cm 위치 양자화 안에서 동일2D/3D hash·동일 shape/정책임을 기록했다. client CPU 셀/캡슐 준비와 Source/보호 에셋 불변도 통과했다. raw pose 완전일치를 강제하지 않았고, Nav/GPU/HLOD·대규모 수용량·패키지 재배포는 별도 미완료다.

같은 Editor58의 실제 RHI200 계측은 normal exit0·로컬 품질636표본·기존 엄격한 workload 소비자 통과였다. 전체 동기 trajectory library 호출의 GT 경과시간을 기본 OFF/nonshipping 선택 counter로 측정했다. 이동은80개 표본에255회·중앙값0.953ms, 공격은29개 표본에5800회(표본마다200회)·중앙값12.522ms/P95 12.851ms였다. 기존 시각 예산은 이동 중 원격 호출을 줄이지만 공격 보호 구간의 예측 비용은 남는다. ticker와 animation update의 frame ID/누적 차이를 기록했으며 이 경과시간을 지연된 GT counter에서 빼거나 worker CPU로 부르지 않는다. 이전 packaged 기준선과 causal A/B도 아니다. 실제 미래 단계마다 spring/tuning 상태가 변하므로 단순 cache·무조건 AnyThread·예측 밀도 감소를 근거 없이 적용하지 않는다.

## 22차 현재 Avatar의 공개 전송과 실제 소비 — 진행 중

공개 장비 definition/slot FastArray의 전송을 전역 PlayerState manager에서 기존 Pawn EquipmentRuntime의 SkipOwner로 옮겼다. private instance·저장/transaction·grant writer는 기존 PS manager에 유지하고, 중복된 PS 공개 배열/dirty writer는 제거했다. 새 Actor·manager·tick·비율 조정은 없다. 원격 Runtime은 자기 완전 snapshot을 소비하며 동일 slot/definition은 시각 generation을 재시작하지 않고 unresolved reference의 후속 receive도 조정한다. Controller/PS뿐 아니라 늦은 Owner 복제도 기존 bind를 깨운다. old cleanup callback이 새 target manager를 종료하는 경우 강한 pin 뒤 accepting을 재검사한다.

기존 Pawn style/profile RepNotify를 원격 표현의 단일 writer로 유지하고 detached rider의 Runtime/grant lease를 보존한다. manager의 원격 공개 조회는 현재 Avatar 범위로 바뀌므로 PS.Pawn이 Runtime 없는 탈것일 때 빈 목록이며, retained rider의 외형은 자기 Runtime snapshot으로 유지된다. 기존 Blueprint 함수 형식은 유지한다. PS/ASC의 always-relevant·polling·세션 참가/해제는 바꾸지 않는다. 이 slice는 불필요한 원거리 공개 장비 전송을 줄이는 실행 구조이며 bytes·GT·FPS 개선률은 아직 측정하지 않았다.

애니메이션은 native trajectory 소비의 임시 값 배열 한 개만 const proxy 참조로 대체했다. 참조는 한 동기 traversal 안에서만 쓰며 Blueprint getter와 PlayerTrajectory 소유 복사는 유지한다. 공격 source clock·예측 밀도·복귀/air/steering·권위 pose를 건드리지 않았다. 측정한 predictor12.5ms 범위 밖의 복사 제거이므로 그 시간을 줄였다고 주장하지 않는다. 기존 publication 회귀에 저장소 격리와 다음 발행 검증을 연결했다.

교차 검토한 장비12파일·복사2파일을 기존 MainWorld 진단을 보존하며 통합하고 realtime protocol6으로 구분했다. 저장 schema는 유지한다.199개 입력의 Editor59는 새 진단의 TObjectPtr 자동 포인터 추론 오류/actual exit6을 보존했고, 명시 .Get() 한 곳만 고친 Editor60 직접 UBT는10.56초/exit0이었다. 같은 입력의 공유191개는66.168초·경고0·실패0/exit0으로 통과했다. 실제 server1/client2의 장비6단계도 각6행·actual exit0×3·timeout0으로 통과했다(ArchitectureBatch22EquipmentInterest1). 두 관찰 역할의 원거리 Pawn/공개행/외형0, 보이지 않는 동안 해제한 뒤 빈 snapshot 재진입, 같은 PS의 실제 Avatar 교체/재장착과 owner private 보존을 확인했다. 파티/속성·quest/clock 검증도 통과했고 actual HUD8행 PNG를 직접 확인했다. wire bytes·정의 payload identity·CPU 감소나 수용량 측정은 아니다. 교체 시점의 missing-object RPC 거절3건은 원문을 보존하며 정확한 old object/function은 이 캡처에서 미확정이다. 전체 로그 무경고/무오류로 부르지 않는다. 같은 소스의 Game14 직접 UBT와 다음 Cook/패키지 기준점을 진행한다.

Game14 직접 UBT112 actions·97.08초/exit0과 같은 입력의5개 명시 맵 Cook10/Stage10도 정상 종료했다. 직접/패키지 SHA가 일치하는 새 Development에서 D3D12 MainWorld S00→S01 도착·24개 CPU 셀/캡슐 준비, 채집/UI·동료 소환/해제 각2회, 제작, 전투3cycle가 모두 actual exit0이었다. 새 protocol6 SSR-only 대조군과 보존 Stage9 구5의 실제 Hello/Upgrade 거절도 통과했다. 정상3개·구버전2개 프로세스가 실제 exit0이며 source/binary 불변·Login/Join/관측 Actor RPC 미진입을 확인했다. 새 저장 schema나 인증 공급자를 도입하지 않았다. 전체 목표는 계속 활성 상태이며 다음은 선택한 실제 source cell과 HLOD 프록시의 연결/언로드 전환이다.

## 23차 선택 source cell과 원경 대체 표시 — 진행 중

기존35/24개 CPU readiness 셀 목록은 nonspatial 항목을 제외하므로 persistent HLOD가 없다는 근거가 아니었다. 설치 SDK의 구 GetHLODActorsForCell은 deprecated 빈 반환이며 current GetHLODObjectsForCell과 interface source GUID를 사용한다. 일반 Actor 외 Custom/FastGeo와 정상 nullable UObject도 구분하고, 최대8개 interface/각64개 component metadata만 실제 iterator로 읽는다. 새 actor/asset load·manager·world 전수 검색 없이 기존 선택 진단1파일에 연결했다.

Editor61 직접 UBT60.50초/exit0 뒤 A10SelectedHLODRead1은 Source/보호4에셋 불변·actual exit0이었다. S00의 `FB8491DCDEAA260EB606FDBCCF77860E` source cell과 실제 normal HLOD1개가 같은 GUID로 연결됐고, registered 메시27개/인스턴스502개를 확인했다. 원본 state2/visible 상태에서 프록시27개의 visibility flag는 모두 false였다. 기존 Actor 이름에 StarterRegion이 남아 있어도 현재 package/GUID 연결을 확인했으며 이름만으로 stale로 판단하지 않는다. NullRHI의 한 원점 snapshot이며 표시 전환·GPU/시각 연속성·전체 HLOD 제작 완료가 아니다. 다음은 기존 실제 Portal 이동 probe가 같은 weak runtime cell/GUID/partition을 유지해 S01 도착 후 원본 언로드와 현재 프록시 표시를 함께 관찰하는 범위다.

기존 실제 Portal probe에 동일 weak cell/GUID/partition·proxy/27개 part 추적을 선택적으로 연결했다.15초/최대64회·0.25초 관찰이며 변화 표본과 최종 성공만 저장하고, Level/proxy 강한 참조를 tick 사이에 보관하지 않는다. Editor62 직접 UBT12.95초/exit0 뒤 A10SelectedHLODHandover1은 actual exit0·Source/보호5에셋 불변·3개 관찰로 완료했다. 원점 Activated/원본 visible/프록시 hidden에서 S01 실제 물리 도착 후 동일 source Unloaded·동일27개 프록시 component visible을 한 표본에서 확인했다.27개/502instance 구성도 유지됐다.

중간 표본에는 coarse state1/원본 IsVisible=false·프록시0이 있었다. SDK는 MakingInvisible도 Loaded로 축소하고 점진 component 제거를 수행하므로 이 표본 간 약0.297초를 픽셀 공백 시간으로 해석하지 않는다. 실제 level visibility/full-visibility 조건과 renderer frame을 구분해 목적지 카메라의 다음 RHI 관찰로 확인한다. 현재 성공은 논리적 셀/표시 전환이며 연속 화면·전체 forest 제작 최신성·GPU 준비·지정되지 않은 고속 이동 성능 보장은 아니다.

Editor63 직접 UBT12.96초/exit0 뒤 A10SelectedHLODRHI1도 D3D12·actual exit0·보호5에셋/Source 불변으로 완료했다. 원점/도착1280×720 PNG 각1개가 실제 viewport2draw 이후 request/processed·파일/hash 검사를 통과했고 두 이미지를 직접 확인했다. frame83의 viewport 관측은 coarse state1/IsVisible=false이나 underlying MakingInvisible·실제 Level visible=true·프록시0, frame92는 Level invisible/removing·프록시27, 이후 Removed·프록시27이었다. 관측 viewport callback에서 실제 Level과 프록시가 둘 다 꺼진 상태는 없었다. 원점의 건물/식생과 도착의 교량/지형/원경은 확인했지만 시점/가림이 달라 선택 프록시의 모든 픽셀이나 연속 전환 품질을 확정하지 않는다. 렌더 제출/표시 상태와 두 정지 화면의 제한된 근거로 마감하며 전체 forest 최신성·GPU 준비·고속 이동 보장과 구분한다.

## 24차 복합 퀘스트와 생활 결과의 원자적 진행 — 진행 중

기존 QuestComponent에2–8개 stable ObjectiveId의 AND 목표를 연결했다. 빈 Objectives는 기존 단일 정의/Count/schema1을 유지하며, 복합 상태는 별도 counters만 writer로 사용하고 legacy Count는0으로 검증한다. 표시명/배열 순서와 규칙 의미를 구분해 fingerprint를 canonical ID 순서로 만든다. 새 복합 상태가 있을 때만 nested quest schema2/domain2를 쓰고 상위 Format2·legacy enum/operation intent/scoped1은 유지한다. 과거 unscoped whole-catalog hash는 정확한 당시 catalog만 지원하는 기존 한계를 완화하거나 추측 이관하지 않는다.

채집은 일반 Add와 구분되는 trusted edit이며, 제작/채집의 아이템과 해당 objective delta를 기존 GT Proposed snapshot 안에서 함께 계획해 같은 worker/receipt/CAS 커밋으로 확정한다. 성공 뒤 별도 이벤트 저장으로 진행도를 추가하지 않는다. 복합 수락도 기존 command로 영속 확정한 뒤 게시하므로 Accept의 bool은 admission이며 OnChanged/committed state를 기다린다. 기본 canonical 정의와 legacy default 준비는 OpenProfile/BeginPlay/CompleteOpen의 명시 idempotent 경계에서 공유하고, callback 후 원래 입장 조건과 수명을 재확인한다.

기존 HUD·owner-only quest 복제·회귀와 owned 채집 prototype의 실제 gather2→기존 bench craft1→journal2/2·1/1→기존 Claim·XP7을 연결하는17파일을 검토/통합했다. 새 service/Actor class/registry/eventbus/timer/worker framework는 없다. realtime layout은 protocol7로 구분한다.200개 Source/Config 입력의 Editor64 직접 UBT를 진행하며, 새 정식 DA1개·owned map bench1개는 성공 빌드 후 백업/보호 검사 아래 제작한다. 아직 실제 Author·native·Play·wire·다음 배포 검증 전이며 운영 경제/DB·전체 MMORPG 완료로 세지 않는다.

Editor64의 aggregate 필드 순서/fixture 이름 컴파일 오류를 보존하고 Editor65·66 직접 UBT는 각각35.57·7.34초/exit0이었다. Entry Author는 실제 exit0으로 정식 복합 DA와 owned map의 bench1개를 저장했고 기존11개 보호 에셋/Source/helper 불변을 확인했다. 첫 native는118개 완료 후 시험 입력의 TArray 자기 별칭 단언으로 중단됐고 GC 이후 raw 주소 비교 시험도 실패했다. 시험 두 곳만 고친 뒤 남은73개는52.018초·경고/실패0·actual exit0으로 통과해 생산 입력이 같은191개 근거를 합쳤다. 실제 RHI Play1은 gather2/craft1과 동일 revision을 확인했지만 중첩 HUD window를 탐색하지 않는 시험 코드 때문에 journal 검사에서 strict 실패했다. 생산 HUD와 구분해 최소 탐색 수정을 진행하며 실제 픽셀·보상·통신·다음 배포 검증은 아직 완료로 세지 않는다.

Editor67 직접 UBT7.22초/exit0 뒤 Play2의 실제 D3D12 채집2→제작1→기존 journal 두 counter→기존 보상 명령·XP7/커밋 revision+4가 actual exit0으로 완료됐다.960×540 PNG를 직접 확인해2/2·1/1과 보상 버튼을 확인했으며 보호13에셋·Source/helper는 불변이었다. QuestWire1도 실제 protocol7 server1/client2·5단계·timeout0·actual exit0×3이고, 각 단계의 정확한 복합 counter/tracking과 foreign progress 부재가 서버 ACK 조건에 포함됐다. after provenance도 일치했다. Game15 직접 UBT74.33초/exit0 뒤 새7 SSR-only 대조군은 actual exit0×3, 보존 Stage10의 구6은 Hello/Upgrade에서 Login/Join/RPC 전에 거절됐고 actual exit0×2·소스/바이너리 불변이었다. Cook/Stage는 다음 호환 실행 묶음과 한 번의 공통 기준점으로 처리한다. BP가 필요할 때 핵심 권위·계산·수명·저장은 C++에 두고 기존 Blueprintable 콘텐츠 설정/표현을 연결한다. 테스트 전용 우회나 기능마다 새 framework를 추가하지 않는다.

## 25차 기존 Niagara 소유자와 풀 재사용 — 진행 중

기존 EffectsPoolLifetime의 기본 OFF 분기로 선택2 NS의 public C++ getter만 읽었다. Editor68 직접 UBT7.28초/exit0, Entry D3D12의 실제1개 native 회귀·경고/실패0·exit0과 보호3에셋/Source 불변이었다. 두 NS는 공유 vendor EffectType을 사용하지만 설정 배열0·인스턴스/전역 예산 없음이며, trail의25m override는 별도 유지됐다. Saved wrapper의 단일 report 경로 배열이 scalar로 풀리는 오류는 기존 저장 자료만 재처리해 바로잡았고 Unreal을 재실행하지 않았다. 원래 실패와 수정 근거는 유지한다.

거리 significance와 resume를 사용하는 Engine의 active 정렬 예산은 warm spawn 입장·peak component 상한이 아니다. 이를 혼동해 정책을 추가하거나 공유 vendor 에셋을 수정하지 않는다. 이 묶음은 World/Attached cue의 로컬 view identity를 precull 전에 전달하고 풀 재사용 시 true/false를 활성화 전에 명시하는 수명 정확도에 한정한다. 기존 generation·취소/교체 gate와 pool API를 유지한다. 새 FX scheduler·추측한 품질 상한은 추가하지 않는다. 이후 우선 CPU 묶음은 기존200명 전원 공격의12.5ms trajectory 근거를 재사용해 예측 한 호출의 불변 조회를 값으로 모으는 후보이며 spring·미래 step·BP 확장·게임플레이 pose 정확도를 유지해야 한다. 별도 NPC cleave 초안은 독립된 후속이며 이 묶음에 합치지 않는다.

작업 방식은 현재 실행의 source/asset을 고정하고 다음 Saved 초안·독립 검토를 병렬로 진행한다.24차는 시험 결함의 집중 재실행과 기존118개 근거 재사용,25차는 후처리 오류의 native 재실행0회를 기록한다. 호환되지 않는 큰 변경을 묶지 않고, 필요한 배포 검증은 공통 기준점에서 수행한다. 이 두 사례를 근거 없는 전체 단축률로 확대하지 않는다.

로컬 시점/풀 수정은 Editor69 직접 UBT8.41초·관련5개0.442초·기존 D3D12 EffectsPoolLifetime0.961초의 첫 통합 실행에서 통과했다. World cue만 captured local flag를 사용하고 Attached는 기존 live view linkage를 유지하며, 풀 재사용 때 이전 flag를 지운다. 실제 카메라 변경·동일 pooled component 재사용·Activate callback의 refresh/GC 취소를 확인했다. 에셋/품질 상한은 변경하지 않았다. RHI 검사에는 정확히 일치하는 MCP 라이선스 안내1건을 정보로 보존하고 모든 기능/그 외 경고는 계속 실패로 취급했다.

Game16/Cook11/Stage11은 성공했지만 최적화된 패키지의 새 screenshot lambda가 실행 중 self-unbind 후 캡처를 재사용해 충돌했다. 관련 Gathering/Party 두 경로를 flag-only callback→기존 tick/poll 해제로 고쳤고, 시험용 bench 이동은 Actor 직접 이동에서 기존 Mover effect queue와 실제 range/LOS 관찰로 바꿨다. Editor70/Game17 직접 UBT는35.45/41.27초·exit0이었다. 반영/에셋이 같아 Cook11을 재사용한 Stage12도50.39초/exit0이며, 동일 Game17 패키지의 복합 채집/제작/journal/XP7과 실제 전투3cycle이 정상 종료했다. 최종 PNG를 직접 확인했고 Mover out-of-band 경고는 재현되지 않았다. 이 공통 기준점으로24–25차의 로컬 범위를 마감하며, 전체 목표·주요 CPU·손실 보간·미정 운영 서비스는 계속 미완료다. 새 screenshot 결함의 재작업은 숨기지 않고 보존한다.

## 26차 지상 이동의 네이티브 실행 — 로컬 통합 검증

200명 전원 공격의 predictor 비용을 우선 검토하며, 단계별 값 payload를 추가하는 후보보다 프로젝트가 이미 보유한 Mover fork의 native 상속 경로를 선택했다. SmoothWalking의 생성자/type helper를 MinimalAPI로 공개하고 새 native 부모가 기존 GroundMovementLibrary를 그대로 호출한다. 새로운 설정·스레드·큐·상태 cache는 없다. 실제 BP override가 있으면 엔진의 표준 dispatch는 계속 이를 호출한다.

Editor71 직접 UBT35.84초/exit0과 기존 NativeDecisionBoundaries의 실제 solver/private spring5필드 비교가 통과했다. 현재 BP의3개 노드·모든 인자 연결과 상속 기본값22개를 읽고 백업한 뒤, 같은 BP 경로의 부모와 그 함수 override만 이관·저장·새 프로세스 재개방했다. 보호5에셋과 Source는 불변이었다. 첫 저장 시도는 함수/이벤트 목록을 혼동한 검사에서 저장 전에 멈췄고 원본은 불변이었다. API 의미에 맞춘 strict 검사는 유지했다.

Editor72는 고정 warmup 시간만으로 준비가 끝났다고 가정하던 fixture를 actual200 무기/가시성/메시 일치 준비 조건으로 보강했다. 첫 미완료 baseline은 보존하고, 같은 binary의 Before2/After1은 실제200명·이동/점프/공격·로컬 품질·정상 종료를 통과했다. 이동 predictor interval 중앙값은0.9849→0.6099ms, 지상 dispatch는 BP200→native200이었다. 호출 빈도가 달라 가중 호출당 비용도 함께 보존한다. 전원 공격은 별도 공중 모드이고9.40015→9.1903ms로 차이가 작아 주요 공격 병목의 해결로 세지 않는다. 한 순서쌍의 cache/스케줄링 한계와 별도 패키지 조건도 구분한다.

통합191개 중190개를 재사용하고, 이전 immediate parent 이름을 기대하던1개만 native 부모·원래 solver 상속·실제 native dispatch 조건으로 갱신했다. Editor73/집중1개가 통과했고 실제 server1/client2의 정상 이동·착지·buffer 관측도 timeout0/exit0×3이었다. 전후 화면의 일부 점프 순간은 촬영 시각이41.350ms 달라 연속 물리/포즈 동치로 확대하지 않는다. 원래 generic prediction은 이미 로컬 상태 하나를 재사용하며 현재 기본 경로에92배 전체복사나 루트모션 추출이 없으므로, 그 추측에 따른 변경은 하지 않는다.

Editor74/Game18 직접 UBT는16.58/30.07초·exit0이었다. 변경한 실제 BP의 native 부모를 포함한 새 Cook12는66.92초·오류/경고0, Stage13은46.94초·exit0이었다. 같은 패키지의 실제200명 이동·점프·전원 공격·200무기·로컬 품질676표본과 기존 전투3cycle이 정상 종료했다. Source205와 전체 source/runtime provenance의 전후 일치, phase4/5 PNG를 확인했다. 기존 검증에 함께 넣은 제한된 점프 관측4행은 입력 요청 뒤 JumpSequence0→1·공중200명·1.0796초에서 지상200명 복귀를 확인한다. 이전 BP의 동일 물리 timeline은 없어 소급 동치 증명으로 세지 않는다.

패키지의 전원 공격은 실제200개 모두 native UFlyingMode·warped montage0이었다. 별도 패키지 조건의 predictor interval 중앙값13.476ms는 Editor의9.1903ms와 전후 비교하지 않는다. 다음 개선은 실제 이 경로의 요청 내 불변 parameter를 한 번 준비하는 후보이며,92개 샘플·solver·정확도·실제 fallback을 보존하고 같은 binary 비교에서 효과를 판단한다.26차는 이 로컬 범위만 검증 완료이고 전체 MMORPG·운영 서비스·수용량·손실 보간 목표는 계속 진행한다.

## 27차 공중 이동 예측의 요청 내 불변값 준비 — 로컬 통합 검증

실제 패키지에서 확인한 exact native Flying 경로는 기존 parameter 구성 함수를 공유하고, 동기 GT·기본 시작 상태·선택 입력 없음·입력/상태의 이동 기저 없음·명시적인 유효 방향일 때 요청당 한 번 준비한다. 각92단계의 속도·방향·시간과 원래 free-move solver·출력·수명·갱신 빈도는 유지한다. 나머지는 기존 GenerateMove로 처리하며 새 데이터 payload/cache/worker 계층은 없다. 기본 비활성 후보를 Editor75와 독립 snapshot 수치 회귀·같은 binary200명 OFF/ON에서 검증한 뒤 채택 여부를 판단한다. 기존 bounded 관측의 누적 counter는 실제 사용 증거이며 모든 actor의 모든 요청이 해당 경로를 사용했다는 증명은 아니다.

Editor76/77과 집중 NativeFlyingTrajectoryParams가 통과했고, 같은 Editor77·Source/에셋의 실제200명 OFF/ON은 전원 공격 predictor interval 중앙값9.6011→6.6162ms·P9510.2453→7.6593ms였다. 측정 구간은 양쪽33표본/6600호출이며 호출당 가중 평균47.9477→33.4681us였다. 전원 무기/의상·로컬 품질·이동/반전17회/착지3회와 정상 종료를 유지했고 phase5 PNG를 직접 확인했다. 하나의 순서쌍의 cache/스케줄링 한계와 전체 frame/GT는 별도로 보존하며, 해당 scope의 감소를 전체 FPS 보장으로 세지 않는다. 프로젝트 SystemSettings에서 활성화하고 다음 전투 묶음의 배포 기준점으로 검증한다. 최초 테스트의 protected API와 진단 CVarRef 내부 읽기값 갱신 실패는 원본 근거를 보존해 수정했으며, 조건을 완화하지 않았다.

## 28차 서버 NPC 다중 근접 접촉과 권위 수명 — 로컬 통합 검증

기존 NPCAction/GAS/Notify/HitValidation 경로에 공격당1–32 unique attempt 상한을 연결했다. 기본1의 locked 대상 선택은 유지하고, 추가 대상은 실제 sweep 접촉에서 등록·적대·생존·범위/LOS를 확인한다. 효과 callback 전에 중복/상한을 예약하고, NPC의 모든 접촉은 entry Transform과 실제 현재 ASC provider·owner/avatar/epoch를 유지해야 한다. callback 중 순간이동·PlayerState 교체가 있으면 낡은 접촉을 폐기한다. opt-in만 canonical Health를 요구하며 기존 비ASC/단일 대상 계약과 플레이어 SSR 한도는 유지한다. 새 worker/manager/RPC/저장 계층은 없다.11파일을 통합한 Editor78 직접 UBT78.29초·exit0, 집중 native와 기존 실제socket 소비자 연결은 진행 중이다. 기존 NPC/Montage/Socket/Tag/Skeleton CDO를 읽었으며, 실제3접촉→2대상 피해·중복·취소·공개Health/개인소유 복제를 다음 실행에서 확인한다.

28차의 기존/신규 native계약200개는66.2초·warning0/exit0이었다. 실제socket 첫2회는3process정상exit0이나새공격단계실패로보존한다. 실패경계기록은실제3접촉·2유일효과/Health감소를확인했고, 의도해제→공격cancel이남았다. 신규대상선정순위와유효한공격lockedintent유지규칙을분리하는후속안을검토하며geometry/가중치/결과gate를바꾸지않는다.

## 29차 모델 수신과 보간 소비의 관측 분리 — 로컬 관측 검증

기존project NetworkPrediction fork4C++접점과기존CSV/validator/runner에기본OFF nonshipping관측을연결했다. live proxy의48byte값만복사하고codec은항상원래대로1회실행한다. GT동기pin·ID/role/epoch/서비스재검증, 종료component제외와nonGT unsupported원자표시를둔다. 고정프레임·독립AP의입력ack·독립SP의server-variable시각을구분하며SP에전송되지않는serverframe은만들지않는다. 적용횟수는패킷수가아니고마지막decode값은임의서비스활성의보증이아니다. 기존캡처의수량/메모리/partial·손실엄격판정은유지한다. Python32개는통과했으며Editor82와기존MoverCaptureBounds·실제Iris관측은진행중이다.

27–29차 공통 배포 기준점은 Game19 직접 UBT139.29초·Cook13의5개 맵 error0/warning0·Stage14 정상 종료다. 같은 Game19 패키지의 실제 전투3cycle과200명 이동/점프/전원 공격은 정상 종료했고, 로컬 품질658표본·실제 Flying 준비 counter phase5=2600/phase6=11600·보호 에셋/전체 Source·실행파일 SHA 불변을 확인했다. phase5 PNG도 직접 확인했다. 패키지 공격 scope 중앙값9.6515ms/34표본/6800호출은 별도 빌드의 실행 근거이며 Editor와 causal 전후 비교하지 않는다.27차는 이 로컬 범위를 마감한다.

첫 패키지 네트워크 실행은 설치 Game target의 IsGameOnly가 dedicated 모드를 허용하지 않아 -server에도 NetDriver를 생성하지 못했다. 실제 fixture가 Iris missing을 명시하고 정상 종료했으며 remote clients는 시작하지 않았다. 실패 원본을 보존하고, 패키지는 listen 서버+별도2remote로 정확히 구분해 검증할 최소 연결을 진행한다. 기존 Editor 실제 dedicated server1/client2 성공을 packaged dedicated로 바꾸지 않는다.28–29차의 전체 MMORPG/운영·손실 인과 후속도 계속 진행한다.

## 30차 서버 모델 생성과 수신 경계 연결 — 로컬 관측 검증

29차 실제 정상/75ms·10% 손실 캡처는 모든 endpoint·역할·시계와 원래 strict 실패를 보존했다. 손실의 심한 SP 지연114표본 모두 raw wire와 보간 endpoint가 같아 최신 받은 상태를 소비하고 있었다. 클라이언트 관측만으로 서버의 poll/quantize와 이후 공급 지연을 구분하지 않는다.

같은48byte의 role-exclusive 값을 기존 NetworkPrediction codec 접점에 재사용하여 SP authority encode와 client receive를 명시적으로 구분하는6파일 후보를 통합했다. 원래 NetSend/NetRecv는 각각1회이고 payload/Hz/buffer/보간 규칙은 바꾸지 않는다. 기본OFF nonshipping·GT 동기 pin/epoch와 기존 캡처 상한을 유지한다. 서버 world-variable 시각과 source Pending/Cached frame, 동일 Windows host의 QPC를 연결하되 shared Iris quantization을 연결별 실제 send/패킷으로 부르지 않는다. coalesced/repeated 시각과 다른 endpoint epoch도 구분한다. Python37개와 기존 실제4client 캡처의 raw 값/판정 동일 근거를 확인했고 native/실제 통신은 다음 검증이다.

30차 Editor84 직접 UBT243actions/226.21초와 기존 MoverCaptureBounds 경고0/0.02482초·exit0을 통과했다. 실제 같은 Source/runtime의 dedicated 정상 통신은 원래 strict 검사까지 통과했고,75ms·10% 손실은 게임플레이·속성90행/각client·actual exit0×3/timeout0이나 strict 보간 실패를 유지했다. 양쪽 authority/raw 캡처·동일 Windows QPC/실제 NP ID 연결과1187파일/39바이너리의 실행 전후·쌍 동일성을 확인했다. 활성 겹침 구간 NPC Pending−Cached는0–2frames, encode는 약10Hz였고 종료 tail의30/28frames를 병목으로 오인하지 않았다. 심한 SP 지연100표본 모두 최신 decode endpoint를 이미 소비했고 더 새로운 서버 model encode도 존재했다. 이후 Iris 검증/게시·연결별 전송·지정 지연/손실은 이 관측으로 세분하지 않는다. 새 하위 계측·Hz/버퍼/보간/실패 gate 변경 없이 이 로컬 관측 범위를 마감한다.

Game20 직접 UBT131.63초와 반영/에셋이 같은 Cook13을 재사용한 Stage15/44.48초가 정상 종료했다. 같은 실제 패키지의 ListenServer+별도2remote에서 로컬 host1 제외·실제 NPC3접촉→2효과/공개Health9행/각client·자연 종료/발동 전 취소·기존 privacy 검사를 통과했다. 별도 packaged 모델 생성/수신도 authority1533행/1499encoded·각client strict buffer/속성90행을 통과했고 actual exit0×3/timeout0·Source/runtime 전후/쌍 동일성을 확인했다.28차의 로컬 실제 소비와29–30차 관측 구현 범위를 마감한다. 전용 Server target/Shipping·실제 운영 서비스·손실 resilience·이동 품질/대규모 수용량은 전체 목표의 열린 경계다.

다음 묶음은 기존 준비/생성/표현 예산과 sampler를 재사용한 실제 원격 렌더링200 Avatar다. 기존 standalone을 역할과 실행량이 다른 실제 client로 확장해 확인하며, 몽타주의 notify/소켓/IK/표현 보호를 근거 없이 해제하지 않는다. 정확한 계산 재사용을 먼저 검토하고 새 프레임워크나 임의 품질 저하를 늘리지 않는다.

## 31차 실제 원격 군중 실행과 정확한 방향 재사용 — 진행 중

기존 NetworkLoad의5단계를 유지하고 실제2owner+198서버AI 소유 Avatar를 기존 등록된 controller-root 준비 lease로 생성한다. 공유 준비8·적용2/tick·1ms soft 예산은 유지하며, 한 번의 완료가 controller/PS/Avatar3개를 담는다는 점과 soft budget 초과 가능성을 명시한다. 기존 RenderedCrowd의 생성/입력과 관측을 분리해 실제 RHI client의 AP1/SP199·로컬 품질·공개 장비/개인 정보·준비→이동/점프/전원 공격/복귀를 검증한다. 준비 전 성능 표본은 없고, 클라이언트 접속 순서·GC/취소/교체와 정상 종료 경계를 보강했다. 새 맵/BP/manager/RPC는 없다.200개 표현 Actor와200개 연결의 수용량은 구분한다.

정확한 Flying 재사용은 zero/nearly-zero 방향의 각 step 현재 orientation에 원래 중력 보정을 다시 적용하는2파일 후보를 함께 통합했다.92개 샘플·원래 solver·갱신 빈도/포즈/notify/IK는 유지한다. 기존 CVar0=원본/1=기존 explicit-facing/2=zero 확장이며 기본0·프로젝트1을 보존한다. 같은 binary의 실제 remote client만1vs2로 비교한 뒤 확장 채택을 판단한다. 기존 level1 zero fallback과 깊은 입력/상태 복사·원본 보존을 유지한 기존 native leaf에서 수치를 확인한다.

217파일을 동결한 첫 Editor85는20.21초 뒤 새 fixture의 inherited Player/Owner 지역명 shadow와 TObjectPtr range-loop auto* 추론 오류로 실패했다. 경고 기준을 낮추지 않고 지역명/명시 타입만 고쳤으며 원본 지문·실패 로그를 보존했다. 새217파일 Repair 지문의 Editor86과 실제 native/원격 실행은 진행 중이다. 몽타주 보호 해제나 PlayerState limiter 임의 증가는 하지 않으며, 실제 원격 비용·소비를 확인하기 전 이를 최적화 완료로 세지 않는다.

31차의 정확한 Flying native는 Editor86에서 통과했다. 원격200명의 첫 실행들은 Pawn이 모두 보여도 PlayerState 생성이 일부 연결에 공급되지 않아 준비에 실패했다. 실제 root/child 상태와 stock Iris CSV를 확인했고, 임의 limiter·deadline 증가는 하지 않았다. class별 CSV의 준비 구간에서 PlayerState write45회/creation0과 Pawn write14,695회를 구분했다. 이를 반복 PS 직렬화 비용이나 전체 수용량 보장으로 해석하지 않는다.

Editor94의 기본 OFF 비교 분기는 기존 public Iris creation-only dependency로 PS가 Pawn보다 먼저 생성되게 했다. 실제 dedicated server와 별도 두 client의 StateFirst1에서 모두200개의 PS/Pawn/ASC/공개 장비 준비를 통과했고, RHI client는 실제 AP1/SP199·준비 후 표본만 수집했다.432개 측정 frame의 역할·로컬 품질·외형과200명 공격 pose를 확인했지만, 원격14명의 이동 반전이0이어서 기존 strict 검사는 실패했다. actual exit0×3/timeout0과 기능 실패를 구분하고 원본·Source/runtime 전후 일치를 보존했다. 시각 전용 바닥의 충돌 profile과 실제 서버/수신 이동을 분리해 확인하며, production dependency 수명·정확한 level1/2 비교·원격 이동 품질은 진행 중이다. 아직 config는level1이며 전체 AAA 완성으로 세지 않는다.

정식 통합은218입력·9파일의 Editor95 직접 UBT226.05초/exit0과 기존 소유권4개 회귀0.05631초·경고0/exit0으로 확인했다. PlayerCharacter가 현재 PS/primary Iris bridge/complete handle pair를 소유하며 양쪽 root의 늦은 등록·같은 pair 재등록·PS 변경·EndPlay에서 검증한다. engine root-stop 정리를 재사용하고 trial writer/스위치는 제거했다. PS의 네 native 개인 컴포넌트만 OwnerOnly로 좁혔고, 공개 Progression/ASC/Health/Pawn 장비와 기본 UObject는 유지했다. 이는 root poll 비용이나 컴포넌트 객체 수를 제거한 변경이 아니다.

같은 Editor95의 Production1은 실제200준비/AP1SP199·431측정frame·로컬품질677과 바닥 NoCollision/default0/registered1을 확인했지만 반전9개 실패를 유지했다. 서버/관측자 phase3/4 네 CSV는 각각200개 current Iris ID/role/finite/identity 검사를 통과했다. 실패9개 모두 서버 속도와 실제 이동은 양·음 방향으로 왕복했고, 관측자는 음수 위치 변화도 있으나 속도 음수 표본은 없었다. 원시 수신/소비·표현의 구분 없이 이를 getter나 buffer 결함으로 확정하지 않는다. 기존 StateFirst1의 stock CSV는 해당 구간 Pawn non-waste writer 약242–248회/s(두 연결 합)를 보였으나 이는 packet/ACK/개별 NP model frame 수가 아니다.

동일 소스/바이너리의 PrivateEquipment1은 각 client6단계의 관심 이탈·현재 공개 장비 재진입·실제 Pawn 교체와 기본 퀘스트 audience를, PrivateCompanion1은 각14행의 실제 InventoryVM/owner 동료 RPC·소환/해제/재소환·공개 NPC Health를 통과했다. 두 실행 모두 actual exit0×3/timeout0이며 세 실행의 전체 Source/runtime 전후 일치를 닫았다. 장비/가방/퀘스트의 정상 owner 명령·correlated 응답 보강, 원격 상태 공급/표현 품질, DS 의상 simulation 생성 수명과 level1/2 비교·다음 배포는 계속 진행한다. 이 결과를 운영 DB·200연결/CCU·전체 목표 완료로 세지 않는다.

같은 Editor95의 standalone200 level1/2 두 실행은 실제 이동·반전·착지·전원 공격·로컬품질682/690과 actual exit0,48개 보호 입력·전체 Source/runtime 전후/쌍 일치를 통과했다. phase5의 동일200호출 interval 중앙값은6.6608→6.5358ms, 호출당 가중 평균33.4918→33.1462us였고 reuse counter는 양쪽phase5에서2600이었다. 추가 사용량/성능 이득을 확정할 근거가 부족하므로 config1을 유지하고 level2는 opt-in 후보로 남긴다. 혼합5/200호출 frame의 중앙값을200명 predictor 성능으로 보고하지 않았으며, 이 비교를 원격 이동 품질 통과로 확대하지 않는다. 추가 반복 측정은 하지 않는다.

220입력의 다음 통합은 기존 ModularMesh.OnRegister에서 DS에만 Super 전에 SetAllowClothActors(false)를 적용하고, 준비 기록의 실제 simulation instance 수를 asset/tick 수와 분리했다. 클라이언트/standalone의 기존 allow 값·mesh/physics asset/충돌·body pose/notify는 바꾸지 않는다. SDK에서는 DS cloth tick을 제외해도 등록의 simulation 생성 경로가 별도였으며, 이전 실제 allocation 개수/bytes는 기록하지 않아 감소량을 주장하지 않는다. 의상 hard-reference 제거와도 구분한다.

Editor96 직접 UBT22.70초/exit0 뒤 PrivateOwners1은 실제 소유자의 InventoryVM bag Swap·Unequip/Equip 요청과 request-correlated 정상 응답, Quest.RequestTracking false/true와 서버6행·각owner3행 및 기존 장비6단계를 통과했다. canonical 항목이 모두 도착할 때까지 기다리고 grant callback의 GC/Pawn 교체/teardown을 재검증한다. provider를 강제로 우회하지 않은 기존 prototype memory 권위 경로의 검증이며, durable receipt/DB·Use/Claim·PS 객체 교체/재접속까지 증명하지 않는다. actual exit0×3/timeout0·220입력과 전체 Source/runtime 전후 일치를 닫았다. DS200 실제 allocation/클라이언트 cloth 품질, 정확한 원시 decode/소비·표현의 제한된 비교와 다음 Game/배포는 계속 진행한다.

Editor96의 Cloth1 실제 dedicated200은 의상 asset200·mesh2200·cloth tick0을 보존하면서 실제 simulation instances0을 확인했다. 같은 RHI client의425측정frame은 cloth parts/tick/simulations 모두200·로컬 cloth 축소0·로컬 최대LOD0, 품질673표본과 전원 공격을 유지했다. Source/runtime·선택 보호 에셋 전후 일치와 actual exit0×3/runner timeout0을 확인했다. 이동 반전16개 strict 실패는 그대로이며 네 witness CSV 각200행은 identity/role/finite를 통과하고 모두 서버 왕복/클라이언트 속도·위치 불일치로 분류됐다. 이 자원 생성 방지 검증을 원격 이동 품질 통과나 이전 allocation bytes/FPS 개선 수치로 확대하지 않는다. 다음은 기본OFF·3개 SP만 선택하는 실제 decode scalar와 소비/표현/Actor의 비교이며 기존 source 수·18MiB/row/수명·부분 실패와 게임플레이 품질 기준을 유지한다.

## 사용자 요청에 따른 일시정지 — 2026-10-27 19:47 KST

새 작업·빌드·분석을 중단하고 모든 서브에이전트를 멈췄다. 코드/에셋/기존 실패 근거는 삭제·되돌리기·커밋 없이 보존한다. 현재 소스는 `ArchitectureBatch31DecodedEndpointSourceHashes.json`의221입력과 일치하며, 기본OFF typed decode float4/3SP 관측과 연결 정책 두 값 및 기존 unittest4개를 통합한 상태다. Editor97 direct UBT244actions/211.16초·exit0, 기존 MoverCaptureBounds1개0.02332초·경고0/exit0과 Python41개를 통과했다.

마지막 `ArchitectureBatch31RenderedClient200_Decoded1`은 모든 프로세스가 종료됐지만 exit codes `[0,3,0]`으로 실패했다. focused capture0/observationValid=false를 그대로 보존했고 전체 Source/runtime 전후 지문을 닫았다. 재개 시 **이 실행의 Client1 최초 실패/크래시 로그와 캡처 시작·selection/receipt 생성 여부**부터 확인한다. 아직 실제 decoded scalar 결과의 판정이나 원격 이동 원인 수정은 완료하지 않았다. 실패를 숨기는 기준/Hz/버퍼 변경은 하지 않는다.

이후 원격 이동 품질 수정·같은 입력의 확인·다음 Game/공통 배포와 기존 전체 미완료 표를 이어간다. 별도 trajectory worker 후보는 아직 패치가 없고, GT 발행/history와 worker 소비 순서를 먼저 확인해야 한다. FullBody의 source update/취소·복귀 이력을 생략하거나 전체 Mover 예측을 UObject/공유 blackboard 접근 그대로 worker로 옮기지 않는다. 전체 MMORPG 목표는 완료가 아니라 **사용자 요청으로 일시정지**하며, 사용자 재개 요청 전 자동 작업을 하지 않는다.

## 사용자 재개와 실행 구조 보강 — 2026-10-28

사용자가 전체 MMORPG 아키텍처 작업을 재개했다. 시작 한도 조회는34% 사용/66% 남음이었다. 처음 요청한30% 사용량 중단 조건은 같은 날 후속 메시지로 명시적으로 해제했으며, 전체 목표를 계속 진행한다. 용량 문제는 사용자가 처리했으므로 저장소 정리 작업은 종료한다. 이전20시간 목표와 일시정지 당시의 프로세스 관측은 현재 실행 상태를 뜻하지 않는다. 사용량 재설정이 이미 수행됐다는 의미로 해석하거나 임의로 완료 처리하지 않는다.

32차는 세 실행 경계를 묶었다. Fixed/Independent 보간의 기존 인스턴스를 주소가 안정적인 소유 저장소에 두고, Independent 끝점 유지도 공개 Sync/Aux와 실제 출력을 일치시키되 수신 history와 분리했다. NPC의 현재 능력 소유 정보가 바뀌기 전에 해당 lease의 행동·경로·grant를 회수하고, 같은 수신자를 유지하는 정상 동료/탑승 갱신은 보존한다. 공용 외형 사용권은 token index와 swap 해제·소유권 없는 순회 링크를 사용하여 다른 캐릭터 수에 따른 검색/배열 이동을 제거한다. 새 요청은 현재 순회 뒤에 들어가며 기존 요청이 지속적인 교체 때문에 밀리지 않는다. 기존 GAS·Mover·공용 로더와 자원 상한을 재사용했다. 해시 조회의 평균상수시간 개선이며 FPS·메모리 절감 수치는 아직 주장하지 않는다.

Editor98 직접 UBT는132작업/139.65초·exit0이었다. Native1은37개 중36개 성공/1개 실패·exit255를 보존했다. 실패한 새 외형 시험은 비동기 미준비 중에도 진행되는 정상 순회를 무시하고 admission 시점을 ready frontier로 가정했다. 생산 코드를 바꾸지 않고 첫 실제 완료 때 생존 순서와 고정 방문 상한을 잡아, Editor99 증분3.80초·exit0 뒤 해당 churn·공유1024·GC3개가2.099초·경고0/exit0으로 통과했다. 연속 churn44회에서도 ready91번째 소비자는46번째 완료로, 기존400 소비자는890상한 안의799번째로 관측됐다. 초기36개의 소유권·NPC·동료·보간/clock·자원 회귀는 생산 소스 불변 근거를 재사용한다. 두 native 실행의 전체 Source/runtime 전후 일치를 닫았으며 캡처 해석 설명만 갱신한 Python41개도 통과했다.

원래 실제 실행을 재사용한 `ArchitectureBatch32RenderedClient200_Decoded2`는 actual exit0×3/timeout0, 전체 Source/runtime·선택 에셋 일치를 확인했다. Client1은 AP1/SP199로758frame/측정396frame·로컬품질624·200명 공격/pose28frame을 기록했다. 원래 접근 오류는 재발하지 않았고, phase3 선택3SP의 phase4 캡처408행이 유효하게 닫혔다. strict 결과는 이동 반전13개 실패를 유지한다. 각 SP에서5개 decode를 관측했고 serial 누락0이었다. 원시 수신의 음속도가 소비·표현·Actor에 이어지는 것을 확인했으나, 수신 좌표의 원래 base/world 여부는 미확정이다. 보간 값과 수신 끝점을 동일한 값으로 세지 않는다. 연결별 송신/ACK·예약/포화는 아직 관측 전이며 이 실행을 원격 품질 통과나 전체 AAA 완성으로 확대하지 않는다.

## 33차 등록부 공정성과 월드 준비 조회 — 로컬 통합 검증

NPC의 agent 등록·해제는 전체 배열 검색/이동을 반복했고, action의 dense swap도 지속적인 취소·재등록에서 다음 생존 consumer를 무기한 미룰 수 있었다. 두 등록부의 dense 저장과 실행 순서를 분리하고 weak identity index·소유권 없는 순회 링크로 기존 요청의 한 순회 방문 상한을 보존한다. target 순서·tie·worker 값 snapshot·등록 revision/공유 record 수명·기존 cadence/상한은 유지한다. 별도 manager·thread pool을 추가하지 않는다.

원격 준비 이동에서는 client ACK 전에도 호출하던 서버 완료 조회를 생략한다. WP source 광고는 독립 provider가 계속 소유하며, 기존 취소/현재 세션/사망/deadline은 먼저 확인한다. ACK 뒤의 서버 독립 cell·충돌·native Mover 커밋 검사는 유지한다. 엔진 완료 조회의 streaming epoch 갱신도 불필요하게 반복하지 않는 변경이며, 대규모 동시 이동의 절감량이나 Nav/GPU 준비 완료를 주장하지 않는다. 현재 두 변경을 실제 소스에 통합했고 단일 빌드·집중 회귀·기존 실제 MainWorld owner probe를 준비 중이다. 원격 상태 공급은 선택3개에 한정한 기존 Iris writer/연결 관측을 연결해 원인을 더 좁힌다.

33차는 Editor100 최초 C3535 오류를 명시 `APawn*` 타입으로 수정한 뒤 Editor101 직접 UBT8.14초·exit0과 native30개/경고0/exit0으로 확인했다. 실제 두 등록부128개·126회 churn에서 보호 consumer는128상한 이내127번째로 방문됐다. 정상 MainWorld BP GameMode의 실제 dedicated/owner2process는 기존 TryInteract RPC·client ACK·서버 cell/충돌/native Mover 검사·물리 도착과 기본 source 복원을 통과했다. 목적지 source가 실제 도착 전에 광고된 것을 확인했으며 Nav/GPU/HLOD/동시 이동 수용량은 미검증이다.

같은 Editor101의 `ArchitectureBatch33RenderedClient200_Iris1`은 actual exit0×3/timeout0, 전체 Source/runtime·선택 에셋 일치와 선택3SP의 decode/Iris 관측 유효성을 확인했다. 원격 반전25개 strict 실패는 유지한다. phase4의8.286초 동안 연결 tracked frame102·saturated frame69, 설정100000B/s와 관측 약40407–40638B/s였다. 포화 판정과 송신 credit 갱신의 실제 순서, `min(DeltaTime,1/DesiredTickRate)` 제한을 소스에서 확인했으며 실제 server 저속 실행과의 결합을 의심한다. 개별 model/ACK 전달량이나 최종 원인은 이 집계만으로 확정하지 않는다. NP의 기존 custom serializer에는 내부 delta가 없어 frame 변경 시 전체 blob을 쓰는 경로도 확인했다. cadence·precision·net speed·packet cap·buffer·품질 기준을 조정해 실패를 숨기지 않는다.

공통 배포 기준점은 Game21 직접 UBT198.56초/exit0과 기존 Cook13을 재사용한 Stage16 정상 완료다. 직전/직후 Source/runtime와 새 Game 실행 파일의 패키지 SHA 일치를 확인했다. `ArchitectureBatch33CleaveCooked1`은 실제 listen server+remote2의 기존 AOI/FastArray·공개 Health/개인 정보 경계와 실제 authored NPC3회 notify·2대상 GE·정상 종료/타격 전 취소를 통과했다. `ArchitectureBatch33CombatCooked1`은 real RHI에서 실제 입력/GAS/몽타주/notify/장비 해제3cycle·exit0과 전체 Source/runtime 일치를 확인했다. 이는 Development Game 기준이며 Shipping·dedicated target·운영 CCU 인증이 아니다.

34A는 Iris가 소유하는 ACK baseline을 그대로 사용하는 bounded lossless word delta를 통합했다. model precision·cadence·순서·buffers·net speed는 유지하며, 델타가 작을 때만 사용하고 기본 encoder는OFF다. 기존 full config 상한과 current object/name/token exports를 유지하고 empty/free의 stale ByteCapacity도 수정했다. wire delta 형식 변경으로 protocol/replay8과 이전7의 checksum 거절 검증을 연결했다. Editor102 직접 UBT249actions/235.33초·exit0과 실제 등록 serializer·bounded codec·기존 보간/캡처/clock/admission6개0.07179초·경고0/exit0, 전체 Source/runtime 전후 일치를 확인했다. heap/ref/token/loss·실제 이전 binary handshake와200명 동일 binary A/B는 아직 검증 전이며 기본 활성화·성능 이득·원격 품질 통과로 세지 않는다.

34B는 정상 탑승의 기존 mount 관계로 persistent owner를 해석하고 현재 동일 ASC tuple의 연결 ensure에 기존 metadata refresh를 재사용한다. 명시 Init/Clear의 epoch fence는 유지한다. 실제 GE Health 감소의 bounded 관측과 기본OFF NPC threat 소비, 월드 이벤트의 연속 등록/퇴역 순회 공정성, 퀘스트의 공식 once-per-frame timer도 함께 소스에 통합했다. 실제 Iris의 heap/empty/small·6refs/names/tokens·관심 재진입/재생성은 별도 저밀도 C++ fixture로 묶었다. 현재 통합 빌드 단계다. 첫 UHT parameter shadow와 다음 compiler shadow/replication macro 인자 오류를 기록하고 fixture만 수정 중이며, 선언·초안·컴파일 중인 코드를 통과 결과로 계산하지 않는다.

34B의 Editor103은 UHT parameter shadow로2.39초·exit6, Editor104는 fixture의 member shadow/replication macro 인자 오류로84.57초·exit6이었다. fixture 명칭/인자만 수정하고 실제 기존 CSV의 동적 final header·중복 stat 열과 접속순서별 role 검증을 보강했다. 종료 예약은 world travel에 취소되지 않는 CoreTicker와 최초 terminal latch를 재사용한다. Editor105 직접 UBT5actions/11.48초·exit0 뒤 기존 실행 계약과 새 Health GE·threat·탑승·이벤트·퀘스트49개가6.549초·경고0/exit0으로 통과했고 전체 Source/runtime 전후 일치를 닫았다. 실제 이벤트256개 중 due survivor는256방문·churn1회 안에 갱신됐고 기존 방문/업데이트 상한·snapshot0을 유지했다. 퀘스트는 실제 owned timer의1.5초 hitch·정상 재개·pending commit·긴 gap·Pawn 교체·퇴역을 검증했다. 이는 혼합 부하 FPS/CCU 검증이 아니다.

첫 실제 `ArchitectureBatch34BLifecycleFull1`은 encoder0/loss0/lag0의 dedicated+2client이며 Source/runtime 일치·세 프로세스 정상 exit0을 확인했지만 case0의 기능 실패를 보존했다. 두 client가 generation1/case0/31bit를 받았어도 exact ACK는0/0이었고, 일반 refs/Names 정상·별도 Tokens6개 None인 조합과 실제 digest가 일치했다. 해시만으로 원시값을 확정하지 않는다. 소스상 Iris property Apply가 빈 context를 새로 만들어 호출하므로 기존 NP Apply의 token store/remote state가 null이 되는 연결 결함을 확인했다. 실제 수신 context가 존재하는 Dequantize에서 같은 dispatch source를 통해 전달하는 최소 수정을 준비한다. startup CSV category 파싱도 모듈 등록보다 빨라 실패했으므로 지원 runtime console enable로 옮긴다. 기존 실패·counter0을 성공으로 바꾸지 않는다. heap/refs/token/empty/loss와 실제200명 A/B는 여전히 미완료다.

현재 후속 통합은 기본OFF threat의 실제 authored NPC 공격 소비와 원격 SP 탑승자의 공개 PS/ASC 조회를 소스에 추가한 상태이며 아직 재빌드/실행 전이다. 원격 PC가 미복제인 경우에만 기존 공개 reciprocal mount/PS.Pawn/ASC owner/avatar 증명을 사용하고 서버/owner 검증은 유지한다. 실제 화면·caption·cached VM 재바인딩까지 해결됐다고 세지 않는다. 이벤트/퀘스트·탑승·전투·복제의 각 완료 조건과 전체 미정 서비스/배포/수용량은 계속 진행한다.

### 34C–E 실제 소비와 델타 정책 검증

Editor106 직접 UBT252actions/212.99초·exit0 뒤 context reset/실제 writer 예산·기존 보간/캡처/clock/admission·탑승/Mover23개가0.316초·경고0/exit0으로 통과했다. 같은 바이너리의 `ArchitectureBatch34CLifecycleFull2`는 dedicated+2client의13case·authority26/client각13행·실제12352bit heap/ref/name/token·empty→small·재진입·재생성을 통과했다. 이전 token context 원인 수정은 원래 무손실/full 조건으로 재현 확인했으며, callback 중 driver 파괴·중첩 map read·replay/multi-driver 실행까지 검증한 것은 아니다.

`ArchitectureBatch34CThreat1`은 실제 authored NPC의 기존9행과 추가6행을 두 client 모두 통과했다. 실제 두 source의5회 GE 확정 감소가 거리와 반대인 primary를 선택하고, 기존 worker/Action/NPCAttack·실제 피격·원격 montage/Health와 새 lease의 타격 전 취소까지 이어졌다. Source/runtime·선택 에셋 전후 일치와 actual exit0×3/timeout0을 확인했다. 기본 DA는OFF를 유지하고 transient 복제 정의에서만 소비했다. 생산 어그로 규칙·규모/성능 인증으로 확대하지 않는다.

WordLoss1/2는13case 복원과 실제 손실을 관측해도 word 송신/수신0이라 coverage gate 실패를 보존했다. huge 경로가 무조건 delta를 우회한다는 근거는 없었다. native Pawn/PS의 허용 정책이 별도 NP ActorComponent에 상속되지 않고, initial state가 default baseline을 사용한 호출까지 `SerializeDelta`로 집계됐음을 구분했다. 클래스별 정책은 각 replicated object의 실제 클래스 계보에 적용된다.5001bit same-size heap·기존 baseline 정책60frame을 읽는122 engineframe 유지와 성공 word decode 계측을 추가했고,12001/8193bit huge·empty·재진입/재생성은 유지했다.

Editor107은 Audit fixture의 설치 엔진에 없는 public header명에서17.04초·exit6이었다. 실제 공개 EngineReplicationBridge header로 수정한 Editor108은8.63초·exit0이다. `ArchitectureBatch34DMount1`은 actual dedicated+2client에서 양쪽 소유자의 정상 탑승·foreign SP 공개 ASC/Health·실제 잘못된 Avatar 복제의 거절/복구·소유자 하차 RPC·foot 복원·개인 데이터 경계를 authority18/client각18행·exit0×3/timeout0으로 통과했다. 원래 run manifest와 후속 Source/runtime 지문도 편집 전에 닫았다. TargetHUD 화면·이름·cached VM late-order·이동/지형 품질은 별도 경계다.

34E는 명시적 실험 runner에서만 서버의 `/Script/NetworkPrediction.NetworkPredictionComponent` delta class 정책을 추가한다. encoder0/1은 같은 정책을 사용하며 프로젝트 기본 Config·다른 ActorComponent·clients·throttle/속도/버퍼는 바꾸지 않는다. 실제 `ArchitectureBatch34ELifecycleWordPolicy1`은 sender word316회·양 client decode135/149회,13case·26/13/13행·실제 손실2·context error0·exit0×3/Source/runtime 일치를 통과했다. 동일 정책의 encoder0 `FullPolicy1`도13case·송신/수신 word0·context error0·exit0×3/Source 일치를 통과했다. 카운터는 serializer/quantized decode 실행 근거이며 packet별 ACK·최종 송신 bytes·FPS가 아니다.

같은 최종1197개 소스/설정/스크립트·39개 binary·세 역할의 정규화 인수로 실행한 `ArchitectureBatch34ERenderedFull1/Word1`은 성공 changed-delta body의 raw-equivalent 대비 encoded 합계가 Word에서103,704,423→34,446,212bit(66.784% 절감)였다. Full/Word의 성공 모집단 수가 달라 두 실행의 전체 bit 차이를 송신 절감률로 계산하지 않는다. 전체 delta attempt당 encode CPU는0.912→1.088μs, writer overflow attempt 비율은62.43→40.18%이고 명시 context error는0/0이다. 선택 phase4 연결의 포화 tick은71/101→59/101이며 실제 tick은12.07/12.25Hz·cached max30·net max120·속도100000B/s였다. 이후 ready/negative queue 스냅샷은 credit 이전 포화 표본과 모순되지 않는다.

두 실행은 actual exit0×3/runner timeout0·Source/runtime/선택 에셋 불변이어도 strict 품질 실패다. 반전14→24개이고 Full에는 air/final 관측 실패도 남는다. RHI phase4 GT 중앙값43.22/43.08ms·P95 61.85/59.08ms로 FPS 개선 근거도 아니다. 기본 encoder와 지속 class 정책은 활성화하지 않는다. 실제 공급 cadence·과도한 per-sim 보간 Display 비용과 품질 원인을 계속 구분한다. Game22 직접 UBT200actions/156.00초·exit0을 확인했고, 새 반영 NPC definition 필드 때문에 기존 Cook13을 재사용하지 않고 fresh Cook14/Stage17 배포를 진행한다.

34F의 Game22·fresh Cook14·Stage17은 정상 종료했고 패키지 exe SHA는 direct UBT 결과와 일치한다. 실제 packaged listen+remote2의 기존 NPC 공격9행·추가 threat6행과 별도 RHI 전투3cycle이 통과했다. 이는 설치 Game target의 검증이며 dedicated Server/Shipping 인증은 아니다. 보존한 protocol7 Stage16의 실제 접속은 가벼운 protocol8 packaged 서버에서 Hello checksum 불일치→Upgrade→OutdatedClient, Login/Join 이전 거절과 양 프로세스 exit0·Source/runtime 불변을 확인했다. 첫 Editor 서버 시도는 첫 tick이 구 client의20초 timeout 이후 실행되어 Hello/Upgrade를 입증하지 못한 실패로 유지한다.

패키지 NP word 수명 첫 시도는13case·26/13/13행·word 송신351/수신129·178·context error0이어도 최종 loss0이라 실패였다. `InPacketsLost/OutPacketsLost`는 연결 통계 주기마다 초기화되는데 fixture가 누적 차이로 사용한 관측 오류를 확인했다. 이전 양수는 그 시점에 손실을 관측한 근거일 뿐 전체 구간 누적 수로 해석하지 않는다. 실제 누적 `InTotalPacketsLost/OutTotalPacketsLost` 차이와 역행 거절로 수정하고 원래 손실 조건을 다음 빌드에서 다시 검증한다. 선택 Iris receipt도 period 표본과 lifetime 값을 명시적으로 구분한다. 실패 파일이나 원래 strict 기준을 변경하지 않는다.

### 35차 진행: 경로 소유 데이터와 서버 병렬 평가

34E의 기존 서버 CSV를 재사용해 phase4 양 끝 frame을 제외한99frame을 비교했다. Full/Word FrameTime 중앙값82.24/81.84ms에서 AnimationBudget GameThread scope가55.16/54.45ms, WorldPreActorTick11.81/11.67ms, ServerReplicateActors8.74/8.76ms였다. 포함 관계가 있는 scope를 더하지 않으며 wall time을 순수 CPU나 worker 대기 없는 시간으로 부르지 않는다. dedicated cloth simulation0·source mesh200·Mover200·PlayerCharacter200은 같았다. 서버의 실제 낮은 갱신은 전송 설정만의 문제로 단정하지 않는다.

설치 엔진의 `FApp::ShouldUseThreadingForPerformance`는 dedicated 기본값이false이고 지원 `-useperfthreads`가 이를 허용한다. SkeletalMesh parallel evaluation dispatch도 이 gate를 확인하므로 native AnimInstance의 multithread 설정만으로 서버 실행을 증명할 수 없다. 기존 runner의 `-ServerPerformanceThreads`는 dedicated의 서버 인수에만 이 옵션을 추가하고 실제 FApp 값을 기존 준비 receipt로 관측한다. 포즈·충돌·SSR 정확도·Hz나 클라이언트 옵션을 낮추지 않는다.

`ArchitectureBatch35ServerThreadsOff1/On1/On2`는1197개 source/config/script와39개 binary·각 run 전후 지문이 같고, 두 client 실제 인수도 같으며 서버의 `-useperfthreads` 하나만 다르다. 실제 dedicated/FApp false→true와 parallel evaluation task 경로를 확인했다. 같은 phase4의 경계 각1frame을 제외한 서버 Frame P50/P95는 OFF82.09/95.70ms→ON38.69/45.79ms·반복39.24/46.36ms, Animation GT는55.11/68.19ms→13.72/15.33ms·반복13.85/15.49ms였다. ON의50tasks/frame은 실제 worker task 경로 근거이며200포즈 전부가 worker에서 실행됐다는 증거는 아니다. 다른 포함 scope나 병렬 task 누적 시간을 GT에 더하지 않는다. 실제 tick은12.10→25.46/25.17Hz, 기존 strict 반전26실패→ON 두 번 통과였고, 실제 NPC threat/타격·취소도 같은 서버 옵션에서 통과했다. 각 실행은 actual0×3/timeout0·source/runtime/선택 에셋 불변이다. client Frame 중앙값은60.50→62.49/62.94ms라 client FPS 개선으로 주장하지 않는다. 서버 실행은 지원 startup 정책을 명시적으로 사용하며 C++에서 캐시된 FApp 값을 강제로 바꾸거나 전용 thread pool을 추가하지 않는다.

실제 NPC Path의 completion·예산 대기 후 delivery·현재 follow·새 RequestMove 소비에 같은 GT `IsPathUsable` 검사를 연결했다. path flags만으로는 NavData actor의 unregister/cleanup을 감지하지 못하므로 live/registered/same-world 소유 데이터를 함께 확인한다. 기존 engine async navigation·슬롯 예약·취소·2초 deadline·backoff를 재사용하며 새 Portal 옵션이나 manager는 만들지 않는다. 실제 Recast/Mover follow 도중 unregister→정지→정상 deferred 재등록→새 async path 도착 사례를 기존 fixture에 추가했다. Editor109/110의 시험 코드가 보호된 등록 API를 호출한 오류를 보존했고, 설치 헤더의 접근 범위를 확인한 공개 `UNavigationSystemV1::RequestRegistrationDeferred`로 수정한 Editor111 직접 UBT가6.03초·exit0이었다. 실제 Nav6개와 경로 취소/압력·NP codec/clock/capture/admission을 묶은23개는7.416초·경고0/exit0·Source/runtime 불변으로 통과했다. MainWorld 전체 Nav/GPU/HLOD 준비나 completion 이후 실제 unregister의 모든 시점을 인증한 결과는 아니다.

독립 보간의 endpoint-hold Display만 기존 service/model 수명에서 집계한다. 같은100ms predicate의 전체 sim-tick 수·early/late·최대 gap·실제 QPC 구간을 보존하고 최초/초당 최대1회·종료의 잔여1회로 text 출력을 제한한다. per-instance 기록·timer·manager를 추가하지 않고 Fixed fault/trace·원시64B 관측·clock/포즈/표현/cue를 유지한다. 기존 실패 식별자와 strict gate도 유지해 지연 summary의 startup 분류는 보수적으로 실패할 수 있다. 이는 로그 IO/formatting 비용 억제이며 공급 부족의 원인 수정이나 품질 통과로 세지 않는다. 실제200명 실행에서 정상 최종 flush·전체 관측 수 보존·기존 gate를 확인했다.

같은 병렬 서버의 Full=`On2`/Word=`ArchitectureBatch35ThreadedWord1`도 원래 strict를 모두 통과했지만 Word 기본 도입은 보류한다. 성공 changed body는144,542,814→48,027,299bit(66.773% 절감), 전체 delta attempt당 encode는0.908→1.059μs였고 실제 wire 전송 절감률은 아니다. 선택 연결의 포화는45/209→104/213로 증가했고, 두 조건 모두 같은 baseline 정책이라 그 정책 자체의 추가 메모리 비용도 분리하지 못했다. 기본 codec/class 정책은 유지한다. 전체 MoverActor 로그62–64줄은 정상 종료의 마지막 flush를 포함하면서 여전히 수십만 건의100ms 초과 sim/frame 관측을 보존했다. 최대 gap1초 이상도 남으므로 strict 통과를 모든 캐릭터의 매끄러운 표현 완성으로 확대하지 않는다.

Game23 direct UBT38.57초·exit0과 Cook14 재사용 Stage18은 같은 exe SHA·Source/runtime 불변으로 닫았다. 반영 필드/에셋 계약은 Cook14와 같고 이번 변경은 native 경로·진단·실행 인수다. 실제 packaged NP13case는 누적 손실152·word 송신225/양 client decode56·143·각 exact13행/authority26행·context error0·actual0×3으로 통과했다. 앞선 period0 실패와 원래 손실 조건을 보존한 원인 수정 확인이다. 기본 codec/class 정책 그대로의 packaged RHI200 strict와 별도 실제8 socket의 관심 영역·owner-only FastArray·정상 이탈도 각각0×3/0×9·timeout0으로 통과했고 공통 Source/runtime 전후 일치도 닫았다. 이는200연결/CCU·인증·운영 저장·Server target/Shipping 검증이 아니다. 이 실행에서 관측한 NullRHI listen-server의cloth simulation200은 dedicated0과 다른 headless 표현 자원 경계로 후속 검토한다.

### 36차 headless 의상 자원

35차 packaged listen-server는 NullRHI인데 cloth simulation200개를 만들었다. 기존 modular visual part의 dedicated 생성 방지 정책을 Game/PIE의 non-commandlet·`!FApp::CanEverRender`에도 재사용했다. Super 등록 전에 같은 `SetAllowClothActors(false)`를 적용하며 renderer 전환의 가변 상태를 사용하지 않는다. 실제 RHI의 authoring 값·Cook/preview/CDO·게임플레이 pose·에셋과 metadata tick은 유지한다. 별도 manager·옵션·캐시·에셋은 추가하지 않았다.

한 cpp 변경의 Editor112/Game24 직접 UBT와 기존 의상/공유 포즈/시각 로드/animation proxy 수명5개는0.378초·경고0/exit0이었다. 같은 reflected/asset 계약의 Cook14를 재사용한 Stage19는 direct Game24 exe SHA와 같았다. 원래 packaged listen NullRHI+RHI200 조건의 `ArchitectureBatch36RenderedCooked1`은 actual0×3/timeout0·Source/runtime/선택 에셋 불변·strict 통과였다. 서버cloth parts200/metadata tick200을 유지하면서 실제 instances200→0을 확인했다. RHI client phase3/4의268frame은 모두cloth simulations200·local cloth reduced0·local 최대LOD0이고 local quality653표본을 유지했다. 이 결과는 불필요한 생성의 원인 수정이며 asset hard-reference 제거·잔여 tick 제거·정확한 해제 bytes·메모리 장기 안정이나 client FPS 개선을 주장하지 않는다.

### 37–38차 client critical path와 스케줄링

기존35–36차 실제 자료에서 trajectory는 프레임당0.60–0.85ms·약5–7회 호출이었고200회/프레임이 아니었다. RHI200 observer도 blocking playback getter를 호출하지 않는다. 이를 근거로 궤적 품질·주기나 존재하지 않는200회 playback barrier를 수정하지 않았다. 대신 기존 stock CSV에 observer Exclusive scope 하나와 기본OFF `-RenderedClientCSV`를 연결했다. Client1만 Animation category·CSV를 켜고 `csv.BlockOnCaptureEnd 0`을 사용한다. 공개 EndCapture future를 콜백 없이 보관하고 phase6 완료만 IsReady로 확인해 파일 쓰기 전에 프로세스를 닫지 않는다. GT Wait·별도 timer·manager는 없고 측정phase/원래 strict는 유지한다. 파일 마지막 헤더·중복 ordinal·완료 metadata/log·sampler scope·SHA를 서버 Iris CSV와 별도 검증한다.

Editor113 direct UBT25.67초·exit0 뒤 `ArchitectureBatch37ClientCSV1`은 actual0×3/timeout0·strict/양 CSV·Source/runtime/선택 에셋 불변을 통과했다. 실제720개 sampler 행과 CSV의 해당720개 행, GC/명령 event·frame ID가 모두 일치한다. phase3/4의 Frame P50은63.02/63.61ms, GT critical path62.86/63.38ms였다. 기존 frames.csv의 일반 GT44ms와 critical path를 구분한다. GT EndPhysics 대기는23.78/24.30ms·Cloth 대기는18.86/18.80ms, observer2.02ms·AnimationBudget1.05/1.08ms였다. worker Cloth404/407ms는 병렬 task 범위 누적이며 frame/CPU 시간에 더하지 않는다. EndPhysics는 tick group 대기라 물리 solver 자체 CPU24ms로 단정하지 않는다. RT wait52ms도 대부분 noncritical idle이었다.

선택 실제 연결의 기존 receipt에 public `OutTotalBytes/OutTotalPackets`를 추가했다. signed32이므로 음수·역행·rollover는 해석하지 않는다. 37차 같은 phase4의8.343262초 구간은680430bytes/722packets·계산상81554B/s였고, 이는 모든 연결 트래픽을 포함한 FlushNet 계산값이지 UDP 전달·NP 전용 bytes가 아니다. 기존 본문/시도 counters를 실제 전송 절감률로 바꾸지 않는다. 공개 Iris export utility에는 ordered refs/names·pending token export·clone/rollback을 대체할 ACK delta API가 없어 별도 codec을 만들지 않았다.

38차는 지원 cloth task priority 문자열 `hnh`/`nnn`만 Client1에서 바꾸는 선택 측정 경로다. solver/정밀도·CCD·품질·주기·completion 의존성은 유지하고 실제 Console query값까지 검증한다. 같은1197개 입력/39개 binary의 `ArchitectureBatch38ClothHigh1/Normal1`은 양쪽 strict·의상/로컬 품질·CSV·Source/runtime/선택 에셋 불변·actual0×3/timeout0을 통과했다. phase3 Frame P50/P95는63.43/67.56→62.57/67.46ms, phase4는62.46/78.55→59.93/64.39ms였다. 그러나 EndPhysics 대기24ms가0.2ms로 줄면서 WorldTickMisc가약3.9→28ms, GT Cloth가약2.8→42–44ms로 이동했다. worker Cloth 누적도 줄지 않았다. 한 쌍의 phase4 tail 개선을24ms 제거·연산 감소·기본 채택으로 해석하지 않으며 engine 기본값은 유지한다. 37–38차는 원인 분해/관측·실행 검증이며 새 client FPS 최적화 완료로 세지 않는다. 로컬·원격 substep public 통계의 기존 engine 한계도 [의상 보고서](../../Performance/Reports/ProjectJ_Cloth_Budget_200_2026-10-25.md)의 구분을 유지한다.

이전 지속 실행은2026-10-28 16:15 KST에 모델 서비스의 `Selected model is at capacity` 오류로 종료됐고 목표도blocked였다. 작업 완료나30% 사용량 조건으로 종료한 것이 아니다. 사용자가 재개했고 목표active를 확인했다. 결과와 소스는 보존했으며 성공한 검증을 처음부터 다시 실행하지 않는다.

### 39차 headless 공유 포즈의 실행 admission

36차의 cloth 생성 admission을 기존 public `GetAllowClothActors()`로 `RefreshSharedPoseTick`에 연결했다. 생성이 금지된 Game/PIE의 metadata만으로 primary tick을 다시 켜던 불일치를 없앴다. simulation instance 존재만으로 판단하지 않아 streamed resource 생성 전 preview/RHI의 admission을 보존하고, 추가 bone용 CopyPose의 독립 graph tick도 유지한다. 새 flag·wrapper·polling·manager 없이 기존 등록·재등록·mesh/parent 변경·Activate의 refresh를 사용한다. inherited non-virtual setter 단독의 즉시 갱신/리소스 해제는 이 변경의 계약이 아니다.

직접 Editor114/Game25 UBT·기존 포즈/의상/로딩·proxy 수명5개와 CopyPose1개가 경고0/exit0을 통과했다. Cook14를 재사용한 Stage20은 Game25 exe와 같고, 원래 packaged NullRHI listen+RHI200의 `ArchitectureBatch39RenderedCooked1`은 strict·server/client CSV·actual0×3/timeout0·Source/runtime/선택 에셋 불변이었다. 서버 cloth metadata200·simulation0에서 primary tick도200→0이 됐다. RHI phase3/4의269frame은 cloth simulations200·local 축소0·local 최대LOD0을 유지했다. 이는 생성 admission과 실행 판단의 원인 수정이며 추가 FPS·정확한 메모리 bytes·장기 회수량의 증명은 아니다. 독립 검토는 기존 CopyPose 경로와 setter 한계까지 확인했고, 테스트의 metadata fixture는 solver 품질 근거와 구분한다.

전체 목표·기존 약60%/잔여40% 추정은 유지하며 client critical path·원격 endpoint hold·Nav/GPU/HLOD·미정 운영 provider/서버 및 수용량은 계속 미완료다.

### 40차 실제 아이템 사용의 recipient epoch

실제 InventoryVM→RequestUseItem→owner 명령→TryUseItem의 소비 알림 뒤 수신자를 기존 project ASC revision으로 재검증한다. 같은 ASC/owner/avatar pointer를 유지한 명시 Init/Clear+Init도 이전 요청의 회복·쿨다운을 새 epoch에 적용하지 않는다. 이미 관찰된 소비는 기존 `Unavailable` 의미대로 보존하고 아이템을 복원하거나 자동 재요청하지 않는다. 전환 delegate 실행 중에는 revision이 증가했어도 아직 이전 tuple일 수 있어, 기존 `IsActorInfoChanging()`으로 비영속 소비 전과 영속 eligibility/완료를 거절한다. 일반 UE ASC의 기존 pointer/tuple 호환과 정상 metadata refresh는 유지한다. 기존 GAS 수명·요청별 UI 결과를 재사용하며 새 상태·transaction·rollback·timer·framework·에셋·반영/wire 계약은 추가하지 않았다.

직접 Editor115/Game26 UBT와 기존 소비/UI·MountedPersistentOwner·LocalProfileLifecycle4개가 경고0/실패0·1.576초/exit0을 통과했다. 소비1/2개의 실제 callback에서 same-pair/Clear+Init·GC·정상 metadata refresh·generic ASC, 실제 transition delegate의 무소비 거절, 실제 VM→owner 명령→결과·수량/pending·자동 재요청 부재를 확인했다. 기존 async local profile의 receipt/epoch 수명도 재사용했다. 테스트 전후 Source/runtime 지문과 빌드 전후 Source는 같았다. 이 native owner RPC/VM 시험을 새 원격 socket·패키지 시험으로 세지 않는다. 최신 실제 배포 검증은39차 Game25/Stage20이며40차의 native-only 변경은 다음 필요한 배포와 묶는다. 외부 provider의 transition 중 완료·운영 영속 경제·성능 수용량 검증은 여전히 남는다.

이어41차는 기존 A08 정의 로드의 실제 서버/클라이언트 소비 경계를 확인했다. Headless Tick 제거를 메시/에셋 closure 제거로 확대하지 않고, gameplay pose·notify·판정과 순수 cosmetic 요청을 기존 호출·근거로 구분한다.

### 41차 순수 VFX 자원의 실행 가능성

A08의 CharacterData registry는 Config/Primary 경로를 이미 dedup하고 gameplay metadata를 검증하므로 새 registry나 일괄 headless mesh/Anim strip을 만들지 않았다. 실제 CombatPresentation의 Preload/Ensure/PlayCueLocal이 headless listen/standalone에서도 pure Niagara payload lease/prewarm을 요청하던 경로만 보강했다. Niagara 생성 자체가 거절하는 `FApp::CanEverRender`를 기존 dedicated admission과 결합한다. 권위 PlayCue의 loop metadata·복구 게시·multicast·notify와 종료, 공용 로더/weapon/pose/ABP, 명시적인 IsDataValid의 제작 검증 로드·soft Cook closure는 유지한다. Preview/Cook 면제나 새 API·friend·상태·manager·framework는 없다.

직접 Editor116/Game27 UBT와 headless CueLeaseOwnership1개·실제 D3D12/SM6의 기존 PresentationRecovery/EffectsPoolLifetime/CueLeaseOwnership3개가 경고0/exit0을 통과했다. unresolved/resident headless payload의 새 shared 요청0과 real notify begin/end·authority loop recovery/종료, stale callback/retirement를 확인했다. 기존 자원 입장·복구 assert는 유지하고 공식 NonNullRHI 요구와 기존 실행기의 명시 UseRHI 옵션으로 실행 환경을 맞췄다. default NullRHI는 유지하고 capability나 role을 모의하지 않는다. Source/runtime 지문은 실행 중 같았다.

40차와 함께 Cook14 재사용 Stage21로 묶었고 Game27 exe SHA 일치·실제 RHI 패키지 입력/GAS/몽타주/notify/장비 해제3cycle가 success/cooked/actual exit0이었다. 최신 native-only 변경에 새 Cook·반영/wire 변경은 없다. Native authority 복구를 새 원격 multicast/200명41차 검증으로 세지 않으며 기존39차200명/2socket 근거는 해당 버전 그대로 보존한다. Niagara는 원래 headless component 생성을 거절했으므로 이 변경은 불필요한 요청의 원인 수정이다. 새 simulation instance/FPS 절감·정확한 memory bytes/장기 회수·전체 에셋 closure 최적화로 주장하지 않는다.

전체 목표·약60%/잔여40%의 기존 작업량 추정은 유지한다. 남은 월드 도착·Nav/GPU/HLOD와 연속 표시, 연결별 전송/원격 상태 공급·loss 품질, 미정 provider/서버의 저장·인계·복구·운영 및 실제 혼합 수용량은 기존 계획에 따라 계속 진행한다.

### 42차 월드 이동 요청의 수신자와 조기 예산

준비 도착 요청은 권위 쪽의 weak ASC와 기존 ActorInfoRevision을 캡처한다. 같은 PS/Pawn/ASC pointer를 유지한 명시 Init/Clear+Init도 이전 요청을 새 incarnation에 적용하지 않는다. 기존 IsArrivalCurrent의 중앙 검증으로 CanInteract 재진입 뒤·ACK·queued native effect·늦은 native outcome을 닫고, 안정된 nonzero epoch에서만 요청한다. 정상 metadata refresh는 계속 허용한다. 비권위 client 분기는 서버 epoch를 전송/비교하지 않으며, 효과의 NetSerialize·RPC/protocol8·Mover commit/receipt 소유권·queued 이후 원점 언로드·WP 대기 최적화를 유지한다. 기존 Finish는 새 weak token도 해제한다. 이미 적용한 물리 이동을 되돌리거나 stale receipt로 새 incarnation의 도착을 확정하지 않는다. destination provider 광고 정리는 기존10Hz 경계까지 남을 수 있으며 별도 즉시 unregister 정책은 추가하지 않았다.

Ready ACK는 값싼 현재 session/recipient 확인→기존 공유 quota/domain 간격→깊은 origin/place/LOS/eligibility 검사 순서다. 이미 quota에서 거절될 ACK에 선형 Place 조회/visibility trace를 반복하지 않고, stale PC/PS binding·수락한 중복 ACK가 옛 session 예산을 차감하지 않게 한다. 기존 public rejection count를 사용해 포화+invalid origin의 순서와 stale binding·같은 generation retry·duplicate 회계를 검증했으며 새 counter/훅/전역 scheduler는 만들지 않았다. callback 전후 FindArrivalPlacement 두 번은 의미가 달라 유지했다.

직접 Editor117/Game28 UBT와 기존 월드 준비·clock·도착/MountedPersistentOwner5개가 경고0/실패0·0.184초/exit0이었다. 실제 Init/Clear+Init·changing callback·metadata refresh·native queued transform/event·late receipt·cleanup을 확인했다. 정상 MainWorld BP GameMode/PC/Avatar의 실제 dedicated+owner socket `ArchitectureBatch42OwnerArrival1`도 actual0×2·양 역할 도착·도착 전 destination source·일반 source hash/geometry 복귀·같은 character·선택 보호 에셋/Source/runtime 불변을 통과했다. 서버는 지원 -useperfthreads 프로필이며 CPU/collision/source/Mover 근거다. 실제 BP CanInteract override에서 reinit을 강제한 별도 시나리오나 Nav/GPU/HLOD·연속 픽셀·동시 도착 수용량·loss 품질을 완료로 세지 않는다.

최신 actual package는41차 Game27/Stage21이며42차 private native 요청 token/ACK 순서 변경은 다음 필요한 배포와 묶는다. 전체 목표와 기존 약60%/잔여40% 작업량 추정은 유지한다. 다음은 이미 측정된 client cloth critical path에서, 기존 CharacterAnimationBudgetSubsystem/Coordinator를 재사용한 전체 실행 자원 예산 후보를 검토한다. 품질·authoring·외부 writer·전환 비용을 보존할 수 있는 경우에만 제한된 opt-in 비교를 진행하고, part별 미세 측정이나 무근거 기본 채택으로 확대하지 않는다.

### 43차 실행 예산의 비용·완료 입력 — 입력 검증, 실행 제한 미완료

기존 FullRemoteParts는 품질 quota이며200개 Cloth 실행을 줄이는 cap은 아니다. 새 manager를 만들기 전에 기존 Coordinator Bind에서 고유 ClothingAsset별 최대 physics LOD vertex 합을 한 번 캐시했다. fixed 정점도 포함하고 empty/overflow/stale binding은 unknown으로 구분하며 Restore/rebind가 cache 수명을 소유한다. known 부분합을 전체 비용이나 unknown의0-cost로 해석하지 않는다. 현재 runtime geometry는 immutable이며 같은 UObject의 제자리 geometry 변경은 명시 release/rebind가 필요하다. CPU ms나 모든 미발견/미지원 Cloth의 전체 비용을 보장하는 값은 아니다.

기본OFF `ProjectJ.ClothBudget.ObserveExecution`은 native modular part의 Super Tick 후 엄격히 유효한 현재 ClothTick/자연 ClothEndTick completion handle 두 개를 복사한다. positive delta·실제 최근 render·active LOD·기본 endwait·등록/활성/interval/GT tick과 mesh/leader/interactor/register incarnation을 확인한다. 기존 AnimationBudget의 실제 WorldPostActorTick 등록부 순회에서 두 handle 완료를 확인하고 참조를 반환한다. 여기에 mesh property를 쓰거나 wait/task/callback/solver를 추가하지 않는다. EOF/paused/compile/visibility skip·미등록/불명확한 handle·동시 tick은 unknown이며 handle 부재나 시간 경과를 완료로 세지 않는다. direct leader 변경도 identity 검사로 닫는다. 이는 적격 native queued pipeline의 완료 근거이며 solver integration step 수나 모든 warmup의 충분조건으로 과장하지 않는다.

직접 Editor118/Game29 UBT·기존 headless5개와 실제 RHI interactor1개가 경고0/exit0을 통과했다. unknown≠empty·실제 garment의 positive cost·solver 품질과 geometry 비용 구분·교체/rebind/해제를 확인했다. `ArchitectureBatch43ClothBoundary1`은 실제 RHI200/2socket의 strict·server/client CSV·actual0×3/timeout0·Source/runtime/선택 에셋 불변이었다. Client1만 선택 관측을 켰고 phase3/4 trim76/82표본 전부 cost506,800·unknown0·qualified native parts200이었다. 기존 로컬 품질·이동/반전/착지/몽타주·gameplay pose gate는 유지했다. actor200은 실제 connection200 수용량이 아니다.

43차 종료 시 다음44차 작업은 이 입력을 사용하는 명시적 default-false 실행 opt-in/default0 vertex cap, protected/unknown 보존·fade-out/park/warmup/fade-in·실제 예약 수명과 한 쌍의 RHI/시각 비교였다. 당시 **실행 cap·fallback 전환은 미구현이었고 CPU/frame 개선이나 기본 채택을 주장하지 않았다.** 실제 package 근거는41차 Game27/Stage21이며42/43의 native-only 변경은 다음 배포와 묶는다. 전체 목표·기존 약60%/잔여40% 추정과 미정 provider/서버·Nav/GPU/HLOD·원격 공급/손실·혼합 수용량 잔여는 유지한다.

### 44차 원격 의상 실행 예산과 실제 작업량 감소

기존 CharacterAnimationBudgetSubsystem의 등록부·camera pass와 Coordinator의 garment binding을 재사용했다. `ProjectJ.ClothBudget.ExecutionVertexCap=0`, 의상별 `SetClothExecutionBudgetAllowed(false)`가 기본이다. 기존 품질 permission만으로 suspend를 허용하지 않는다. immutable max-LOD physics vertex 비용으로 desired와 실제 mayResume를 분리하고, 활성·fade-out·warmup·pending native drain을 예약에 포함한다. 보호/미지원 known 비용은 debt에 포함하고 unknown이 있으면 일반 resume를 허용하지 않는다. geometry 단위는 CPU ms가 아니며 모든 Cloth의 절대 hard cap도 아니다.

원래 authored blend를 기준으로0.2초 fade→blend0→suspend한다. alpha0에서 현재 queued native Tick/EndTick completion 둘을 복사하고 자연 post 완료 전에는 예약을 반환하지 않는다. 복귀는 먼저 예약→resume/reset→같은 incarnation의 positive native 완료→fade-in이다. post 정책은 요청·회계만 게시하고 실제 component 쓰기는 Coordinator PrePhysics/명시 pre-mutation handoff에서 수행한다. local/AP/mounted local/locally viewed/full-quality 보호 전환은 warmup을 기다리게 하지 않고 PrePhysics에서 기존 scalar를 즉시 복원한다. 기존 source pose/판정/notify/복제·solver 설정·native task priority는 유지했다.

part opt-out·실제 attach/mesh 교체·physics destruction/unregister/tick disable·Coordinator 종료에서 기존 소유권을 반환한다. component blend/suspend는 마지막 자체 write와 일치할 때만 독립 복원하며, 다른 asset simulator에 옛 solver defaults를 쓰지 않는다. 같은 값의 외부 writer 의도는 검출할 수 없으므로 BP/Sequencer/custom 제어는 먼저 명시 opt-out한다. 지원 밖 수명·가시성/인플라이트 작업은 보수적으로 보호한다. 새 manager/worker UObject 접근·wait·timer는 추가하지 않았다.

직접 Editor119/Game30 UBT, 기존 headless5/RHI interactor1이 모두 통과했다. 실제 garment에서 opt-in 분리·authored suspension·소유 scalar를 구성한 public opt-out/외부 blend 보존/virtual mesh 교체의 원상 복원을 확인했다. native value 회귀는 실제 시뮬레이션 완료를 위조하지 않는다. 독립 정적 검토에서 post 쓰기·보호 승격·회귀 공백을 발견하고 적용 전에 해소했다.

`ArchitectureBatch44ClothOff1/ClothOn1`은 동일 Source/binary/선택 에셋·정규화한 세 역할 명령을 검증했고 Client1 cap0/60,816만 달랐다. 두 실행 모두 strict·CSV·actual0×3/timeout0·소스/에셋 불변과200개 garment의 종료 scalar 복원이 통과했다. 실제2연결/200avatars이며200CCU가 아니다. phase3/4 game time2–7초·스크린샷 제외 표본은 OFF77/76, ON127/127이다.

| 실제 Client1 지표 | phase3 OFF→ON | phase4 OFF→ON |
| --- | --- | --- |
| FrameTime P50/P95 ms | 64.47/68.13→39.15/41.65 | 65.44/71.88→38.79/42.90 |
| GT critical P50 ms | 64.47→39.13 | 65.20→38.89 |
| EndPhysics wait P50 ms | 24.47→1.68 | 24.87→1.67 |
| worker Cloth scope합 P50 ms | 413.35→65.81 | 416.82→66.35 |

worker 합은 서로 겹치므로 frame/GT wait에 더하지 않는다. Cloth wait P50은18.58→17.68/18.90→17.45ms로 남았고 GT Cloth scope는2.83→6.76/2.87→6.69ms로 증가했다. 대기 위치 이동만이 아닌 native qualified parts200→24와 worker 작업량 감소를 함께 확인했지만 모든 CPU 비용을 해결한 것은 아니다. ON steady 표본 전부 reserved60,816·protected2,534·unknown0·park176이며200개 actor/garment simulation instance는 유지됐다. 전체1,141표본에서 Warming6/FadeIn6프레임도 관측했다. public completion은 적격 native pipeline 근거이며 물리 integration step counter가 아니다.

phase3 OFF/ON PNG를 직접 확인해 군중·외형이 유지된 것은 확인했지만 작은 원경 silhouette만으로 의상 변형·전환 품질을 최종 승인하지 않는다. 실제 remote→local 전환 화면·다양한 의상·플랫폼·장기 부하와 반복 성능은 남는다. 따라서 기본OFF/per-part false를 유지하고 임의 생산 cap을 채택하지 않는다.

새 반영 permission 때문에 새 Cook15/Stage22로 Game30을 배포했다. 최초 package 호출은 dedicated 전용 performance flag를 잘못 넣어 실행 전 guard에서 거절됐고, 생성물 재사용 후 지원되는 listen+2remote의 `ArchitectureBatch44ClothCooked2/3`을 통과했다. 첫 실제 Cooked1은 의상 복원·기능 판정 이후 CSVProfiler의 두 번째 writer finalization에서 exit3으로 실패했다. 소스와 로그가 일치하는 nonblocking stop 재진입 가능성을 좁혀(정확한 thread interleave는 추론) runner의 csv.BlockOnCaptureEnd0을 지원 기본값1로 되돌렸다. 같은 Game30/Cook15/Stage22를 재사용했고 Source/Config/binary는 그대로이며 Measure-Network 종료 옵션만 바뀌었다. 두 재실행에서 최초 trigger와 반복 종료·파일 완료·actual0×3/프로세스 해제를 확인했다. 이는 vendor 원인 수정이 아닌 프로젝트의 구조적 우회이며 엔진 파일은 보존했다. 이는 Development Game/기본 두 map closure의 호환 근거이며 dedicated Server/Shipping·MainWorld 전체 Cook 인증은 아니다. 다음은 기존 증거를 재사용한 원격 NP 공급/연결별 예산 경계이며, 전체 약60%/잔여40% 추정과 미정 운영 서비스·Nav/GPU/HLOD·혼합 수용량은 유지한다.

### 45차 Mover 카메라와 서버 시점 생산자 입력의 연결

원격 공급의 기존44차 근거를 재사용했다. 두 실제 연결의 선택3 SP는 이미 in-scope·creation confirmed·base priority1·dirty subobjects 상태이며, writer의 기존 누적 공정성이 있었다. 실제 parent connection은100,000B/s·packet1024였고, 44 phase4의 전체 FlushNet 계수는81,440/86,881B/s·관측 endpoint age 약0.8–1초였다. 두 작은 snapshot으로 개별 packet fit/commit/ACK 인과를 확정하지 않는다. 기존 Combat/Party/Guild multiplier 진단을 실제 live Iris 적용으로 세지 않고, 전원 동일 중요도에서 FOV/priority boost가 총 수용량을 늘린다고 주장하지 않는다.

FOV 적용 전에 실제 시점 계약을 확인하면서 native Mover/NavMover가 UPawnMovementComponent가 아니어서 stock PC의 camera-send eligibility를 만족시키지 못하는 경계를 찾았다. 기존 opt-in 관측의 두 stage boundary에 서버 GetPlayerViewPoint/PCM flags와 client의 실제 POV/physical backend를 읽기 전용으로 추가했다. 이는 NetDriver replication-view 생산자가 소비하는 공개 입력이며 마지막 published Iris view를 private 구조에서 읽은 결과는 아니다.

Editor120의 관측 코드 TObjectPtr 추론 오류를 보존하고 명시 pointer/Get으로 수정한 Editor121에서 `ArchitectureBatch45CameraBefore1`을 실행했다. 실제 BP의 legacy movement component=None·Mover present·client camera update enabled와 phase3/4/5의 동일한 실제 화면 POV를 확인했다. server phase4 start/end의 위치 차이는4,537.86/4,490.31cm였다. 원래 strict·CSV·actual0×3·Source/runtime/선택 에셋 불변도 통과해 이 조건을 실제 trigger로 확정했다.

기존 Core MovementQueries에 공통 GT 함수 `RequestMoverCameraUpdate` 하나를 추가했다. local NM_Client·현재 pawn/controller/camera/world 소유관계·유효 Mover·legacy movement 없음·기존 feature enabled일 때 native one-shot `bShouldSendClientSideCameraUpdate` flag만 준비한다. production PC와 별도 socket fixture PC의 UpdateCameraManager가 같은 함수 후 Super를 호출한다. native 위치/회전 변경·시간 간격·timeout·기존 ServerUpdateCamera가 계속 전송을 소유한다. 새 RPC/캐시/상태 owner/timer·서버 view 강제 대입·FOV/Hz/packet/bandwidth 변경은 없다. controller/pawn/camera 교체·종료에서는 현재 관계를 재조회하고 world teardown·invalid owner는 건너뛰며 기존 flag를 임의로 clear하지 않는다.

Editor122와 `ArchitectureBatch45CameraAfter1` 실제200명/두 연결의 기존 strict·CSV·0×3/timeout0·Source/runtime/에셋 불변·의상200개 원상 복원이 통과했다. 두 server boundary의 위치 차이는0cm, 압축 pitch 차이는 약0.000584도였다. 전후 binary가 다르므로 성능 비교로 해석하지 않는다. Game31 직접 UBT·native-only Cook15 재사용 Stage23의 `ArchitectureBatch45CameraCooked1` listen+remote도 같은 시점 정합성/품질/종료를 통과했다. 새로운 MainWorld production PC의 실제 렌더링/동적 orbit·mount·spectator handoff checkpoint는 이 socket fixture 근거와 구분해 남긴다.

stock RPC는 FOV를 전송하지 않으므로 client65/server90은 그대로 드러낸다. stock FOV prioritizer의 cone 각도도 profile 값이며 client viewport FOV의 자동 복사라고 부르지 않는다. camera 입력은 표현/관심도 자료이며 공격·거래·물리 이동의 서버 권위를 대체하지 않는다. FOV profile/새 scope adapter는 아직 채택하지 않았다. scope delegate는 연결별 bitarray copy/XOR 비용을 활성화할 수 있어 사용 자체를 무료라고 보지 않는다. 실제 NP 공급 저율·선택 이후 packet/ACK 경계와 총량은 남으며 다음은 기존 serializer/export 비용의 최소 개선 가능성을 확인한다. 전체 약60%/잔여40%·미정 provider/서버·Nav/GPU/HLOD·혼합 수용량은 유지한다.

### 46차 RPC 메타데이터 비교와 고정된 대상의 코덱 채택 검증

현재 공급을 고치기 위해 새 protocol/export 생략을 바로 추가하지 않았다. 설치 util의 captured name/object storage stride와 memcmp prefix 크기가 다르고 pending token 값은 비교하지 않는 경계를 확인했다. 실제 NP Proxy는 CachedPendingFrame을 비교하므로 이를 dense200 상태 누락 원인으로 확대하지 않는다. 해당 util을 사용하던 RPCParameter의 quantized 비교를 공개 object/name serializer별 동등성과 모든 token 원소 비교로 보강했다. Source 비교도 body indices만 같아 다른 이름/참조/토큰을 놓치지 않도록 typed 배열을 함께 비교하고 기존 config/count/storage 한계를 먼저 확인한다. 현재 RPC 송신의 full NetBlob 경로에서 잘못된 동일 판정 때문에 실제 payload가 누락됐다는 관측은 아니며 비교 API의 의미 계약을 닫은 것이다.

실제 registry serializer·정상 PackageMap/FNetBitWriter·같은 body index0/1와 다른 두 번째 hardcoded name·public tokenstore의 같은 count/다른 token을 사용했다. 양자화 전후 동일/반사/변경/대칭·범위와 기존 full wire roundtrip/dequantize를 확인했다. vendor util·Proxy/frame/clone/free/current exports·wire/serializer versions/protocol8은 유지했다. Context의 export 부작용·ACK/AOI/GC/alias 수명이 확정되지 않은 신규 export reuse는 보류했다.

기존35차에는 작은 decode median 개선 신호가 있었지만 선택 대상/observer가 달라 채택 판단이 미완료였다.46차는 원래 증상 선택을 기본으로 유지하고, 비교 때만 NPC canonical indices2/3/4·PlayerId10002/10003/10004를 고정하는 opt-in 관측을 추가했다. client의 실제 SP/안정된 binding과 server의 실제 canonical mapping을 확인한다. 같은 NP CSV 계측을 production에도 켜고 encoder 실제 query를 남겨 계측 차이를 없앴다. 새 polling/전송/상태 권위나 모델 정밀도 변경은 없다.

Editor123의 `ArchitectureBatch46NPProduction1/NPAckFull1/NPAckWord1`은 같은1197 source/39 binary/선택5 asset·같은 cap60,816/관측/두 연결/200avatars였고 명시 ACK class request·encoder와 격리 경로만 달랐다. 원래 strict·CSV·actual0×3/timeout0·200명/의상 표시·로컬 품질·해제 복원이 모두 통과했다. 실제 encoder는0 Constructor/0 Console/1 Console이다. canonical3명은 각각11개 latest decode serial·10개 연속 interval·serial gap0이었다.

| 실제 관측 | Production | ACK Full | ACK Word |
| --- | --- | --- | --- |
| NPC별 decode 간격 P50 범위ms | 850.50–855.76 | 829.40–835.72 | 811.21–824.30 |
| NPC별 decode 간격 P95 범위ms | 880.49–916.53 | 887.04–911.72 | 866.74–913.55 |
| 같은 parent 구간 Flush 계수B/s | 85,186.91 | 85,324.94 | 85,657.08 |
| 별도 driver stage43 두 연결B/s | 170,220.87 | 170,601.49 | 171,167.27 |

NPC별 quantile을 평균하지 않는다. decode/apply 간격·sampled age·pending-view와 순수 network latency/packet 수는 다르다. parent와 driver 경계/모집단도 달라 서로 차감하지 않는다. Word changed-body68.745% 절감과54,696회 WordDelta는 실제지만 성공 serializer entry37,029/36,928/81,917은 writer rollback/후보 시도를 포함해 전달된 update 수가 아니다. 포화 counter는 pre-credit 관측이고 증가만으로 공급 퇴행을 판정하지 않는다. 작은 median 신호에도 약800ms hold와 tail이 남았으며 wire 계수 감소·baseline 추가 메모리/장기 회수는 입증되지 않았다. **기본 encoder와 NP class ACK policy를 OFF로 유지한다.** 이는 모든 조건에서 이득이 없다는 증명이 아니라 현재 채택 근거가 부족하다는 판단이다.

Word의 Cmd 로그 prefix 한 개가 CSV index와1frame 달라 기존 Saved reader가 처음 거절한 결과도 보존했다. 설치 publisher는 timestamp AddEvent와 CaptureFrameNumber/log prefix를 별도로 기록하고 CSV/logical boundary가 다를 수 있다고 명시한다. buffered log formatting의 GFrameCounter 시점도 별도다. 전체 sampler frame-set/GC anchor·모든 event의 실제 CSV index와 label 검증은 유지하고 non-GC prefix만 진단으로 기록하도록 Saved reader/재생성 recipe를 수정했다. 품질/실행 판정은 완화하지 않았다.

양자화 보강 Editor123/집중5개/triad 뒤 같은 비교 계약의 Source 분기를 보완한 Editor124/집중5개와 Game32를 통과했다. triad는 보완 전의 동일 Editor123 binary 비교이며, 보완은 Proxy 코덱·전송 형식을 바꾸지 않는다. 최종 Game32/Cook15 재사용 Stage24의 `ArchitectureBatch46NPProductionCooked1`은 실제 listen+remote200의 기본 정책·고정 관측·정상 종료/복원을 확인했다. 새 MainWorld/Shipping/Server·대규모 수용량이나 손실 복원 전체를 인증한 것은 아니다. 다음은 미정 source-engine Server와 구분해 지금 가능한 Shipping Game 경계를 진행하며, 네트워크 공급·GPU/HLOD/Navigation·다중/고속 이동·미정 provider 잔여도 유지한다.

### 47차 현재 소스의 Shipping 구성과 배포 경계

설치 UE5.8.4의 Win64 Shipping Game 지원·precompiled 출력을 먼저 확인한 뒤 직접 UBT를 실행했다. `MMOFoundation_20261028_Shipping1`은468actions·native238.06초/exit0이었다. 기존 Shipping exe/receipt를 현재 소스로 갱신했고 기존 Development/Editor 제품과 소스는 보존했다. 최초 Saved 검증 guard가 기존 Shipping 출력 두 개까지 보존 대상으로 분류해 거절한 것은 UBT 실패가 아니다. 정확히 의도한 Shipping exe/target 두 개만 변경됐음을 별도 확인했다.

Cook15의 명시 두 맵/AssetManager closure를 재사용한 `ShippingStage1`은exit0이며, bootstrap과 구분한 실제 `Project_J-Win64-Shipping.exe` SHA256은 `D1F3C8B2A2E9CB2B678DDF499DEDE6779A596AC5AA529154656548A5E582BDBC`로 직접 빌드와 일치한다. 새 production Shipping 콘텐츠 Cook·전체 MainWorld 배포나 서비스 연결을 인증한 것은 아니다.

기존 `Run-ContinuationBuild.ps1`에 기본 Development를 보존한 Configuration 선택을, `Run-ContinuationPackaging.ps1`에 StageConfiguration 선택을 추가했다. Editor의 Shipping 및 Cook 단계의 StageConfiguration 오용은 실행 전에 거절한다. 기존 위치 인자는 보존했고 새 별도 빌드 framework는 만들지 않았다. canonical helper의 `MMOFoundation_20261029_Shipping2`는 현재 소스 target up-to-date·native4.37초/exit0이었다. Stage helper는 AST/인자 구성을 확인했고 같은 직접 AutomationTool 명령의 ShippingStage1 실행을 재사용했다. 변경 후 canonical Stage helper 자체의 새 Shipping 실행은 중복하지 않았다.

격리 `ShippingStartup1`의 PID28496은 실제 구성 exe로 생성됐지만 UI 도구에서 targetable game window가 없었으며 map-ready 관측도 없었다. 배경 helper 규칙에 따라 Hidden으로 시작했으므로 창 부재를 crash/hang으로 해석하지 않는다. Shipping의 자동 seconds 종료는 설치 엔진 guard에서 제외되므로 사용하지 않았다. 최초 정상 OS 종료 명령은 AGENTS의 기존 프로세스 보호 규칙에 따른 자동 승인 검토에서 실행 전 거절됐다. 사용자가 해당 검증 앱만 종료하도록 명시 허용한 뒤 경로·시작 시각을 다시 대조하고 정상 종료 신호를 보냈다. 실제exit0·source/runtime 불변이며 강제 종료나 다른 프로세스 종료는 없었다. 이 결과는 생성/종료 수명 확인이고 게임 화면·맵 준비의 성공 근거가 아니다. Saved의 최초 guard/시각 변환 오류와 검증 한계도 보존했다.

C++/에셋 변경은 없으며 기존 Editor124/Game32 회귀를 반복하지 않았다. source-engine Server·공급자/인증/DB·전체 원격 공급/손실·Nav/GPU/HLOD/고속·동시 이동·혼합 수용량은 남는다. 다음은 기존 월드 도착 준비와 실제 Navigation 처리의 소비·자원·취소 경계를 묶어 진행한다. 전체 목표와 약60%/잔여40% 추정은 유지한다.

### 48차 경로 대기열의 최신 목적지와 실제 동료 소비

기존 NPCAction은 매 cadence 최신 목표를 읽어도 pending token이 있으면 return했다. 아직 프로젝트 큐에만 있는 경로도 제출 당시 목적지로 계산한 뒤 completion의 GoalDrift 검사에서 버릴 수 있었다. PathSubsystem의 GT `RefreshQueuedGoal`은 같은 owner/intent·유효한 pawn/controller·원래 deadline의 미dispatch 요청에서만 목적지를 갱신한다. 같은 목적지는 성공 no-op이고 token/Submitted/priority/FIFO/deadline·예약·callback은 바꾸지 않는다. 실행 중인 엔진 query·완료/취소/퇴역 요청은 갱신하지 않는다.

기존 추적·비전투 동료가 사용하는 NPCAction에 이를 연결했다. pending token의 마지막 허용 goal을 현재 따라가는 FollowingGoal과 구분하며, RepathDistance를 넘는 변화에서만 조회한다. 이미 dispatch돼 갱신이 거절되면 같은 token의 추가 조회를 중지한다. 완료/취소에서 두 값도 해제한다. 새 manager/타이머/병렬 worker/RPC·네트워크 형식은 없고64개 요청/4개 dispatch/16개 물리 슬롯/8개 delivery/2초 deadline·기존 유효 경로·completion drift·NavData 수명 검사는 유지한다.

기존 PathPressure는 same token/latest goal/no-op·다른 owner/intent/invalid goal·in-flight 불변·원래 deadline 만료·FIFO/유한 urgency·취소 슬롯 회수를 확인한다. 기존 Companion.NativeFollowMovement는 유효한 old path의 새 queued replacement에서만 목표 offset을 계속 유지하며 변경한다. 추가 admission 없이 같은 token·old move를 보존하고 실제 Recast endpoint와 새 RequestMove가 그 최신 목표를 소비한 뒤 Mover가 설정 거리까지 도착해야 통과한다. fake path/역할이나 목표를 원래 위치로 되돌리는 예외는 없다.

`MMOFoundation_20261029_Editor125`는26actions·native117.94초/exit0이며, `Batch48Native1`은18개·경고0·8.045초/actual exit0이다. source는 빌드 중, source/runtime은 native 실행 중 불변이다. 이는 큐 목표 노후화의 구조 수정과 수명/실제 소비 검증이며 새 CPU/FPS 개선률·100/200명 경로 수용량의 증명은 아니다. 기존 Mixed100 queue P95 약75ms와35차 NavData retirement 근거를 재사용한다. Game/Shipping/Cook/Stage는 다음 필요한 호환 배포와 묶으며47차 Shipping을48차 배포 검증으로 읽지 않는다.

별도 좁은 A10 검토에서 수동 플레이어 도착은 WP source·floor/capsule·native Mover를 소비하므로 Nav 필수 gate를 추가하지 않았다. 현재 컴포넌트별10Hz/30초/128cell/최대8 prediction sphere 상한은 합산 I/O/activation 예산이나 다중 고속 이동 수용량과 다르다. 기존 MainWorld probe의 두 owner 중첩 준비/한 owner 정상 종료·교체 취소와 독립 도착/source 해제 검증은 남긴다.

네트워크 검토도 기존46차 자료를 재사용했다. 실제 송신은 class mapping의 stock Sphere이며 일부 프로젝트 Combat/party/distance 계산은 현재 진단 소비와 구분된다. native NPC10Hz와200 PlayerCharacter fixture를 혼동하지 않는다. stock FOV opt-in은 아직 구현하지 않았다. 현0.5 floor·중간거리 우선순위·중요 대상·원래 정책 복원/연결별 공정성이 확보돼야 하며, ambient 도입만으로 동일 중요200개의 약800ms 공급이 해결된다고 보지 않는다. 다음은 실제 송신 예산·공급 경계와 작은 실제 소비 연결을 우선하고 전역 시야 정책/순수 튜닝의 성급한 기본 채택은 피한다. 전체 목표·기존 약60%/잔여40% 추정은 유지한다.

### 49차 검증 산출물 보존 범위와 실행 저장 경로

사용자의 새 정리 요청에 따라 빌드/UE/검증 프로세스 부재와 정확한 생성 경로부터 확인했다. Saved 약134.5GiB 중 검증 약109.6GiB·연속 검증 약73.7GiB였고, 같은 Cook의 이전 고유 Stage가1.2–3.1GiB씩 누적됐다. 이전 패키지19개·Cook2–13과 그 격리 캐시만1,169파일/논리59.585GiB를 정리했다. source1,197/runtime38·Git index는 전후 동일하고 에셋·Git/LFS·현재 Intermediate/Binaries/DDC와 제작/사용자 자료는 보존했다. 원래 로그/인자/결과·CSV·실패trace와 Stage3/21/22/24/Shipping1·Cook14/15는 유지한다. 퇴역한 부분 디렉터리는 실행/완전한 Cook 재사용 근거로 읽지 않도록 receipt를 남겼다.

표본 컨테이너의 Documents 외부 `.tmp.driveupload` 하드링크 때문에 초기에는 실제 공간이 거의 반환되지 않았다. 외부 파일·프로세스·설정은 건드리지 않았고 이후 관측 링크가 해제되며 C 여유87,723,581,440→151,861,768,192bytes로약59.733GiB 증가했다. 논리 크기와 실제 여유 관측을 구분하며 이 수치는 다른 디스크 활동도 포함한다. [정리 근거](../../Maintenance/Storage_Cleanup_2026-10-29.md)에 보존 이유와 삭제 경로·SHA를 둔다.

기존 Tests/Packaging 두 runner와 작은 공유 path helper를 보강했다. 큰 UserDir/Cooked/Package는 영속 OS User TEMP(없으면 Machine)의 checkout SHA/실행별 경로로 분리하고 Saved 로그·process·리포트는 유지한다. 호출별 sandbox TEMP를 다음 Stage의 Cook root로 재사용하지 않는다. 실제 process.json 경로를 소비하고 경로 이탈·reparse·중복 실행 덮기·퇴역 Cook 재사용을 거절한다. 현재 정확한 Cook14/15 재사용은 허용한다. 새 캐시 서비스/자동삭제/Drive 설정 변경·vendor 수정은 없다.

AST3개와 실제 legacy Cook/퇴역 Cook 경로, 별도 호출의 stable root·중복/이탈을 확인했다. 기존 Editor125의 `Batch49StorageNative1`은18개·경고0·7.492초/actual exit0·source/runtime 불변이며, 실제 Temp UserDir15파일/166,450,696bytes와 Saved 실행 근거를 확인했다. 대표 파일의 관측 하드링크는 Temp 자기 경로 하나였다. 이는 생성물의 구조적 격리·회피이며 외부 동기화 영구 수정이나 장기 안정성을 보장하지 않는다. C++ 변경/새 UBT/전체 Cook/Stage는 없고 다음 필요한 배포에서 새 packageDirectory의 실제 소비까지 확인한다.

정리 뒤 전체 목표를 계속한다.48차 queued goal의 현재 Game/Shipping 배포·원격 공급/송신 예산과 실제 Iris 소비·Nav/GPU/HLOD·다중/고속 준비/취소·미정 운영 서비스·혼합 수용량은 남는다. FOV는 검토 후보이며 성급한 전역 적용이나 동일 중요200명의 공급 완치로 세지 않는다. 약60%/잔여40%의 기존 작업량 추정도 유지한다.

### 50차 실제 송신 자원과 현재 Game 배포

기존46/48차 자료와 설치 소스를 재사용했다. UPlayer 광고·client/server IpNetDriver cap이 모두100000B/s이며 광고만 높이거나server cap만 높여서는 실제 협상값이 바뀌지 않을 수 있다. Full DataStream은 credit와 normal batch cap3을 함께 쓰고 PostTickDispatch·huge/OOB/control은 다른 경계다. 긴 frame의 credit는DesiredTickRate로clamp되므로설정B/s가실제송신량/갱신주기를보장하지않는다. activation이꺼졌다는이유로NPC의다른이동/피격/퀘스트/동료소비까지저율화하거나dormancy로묶을근거도없어추가writer를넣지않았다.

기존 Measure-Network에 명시 opt-in B/s와 normal batch packet허용량을 추가했다. 기본값은override없음이고 운영 Config를바꾸지않는다. scalar INI로광고/양쪽cap을맞추고 서버Console만batch cap을설정한다. 서버부모2·client완료2·selected parent양경계2의actual을확인하며불일치/대역폭throttle우회는비교실패다. C++는fixture의read-only budget/client connection 관측이고RPC/wire/protocol8은유지한다. 큰UserDir은Temp로옮기고finalized CSV와capture main/actors/summary전체만bounded hash검증후기존Saved근거경로에보존한다.

최초 manifest의정수키직렬화오류는게임시작전실패로보존했다. 이어b기준run은actual0×3·CSV/예산관측통과였지만이동raw의후처리경로누락으로원래strict false였다. 원자료와false를보존하고offline pass치환/동등성예외를만들지않았다. 수집closure를고친c두실행만공식비교다. Editor126 직접UBT4actions·native23.31초/exit0이며1198source/script·39runtime(host포함)·에셋5개/정규화argv가두c실행간동일했다.

| 실제 동일조건 비교 | 100000B/s·normal cap3 | 300000B/s·normal cap16 |
| --- | ---: | ---: |
| 고정NPC3개 각각의간격P50 범위ms | 729.94–733.25 | 217.05–218.55 |
| 각각P95 범위ms | 765.14–790.09 | 250.33–253.09 |
| 각각최대간격 범위ms | 775.10–811.34 | 269.54–317.34 |
| 같은parent Flush B/s | 96,882.81 | 294,147.23 |
| 같은parent Flush packets/s | 103.09 | 326.98 |
| 실제connection tick Hz | 29.642 | 29.604 |
| server FrameTime P50/P95 ms | 33.46/36.25 | 33.43/35.93 |
| nativewrite GT scope P50/P95 ms | 0.249/0.334 | 0.429/0.553 |
| client phase4 critical P50/P95 ms | 34.23/36.16 | 35.38/36.76 |

NPC quantile을평균하지않았다. 간격은decode/apply관측이고packet latency가아니며,Flush는전체connection accounting이다. 서버GTaggregate열은없어NA로두고scope를합산하지않았다. 두원래strict·CSV·endpoint·exit0×3/timeout0·역할1AP+199SP/200표시·의상복원200/local품질을유지했다. 이공유자원변경은공급개선을뒷받침하지만bytes단독인과/코드효율개선·200CCU·다른연결전체기아·손실/장기메모리/최종SLA를증명하지않는다. 한쌍의결과이며CPUwrite/client비용과약250–317ms tail도남는다. 따라서운영기본100k/3·NP codec0/추가ACK class OFF와cloth기본권한false/cap0은유지한다.

Game33 직접UBT·기존Cook15 재사용 Temp Stage25가통과했고실제구성exe SHA가일치했다. Stage첫호출에서기존상대Cook경로의호환누락을실행전guard로발견해정규화후같은허용범위검사를하도록보강했다. 실제새Temp packageDirectory를소비한 `ArchitectureBatch50BudgetDefaultCooked1`은listen+remote2·200Avatar의기본네트워크100k/3/우회0·원래strict/CSV/endpoint·actual0×3/timeout0·의상복원200/source-runtime/선택에셋불변을확인했다. 새fullCook/Shipping/Server target이나MainWorld/GPU/HLOD검증으로확대하지않는다.48차runtime/private queue변경과49차Temp Stage 연결의현재Game배포기준점이며,이통합범위외전체MMORPG완료는아니다.

원자료/reader recipe는Saved/ArchitectureBatch50NetworkBudgetAnalysis에로컬보존하고핵심결과·SHA/배포는기존검증요약에둔다. 다음은기존MainWorld owner준비의겹침·취소/교체·독립진행/source회수와기존필수잔여다. 사용자추가요청에따른남은사용량40%에서의안전마감/goal pause/정상PC종료조건은 [현재문맥](../../Overview/ProjectContext.md)을따른다. 보고직전사용31%/남은69%로아직기준전이다.

### 51차 실제 두 소유자의 준비 겹침·한 Avatar 교체

기존 A10 single-owner/MainWorld/HLOD fixture와 production Portal·준비 component를 재사용했다. non-Shipping의 추가 명시 opt-in에 고정 두 row만 두고, 실제 client의 CharacterId를 실제 서로 다른 connection/PC/PS/Pawn과 대조한다. 정상 BP GameMode의 실제 TryInteract/RPC·WP source·ACK·floor/capsule·native Mover effect를 소비하며, 위치/Role/ASC 강제 대입·ACK/tick 지연·추가 RPC/manager는 없다. 기존 single-owner/default/HLOD 경로는 유지했다.

`ArchitectureBatch51OwnerOverlap1`은 무지연에서 양 authority가 Preparing이고 양 client가 물리 도착 전 목적지 source를 관측한 뒤 한 owner의 public UnPossess→old Pawn 정상 Destroy를 수행했다. production 요청의 Failed/임시 목적지 광고 해제를 기다린 뒤 public GameMode RestartPlayer가 새 실제 Pawn을 생성했다. PC/PS/CharacterId/connection은 유지하고 server-local ASC epoch2→3, 해당 client-local3→5와 새 avatar를 확인했다. 두 프로세스의 epoch 절대값이나 UObject 이름은 서로 비교하지 않는다. survivor는 원래 Pawn/epoch로 native Arrived·S01 floor/capsule 도착했고 old 교체 요청은 도착하지 않았다.

양 client는 결합 authority terminal까지 자기 역할 phase와 정상 predictor를 재확인했다. 추가 source는 각각 정상 source로 돌아갔고 기본 PC source도 유지했다. 실제 engine-consumed source가 같은 partition-local 2D/3D hash·shape·target policy였으며 raw Z 차98cm/0cm는 UE400cm 위치 양자화와 구분했다. 각 client의 physical 위치는 같은 identity의 authority 위치와 capsule42cm 안에서 일치했다. server row에 관측하지 않은 client 기본 source 주장을 넣지 않았고 final disconnect 전 값 snapshot과 weak 수명을 유지했다.

직접 Editor127은4actions·native18.61초/actual exit0, 기존 World 준비/clock4개는 경고0·0.106초/actual exit0이었다. 실제 server/client2는 원래 strict passed·actual0×3·강제 종료0,1198 source/script·39 runtime(host포함)·보호5에셋이 전후 같았다. 원본 input/최종 patch/runner SHA와 모든 args·역할별 phase/receipt·실제 Temp UserDir 경로를 Saved에 남겼다. 양 request30초·fixture180초·terminal35초의 기존 한계를 유지하며 실패를 pass로 치환하지 않았다.

교체 직후 서버에는 대상/function이 없는 RPC를 거절한 `LogIrisRpc Error`2줄도 있다. 직전 기록의 old Pawn root는Id26이고 오류는Id40으로 원문에서 대상/함수 매핑이 없다. Client2의 교체 이전 NP interpolation fell-behind 경고1도 보존한다. 역할 proof/정상 종료와 구분하며 retired Pawn 자체의 in-flight RPC·로그 전체 clean·무해함·원인 수정 완료를 주장하지 않는다. 현재 phase/source/identity·실제 도착 gate를 약화하거나 엔진 로그를 억제하지 않는다.

이는 기존 취소/교체/독립 source 수명의 실제 통합 근거다. production Portal/Streaming/Mover/ASC나 정책·설정·에셋 변경, 새 성능 개선률·200CCU·동시 고속 이동/합산 I/O 예산·Nav/GPU/HLOD 준비·운영 respawn 서비스 완료를 주장하지 않는다. 이미 queued된 effect의 incarnation/late outcome은42차 집중 근거와 구분한다.51차는 명시 fixture만 달라 현재 production Game33/Stage25를 새 Cook/Shipping으로 반복하지 않았다. 전체 추정과 미정 서비스·원격 공급/손실·혼합 수용량 잔여는 유지하며 다음은 기존 실제 소비자의 작은 자원·수명 개선을 묶는다.

### 52차 NPC 제작 입력과 기존 네트워크 writer 경계

기존50차의 작은 입력 누락을 재사용했다. Action의 finite/bounds·Decision의 기존 cadence 제한·Spatial 후보 상한과 Companion/Encounter validation은 이미 있어 새 scheduler/주기 정책을 추가하지 않았다. NPCCharacter의 BeginPlay 자동 network tuple만 metadata 이외의 검증 없이 Actor setter→현재 Iris poll 경계에 전달될 수 있었다. 실제 잘못된 NPC 에셋/공급 피해나 무한 작업은 관측하지 않았다.

NPCCharacter의 private predicate 하나를 기존 runtime 적용과 WITH_EDITOR IsDataValid가 공유한다. cull은 finite/0이상·double square가 float에 표현 가능, frequency/min은 기존 metadata대로 finite/1이상·min≤update를 요구한다. 새 authoring 최대 Hz/거리 cap은 없다. valid tuple은 기존 float square와 setter 그대로이며 invalid는 세 network setter를 함께 생략해 현재 actor 값을 보존한다. 다른 writer의 기존 값까지 sanitize하거나 fallback Hz를 소유하지 않는다. 독립적인 mesh URO/visibility 적용은 계속한다. editor는 Super 결과를 유지하고 자동 정책 opt-out의 미소비 필드로 새 오류를 만들지 않는다.

production은 header/cpp2개이며 새 public setter/manager/RPC/반영 필드·asset/Config 변경은 없다. 기존 SpatialImportance 회귀 안에서 실제 setter의 valid17/3Hz·123.5cm 보존, NaN/Infinity/zero-frequency/min 역전/float-square overflow의 tuple 보존, mesh 독립, 기존 거리0/min1과 editor enabled 거절·opt-out을 확인했다. 직접 Editor128은62actions·native71.74초/actual exit0, 기존 NPCDecision6개는 경고0·0.373초/actual exit0이다. source는 빌드 중, source/runtime은 native 중 불변이며51차 종료 뒤 보호5에셋도 같다. actor의 IsDataValid 직접 실행 근거이며 기존 모든 BP/맵의 제작 validation을 전수 실행한 결과가 아니다.

Saved wrapper의 마지막 expected count를5로 적은 집계 오류도 보존한다. 다른 module의 기존 RegistryChurnFairness를 포함한 실제6개 등록·개별 Success·native exit0을 확인했다. outer wrapper exit1과 native 성공을 구분하고 원래 wrapper/report를 덮어쓰거나 엔진 검증을 반복하지 않았다.

별도 좁은 A07 검토는31/46–50차 자료와 현재 owner-only FastArray→PlayerUI→HUD 및 private component scope를 재사용했다. 기존8행 요약은 remote Actor/ASC를 필요로 하지 않아 전역 relevance 확대·새 관계 manager를 추가하지 않았다. 현재 Combat/Distance의 관계 계산을 Iris runtime 등록 완료로 세지 않는다. tombstone도 backend retention/watermark 계약 없이 TTL 삭제하지 않는다. 향후 필요한 관계별 실제 consumer와 장기 퇴역은 남는다.

이는 제작/실행 입력 경계의 구조 보강이며51차 unknown RPC40의 원인 수정·새 송신/CPU/FPS 개선이나 대규모 수용량 증명이 아니다. 현 network/codec/ACK/cloth 기본과 실제 Game33/Stage25 근거를 유지한다.52차 runtime의 다음 Game 배포는 필요한 호환 변경과 묶고 새 Cook/Shipping을 반복하지 않았다. source-engine Server·provider/auth/DB·원격 손실/기아/메모리·Nav/GPU/HLOD/고속/합산예산·혼합부하 및 전체 약60%/잔여40% 추정은 유지한다.

### 53차 교체 후 RPC 대상의 같은 실행 귀속

51차 오류의 Handle 대상이 없어 기존 엔진 단일 `LogIris Verbose` category만 추가해 현재 Editor128에서 실제 같은 lifecycle case를 한 번 실행했다. Source/Config/asset·바이너리·정책·원래 strict는 바꾸지 않았고 새 빌드/Cook/trace 수집은 없다. `ArchitectureBatch53OwnerLifecycle1`은 실제 전용 서버+client2의 strict/actual0×3·정상 종료·source/runtime/보호5에셋 불변이다. verbose 관측 조건이므로 새 성능 비교로 사용하지 않는다.

이 실행의 교체 대상은 slot2 old BP_Greatsword_C_1/root64다. 초기 BackendLiaisonComponent78 등록을 해당 root와 직접 연결했고57.714의 동일 component detach 뒤57.745/.779에 그 Handle을 대상으로 RPC2건이 거절됐다(Error4줄). 새 Pawn C_2/root88의 liaison102는57.788에 별도 등록됐다. 따라서 **이 실행에서 퇴역 Mover liaison 대상 거절이라는 귀속은 확정**되며, 함수명은 deserialize에서 복호화되지 않았다. NetworkPredictionComponent가 선언한 ServerReceiveClientInput과의 연결은 소스 기반 추론으로 구분한다.51차Id40에53차Id40을 대입하지 않는다.53차Id40은 survivor의 liaison이며 서로 다른 실행의 숫자는 identity가 아니다.

원래 독립 도착/교체·epoch·source 회수 proof는 유지됐지만 이 선택 case만으로 모든 늦은 입력의 무해성·네트워크 품질·로그 전체 clean/원인 수정 완료를 주장하지 않는다. old 대상이 없는 엔진 거절 경로를 억제하거나 Pawn 보존 timer·강제 reset·추가 ACK·설치 엔진 수정으로 가리지 않았다. 원자료/같은-run 매핑·독립 검토를 Saved에 보존한다. 현재52차 입력 보강과 이 관측은 서로 다른 책임이다. 전체 목표/추정과 손실·재접속/혼합부하·서비스·Nav/GPU/HLOD/합산 준비 및 다음 필요한 Game 배포 잔여는 유지한다.

### 54차 합산 도착 준비의 실제 GT 실행 예산

기존 component별10Hz 준비 Tick에는 world 합산 readiness·floor/capsule·commit work 상한이 없었다. 실제 소비가 있는 작은 WorldArrivalSubsystem 하나로 이 body를 이주하고 개별 Tick writer를 제거했다. source 광고는 계속 component/WP가 소비하며 WorldState snapshot/EventRegistry의 다른 책임은 확장하지 않았다. Server/client 모두 현재 world의 subsystem에 요청 lease를 등록하고 등록 실패·완료·취소·EndPlay/OnUnregister에서 해제한다. 새 RPC/설정/에셋·일반 task framework는 없다.

GT weak component+request+단조 token의 공정 ring에 logical256 backpressure, 실제 GFrameCounter별 visits64/Advance4·누적 soft0.5ms와0.1초 next-due를 적용했다. cursor/due와 비용은 callback 전에 반영하고 old entry를 callback 뒤 다시 쓰지 않는다. 새 admission은 frame frontier 밖에 두고 취소/재등록·동일 frame 재호출로 비용을 환급하지 않는다. Stop은 admissions와 ring을 먼저 분리한 뒤 일치하는 lease만 정리한다. 같은 world/등록/non-template/request/token 소진을 확인하고 거절·지연·예산/peak를 읽을 수 있다. 수치는 초기 보호 정책이며 최종 수용량/SLA가 아니다.

원래 component의 current binding→known native outcome→deadline 순서와30초 원래 deadline·client ACK0.5초·source/ASC epoch/큐 effect fence는 유지한다. TickableWorldSubsystem은 actor tick 뒤이므로 다음 native simulation frame에 queue가 소비될 수 있다. 강제 transform/current-frame 성공을 추가하지 않았다. soft 시간은 시작된 엔진 검사를 preempt하지 못하고, source gathering/ACK 검증/WP 비동기 I/O·엔진 task reservation 전체를 제한하는 구조도 아니다. logical slot 해제를 물리 load 완료나 I/O 취소로 설명하지 않는다.

직접 Editor129은11actions·native16.98초/actual exit0, 기존 World4+새 PreparedArrivalExecutionBudget는 경고0·0.101초/actual exit0이었다. 기존3개 직접 Tick 시험은 private Advance의 native recipient/outcome 경계로 이주했다. 새 시험은 실제 Game world/subsystem에 invalid-recipient record256개를 등록해 logical backpressure와 동일 실제 frame 반복 호출/credit 불환급·old token/request fence·Stop을 확인한다. WP 성공·frame/clock·역할을 위조하지 않았으며 실제 이동/모든 callback·장기 fairness 검증과 구분한다.

현재 binary의 `ArchitectureBatch54OwnerOverlap1`은 원래 정상 MainWorld BP GameMode·실제 dedicated+client2의 strict를 유지했다. 양 Preparing/목적지 source, 한 Pawn 정상 퇴역/Failed/RestartPlayer 새 incarnation, survivor native 도착·양 client 기본 source/정상 predictor 복원과 실제 role별 위치가 통과했다. actual0×3·강제 종료0, source/runtime/보호5에셋은 전후 동일하다. 새 writer 도달의 실제 근거이며256명/고속·원경/GPU/Nav·새 CPU/FPS 개선률은 아니다. runtime 로그의 오류 원문은 별도 Saved 관측에 보존하고53차 숫자 handle을 교차 대입하지 않는다.

독립5파일 검토와 원본 입력/최종 patch SHA를 보존했다. source-engine Server·공급자/auth/DB·전체 손실/기아·혼합부하·합산 I/O/메모리와 다음 Game 배포는 남는다. 전체 약60%/잔여40% 추정은 유지한다. 다음은52/54의 필요한 공통 Game 통합과 기존 미완료 실행 경계를 묶으며 새 전수 감사나 작은 측정을 반복하지 않는다.

### 55차 현재 Game과 실제 MainWorld 공통 배포

52차 NPC authored 입력과54차 shared arrival 실행을 현재 Game에 함께 통합했다. 직접 Game34는43actions·native50.68초/actual exit0이며 source는 전후 같다. runtime 지문은 직접 Game exe 하나만 바뀌었고 target receipt/Editor129 등 나머지는 유지됐다. 기존 Cook15는 명시 Entry/ThirdPerson2맵이므로 현재 MainWorld closure의 근거로 확대하지 않았다. 반영 cloth permission 전의 오래된 full MainWorld Cook14도 새 기준으로 무조건 재사용하지 않고 현재 소스/기존5맵을 명시한 Cook16을 격리 Temp에 만들었다. 새 전체 프로젝트 전수 Cook은 아니다.

Cook16과 Stage26은 정상 exit0·source/runtime 불변이다. 실제 process.json의 output/packageDirectory를 소비했고 staged configuration exe SHA가 직접 Game34와 같다. source asset/Config/프로토콜을 바꾸거나 이전 Cook/Stage를 덮어쓰지 않았다. 큰 Cook/User/Package는 checkout SHA/고유 실행의 기존 Temp helper로 분리하며 Saved에는 인자·로그·process·소스 지문을 유지한다.

`ArchitectureBatch55MainWorldCooked1`은 그 실제 구성 exe로 정상 MainWorld BP GameMode/PC/Avatar를 실행했다. prototype identity 조건의 정상 TryInteract→production Portal/준비 source→native Mover outcome→S01 floor/capsule 물리 도착이 Preparing1/Queued2/Arrived4로 끝났다. S00(-93200,-93200,2348.75)에서 S01(6800,16800,7897.999)으로 도착하고 추가 source의 정상 predictor 광고가 실제 Pawn 위치로 복원됐다. 실제 native exit0·강제 종료0·source/runtime/보호5에셋 불변이고 native runtime Error/Ensure/Fatal 관측은 없었다. 이는 실제 packaged standalone/NullRHI의 현재 writer 소비 증거이며,54차 Editor dedicated+2owner의 별도 수명/소스 hash proof와 구분한다.

이번 배포는 새로운 임의 콘텐츠·socket capacity·GPU/Nav readiness/HLOD 연속 픽셀·고속/합산 I/O·새 CPU/FPS 개선률을 증명하지 않는다. 전체 원격 손실/기아·장기 메모리/공정성·미정 auth/DB/provider/Server/운영과47차 뒤 최신 Shipping은 남는다. 전체 약60%/잔여40% 추정은 유지한다. 이후 작은 검사를 반복하기보다 현재 실제 provider/전송 소비 계약 등 기존 잔여의 독립 구현 가능 범위를 먼저 재사용하고, 환경 결정을 대신하는 빈 service/framework는 추가하지 않는다.

### 56차 기존 Gateway timer의 GI 수명과 준비 API

기존 UEHTTP·streamed byte cap·timeout/once-only/cancel은 이미 있어 새 전송 adapter/endpoint를 만들지 않았다. local profile/reward는 SQLite·기존 receipt 경로이며 외부 domain DTO/auth/route는 여전히 미정이다. Source/Plugins에서 UE_LOG_REMOTE 제품 호출을 확인하지 못했으므로 준비된 macro→Gateway API를 production 로그 전송 완료로 세지 않는다.

기존 Initialize의 world-dependent 반복 timer를 제거하고 accepted GT enqueue가 기존 GI manager에 one-shot을 예약한다. 추가 enqueue는 첫 deadline을 밀지 않고 idle/in-flight에는 timer가 없으며 완료 후 필요한 bounded queue/retry만 다음 interval을 예약한다. 종료는 member tracker를 먼저 철회하고 old tracker close→GI timer clear→CancelRequest 순서를 사용해 no-world 종료와 valid-world retired API의 재부활을 막는다. 원격 분기는 GT에서만 world를 읽고 로컬 UE_LOG는 유지한다. 표준 UE_LOG token 문법의 ELogVerbosity::Verbosity로 매크로를 바로잡고 기존 native의 local Display 호출로 compile/실행했다. 새 worker 원격 큐·caller·서비스·설정/에셋은 없다.

Editor130(9actions/native35.77초)은 성공했지만 Native1은 새 AdmissionAndShutdown의 context 복원에서 exit3/report없음으로 실패했다. Engine OnWorldChanged는 SetCurrentWorld(nullptr)에서 GI.WorldContext도 비우는데 시험이 그 pointer를 다시 꺼낸 것이 원인이었다. engine-owned context를 detach 전에 보관해 복원/cleanup하는 test-only 수정을 하고 Editor131·Native2를 마감했다. 이 실패는 생산 코드의 root-cause fix라 부르지 않으며 원문/입력·repair를 보존한다. 이후 공개 macro compile 사례를 묶은 Editor132(native4.14초)/Native3도 actual exit0이다.

Native2/3은 AdmissionAndShutdown·GatewayBounds·GatewayRealHTTP 세 개 모두 Success이고 각 source/runtime은 전후 동일하다. HTTP oversized-response sink가 의도적으로 receive를 거절해 RealHTTP에 libcurl23/info warning18줄이 있으며2clean+warning사례1개로 기록한다. 이를 숨기거나 전체 warning0로 보고하지 않는다. no-world/late-world·first timer/재큐·shutdown 재admission은 실제 GI 공개 API와 preflight로 확인하며 외부 요청/clock·frame 위조는 없다. HTTP fixture는 실제 localhost이며 production HTTPS/auth·travel/PIE 반복·worker delivery 인증과 구분한다. 보호5에셋은55차부터 같다.

56의 Game/Shipping은 다음 호환 변경과 묶고 현재 Game34/Cook16/Stage26 배포를 유지한다. 병렬57 분석은 정상 HealthPotion 새행456개의 item-array 길이 하한만65,557B로64KiB보다 큼을 확인했다. 이는 실제 codec 최초실패 측정이나4096슬롯 보장이 아니며 persistent Proposed는 precommit 실패해 원본을 보존한다. Gather의 기존 스택 활용 후보와 legacy checkpoint/수동split/claimed128 이력·전체 byte-budget 경계는 구분하고 임의 capacity/DB/schema를 정하지 않는다. 전체 목표/약60% 추정과 사용자사용량40%중단조건은유지한다.

### 57차 실제 채집의 호환 스택과 원자적 결과

정상 HealthPotion 반복 채집은 매번 새 GUID 행을 만들었고, 짧은 실제 ID의 item-array456행만65,557B라는 문법 길이 하한으로 전체64KiB가 reader4096 방어상한보다 먼저 막힘을 확인했다. 실제 codec의 최초 실패 revision/455행 성공이나4096 가방 슬롯 보장으로 바꾸지 않는다. persistent Proposed 실패는 원래 저장을 지키며, legacy live→checkpoint 실패는 별도 fault/reload 경계다.

Gather만 기존 MaxStack/같은 definition·version·level/bag·slotNone/비잠금 계약으로 빈 stack을 채운다. 명시 Add와 Equipment/Companion/Mount identity형은 제외한다. 기존 capture가 GT 잠금 집합을 같이 제공하며 새 worker 가변 상태/lock manager를 만들지 않는다. incoming Amount≤MaxStack라 잔여는 최대 한 행이고 원래 server-issued Edit.Instance·유한 NextOrder를 쓴다. full merge에는 새 order가 필요 없다. quest는 총 Amount를 같은 Proposed에1회 적용하며 worker/receipt/CAS/durable cooldown·unknown/late owner와 Add/OnItemAdded 계약은 유지한다. API/형식/64KiB·에셋/DB 선택은 바꾸지 않았다.

기존 ExclusiveDurableLifecycle은 실제 canonical HealthPotion의 현재 MaxStack99를 읽고, 두 채집 뒤 재접속한 한 stack2·full merge 기존GUID/Changed1·partial 잔여GUID/Added1+Changed1·level불일치·명시Add 새identity를 확인했다. lost ACK 뒤 동일 receipt/revision·총수량/quest delta는 한 번이고, partial 계획의 쓰기 실패는 원래 수량/GUID/quest/events/revision을 보존했다. 기존 dynamic receiver를 재사용하고 중복 callback latch를 매번 재무장해 숨기지 않는다.

Editor133(5actions/native8.46초)은 성공했고 Native1도 기능 Success/actual exit0이었으나 post-tick actor TeleportTo로 Mover 불일치 경고2줄이 있었다. fixture에 정상 WorldStatic 지면을 만들고 불필요한 post-tick 위치 reset을 공간 검사로 교체했다. root의 최초 fixture 수정은 private IsSpatiallyAvailable 호출로 Editor134 C2248을 냈으며, 공개 IProject_JInteractable 계약으로 고쳤다. 원문/Native1·실패 빌드를 보존하고 생산 Gather/Mover/logging 코드는 바꾸지 않았다. Editor135(native6.36초)/Native3은1success·경고0·약0.338초/actual exit0과 source/runtime/보호5에셋 불변이다.

이는 실제 획득의 행 증가를 줄이고 동일 commit/소비 계약을 확인한 변경이다. 새 CPU/FPS 개선률·전체 payload/가방 수용량 해결이 아니며, split/비스택/다양한 정의·claimed128 이력·legacy checkpoint는 여전히 byte budget을 소모한다. 자동 병합으로 모두 해결됐다거나 임의 64KiB 상향·claimed 삭제를 하지 않는다.56/57은 다음 공통 Game 배포에서 실제 기존 Gather/Craft/HUD 소비를 재사용하고 현재55차 Game34/Cook16/Stage26과 구분한다. 전체 목표·미정 provider/auth/DB/Server·혼합 부하/원경 준비와 약60%/잔여40% 추정은 유지한다.

### 58차 저장·전송 수명 개선의 공통 Game 배포와 실제 소비

56 Gateway의 GI one-shot/종료 경계와57 Gather의 호환 스택을 Game35 직접 UBT(정상 exit0, native26.50초)와 Stage27에 묶었다. 반영 속성·직렬화 데이터·에셋 변경이 없어55차의 명시5맵 Cook16을 재사용했다. 실제 패키지 exe SHA는 직접 Game 바이너리와 같고 기존 Stage26과 구분한다. 새 Cook/Shipping/Server 성공으로 세지 않는다.

실제 Development 패키지를 격리 UserDir/RHI offscreen960×540으로 실행해 정상 Mover·TryInteract의 채집2회→제작1회→owner journal→HUD claim을 검증했다. stack 행 수가 아닌 Health 수량1→2→0/Mana1, AND 목표2/2·1/1, XP+7/revision+4의 기존 완료 조건을 사용했다. phase15/success1/compound_quest1, actual exit0, 경고·오류0이며 source/runtime/보호7에셋이 전후 동일하다. 서버/인증/DB 공급자·이미지/GPU 준비·연속 시각 품질·실제 Nav·CCU/FPS를 인증하지 않는다.

현재 배포 기준은 Game35/Cook16/Stage27이고 Editor135와 Shipping47의 이전 소스를 구분한다. 기존56 실제 localhost/native와57 저장 재개·실패 원자성 근거를 재사용한다. 전체64KiB 용량과 legacy live→checkpoint, 자동 objective의 command-busy 처리·claimed128 이력은 이 통합으로 해결되지 않는다. 전체 목표와 약60%/잔여40% 추정 및 사용량 임계점 중단 조건은 유지한다.

### 59차 퀘스트 수락·추적의 확정 상태 게시

legacy Accept와 실제 HUD Track→RequestTracking→server RPC도 기존 persistent 명령 경로를 사용한다. 기존 AcceptCompoundQuest 값13·ProfileEdit.13·intent 순서를 보존해 순수 PlanAcceptance의 legacy 지원을 재사용했고 SetQuestTracking14만 끝에 추가했다. Tracking은 기존 Quest/Amount의 정확0·1로 desired 상태를 Proposed에만 계획한다. 무영속/초기 binding 전 동기 동작, 권위·quota·unclaimed/changed/no-op 규칙과 owner-only UI 계약은 유지한다.

GT JSON preflight·새 서비스/queue/schema는 없다. 기존 worker의 bounded Before/Proposed encode·receipt 재조회/unknown 재시도·전체 상태 설치 후 게시·owner/session fence를 재사용한다. pending/거절/저장 실패는 UI live 값을 미리 바꾸지 않으며 반복 수락/동일 추적은 새 명령을 내지 않는다. 자동 RecordObjective/SampleTravel 함수는 원문 그대로이고 command-busy event 유실과 live→checkpoint 경계는 미완료다. Accept bool은 영속일 때 admission이며 OnChanged/확정 States 뒤에 progress를 소비한다. 기존 C++ 생활/runtime 호출은 대기하며 BP 제작 전체의 즉시 이벤트 연결은 전수 확인하지 않았다.

Editor136 직접 UBT102actions/native119.03초·actual exit0, 기존 ProfileCodec/LocalProfileLifecycle/RuntimeFrequencyAndProgression/IndexedTravelExecution/QuestRetirement/QuestRewardRollback/QuestRewardReplayAndRestore7개가 Success·경고0·actual exit0이다. source/runtime과 보호7에셋을 보존했다. lifecycle은 실제 legacy 수락·tracking의 pending live 불변→commit 게시·반복/no-op 뒤 기존 progress/claim/reopen을 연결한다. codec은 허용된 이름/고유 ID/정상 count의 합성 DTO로 정확65536B를 만들고 tracked true→false의1B 증가 거절과 원본 bytes 보존을 확인한다. 이는 canonical 생산 정의나 실제 near-capacity DB 보증이 아니다.

현재 패키지는58차 Game35/Cook16/Stage27이며59를 아직 포함하지 않는다. 전체 목표와 약60% 추정은 유지하고, 자동 누적 진척·퇴장 전 미확정 delta·payload/claimed 이력·실제 온라인 provider/손실/수용량과 표현/월드 준비를 계속 다룬다.

### 60차 상태 효과의 중복 HUD 게시 제거

실제 StatusEffectModel→PlayerUIComponent→PlayerHUDWidget 경로는 infinite UI 효과의 동일 값도0.25초마다 게시해 최대12아이콘의 색/텍스트/tooltip 갱신을 반복했다. Refresh의 기존 스캔/정렬/타이머·States capacity를 유지하면서 함수 지역 inline12 이전 값과 Handle·Name/Description·soft Icon·Stack·정확 Remaining/Duration·debuff·priority를 비교해 같을 때만 알림을 생략한다. epsilon/잘림·새 persistent cache/hash/manager는 없다.12는 지역 inline capacity이고 초과 상태는 정상 확장한다. 값 복사 비용과12초과 추가 allocation이 남으며 새 CPU/FPS 개선률은 측정하지 않았다.

명시 Bind/Unbind reset·ASC 교체·수명 guard는 유지한다. HUD 생성은 새 model을 만들고 Bind의 reset/현재 값 게시를 받아 기존 동일-ASC no-op와 충돌하지 않는다. 아이콘은 기존 SkillButton의 generation/owner 검증 비동기 완료가 직접 갱신하므로 주기적인 동일 model 알림에 의존하지 않는다. 실제 GAS의 유한 countdown·정상 stacking·inhibition 복귀·expiry·표시 metadata 변경과 retired source silence를 기존 StatusEffectLifetime에서 확인했다. 기존 StatusBindingReentry의 마지막 GC 사례는 빈 Refresh의 무조건 발행 대신 실제 UI GE 적용으로 바꿔 같은 model 수명 보장을 검증한다.

Editor137은 시험의 public SetStackingType 선언이 설치 엔진에서 export되지 않아 LNK2019/LNK1120으로 실패했다. 시험용 transient 정의의 기존 EditDefaultsOnly enum을 FProperty로 설정하고 exported GetStackingType을 확인해 Editor138이 성공했다. Native1은 실제 Apply→model callback 강제GC 중 GameplayAbilities AV/exit3·report 없음으로 실패했다. transient GE definition과 실제 ASC를 strong pin해 같은 trigger/기존 weak-model 수명 assertions를 유지한 Editor139/Native2는 두 Success·경고0·약0.095초/actual exit0이다. source/runtime/보호7에셋이 같다. 원문 실패는 보존하며 AV의 정확한 회수 객체는 분리 진단하지 않아 엔진/제품 원인수정으로 부르지 않는다. 생산17줄은 후보와 동일하다.

실제 authored status metadata 범위와 전체 CPU/GPU 이득은 미확인이다. Niagara의 중요 경고 분류·실측 없이 총량 cap을 임의 도입하지 않았다. 현재 패키지는58차이며59/60은 다음 호환 통합 대상이다. 자동 진척 검토는 native writer/정상 shutdown drain·receipt와 GT revision 순서를 닫아야 하므로 구현 완료로 세지 않는다. 전체 목표/약60% 추정과 사용량 중단 조건은 유지한다.

### 61차 자동 진척의 native 소유와 일시정지 체크포인트

현재 묶음의 구현·직접 연결된 검증·배포를 마감했다. **사용자 요청에 따른 다음 동작은 목표 일시정지이며 전체 목표 완료가 아니다.40% 기준과 PC 종료는 취소됐고 종료/예약을 수행하지 않는다.** 재개 전에는 이 기록과 실제 goal·프로세스·소스를 확인한다.

기존 QuestComponent는 실제 수락된 unclaimed quest/objective의 canonical counter 입력을 모은다. native Work는 최대128×8 슬롯의 desired/confirmed 목표를 합치고, primary와 같은 reservation/job에서 한 번 동결한 tail을 확정한다. raw event FIFO·새 전역 scheduler/서비스/테이블은 없다. native current 또는 idle gate 획득 때만 변경값을 수락하며, tail 닫힘/포화는 bool false다. Travel 정수는 수락 뒤에만 빼고 fractional/pawn/warp 규칙은 유지한다. 정상 종료는 GT 후속 job 없이 수락분을 drain하고, idle 입력 거절이나 hard-crash까지 무손실이라고 주장하지 않는다.

기존 void RecordObjective는 thin wrapper로 보존하고 새 TryRecordObjective를 실제 prototype producer와 admission 회귀가 소비한다. 정상 no-match/완료는 새 job 없는 true다. actor/session/rules fence·원 OnDurable→weak Self 재해석→완료 순서를 유지하고, 원 command Confirmed/Status/First/Durable은 후속 진척 결과와 분리한다. 전체 confirmed 값/revision을 함께 전달해 설치·게시하고, stale callback은 설치만 건너뛴 뒤 원 pending/callback을 정리한다. 실제 BeforeCommandEncode barrier에서2회 수락·primary Invalid/tail 성공·lost ACK/replay·역순 held projection 및 정상 GI shutdown 후 재읽기 Count4를 확인했다. 기존 HUD status getter는 revision별 blocked 진단을 별도로 보여준다.

Editor140은37actions/native41.78초로 성공했다. 최초 Native1은17개 중14 Success/3 Fail, 경고0/actual exit255였다. 제작 fixture는 retired 완료가 외부 IsCurrent에서 잘려 기존 hold 관측에 도달하지 못해 timeout됐고, weak Self 이후의 중복 외부 검사만 제거해 CompleteEdit 내부 hold→현재 fence를 유지했다. 이동 fixture는 정수 잔여0인데 옛 admission 호출1을 기대했으므로, 실제 미수락1m를 남기고 long-gap의 native consumer1회·잔여/표시 불변·다음 Pawn 미상속을 확인하도록 보정했다. Native2는 관련9개 중8 Success/이관1 Fail, 경고0/exit255였다. 초기 오류와 과정은 덮어쓰지 않았다.

이관 진단은 실제 Create 호출 뒤 Error를 읽도록 분리했다. Native3은 migration staging copy 실패를 확인했다. 격리 경로에서 기존 stage252자+저널8자가260에 닿았고, 같은 Win32 CREATE_NEW probe239/254자는 성공·260자는 실패(error3)했으며 만든 probe만 제거했다. migration/rollback 임시 이름을 대상과 같은 폴더의 짧은 GUID 이름으로 생성해 불필요한 목적 파일명 반복을 없앴다. 원본 hash·같은 폴더 no-overwrite publication·rollback 보존을 유지한다. 이는 현재 생성 경로의 **구조적 보정**이며 전체 Win32 extended/UNC 경로/VFS32k 지원의 완전한 원인 수정으로 부르지 않는다. Editor141/142/143은 정상 종료했고 최종 Native4는 SQLite/복구/이관/진척·제작·이동7개 Success·경고0·약3.457초/exit0과 source/runtime 불변이다. Native1의 변경되지 않은 장기 보관/반복 저장 성공과 Native2의 직접 연결된 성공을 재사용했다. 최종 소스에서17개 전체를 재실행했다고 보고하지 않는다.

기존 BP17개는 동일 파일/보존 graph40 binding 근거를 재사용했다. 현재 /Script/Project_J 직접 package19개 차집합에서 새 BP0/정확·미확정 call0을 읽기 전용으로 확인했다. 제작/채집 두 World의 LevelScript는 Python getter가 snake alias 실패와 native protected 거절을 내어 미확정이며 Gather는 조회되지 않았다. 두 실행의 process0은 조회 성공이 아니다. 새 exporter/우회/세 번째 반복을 추가하지 않고 원문을 남겼다. 기존 void 반영 서명과 직렬화 필드를 유지했으므로 에셋을 변경하지 않고 Cook16을 재사용했다. 준비만 했던 Cook17은 실행하지 않았다.

현재 Editor143/직접 Game36(native73.00초)/Stage28은 같은 최종 소스다. 직접 Game exe와 패키지 SHA가 같고 실제 RHI offscreen의 채집2→제작1→AND journal→HUD XP claim이 phase15/success1/actual exit0·경고0·오류0이다. source/runtime/보호7에셋이 전후 같으며59/60도 이 공통 배포에 포함한다. 이전 Stage27과 Shipping47은 이전 소스이며 현재 통합 성공으로 바꿔 세지 않는다. 새 GPU/FPS/CCU/온라인 provider 인증이 아니다.

재개할 때는 `Docs/Overview/ProjectContext.md`와 이 절을 읽고 goal 상태·진행 중 프로세스·실제 소스를 먼저 확인한다. 최종9파일 지문은 `Saved/Validation/MMOFoundation_20261029/ArchitectureBatch61FinalSourceInputs.json`, native 근거는 `MMOFoundation_20261029_Batch61Native1`부터4, 배포 manifest는 `ArchitectureBatch61Deployment.json`, 실제 소비는 `ArchitectureBatch61GatherCraftCooked1`에 있다. 이미 적용한 candidate/repair와 사용된 Editor140–143·Game36·Stage28/Native1–4를 같은 이름으로 재실행하지 않는다. Saved/Temp 원자료가 없어졌다면 문서의 조건·소스와 새 실행 이름으로 재현하고 과거 수치를 새 검증으로 대신하지 않는다. commit/push나 원본 에셋 변경은 하지 않았다.

**다음 필수 경계는 보류했다:** 실제 canonical near-cap DB에서 primary 성공/progress Invalid와 감소/병합 revision 후 회복, idle gate 포화/거절과 legacy void의 미수락 관측, 전체 extended/UNC 파일 경로, 실제 producer occurrence ID가 없는 중복 호출 의미다. 현 bool은 memory admission이며 durable acknowledgement가 아니다. 콘텐츠/서비스/Server·손실/혼합 수용량·GPU/Nav/HLOD/고속/합산 I/O 등 기존 전체 미완료도 유지한다. 이 체크포인트에서 추가 개선을 시작하지 않는다.

### 62차 재개 — 공유 값 소유권·진척 포화와 실제 용량 복구

2026-10-30 사용자 재개 지시와 목표 active·기존 프로세스 부재를 확인하고61차 근거를 재사용했다. NPC/타깃·저장·진척을 같은 호환 묶음으로 통합했으며 새 전수 감사·서비스 선택·에셋 변경은 하지 않았다.

**실제 변경:** `FTargetSpatialSnapshot::Build`는 호출자가 넘긴 값 배열을 move해 유효 행만 제자리 압축한다.4096행의 실제 입력/결과 포인터 일치와 모든 값·순서, 잘못된 행/중복 실패 후 재사용을 확인해 두 번째 타깃 값 배열 할당·복사를 제거했다. cell bucket/중복 검사/GT registry 복사는 각 역할 때문에 유지한다. NPC life callback 직후 기존 registry/world fence가 무효화되면 이후 조회·좌표 읽기를 즉시 중단한다. 원 코드도 마지막 short-circuit로 retired spatial build를 건너뛰었으므로, 그 index 생성을 새로 없앴다고 보고하지 않는다.

진척은 기존32 gate·한 번의 frozen tail을 유지한다. 실제 AddProgress 미수락 요청만 saturating 세션 누적으로 기존 상태 getter/HUD에 표시하며, no-match no-op·잘못된 입력은 포함하지 않는다. 로그 반복·새 queue·자동 재전송·영속 필드·void BP ABI 변경은 없다. 실제 gate32 예약/33번째 거절, Try/void 거절 관측, tail transaction 동안 예약 유지, 완료 후 다른 owner 획득과32슬롯 전체 재사용을 확인했다.32개의 실제 DB worker나 엄격한 공정성의 증명은 아니다.

실제 등록된 potion453행·quest2개와 유효 규칙 지문으로 canonical65,536바이트를 SQLite에 seed하고 정상 OpenProfile로 복원했다. XP primary는 성공/receipt·XP1을 유지하고 Count9→10 tail만 초과로 보류되며, 같은 revision의 추가 입력은 새 job/commit 없이 거절됐다. 실제 BagMerge가 한 행을 줄인 뒤 기존 보류 목표를 한 번 저장해 Count10, 다음 명시 입력은11이 됐다. 정상 재접속과 별도 DB 읽기는65,395바이트·452행·XP1·Count11·revision5(seed1)를 확인했다. 이 유효 fixture는 전체 콘텐츠 수용량의 보증이 아니다.

private Windows rollback VFS는 canonical 논리 이름과 native 파일 경로를 분리해 Open/Delete/Access 각각에 긴 경로 prefix를 적용하고 Close는 실제 열린 이름을 유지한다. DB가 짧아도 저널만 길어지는 원 trigger, 긴 Unicode 경로 commit/reopen/receipt/backup/no-overwrite·journal/delete-on-close와 explicit drive prefix를 검증했다. UNC는 문자열 canonical 변환만 확인했다. [Windows 경로 규칙](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)에 따른 현재 일반 파일 경로의 원인 보정이며, 실제 SMB locking/권한·특수 namespace/Volume GUID/trailing dot·space까지 지원했다고 표현하지 않는다. 이전61차의 짧은 같은 폴더 migration 이름은 publication/rollback과 경로 절약 역할로 유지한다.

**검증/실패 보존:** Editor144 direct UBT 성공 뒤 최초Native1은13 Success/1 Fail 완료 로그 후 capacity fixture 단언으로 actual exit3·automation report 없음이었다. path fixture의 중간 directory259자는 엔진의 prefix260자 기준 이전에 실패했고, 소유 root에서290자로 직접 늘려 DB 접근 시험과 분리했다. 엔진 코드/전역 정책은 바꾸지 않았다. capacity fixture는 OpenReady 뒤 뒤따르는 checkpoint가 끝나기 전에 idle-only hook을 바꿨다. 실제 Pending 종료를 기다리고 최초 revision1도 확인하도록 보정했으며 임의 delay나 production 우회는 추가하지 않았다. 원 실행·단언·실패와 전후 source/runtime 일치를 보존했다.

Editor145 정상 빌드 후 동일 소스의 집중21개는 전부 Success·경고0·actual exit0·8.424초다. 실제2048 decision, 기존 callback GC/retirement·공간 질의·제작·프로필/이관/복구도 포함한다. 같은 소스의 direct Game37도 정상 종료했다. 현재 Editor145/Game37이며 패키지는 이전61차 Game36/Cook16/Stage28이다. 이번에는 새 Cook/패키지·RHI·CPU/FPS 비교를 요구하는 에셋/직렬화 변경이 없어 반복하지 않았다. source10개 최종 지문·첫 실패·21개 실제 이름·빌드/검증 조건은 기존 ValidationSummary의62차와 `Saved/ArchitectureBatch62ProfilePaths`에 있다.

**남은 경계:** 무조건 수락/메모리 admission의 crash 내구성·실제 occurrence ID 중복 의미·엄격한 공정성은 해결 완료가 아니다. 엔진의 중간 directory 경로 제한, 실제 UNC/특수 namespace와 미정 provider/Server·손실/혼합 부하·GPU/Nav/HLOD/고속·합산 I/O도 남는다. 전체 약60%/잔여40%(55–65%) 추정은 테스트 개수로 올리지 않는다. 목표는 활성 상태로 다음 실제 실행·자원 개선을 이어간다.40% 중단/PC 종료 조건은 없다.

### 63차 시각 에셋 입장 비용과 실패 알림 재진입

실제 Niagara 준비/재생의 `CombatPresentationComponent`와 soft 무기 class의 `WeaponPresentationComponent`가 공유하는 기존 VisualAssetSubsystem을 보강했다. 정상 lease 요청마다 실패 cache 최대256개를 전수 만료 처리하던 경로를 요청 path 하나의 조회/만료로 바꿨다. 새 실패를 기록할 때 map이 가득 찬 경우에만 만료 항목을 회수하고 callback 전에 backoff를 기록한다. 새 상태·manager·timer·cvar·한도·품질 정책은 없다.

이는 반복 입장 비용 제거와 실제 실패 경로의 원인 수정이다. 원 Tick은 cache가 만료 항목으로 가득 차도 새 실패를 기록하지 못했다. 이어 consumer callback의 같은 path Request가 옛 항목만 지우고 즉시 재입장할 수 있었다. 현재 시험은 실제 timeout 요청을 수락한 **뒤**255만료+1활성 항목을 다시 채워 입장 시점의 정리와 분리하고, 실제 Tick/실패 callback에서 새 path의 backoff가 먼저 기록돼 재진입을 거절하는지 확인했다. 모든256개가 아직 활성인 경우의 no-eviction/기록 상한은 유지하며 모든 실패 path의 재시도를 무조건 억제한다고 주장하지 않는다.

정상 warm asset32 lease의 단일 group/inline callback 없음/취소, requested expiry·활성 backoff, timeout/상한/종료와 기존 실제 async 공유1024·churn1024·resident cadence·GC·VFX pool·무기 identity를 같은 D3D12 RHI 묶음으로 확인했다. Editor146 direct UBT와8개 Success·경고0·actual exit0·4.256초, source/runtime 전후 동일이다. Game38 direct UBT도 정상 종료했다. 현재 Editor146/Game38이며62차 소스10개도 그대로 포함한다. 최종12파일 지문은 `Saved/ArchitectureBatch63VisualAdmission/FinalSourceInputs.json`에 있다.

실제30초 대기나 물리적 missing-package 실패는 forced deadline 시험으로 대신하지 않는다. 실패 metadata는 최대256 soft path로만 남고 UObject residency를 pin하지 않는다. 기존 물리적 load reservation·lease64/2048·resident 갱신·약한 owner·callback 순서와 종료 계약을 유지한다. 새 CPU/FPS 개선률·GPU/VFX 총량·중요 전투 경고 가시성·실제 온라인 수용량은 미측정/미완료다. 에셋/반영/직렬화 변경이 없어 Cook/패키지를 다시 만들지 않았고 패키지는61차 Stage28이다. 전체 목표는 활성 상태로 남으며 기존 미정 서비스/Server와 큰 자원·복제·통합 부하 작업은 계속 남는다.

### 64차 정의 게시 재사용과 소셜 식별자 수명

GI 초기화 때 이미 검증·게시한 class/advancement/item registry를 실제 OpenProfile가 매 입장마다 다시 검색·해석·graph검증·optional recipe capture하던 비용을 제거했다. 새 non-reflected Ensure는 성공한 현재 게시를 재사용하고, 기존 Load는 명시 refresh로 유지한다. 실패한 refresh는 기존 map을 그대로 두되 새 입장의 readiness를 닫는다. 표준 UObject ReloadConfig/PostReloadConfig는 즉시 로드 없이 readiness만 무효화하고 다음 Ensure가 한 번 갱신한다. 다른 제작 catalog 변경은 명시 Load를 호출하는 계약이며 임의 UObject 편집 자동 감지·전체 hard closure/서버 pose 제거가 아니다. 기존 recipe 오류는 crafting에만 영향을 준다.

실제 profile 최초 입장·재접속의 Refreshes 불변,32회 warm Ensure의 추가 refresh0, 실제 ReloadConfig의 eager refresh0→Ensure1→후속0, duplicate/graph/멤버 제거·실패/회복·생활 소비를 확인했다. 이는 실제 반복 GT 작업 제거이며 디스크 I/O 중복이나 CPU/FPS 개선률을 측정한 것으로 바꾸지 않는다.

소셜 생성은 매 group마다 GUID 문자열을 intern하던 방식에서 GI/kind별 GUID namespace를 한 번 만들고 FName internal number1..MAX_int32를 단조 발급하는 방식으로 바꿨다. 기존 FName public/BP/복제 타입과 opaque ID 소비·schema/프로토콜8은 유지한다. 퇴역 번호를 재사용하지 않으며 trusted restore의 같은 namespace 미래 번호는 publication 전에 high-water mark에 반영한다. 마지막 번호는 한 번 발급하고 소진 시 기존 RevisionExhausted 결과로 부분 변경 없이 거절한다. 실제 Create/Leave/Restore, 공유 comparison index·서로 다른 번호·문자열 왕복·복수GI/kind·32회 churn·stale restore·미래 번호 충돌·마지막 번호/거절을 확인했다. 현재 Editor/Game의 UE_FNAME_OUTLINE_NUMBER=0에서 per-group name entry를 추가하지 않지만 다른 outline1 target의 numbered entry 메모리까지 보장하지 않는다.

**검증:** 같은 소스의 Editor147 직접 UBT 성공 뒤11개 Success·경고0·actual exit0·3.193초다. 이어 기존 실제 Editor dedicated host1/client2의 파티 cohort가 strict pass·actual exit0×3·timeout0이었다. 실제 numbered GroupId가 join/health/leave/rejoin/offline8/disconnect와 owner-only 요약을 통과했고 각 client의 요약8/7행·member25/17행이 authority와 일치했다. 패킷 지연/손실0·NullRHI 조건이며 production Server target/CCU/화면 이미지 승인과 구분한다. native/원격 전후 source/runtime 불변이고 direct Game39도 정상 종료했다. 최종18소스 지문은 `Saved/ArchitectureBatch64DefinitionLoad/FinalSourceInputs.json`에 있다. 에셋/반영 타입 변경 없이 Cook/패키지는 반복하지 않아 현재 패키지는61차 Stage28이다.

**유지/남은 경계:** tombstone map 자체는 여전히 GI 수명 동안 증가한다. TTL/FIFO 삭제로 stale 복원 거절 권위를 잃거나 번호를 재사용하지 않았다. 외부 provider의 ordered watermark/namespace 권위와 같은 ID 부활/terminal deletion 계약이 정해져야 resident 퇴역을 완성할 수 있다. FName 문자열 증가 보정을 전체 장기 bounded social 완료로 세지 않는다.

다음 A07의 좁은 읽기는 실제 PS가 기본 AlwaysRelevant이며 count limiter는 scope 제한이 아님을 확인했다. 설치 Iris의 PS→Pawn creation link는 parent relevance까지 강제하므로 단순 owner-only/거리 flag는 remote Pawn도 숨긴다. 지원되는 base grid protected update hooks와 public Avatar cached WorldLocations를 재사용할 방향은 있지만, owner + current Pawn/ASC Avatar뿐 아니라 잔존 old Avatar 참조·탑승 전환·동일 grid/profile/cull override·hysteresis의 보수적 포함 관계를 실제 원격에서 입증해야 한다. `IsRelevantForConnection` 실험 API/private 내부 접근이나 임의 grace timer로 해결됐다고 세지 않는다. 필터/Config를 아직 추가하지 않았고 A07은 미완료다. 목표는 활성 상태로 이 실행·복제 경계와 기존 큰 자원/서비스 작업을 이어간다.

### 65차 설계 검토의 안전 일시정지

2026-10-30 사용자가 다른 Unreal 작업을 위해 잠시 멈추고, 즉시 강제 중단 대신 현재 하던 저장/정리를 마무리해도 된다고 요청했다.64차의 구현·11개 native·실제 server1/client2·Editor147/Game39 검증과 문서 마감은 이미 끝났으므로65차 읽기/설계만 Saved 체크포인트로 마감한다. 새 필터·PS hook·Config·test·원격 fixture 코드는 아직 작성/적용하지 않았다.65차 빌드/UE/소켓 실행도 시작하지 않았으며 부분 적용 소스를 되돌리거나 실행 중 빌드를 중단할 필요가 없다.

재개 기준은64차 `Saved/ArchitectureBatch64DefinitionLoad/AfterGame39.json`과 누적18파일 `FinalSourceInputs.json`이다. 루트는 전체 provenance 수집 범위의 소스/설정/스크립트/플러그인·런타임 바이너리가 이 기준과 일치함을 확인하고 `Saved/ArchitectureBatch65Pause/SourceAtPause.json`에 보존했다. 확인 시 실행 중인 Unreal/UBT/dotnet/Shader/검증 프로세스는 없었다. 서브태스크도 Saved 기록을 마감하고 종료했으며 다른 작업의 프로세스를 종료하지 않았다.

다음에 읽을 근거는 `Saved/ArchitectureBatch65Pause/HANDOFF.md`, `Saved/ArchitectureBatch65PlayerStateAudience/CHECKPOINT.md`, 기존64차 audience `NOTES.md`다. 제안한 `UProject_JPlayerStateAudienceFilter`와 TryBind/ObserveAvatarCreation/QueryBinding 등 이름은 **아직 선언·구현이 아니다**. 설치 ABI상 base Grid 생성자/소멸자 NO_API 때문에 exported WorldLoc subclass와 base PreFilter/update hooks를 사용할 방향, 실제 미등록 deferred-spawn witness와 owner-only placeholder의 bootstrap 구분, 기존 Avatar history·잔존 참조를 timer 없이 보호할 계약이 남는다. 지원 API 읽기와 조건부 수학 포함 관계를 runtime 성공이나 A07 완료로 바꾸지 않는다.

루트의 far20–26→near28→far29→기존 disconnect27 원격 cohort도 현재는 HANDOFF의 계획만 있고 fixture/script 코드를 작성하지 않았다. 재개 시 기존 기본 파티 검증을 유지하면서 실제 PS/Pawn/ASC entry/exit·멀리서 바뀐 health·private 데이터·정확한 filter assignment/fallback을 연결해야 한다.64차 파티 pass를65차 audience 검증으로 재사용하지 않는다. 첫65차 실행명/빌드는 아직 사용하지 않았다.

문서·링크·지문·서브태스크 종료를 확인한 뒤 사용자 요청대로 goal 도구에 paused를 설정하고 반환을 확인한다. 전체 목표 완료가 아니다. 이전40% 사용량 조건과 컴퓨터 종료는 계속 취소 상태이며 실행하지 않는다. 사용자가 다시 재개를 지시하기 전까지 코드·빌드·UE 작업을 시작하지 않는다. 재개 시에는 최신 goal·현재 소스/사용자 변경·다른 Unreal 프로세스를 먼저 확인하고 기존 적용 후보/사용한 실행명을 재실행하지 않는다.

### 65–66차 실제 PS 관심 영역과 일반 생성 경계

2026-10-31 중단 지점과 동일한1202개 source/runtime 지문에서 재개했다. 기본 OFF인 실제 Iris runtime filter를 연결했다. PS 자신의 transform 대신 현재 Pawn·State 소유 ASC Avatar의 cached/clamped 위치와 cull override를 포함하는 보수적 구를 기존 WorldLoc grid에 공급한다. float 반경과 저장된 제곱 반경을 바깥으로 반올림하며, 잘못되거나 상한을 넘는 구는 유한 placeholder와 기존 broad audience를 사용한다. owner connection은 별도 허용하고, PS count-limiter prioritizer·PS→Pawn creation link·GAS·private property/component 조건은 유지했다. PS×connection 전역 표나 임의 grace timer는 만들지 않았다.

66차 연결은65차 검증과 함께 마감한다. 테스트 helper의 수동 생성 관측 API를 제거하고 실제 `UWorld` pre-spawn delegate와 PS 할당 serial을 사용한다. 정확한 PS lease가 Pawn 생성보다 먼저 존재하고, 첫 실제 PreFilter에 PS/Pawn/ASC 관계와 지원된 Spatial assignment가 완성된 경우만 fresh 이력을 인정한다. 등록됐지만 소비되지 않은 약한 witness는 그 pass에서 퇴역한다. 늦은 연결·교체·잔존 Avatar·unknown profile/coarse·정책 변경은 broad fallback을 유지하며, 종료 시 같은 world delegate를 해제한다.

**실패와 원인 수정:** Editor148의 이름 충돌은 GridPolicy로 보정했다. 최초 actual Audience1은 far phase20에 foreign PS1/Pawn0/private0으로 strict 실패했고 모든 역할은 정상 exit0이었다. Iris가 Config를 필터별 NewObject로 생성하는데 CDO 포인터 동일성을 요구한 검사가 항상 실패한 것이 원인이다. 실제 인스턴스의 클래스·값·수명·프로필과 초기 정책을 검사하도록 수정했다. 생성 API 교체 후 Editor150에서 드러난 간접 include 의존성은 실제 fixture의 명시 include로 보정했다. 실패 자료와 이전 성공 자료는 보존했다.

**검증:** 최종 Editor151/Game40 direct UBT 정상 종료, 현재 소스의 native5개 Success/경고0/exit0이다. actual dedicated Editor server1+client2의 Audience2는 far→near28→far29→disconnect에서 strict pass·exit0×3·timeout0이었다. far foreign PS/Pawn0, near 각각1 및 reciprocal ASC/최신 health60·80, foreign private0, owner 파티/HUD를 함께 확인했다. 실제 생성 관측4/소비2/pending0,19 binding행의 fallback0, authority74행이다. 같은 binary의 기본 OFF Baseline1도 기존42행 파티 strict pass다. native/원격 전후 source/runtime은 같으며 최종 지문은 `Saved/ArchitectureBatch65Cohort/FinalSourceInputs.json`이다. 에셋/Cook은 반복하지 않아 실제 패키지는61차 Stage28이다.

추가로 동일 Editor151/Game40 source/runtime의 `Batch66AudienceLoss1`도50ms 지연·2% 손실 주입 설정에서 같은 far→near→far strict pass·exit0×3·timeout0이었다. 전후 지문은 Game40 이후와 같으며 실제 손실률·탑승/교체 중 loss·대규모 resilience의 근거로 확대하지 않는다.

**유지/남은 경계:** 현재는 명시 `-ProjectJPlayerStateAudience` 실험에서만 적용한다. 탑승·Avatar 교체·늦은 등록·교체 중 packet loss와 old remote reference 퇴역은 추가 실제 검증 및 수명 설계가 필요하므로 A07/전체 확장성 완료나 운영 기본 활성화로 세지 않는다. unknown transition의 broad 비용은 관측 가능하게 남겼다. 대규모 bytes/CPU/FPS/CCU 개선률은 아직 측정하지 않았다. 사용량은 묶음 종료 때 확인하며30% 근처에서 안전 중단한다. 전체 목표는 active이고 PC를 종료하지 않는다.

### 67차 실제 Avatar 교체·옛 Pawn 종료와 장비 수명

기존65–66 관심 영역을 기본 OFF로 유지하고 별도 lifetime cohort를 추가했다. 근접한 두 owner 중 한 명을 정상 authored Spawn/Possess로 교체하며 같은 full PS handle·ASC/최신 health를 유지하고, old Pawn의 PS/ASC/Controller는 실제 UnPossessed로 정리돼야 한다. old Pawn을 잠시 살려 근접에서 확인한 뒤 active Pawn들을 원거리로 이동하고, 마지막에 old Pawn을 정상 Destroy한다. 옛 참조를 직접 지우거나 health를 reset하지 않는다.

교체한 peer는 정확히 `RetiredSource` fallback을 유지해야 하고 leader는 계속 spatial이어야 한다. 따라서 교체 후 원거리에서도 leader가 보는 foreign PS1은 명시적 containment이며 최적화 성공이 아니다. 옛 actor/handle이 사라져도 이를 원격 ACK로 오인해 fallback을 해제하지 않는다. 별도 수명 CSV가 owner8행 파티/HUD·private0·현재 PS/Pawn/ASC·helper prototype weapon GUID와 public projection의 보존을 연결한다. 해당 weapon은 가방 소유 영속 아이템이 아니며 그 보존으로 확대하지 않는다.

Editor152의 첫 C3535 오류는 UE5.8 GetPawn TObjectPtr 반환을 명시 APawn*로 받도록 보정했고 Editor153 direct UBT가 정상 종료했다. actual dedicated Editor host1/client2의 정상 Lifetime1과50ms 지연·2% 손실 주입 LifetimeLoss1이 모두 strict pass·exit0×3·timeout0, 동일 source/runtime이었다. authority106/binding23행, slot0 summary12/member57 및 slot1 summary11/member49, old near1→far0→Destroy 후 fallback 유지와 새 prototype GUID 보존을 확인했다. 기존65–66 생산 filter/PS/character/GAS는 바꾸지 않아 관련 native5개 근거를 재사용한다. 새 Game target 검증은 다음68차와 묶고 현재 Game40은65–66 기준이며 실제 패키지는61차 Stage28이다.

공개 scope/handle/local stop은 원격 퇴역 ACK가 아니다. 지원 async stop의 소유·Destroy/flush/re-register 교차 계약도 아직 입증하지 않아 새 manager나 wrapper를 만들지 않았다. 이 결과는 교체 중 안전한 fallback과 새 consumer 일관성의 증거이며, 교체 후 spatial 최적화·탑승·CCU·전체 loss 복원 완료는 아니다. 최종26소스 지문은 `Saved/ArchitectureBatch67AudienceLifetime/FinalSourceInputs.json`이며 목표는 active다.

### 68차 월드 streaming source 식별자의 등록 수명

기존 준비/도착 source 조회 두 경로가 매번 `FName(*GetPathName())`를 만들어, owner/컴포넌트 경로 변형에 따른 영구 이름 base 누적과 반복 문자열 생성을 유발하던 생산 경로를 수정했다. 기존 WorldArrivalSubsystem이 Game/PIE world별 namespace 하나와 양수 FName 내부 번호를 발급하고 컴포넌트가 등록 수명 동안 보관한다. 재등록에는 새 번호를 쓰며 MAX_int32 소진은 alias/재사용 없이 source/arrival admission을 거절한다. OnUnregister는 ID를 먼저 닫고 기존 취소/해제를 수행한다. 요청 token과 provider identity의 서로 다른 수명은 유지하며 새 manager/전역 cache/임의 source 수 제한을 만들지 않았다.

실제 parent Super의 Actor 이름/label 변환과 target settings는 유지했고 project 전체 컴포넌트 경로의 FName 생성만 제거했다. 따라서 전체 Name pool 증가나 모든 할당을 해결했다고 세지 않는다. 현재 outline0 target의 namespace 공유와 번호, world별 namespace 한 개의 잔존 비용을 구분하며 outline1 target의 추가 numbered entry 가능성을 남긴다. 기존 Game/PIE 외 world type의 경로 동작도 보존했다. 실제 overlap fixture의 경로 식별 가정은 opaque getter/ID와 별도 진단 경로로 이주해 제공·소비·기본 source 일치를 확인한다.

Editor154/Game41 direct UBT가 정상 종료했고 준비·도착/예산·churn32/재등록/부모 설정/MAX 경계와 이전 Avatar의 취소·관측 무효화를 묶은6개 native가 Success/경고0/exit0이었다. 실제 MainWorld의 정상 BP GameMode dedicated host1/client2 NullRHI에서 동시 Preparing·한 Pawn 정상 퇴역/Failed/RestartPlayer·survivor native 도착·기본 source 복원 및 역할별 opaque ID4개가 strict pass·actual0×3·강제 종료0이었다. source/runtime/보호5에셋은 전후 같았다. 최종32소스 지문은 `Saved/ArchitectureBatch68WorldBudget/FinalSourceInputs.json`이다.67차와 함께 Game41을 검증했지만 실제 패키지는61차 Stage28로 유지한다.

이는 project source 조회 생산자의 원인 수정이며 cleanup/이름 출력 위치 이동만의 조치가 아니다.256개 요청의 합산 WP I/O·메모리·동시 target 광고와 GPU/Nav/HLOD·고속 원경 준비, A07 old reference ACK/탑승/운영 cutover·미정 서비스는 별도로 남긴다. CPU/FPS/전체 resident memory 개선률은 새로 측정하지 않았다. 다음은 기존 headless cosmetic consumer의 실제 요청/수명 비용 경계이며 전체 목표는 active다.

### 69차 headless 표현 소비와 실제 RHI 검증 연결

41/64차 근거를 재사용해 정의 registry를 다시 만들거나 hard reference를 일괄 soft로 바꾸지 않았다. 실제 gap은 headless standalone/listen에서도 순수 cosmetic 무기 class·장비 mesh의 요청/생성/재시도가 가능했던 것이다. 기존 무기 생성 admission과 장비 요청·완료·재시도·timer에 FApp::CanEverRender를 결합했다. 장비 effect/grant·RuntimeItems/revision·public slot·전투 설정은 visual 진입 전 설치하며 source body pose/notify/판정은 보존한다. Dedicated는 이미 차단돼 있었으므로 새 dedicated 절감으로 세지 않는다.

같은 호출 계약의 검증도 연결했다. 실제 무기 Actor를 요구하는5개 테스트는 NonNullRHI로 선언하고 실제 RHI에서 원 검사를 수행했다. 영구 notify 실행기는 RenderOffscreen과 기존 Temp scratch helper를 사용하며 trace/JSON 증거는 Saved에 보존한다. EquipmentInterest의 기존 RemoteVisuals1을 약화하지 않고 두 client를 RHI로, authority는 NullRHI로 실행한다. 이전 NullRHI와 새 RHI 시간을 직접 비교하지 않는다.

**실패와 보정:** 첫 headless1은 새 fixture가 BeginPlay 없이 EndPlay를 불러 bHasBegunPlay 단언/exit3이었고 실제 수명 순서로 보정했다. headless2는5 Success/1 Fail/exit255였는데, OriginalASC가 공유 소유권으로 옮긴 effect를 옛 로컬 ledger에서 세던 검사였다. effect apply/remove 본문이69차 전과 같은 것을 확인하고 실제 ASC effect1/정리0으로 보정했다. headless3은 그1개 집중 재실행이 Success다. 나머지5개는 소스 차이가 그 테스트 한 파일뿐임을 확인해 재사용하며 단일6-success 실행이라고 기록하지 않는다. cold Entry/NullRHI의 soft class 비로드·요청/lease/Actor/불필요 retry0과 상태/소유권을 함께 확인했다.

현재 Editor157/Game42 direct UBT가 정상 종료했다. 실제 RHI10개는 Success/경고0/exit0, authored weapon/montage의50/100/200명 notify12행은 RHI1 Success/경고0/exit0이었다. actual dedicated Editor host1+RHI client2의 장비6단계/Avatar 교체·기존 owner VM 가방/Unequip/Equip/퀘스트 명령3행씩·개인정보가 strict pass·actual0×3·timeout0이었다. 모든 실행의 source/runtime 전후는 같았고 에셋은 수정하지 않았다. 최종43소스 지문은 `Saved/ArchitectureBatch69DefinitionConsumption/FinalSourceInputs.json`이며 실제 패키지는61차 Stage28이다.

GetSpawnedWeapon의 임의 BP 사용·custom collision을 전부 인증한 것이 아니며, native/current authored consumer와 기존 dedicated cosmetic 계약을 근거로 한 변경이다. gameplay/pose hard closure, 새 bytes/CPU/FPS/전역 memory 개선률·CCU·운영 서비스·old Avatar ACK/탑승 등은 남는다. 최신 사용자 지시로30% 중단 조건은 취소됐고 목표를 active로 계속 진행한다. PC를 종료하지 않는다.

### 70차 장비 효과 제공자의 객체 수명 식별

장비 효과의 기존 ASC lease를 유지하면서, 제공자 전체 경로를 매번 FName으로 만드는 경로를 slot/definition comparison name과 엔진의 공개 weak-object incarnation serial 번호로 바꿨다. 재사용되는 UObject index나 hash를 identity로 쓰지 않는다. 같은 manager의 rename/여러 Avatar는 같은 source를 공유하고, 다른 manager와 GC 이후 새 제공자는 구분한다. definition 이름의 숫자 suffix를 보존하는 고정 Provider suffix와 기존 null-provider 공유도 유지한다. plain ASC 경로에는 불필요한 keyed name을 만들지 않는다.

Editor158/Game43 direct UBT가 정상 종료했고 실제 effect/stat 적용·해제, 공유/final release·stale generation·별도 제공자·null·숫자 definition·정상 Destroy/GC와 남은 unpublished ledger를 포함한 기존 소유권5개가 Success/경고0/exit0이었다. 실행 중 source/runtime은 같았고 최종43개 source 지문은 `Saved/ArchitectureBatch70GrantSourceIdentity/FinalSourceInputs.json`이다.69차 실제 RHI/원격 소비 근거는 ASC-local key 변경 범위에서 재사용하며 새 에셋/복제/반영/저장 계약이나 Cook/패키지는 추가하지 않았다. 현재 패키지는61차 Stage28이다.

이는 반복 경로 이름 생성과 provider incarnation 계약의 원인 수정이다. 특정 object index 재사용을 강제로 재현했다고 주장하지 않으며, 엔진 공개 serial 계약과 정상 GC 소비 검증을 구분한다. 새 manager/map/strong pin은 없다. content definition 경로와 다른 FName outline의 할당, 전체 memory/CPU/FPS 개선률은 해결 완료가 아니다. A07 old reference/history, 합산 streaming I/O/GPU/Nav/HLOD·혼합 부하·미정 서비스는 남고 목표를 active로 계속한다. 사용량30% 중단 조건과 PC 종료는 없다.

### 71차 준비 도착의 목적지 demand와 현재 수명

WP source 수집은 중앙 arrival poll과 독립적이다. 기존 pointer 검사만으로는 같은 Pawn의 ASC Init/Clear epoch·account/character·PS owner 변경 후에도 다음 budgeted Advance까지 옛 목적지를 광고할 수 있었다. GetStreamingSource가 기존 IsArrivalCurrent(false)를 재사용하도록 해, 이미 무효인 local request는 즉시 정상 predictor로 복귀하고 nonlocal authority/dedicated는 목적지를 광고하지 않게 했다. const 조회는 request/phase/deadline/RPC를 변경하지 않으며 원래 중앙 writer가 종료·정리를 소유한다.68 opaque ID와 정상 Preparing/Queued의 geometry는 유지한다.

Editor159 direct UBT와 기존 world 집중4개가 Success/경고0/actual0이었다. 실제 MainWorld dedicated Editor host1/NullRHI owner2의 overlap·old Pawn 퇴역·RestartPlayer·정상 survivor 도착/source 복원이 strict pass·actual0×3이었고 source/runtime·보호5에셋이 같았다. 같은-pair epoch와 identity/owner fence는 focused-native 근거이며, 실제 소켓에서 그 epoch 변경을 재현했다고 확대하지 않는다. 실제 opaque source ID4개도 보존했다. 최종43개 source 지문은 `Saved/ArchitectureBatch71WorldBudget/FinalSourceInputs.json`이다.

이는 무효 demand의 수명 원인 수정이며256개의 유효 목적지 합산을 제한한 것이 아니다. 엔진의 client/nonblocking cell-load knob를 server source cap으로 오인하지 않았고 새 임의 상한·queue·I/O 예약은 없다. 이미 시작된 engine 작업·resident/GPU/Nav/HLOD·고속/혼합 부하는 남는다. 다음72차와 호환 Game 검증을 묶으며 마지막 Game43은71차 전 소스다. 패키지는61차 Stage28, 전체 목표는 active이고 사용량 중단/PC 종료는 없다.

### 72차 Avatar 게시 수명과 사용자 요청 일시정지

사용자가 다른 Unreal 작업에 더 시간이 필요하다며 안전한 일시정지를 명시했다. 현재 중단은 사용량 기준이 아니다.30% 조건과 PC 종료는 취소 상태를 유지하며, 재개 요청 전 새 빌드·엔진 검증·후보 수정을 시작하지 않는다. 중단 확인 시 Unreal/UBT/dotnet/LiveCoding/ShaderCompileWorker 등 조회 결과가 없었고 세 subagent도 완료/대기 상태였다. 다른 프로젝트를 종료하거나 프로세스를 강제 중단하지 않았다.

**적용:** client PlayerState ASC의 recipient Init은 GAS의 실제 게시를 따르게 하고, 기존 authority/local ASC bootstrap은 유지했다. Init/Clear의 가장 바깥 정리 뒤 실제 일관된 settled tuple을 알리는 native event와 Pawn의 weak owned 구독을 연결했다. prechange/OnAvatarSet 중첩 Init/Clear가 새 상태를 덮어쓰거나 완료 알림을 잃지 않게 한다. client의 정확한 게시 tuple을 APawn.OnRep_PlayerState의 State.Pawn 쓰기 전에 확인하고, 기존 retained mount owner 해석을 보존한다. 장비 manager도 같은 runtime owner에서 해석한다. 기존 audit runner의3개 UserDir는 기존 helper를 통해 checkout별 Temp로 분리했다. 새 네트워크 epoch/manager/polling timer는 없다.

**검증:** Editor160은 probe local Owner가 AActor.Owner를 가리는 C4458/exit6으로 정상 종료했다. 해당 선언과2개 참조만 ObservedOwner로 고친 Editor161 direct UBT는 성공했고 집중13개가 Success/경고0/actual0이었다. native의 PS-first/ASC-first는 공개 role API를 사용한 synthetic order injection이며 실제 wire 순서 근거가 아니다. 실제 GAS OnRep 및 OnAvatarSet 중첩 Init/Clear와 기존 mounted/retained/local 소유권을 함께 확인했다. source/runtime 전후와 중단 지문은 같으며 최종 source 지문은 `Saved/ArchitectureBatch72AvatarPublication/FinalSourceInputs.json`이다.

**미실행:** 기존 실제 lifetime fixture의 phase30 client late PS callback probe와 strict CSV 검사는 작성됐지만 실행하지 않았다. 실제 mount/normal/loss72 실행은 다른 UE5.7 프로젝트·LiveCoding이 열려 있던 때 기존 guard가 launch 전에 차단했다. Mount1 출력 디렉터리도 없으므로 실패한 네트워크 실행이나 통과 근거로 세지 않는다. 다음 재개는 native 소스/바이너리 불변 확인 후 `Saved/ArchitectureBatch72AvatarPublication/Run-Network72.ps1`의 actual mount와 lifetime 정상/손실 검증이다. 이후 호환 Game 검증을 연결한다. 현재 Game43은71/72차 전 소스이며 패키지는61차 Stage28이다.

73차는 이미 지연된 원격 cosmetic Niagara loop의 완료 fan-out을 기존 공유 GT apply 예산에 연결하는 Saved-only6파일 후보다. patch SHA는 `3f4372ce5ff551319f01eb970ccd520f5aebb953116263cf3677d0b256c702d1`이고 실제 Source는6개 모두 원본 지문과 같다. 독립 검토에서 loader의 순환 lease cursor 때문에 포화 fixture의 callback 순서를 가정할 수 없다는 반례가 남아 있다. 이를 실제 admission을 이용한 결정적인 단계로 보정·재검토한 뒤에만 적용한다. 아직73차 빌드/RHI/성능 검증은 없고 shared per-Tick apply 예산은 active emitter/GPU cap이 아니다.

재개 기록은 `Saved/ArchitectureBatch72AvatarPublication/HANDOFF.md`다. arbitrary mount prerequisite-last·old remote ACK/history·합산 I/O/GPU/Nav/HLOD·혼합 부하와 미정 서비스 등은 남는다. 전체 목표 완료로 표시하지 않고 goal 도구의 paused 반환을 확인한 뒤 작업을 멈춘다.

### 72–73차 재개: 탑승 topology 수렴과 공유 표현 적용 예산

사용자의 재개와 UI 활성화 뒤 지속 목표의 active를 확인했다. 아래는 앞선 일시정지 이후의 진행이며30% 중단/PC 종료는 취소 상태다. 기존 검증과 Source 지문을 재사용하고 실행 중 Source/Config/Scripts/Plugins/에셋을 동결했다.

**73차 실제 구현:** 이미 비동기 로드를 기다리던 nonlocal cosmetic loop의 준비 완료를 기존 weapon apply와 같은 world PresentationBudget의 약한 consumer FIFO에 연결했다. world Tick당4 apply attempt/128 visit/1ms soft budget을 공유하며 별도 queue/manager를 만들지 않았다. 한 admission은 현재 유효한 loop 하나만 적용하고 나머지는 FIFO 뒤로 돌린다. queue 포화 때 같은 incarnation의 ready desire를 명시적으로 거절하며 동기 fallback이나 timer를 만들지 않는다. stop/reset/교체/종료가 기존 lease와 queue를 함께 퇴역시킨다. local view, 기존 warm/one-shot/notify critical 경로는 보존했다. 이는 active emitter/GPU/global frame cap이나 FPS 보장이 아니다. 포화 fixture의 loader callback 순서 가정을 제거한 V2를 독립 검토 후 적용했다.

**72차 실제 연결 수정:** Mount1의 slot 혼선은 접속 도착 순서를 fixture identity로 사용한 원인이었다. native login option에서 유일한0/1을 검증하고 client1→client0의 역순 등록[1,0]도 검증한다. Mount2는 PS-first dismount에서 ASC revision이 그대로인 채 mount topology만 뒤늦게 도착하면 재시도가 없어 State.Pawn이 mount에 남았다. 기존 mount component의 native prerequisite 알림과 Pawn의 owned weak subscription으로 실제 topology 완료가 기존 OnRep fence를 다시 실행하게 했다. 같은 mount를 semantic change로 재방송하거나 timer/강제 State reset/추가 ASC Init writer를 만들지 않았다. payload/self/owner pin과 재진입 revision fence를 유지한다.

**검증:** Editor162/163 direct UBT가 성공했고 현재 Editor163 집중15개가 Success·경고0·actual0이다.73차 cold loop 공유 예산·포화·공정성·stop/refresh와 실제 weapon/notify/pool/GC 소비를 확인한 RHI4개는73차6소스가 동일한 Editor162의 유효한 근거를 재사용한다. actual Mount3는 역순[1,0], authority18/client18×2행이 strict pass·actual0×3이다. 실제 Avatar lifetime 정상 조건도 old callback probe를 두 client 모두 통과했다. 원 Mount1/Mount2 실패는 보존했다.50ms/2% LifetimeLoss1은 phase29에서 slot1 own_linked0/retired1로 strict 실패했고 실제 프로세스는0×3으로 정상 종료했다. timeout 파일0을 phase deadline 성공으로 오인하지 않는다. 원인 확정 전 bounded ownership/view 진단을 검토 중이며 카메라 상태를 바꿀 수 있는 getter를 제거하고 있다.

추가 진단은 PC.GetViewTarget/GetPlayerViewPoint의 CheckViewTarget 부작용을 적용 전에 제거하고 raw target/저장 camera cache와 이름·full handle·소유/ASC tuple만 읽는다. Editor164 direct UBT와 같은50ms/2%의 단일 LossDiag1은 strict pass·actual0×3·source/runtime 불변이었다. phase30→29에서 slot1의 server camera cache1600→39600, current/ack Pawn286과 State226/ASC tuple은 정상 수렴했다. 관측만 추가했으므로 이는 원인 수정이 아니며 LifetimeLoss1의 실패는 미해결이다. 무작정 반복 대신 detached camera에서 소유 Pawn이 보존되는 계약을 명시적으로 시험할 opt-in 기존 cohort 변형을 Saved에서 검토한다. 설치 엔진의 일반 PC→Pawn dependent 구제가 creation-dependent object에는 적용되지 않는 상호작용을 정적 근거로 보존하되 원 실패의 원인으로 단정하지 않는다.

근거는 `Saved/ArchitectureBatch73VisualBudget/`, `Saved/ArchitectureBatch72SlotIdentity/`, `Saved/ArchitectureBatch72MountTopology/`, `Saved/ArchitectureBatch72LossOwnership/`, actual network의 `MMOFoundation_20261031_Batch72Mount3` 및 `Batch72Lifetime1`/`Batch72LifetimeLoss1`/`Batch72LifetimeLossDiag1`이다.71–73차를 포함한 full Game44 direct UBT도102 actions/exit0으로 완료했고 source 불변이다. 누적62소스 지문은 `Saved/ArchitectureBatch73VisualBudget/FinalSourceInputs.json`이다. 패키지는61차 Stage28이다. 손실 수명 원인·arbitrary prerequisite-last·old remote ACK/history·합산 streaming 자원·혼합 부하·미정 서비스는 남는다. 이 묶음을 전체 A07/AAA/CCU 완료로 세지 않는다.

### 74차 detached camera와 현재 소유 Pawn 복제 범위

기존 loss를 무작정 반복하지 않고 실제 서버1/client2의 명시 opt-in `PlayerStateAudienceOwnerView`를 기존 lifetime cohort에 연결했다. phase29에서 현재 Pawn은40000uu로 이동하고 slot1은 정상 SetViewTarget 경로로 남겨 둔 old Pawn을 바라본다. 엄격한 소유 tuple과 기존 private/foreign 조건을 유지하며, client와 authority의 저장 camera cache가 현재 Pawn cull 반경2배 밖에서8초 안정적인지 별도로 검사한다. phase31은 현재 Pawn view 복원 뒤 old Destroy를 검증한다. 기본 cohort는 변경하지 않았다. 이는 별도 계약 시험이며 원 LifetimeLoss1의 재현이라고 부르지 않는다.

Editor165는 새 helper2개와 CSV의 auto*가 설치 UE의 GetPawn 반환형 TObjectPtr를 추론하지 못해 exit6으로 정상 종료했다. 네트워크 실행은 시작되지 않았다. 정확한3선언을 명시 APawn pointer/GetPawn().Get()으로 교정한 Editor166은 full direct UBT/exit0이다. 실제 OwnerView1은0lag/0loss에서 phase29 strict fail·actual0×3, source/runtime 불변이었다. 서버의 current/ack Pawn286@40000·owner connection2·State226/ASC tuple은 정상이며 raw view는 old264@2050, cache는2051 부근이었다. 실제 slot1인 Client2는 phase30 current286 tuple이 정상이었으나 phase29 final에서 PC.Pawn/ack/State.Pawn/ASC Avatar가 null이고 old264만 남았다. 이는 명시적 카메라 분리에서 소유 root가 유실되는 관측이며, 원 손실 실패의 직접 원인은 아직 미확정이다.

설치 Iris는 PC→Pawn 일반 dependent를 만들지만 creation-dependent object를 그 구제에서 제외한다. PS→Pawn 생성 의존성은31차 remote200의 PS PendingCreate/초기 준비 지연을 해결한 별도 계약이므로72차 local guard가 이를 대체하지 않는다. 이 보호와 default Spatial/nonowner cull을 유지하면서 정확한 소유 연결에만 inclusion하는 후보를 검토했다. 같은 PC/connection/System 수명의 native group1개 재사용, 약한 수명/정확한 handle·connection, cap 소진과 등록 대기의 구분·기존 세션의 명시 실패 결과가 구현 기준이며 새 전역 manager나 per-frame scan은 없다.

**적용 이후:** 독립 검토한 `520b59e8…`6파일을 Source에 통합했다. generic authoritative PC의 비복제·Tick 없는 component1개가 connection/System incarnation당 native group1개와 현재 controlled Pawn1member를 재사용한다. 기존 Player 생성 의존·등록 hook과 generic Controller notifier가 변경 시에만 one-shot WorldPostActorTick을 요청한다. 명시적인 registration pending/초기 admission 실패, old connection mask 퇴역·full member identity, native Shutdown 후 Impl 접근 방지, logical close와 지연 physical retirement를 구분한다. 예외 퇴역 ticket은 정확한 group과 weak System/bridge만 소유하며 phase/cleanup/GC에서 해제할 때 로컬 shared pin으로 자기 해제 중 접근을 방지한다. retained rider의 ASC Avatar 보호와 현재 controlled Mount 보호는 구분한다. UI에는 서버 용량 메시지만 전달한다.

Editor167은 bridge가 UObject인데 IsBeingDestroyed를 호출한 C2039/exit6으로 정상 종료했다. 정확한1표현식을 UObject의 BeginDestroyed/FinishDestroyed flag 검사로 교정한 Editor168 full direct UBT가 exit0이며 현재 native15 Success·경고0·actual0이다. 이후 Editor169/Game45도 direct UBT/exit0이다. OwnerView2와 원래 view 정책의50ms/2% LifetimeLoss2,75차 수정 뒤의 parallel/loss OwnerView, 역순[1,0] Mount4가 모두 strict pass·actual0×3·timeout0이며 실행 중 source/runtime 불변이다. OwnerView2는37949uu 거리에서 client8.000초/authority8.556초의 분리 view를 유지한 뒤 정상 복원했고, 같은 PC의 Pawn264→286 교체 동안 group token 동일/생성1회를 확인했다. Mount4는 기존18행×3의 소유/foreign tuple·복구 조건을 유지했다.

명시 cap7의 Capacity1은 reserved group1–4/Entry5 뒤 owner group6 하나가 생성되고 다음 할당이 handled ensure로 실패했다. 정확한 PC0/connection1의 GroupCapacity가 기존 GameSessionRejected1로 이어졌으며, UniqueId/endpoint로 거절 Client1을 식별했다. 살아남은 연결의 group6/id6 member 제거·Destroy는 서버 종료 전에 실제 로그로 확인했다. 일반 verification은 false·timeout1·Client1 결과 누락 그대로이며 별도 ExpectedRejectionValidation만 제한 거절/수명 범위에서 PASS다. Client1은 ConnectionLost 후 ThirdPerson 반환 중 BP_NPC_Test 이동 component가 Character 소유 component를 갱신해야 한다는 비치명적 Error를 남겼다. 이 오류는 미해결이며 깨끗한 반환 UI/게임플레이 성공으로 세지 않는다. 정상 exit0만으로 자원 정리를 추정하지 않는다.

근거는 `Saved/ArchitectureBatch74OwnerView/`의 manifest/Safety/RootCompileAmendment와 before/after 지문, 실제 OwnerView1 실패·OwnerView2/LifetimeLoss2 성공의 verification 및 `party-ownership.jsonl`, 설치 API 검토 `Saved/ArchitectureBatch74OwnedScope/NOTES.md`·`CHALLENGE.md`, `Saved/ArchitectureBatch74OwnerScope/Applied.json`·`RootCompileAmendment.json`·`Safety.md`다. cap7은 `Saved/ArchitectureBatch74CapacityV2/Proof_MMOFoundation_20261031_Batch74Capacity1/`의 ExecutionProof·ExpectedRejectionValidation·SafetyRejection과 원 Capacity1 로그/verification을 따른다. 임의 Pawn/Mount registration-last·retained mounted rider·실제 travel/특수 phase·custom GameSession 실패/재진입은 미검증이다. 원 LifetimeLoss1 원인은 이 별도 계약으로 소급 확정하지 않는다. 현재 Game45, 패키지는61차 Stage28이며 전체 목표는 active다.

### 75차 NP serializer worker 계측과 실패 결과 보존

74차 OwnerView2와 원 view 정책의50ms/2% LifetimeLoss2는 strict pass·actual0×3·source/runtime 불변이었다. authority JSONL에서 같은 PC의 group token/생성1회를 유지하며 Pawn264→286으로 member를 교체했고, camera 거리37949uu(반경15000의2배 초과)를 client8.000초/authority8.556초 관측한 뒤 정상 복원했다. 원 OwnerView1/LifetimeLoss1 결과는 보존했다. 그러나 추가 parallel/loss는 frame41 Foreground Worker #0의 NP SerializeDelta에서 CsvProfiler.cpp5308 IsInGameThread assertion으로 중단됐다. 최초 project frame은 저장소 Plugin의 FProjectJNPBlobWriteScope ctor583이었다. 이는 실제 worker 직렬화 경로의 계측 API 오용이며 owner inclusion 실패로 합치지 않는다. Native flags readback은1/1/min2/CVar1이나 특정 connection-tick branch/속도 향상 증거로 세지 않는다.

**적용:** GT 전용 IsCapturing/중복 category query를 제거하고 native CSV_CUSTOM_STAT의 자체 capture/category gate와 TLS 기록을 사용한다. 별도 profiler 상태/락/GT task는 추가하지 않는다. Compile-off/minimal에서는 계측 context/entry/산식을 제외한다. stat명·산식·codec·wire Version·CVar는 보존하며 각 native 기록 시점의 capture 경계는 원자적 scope transaction/ACK가 아니다. 실제 Serialize/full+delta·baseline 보존·overflow/context error를 ThreadPool에서 실행하는 집중 회귀1개를 기존 NP 테스트에 더했다. 설정/단언·category 복원은 GT에서 한다.

Editor169 full direct UBT/exit0, 기존4+신규worker1의 NP native5가 Success·경고0·actual0이다. 동일 옵션의 Batch75OwnerViewParallelLoss2와 역순 Mount4도 strict pass·actual0×3·timeout0·source/runtime 불변이다. Game45 direct UBT도76.93초/exit0으로 완료했다. 첫 CSV-on 요청은 기존 runner의 rendered-only guard가 launch 전에 차단한 것으로 보존하며 실행 실패나 통과로 세지 않는다. 기존 native START/STOP consumer가 generic임을 확인한 뒤 Source를 바꾸지 않는 명시 Saved headless worker 관측 invocation으로 WorkerCSV2를 실행했다. strict pass·actual0×3·timeout0, finalized CSV와 parallel readback·source/runtime 불변을 확인했다.

최종 CSV1901행에서 같은 이름의 native 열을 ordinal로 구분해 보존했고 `ProjectJNPBlobDelta/AllWorkers/DeltaEncodeCPU`가4프레임에서 양수였다. serializer attempt/success-bit 기록도 실제로 존재한다. 이는 worker 직렬화 계측과 capture 종료/파일 소비의 근거이며 특정 native 분기 전체 실행·성능 개선률·전송량/CCU의 보장은 아니다. capture/category 시작·종료에 걸친 entry/completion clipping과 codec 재시도를 ACK나 고유 packet bytes로 해석하지 않는다.

첫 parallel crash 때 CSV 파싱 예외가 마지막 verification 작성보다 먼저 나서 실제 종료 코드가 별도로 남지 않았다. 기존 WaitForExit/handle pin 뒤, result/CSV parse 전에 native-process-exits.json을 저장하게 runner를 보강했다. 원 fail-stop/strict/timeout은 유지한다. 실제 숨김 native child0/3/0과 뒤이은 의도적 missing CSV에서 기록 보존을 확인했고 새 병렬 통합 실행은 native0×3을 이 파일과 기존 verification 양쪽에 남겼다. 이전 crash의 actual exit codes를 소급 생성하지 않으며 서버 로그의 requested status3·종료된 PID·누락 결과를 구분한다.

근거는 `Saved/ArchitectureBatch75NPWorkerCSV/`의 Applied/Safety·before/after·`FinalSourceInputs.json`(66소스, SHA256 `45e2314aa1f6c3d353562b766258e49a45fdda31bde51c56bdd4acdd9e75f2eb`), `Saved/ArchitectureBatch75NativeExit/`의 실제 child probe, `Saved/ArchitectureBatch74OwnedScope/ParallelEvidence.md` 및 원 parallel crash 로그, ParallelLoss2/Mount4의 엄격한 결과다. WorkerCSV2는 `Saved/ArchitectureBatch75WorkerCSVInvocation/WorkerColumnEvidence.json`과 `Proof_MMOFoundation_20261031_Batch75WorkerCSV2/ExecutionProof.json`을 따른다. 원 실패와 첫 guard 차단은 보존했다.74/75의 이 범위는 검증됐지만 반환 맵의 별도 CMC 오류·임의 등록순서/여행·A07 remote history·합산 자원·혼합 부하·미정 서비스와 전체 목표는 미완료다.

### 76차 NPC 배치 직렬화와 반환 맵 CMC 오류 수정

원 cap7 거절 뒤 ThirdPerson 반환에서 발생한 CMC Error를 별도로 추적했다. 공개 editor 조회의 BP CDO/SCS/배치 owned 목록에는 CMC가 없었지만, 새 standalone 단일 맵 로드는 같은 오류와 해당 NPC outer의 `TRASH_CharacterMovementComponent_0`를 재현했다. 설치 엔진은 orphan native subobject를 등록 후 construction cleanup에서 제거·TRASH rename할 수 있다. 따라서 owner-group 용량 거절은 이 기존 로드를 드러낸 trigger였고 CMC wiring을 만드는 writer는 아니었다.

공개 `actor.get_package()`로 확인한 정확한 ExternalActor `Content/__ExternalActors__/ThirdPerson/Lvl_ThirdPerson/4/60/IIBJ6YNVYRN6N7J985930B.uasset` 한 개만 원본 backup/hash 뒤 지원 `save_packages`로 재직렬화했다.6,848→9,819B로 바뀌었고 BP/umap은 보존했다. Transform wrapper의 메모리 주소를 제외한 실제 출력 수치·tags·owned components는 같으며 C++ CMC disable/reset이나 에셋 일괄 삭제를 추가하지 않았다.

Standalone2 actual exit0에서 Error0·정상 Wyvern CMC1·NPC orphan0, Inspect2 actual0/nosave와 이전 공개 NPC JSON 동일을 확인했다. 원 trigger 반복 Capacity2도 native0×3/all closed·Source1,208/바이너리39 before/after 동일이다. conn2 하나의 명시 거절과 group6 회원 제거·destroy를 확인했고 이번 Client2의 실제 기본 맵 반환 완료에는 CMC Error가 없다. 원 cap1 오류와 두 실행의 일반 false/timeout1은 그대로 보존한 별도 제한 거절 결과다. 이는 해당 배치의 오래된 직렬화에 대한 원인 수정이며 임의 travel/다른 배치·A03 Mover/Mass 완료를 뜻하지 않는다. 정상 탈것 CMC는 유지한다.

근거는 `Saved/ArchitectureBatch76Disconnect/NOTES.md`, Resave1의 원본/저장 receipt, Standalone1/2·Inspect1/2와 `RepairProof_Capacity2/RepairValidation.json`(17 checks), `NativeMembershipRetirement.json`이다. 기존 capacity verifier의 판정식은 바꾸지 않고 실행 이름과 입력/출력 위치만 명시했다.77차 진행 사항은 이 검증에 포함하지 않는다.

### 77차 원격 arrival 준비 시작을 기존 world 예산에 연결

원격 RequestPreparedArrival 접수 때 즉시 보내던 ClientPrepare와 목적지 source 광고를 기존 world arrival writer의 첫 유효 Preparing Advance로 옮겼다. request별 비반영 bool은 시작 허가만 뜻하며 readiness/I/O 완료가 아니다. 이미 존재하는 Advance charge 뒤 허가를 기록하고 RPC 후 즉시 반환하므로 같은 세대의 중복 전송이나 callback 뒤 옛 상태 접근이 없다. frame당 기존4 Advance는 신규 authority start도 최대4개로 제한한다. 별도 queue/manager/timer를 만들지 않았으며 local authority/owning client는 즉시 준비한다.

시작 전에는 원격 목적지를 광고하지 않고 early ACK도 shared quota/origin 검사 전에 거절한다. finish는 이전 세대의 시작 여부를 snapshot한 뒤 상태를 비우며, ClientPrepare를 시작하지 않은 취소/만료 세대에는 terminal RPC를 보내지 않는다. 원 deadline은 대기 시간을 포함하고 연장하지 않는다. 시작 후 opaque source identity/geometry, 실제 WP 소비와 nonvacuous completion, collision placement·native Mover outcome 및 기존 recipient/epoch/teardown 계약은 유지했다. 실제 MainWorld sample 소비도 Preparing 내부의 waiting/start를 구분하고, 시작 후 기존 strict source 조건과 별도 unstarted leak 실패를 유지한다.

Editor170 full direct UBT65.827초/exit0, world native4 Success·경고0·actual0(0.153511초), Game46 full direct UBT55.646초/exit0이다. native의6 provider는 한 recipient를 공유하며 기존 writer의 실제 charged1–4 start·waiting source 차단·early ACK quota·취소/GC/세대와 same-frame 재등록 경계를 검증한다. 실제 물리 지면/WP 또는6 socket owner 성공을 뜻하지 않는다. 원 quota 테스트는 started를 명시해 새 early-return으로 무의미해지지 않았다.

실제 `ArchitectureBatch77OwnerOverlap1`은 dedicated+client2 모두 actual0/정상 종료, strict PASS 및 full source/runtime+보호5asset 불변이다. server slot1은 시작 후 destination source를 거쳐 실제 도착했고, slot2는 old Pawn 퇴역과 RestartPlayer replacement/epoch2→3에 따라 기대된 취소·source 퇴역/복구를 검증했다. **실제 waiting_for_preparation_start_observed는 두 owner 모두 false**이며 phase sample은 Preparing을 처음 볼 때 이미 started=true였다. 그러므로 이 실제 실행에서 준비 대기 구간을 관측했다고 주장하지 않는다. unstarted leak/pending source missing은 false이며, 대기/시작 한도의 직접 근거는 native fixture다.

근거는 `Saved/ArchitectureBatch77ArrivalStart/`의 Applied·Safety·ExecutionValidation(12 checks), 실제 Native1/OwnerOverlap1와 before/after, `FinalSourceInputs.json`(66 selected, SHA256 `2e68627d14c9e20ced2365e913ad191502fc9ca3ca6f816b39071f40215774fd`)이다.76차 `RepairProof_Capacity2/RepairValidation.json`17 checks와 exact native membership retirement도 재사용했다. 원 cap1 오류·일반 cap1/2 false/timeout·이전 worker crash는 보존한다. 패키지는61차 Stage28으로 새 NPC ExternalActor 재저장과77 Source의 새 Cook/배포 완료 근거가 아니다.

신규 start와 기존 readiness/commit poll은 같은4 Advance/frame을 공유하며 기존 공정성 정책을 따른다. 과부하에서는 준비 시작 대기가 증가할 수 있고 원래30초 deadline은 그 대기만큼 차감된다. 지연 상한이나 FPS 이득을 보장하지 않으며 calibrated active-count 정책은 미정이다.

이번 한도는 authority 신규 시작률이며 네트워크 수신 burst·전체 활성 source 수·실제 WP I/O/resident memory/GPU/Nav 합산 cap이나 CCU/성능 개선률을 보장하지 않는다. 임의 등록순서/retained rider·실제 travel·remote history·source engine/서비스 결정은 남는다. 전체 목표는 active/미완료이며 완료율은 새로 추정하지 않는다.

### 79–82차 공유 Development 배포와 listen cohort 검증

누적62–77 Source와76차 배치 수정을 기존과 같은5개 root(ThirdPerson·Entry·Crafting·Gathering·MainWorld)의 fresh Cook79에 반영했다.3157 packages/remaining0·Error0·Warning0이며 보호9asset와 source/runtime은 불변이다. Shipping79 full139 actions compile도 성공했으나 실제 초기 Stage79는 Game46과 exact exe hash가 같은 **Development**다. 기존 Stage28/Cook16은 보존했다. 이후81/82는 정확히4개의 fixture cpp/inl·기존 CSV header literal·validator만 바뀌었고 에셋/Config/반영 layout/RPC/게임플레이/Cook closure는 바뀌지 않아 Cook79를 재사용했다. 새 Game48 direct target과 exact hash가 같은 고유 Stage82를 생성했으며 구 실행 파일을 최신으로 재명명하지 않았다.

79 실제 RHI에서는 채집2/제작1/quest와 native0·오류/경고0, cooked 전투 기본3회/native0·PNG3개를 확인했다. 이미지는 capsule/VFX 전투 fixture의 결과이며 실제 캐릭터 전체 visual-quality 인증이 아니다. 별도 독립 default-map 실행은 정확한 cooked NPC1·NPC CMC0·정상 Wyvern CMC1·CMC Error0/native0을 확인했다. 이는 새 Cook의76차 수정 소비 근거다. 해당 RHI/placement 실행은 Game46/Stage79였으며,82에 새 RHI 실행을 했다고 쓰지 않고 위4파일 변화만 있음을 확인한37-check closure로 근거를 재사용한다.

원79 packaged listen+remote2는 phase20에서 실패했다. 실제 own Pawn/PS/ASC·owner group은 정상이었지만 local listen host PS까지 foreign peer count에 합산했다. far HP0은 RemoteHealth 값이고 빈 roster/HUD0은 해당 phase의 정상 조건이었다.81은 기존 public PlayerId의 예약 MAX_int32를 fresh authority listen/local/no-connection host에만 부여해 peer cardinality와 구분했다. unknown/default는 여전히 foreign이고 own/duplicate marker는 실패한다. PlayerId는 InitialOnly이므로 host-before-remotes의 새 cohort에 한정하며 late resend/travel 복구는 보장하지 않는다. host를 포함한 모든 foreign private/roster 검사는 유지했다.

81 Dedicated는 통과했으나 packaged listen은 marker 수신 후 서버의 raw global binding 통계를 두 remote cohort 기대와 비교해 다시 실패했다.82는 raw active_spatial/fallback_bindings 의미를 보존하고, 정확한 marked host의 current PC/State·full handle roundtrip·공개 QueryBinding assigned/index 증거로 observer 기여도를 따로 기록한다. global=기존 cohort 기대+측정 observer partition을 검사하며 알 수 없는 extra fallback/spatial은 계속 실패한다. hardcoded1 차감·filter OFF·host 삭제는 없다. 실제 validator fragment의3양성/13음성은 통과/거절 기대와 일치하며 native runtime 증거와 구분한다.

Editor172/Game48/Shipping82 full direct UBT는 모두 exit0이다. Shipping82는 compile-only이며 Shipping runtime은 실행하지 않았다.82 Dedicated와 새 Development Stage82 packaged listen은 모두 strict PASS·native0×3·timeout0, source/runtime 불변이다. Dedicated observer는0, listen의 host marker2147483647/observer1·full identity는 안정적으로 관측했다. listen phase20 raw2 spatial/1 fallback, replacement phase30 raw1/2, 최종27 raw1/1은 각각 remote cohort와 측정 host fallback1로 정확히 분해된다. subject/retired/gear/privacy/view의 원래 조건은 그대로 통과했다. 원79 aggregate/listen false와81 listen false·native 정상 종료 기록을 덮어쓰지 않았다.

근거는 `Saved/ArchitectureBatch79Deployment/Closure82/Verification.json`(37 checks, SHA256 `fa91a9992ce29e3d75dda77a949052edfb61fa9ed94b74e861f5257766fceb13`), 원79 PackageInputs/Gameplay79·독립 Placement79, `Saved/ArchitectureBatch81ListenCohort/Safety.md`, `Saved/ArchitectureBatch82ListenBindingAccounting/`의 Applied/Safety·PackageInputs·before/after와 실제82 verification들이다.75/77 selected-input 원문과 모든 과거 실패는 보존한다. 최종 `Saved/ArchitectureBatch82ListenBindingAccounting/ExecutionValidation.json`27 checks도 PASS이며,77→81→82의 exact-before chain을 보존한 `FinalSourceInputs.json`66개 SHA256은 `80e017ebb3e5289681d7e27c8fbaa1c204252a3b51de20d798971feab016e08d`다. 새83 runtime 결과는 이 마감에 포함하지 않는다.

78 A08 검토에서는 heavy cosmetic payload가 이미 soft/consumer guard를 사용함을 확인했고 GAS 종료 소비에 필요한 hard gameplay montage는 유지했다. metadata를 무조건 soft로 바꾸는 안은 override 우선순위 문제로 구현하지 않았다.80 검토도 render fence를 GPU readiness로 간주하지 않으며 native view의 공유 duration/cancel 경계가 남아 새 준비 기능을 구현 완료로 세지 않는다. 근거는 `Saved/ArchitectureBatch78AssetClosure/`와 `Saved/ArchitectureBatch80RenderPreparation/`의 좁은 검토 기록이다. 이 배포 체크포인트는 서비스 연동·목표 CCU/성능 이득·physical I/O/resident/GPU/Nav 합산 cap·임의 travel/history 또는 전체 MMORPG 완성을 뜻하지 않는다. 목표 active/미완료를 유지하며 완료율을 새로 추정하지 않는다.

### 83차 같은 Stage82의 실제 RHI와 선택 HLOD handover 관측

새 Source/Cook/Stage 없이 현재 Game48/Development Stage82를 packaged standalone D3D12로 실행해 native0·정상 종료를 확인했다. 정상 ServerTryInteract/Portal/native Mover의 phase1→2→4 및 실제 도착 후 source 광고/CPU 소비 복원을 관측했다. 같은 source cell GUID와 동일 captured proxy27part/502instance를6표본으로 추적했고, source Unloaded와 같은 proxy visible을 한 CPU 표본에서 확인했다. source/runtime과 보호8asset 전후 지문은 같으며82 selected66 SHA256 `80e017ebb3e5289681d7e27c8fbaa1c204252a3b51de20d798971feab016e08d`도 유지한다. 실제 socket/군중 부하 실행은 아니다.

1280×720 PNG2개는 각 stage 뒤 GT viewport draw4회 후 요청/처리 frame51→52·83→84로 기록됐다. root의 직접 endpoint 검수에서는 원점 마을/산림과 도착 교량/하천/원경이 보였다. 선택 proxy의 실제 픽셀이나 연속 무공백·전체 HLOD 최신성·GPU texture/PSO residency-ready는 입증하지 않았고 원 `renderedVisualVerified=false`를 유지했다. `coarseCellAndProxyHiddenAtViewportDraw=true`와 `levelAndProxyHiddenAtViewportDraw=false`는 서로 다른 CPU/GT 관측이다. coarse SDK 상태를 픽셀 공백으로 바꾸거나 GT 표본을 연속 렌더/GPU 보증으로 확대하지 않는다.

근거는 `Saved/Validation/MMOFoundation_20261031/ArchitectureBatch83WorldRHICooked1/`의 원본 Process·arrival·handover·PNG·전후 지문과 `Saved/ArchitectureBatch83WorldRHI/ExecutionValidation.json`11 checks PASS(SHA256 `605a7e1deae7fdcf66c9bad3a963ba3c21bfcc9ba187af00a436b747fe239454`), `VisualReview.md`(SHA256 `900c1e3faca7ab6f2042e15f4fb7e264b066a10977b95ee9f798b751fc2f70eb`)다. 원79/81 실패와82 배포 마감은 보존한다. 이는 기존 동작의 제한된 실행 근거이며 새 GPU-ready 기능·고속 이동/공유 I/O cap·성능/CCU 또는 전체 AAA 완료가 아니다. 목표 active/미완료를 유지한다.

## 개발 파이프라인

[MMO foundation workflow](../../../.github/workflows/mmo-foundation.yml)는 PR과 main 변경에 모듈/카탈로그 계약·기존 Python 회귀·문서 링크 검사를 실행한다. Windows runner·Python3.14·Node24를 사용하며 엔진이나 LFS 에셋 전체를 내려받지 않는다. 세 공식 action은 확인한 v7 commit SHA로 고정했다. [checkout](https://github.com/actions/checkout), [setup-python](https://github.com/actions/setup-python), [setup-node](https://github.com/actions/setup-node)의 현재 계약을 참고한다.

Unreal native·RHI·socket·Cook/Server 검증은 엔진이 준비된 실행 환경에서 별도로 수행한다. workflow 파일 작성과 로컬 동일 명령 통과는 원격 Actions 성공이나 branch protection 설정 완료를 의미하지 않는다. 이번 작업에서 commit/push·원격 CI 실행은 하지 않았다.

## 2026-10-31 21:42 KST 사용자 요청 안전 일시정지

사용자가 다른 작업을 위해 안전한 일시정지를 요청했고 UI의 목표 상태도 paused다. 새 구현·빌드·테스트·에셋 변경을 시작하지 않고 현재 실행 상태만 재확인했다. CIM 조회에 Unreal/UBT/dotnet/LiveCoding/MSBuild/ShaderCompileWorker/Uba/검증 프로세스가 없고 서브에이전트 세 개는 모두 완료 상태다. 다른 작업의 프로세스나 문서를 종료하지 않았다. 컴퓨터 종료와 사용량 기준 중단 요청은 계속 취소 상태다.

재개 기준은 Editor172/Game48·82 Shipping 컴파일·Cook79/Development Stage82와83 동일 패키지 RHI/Portal/HLOD 근거다. `Saved/ArchitectureBatch82ListenBindingAccounting/FinalSourceInputs.json`은 selected66개/SHA256 `80e017ebb3e5289681d7e27c8fbaa1c204252a3b51de20d798971feab016e08d`이며 현재 파일 불일치0이다. Closure82 PASS37과83 ExecutionValidation PASS11을 재확인했으며 원79/81 실패·83 캡처 한계를 보존한다. 전체 목표를 완료로 표시하지 않는다.

84차 `Saved/ArchitectureBatch84NavigationLifetime/NOTES.md`는 NPC/Pet 취소의 기존 tombstone/동일 owner 예약을 native 완료까지 유지하는 계약을 확인한 읽기 전용 검토다. 현재 실제 호출 경로에서 조기 반환·영구 잔류 결함을 확인하지 못했으므로 새 shared ticket/queue/manager를 추가하지 않았다. live-world NavSystem 교체와 실제 목적지 Nav readiness·합산 자원 경계는 미완료로 남는다. 이 턴의 미적용 Source 후보나 진행 중 native 세션은 없다. 재개 시 기존 근거와 실제 프로세스/지문을 먼저 확인하며, 사용자 재개 요청 전에는 새 작업·빌드·검증을 시작하지 않는다.

## 85차 공용 공간 snapshot의 할당 구조 개선 — 2026-11-01

사용자 재개와 도구상 active를 확인했다. 시작 시82 selected66개가 모두 일치했고 기존 Unreal/빌드 프로세스는 없었다. 기존 A03/A10/84 근거를 재사용해 Mover 가드 해제·component TickInterval 변경·임의 active-source cap은 도입하지 않았다. 현재 NPC의 Mover 단일 writer와 native WP 작업의 물리 수명을 보호하기 위한 유지이며 해당 미완료 영역을 해결한 것으로 세지 않는다.

실제 NPCDecisionSubsystem과 WorldEventRegistry가 함께 쓰는 `FTargetSpatialSnapshot`에서 셀마다 별도 `TArray<int32>`를 할당하던 구조를 셀의 First/Last와 snapshot-local `NextInCell` 배열 하나로 교체했다. 최대4096개 유효 값의 기존 상한 안에서만 인덱스를 보관한다. 셀 삽입 순서·HasAny의 origin-cell 우선/조기 종료·Query stable ID 정렬·invalid row 압축/duplicate 실패 후 재빌드·extreme unindexed 경로를 보존한다. worker가 UObject를 읽는 경로, 새 manager/캐시/품질 cap은 없다. 구성/조회 방식의 구조 개선이며 기존 동작 버그의 원인 수정으로 보고하지 않는다.

영구 `SpatialStorageBenchmark`는 동일2048/4096 값·밀집1셀/교차32셀/분산 셀·fresh/reuse를 각각32표본으로 관측한다. Editor173 원본과174 변경의 순차 두 프로세스에서 반환·방문·존재 결과가12조합 모두 같았다. fresh 분산 build p50은2048개225.198→120.398µs,4096개464.898→252.802µs였다. 입력 수집/복사는 측정에서 제외한다. 이 PC의 제한된 순차 관측이며 CPU 주파수/캐시/혼합 worker 경쟁을 통제한 반복 A/B·전체 frame/FPS 또는 CCU 보증이 아니다. 셀별 heap 배열 제거는 코드 구조 근거이고 실제 peak bytes나 allocator 호출 수를 계측했다고 쓰지 않는다. 원본 두 파일·입력 지문·원 native reports 및 `Comparison.json`은 `Saved/ArchitectureBatch85SpatialStorage/`와 `Saved/Validation/Continuation_20261014/MMOFoundation_20261101_Spatial*85/`에 보존했다.

Editor174 full direct UBT23.69초와 native13개(공간 동등성/경계·실제 NPC 배치/중요도/등록 교체·2048 밀집 포함), WorldEvents 수명/실행/공정성3개는 모두 Success·경고0·native0이었다. 독립 리뷰의 같은 셀 invalid-compaction 결합 경계를 기존 OwnedInput에 보강해 Editor175 full3.80초·집중1개 Success/경고0/native0을 확인했다. Game49 full direct UBT31.22초/exit0이다. 첫 WorldEvents wrapper 뒤 추가했던 shell LASTEXITCODE 검사는 native 성공과 무관한 stale shell 값으로 후속 Game 시작을 막았고, 실제 process.json/native0·3 Success를 확인한 뒤 지원 helper의 원래 오류 전파를 재사용했다. 테스트 실패로 덮거나 엔진을 재실행하지 않았다.

최종 selected66 지문은 `Saved/ArchitectureBatch85SpatialStorage/FinalSourceInputs.json`(SHA256 `fb40a680df76b947731b34c42290e2888af8baa2f4c559807fd58f92fed209f7`)이다. 82 대비 변경은 공간 snapshot header/cpp와 영구 test3파일뿐이다. 반영·직렬화/wire·Config·에셋은 바꾸지 않았다. 기존 Cook79/Development Stage82와82/83 원격/RHI 근거는 해당 버전 그대로 유지하며 새 배포물/Shipping/혼합 부하 승인으로 확대하지 않는다. A06 전체 GT snapshot/worker 경쟁, A03 권위 이동 전환, A10 실제 합산 자원·서비스/운영·전체 MMORPG 목표는 남는다.

## 86차 상태 효과 UI의 이벤트 기반 갱신 — 2026-11-01

실제 PlayerUIComponent→StatusEffectModel→HUD 경로의 생산자 비용을 개선했다. 기존에는 UI metadata가 있는 효과가 하나만 남아도 무한/억제 상태까지0.25초마다 전체 ASC handle 수집·metadata·정렬·비교를 반복했다. 이제 bind와 공개 명시 Refresh에서만 전체 membership을 수집하고, GAS의 실제 add/remove로 UI 효과 관측 집합을 유지한다. 각 UI handle에 공개 stack/time/inhibition delegate를 등록하며 억제 중에도 구독을 유지한다. 표시 중 유한 countdown만 기존0.25초 timer를 사용하고, timer는 관측한 UI handle만 요청한다. internal-only·무한·모두 억제된 상태는 UI timer가 없다. 공개 Refresh의 명시적 metadata 변경 반영과 기존 정렬/동일 view 알림 억제·owner-only ASC 소비는 보존한다. UI에서 실제 효과를 종료하거나 새 queue/manager를 만들지 않는다.

구독은 weak model·정확한 ASC/BindingRevision으로 한정한다. Unbind는 source·effect 구독 집합·timer를 먼저 detach하고 해제한 뒤 기존 reset을 방송한다. native 삭제 중 pending effect의 공개 delegate getter가 null일 수 있지만 관측 집합을 먼저 퇴역시키고 native EventSet 폐기와 weak/revision fence를 사용한다. 새 source를 옛 callback이 갱신하지 않는다. Bind는 reset 방송 중 GC에도 입력 ASC의 임시 strong pin을 유지한다. 실제 ClearSources의 Unbind 연결과 stack/time replication의 native 통지 경로는 설치 엔진 코드로 확인했고 새로운 socket 시험으로 세지 않는다.

기존 StatusEffectLifetime/StatusBindingReentry에 internal 관측0, 무한/억제 timer0, 실제 stack/time/inhibition의 수동 Refresh 없는 즉시 반영, 옛 source의3종 구독 해제·native effect 방송 중 nested Bind와 GC를 추가했다. 새 StatusCountdownEvents는 실제 서로 다른 engine frame의 World Tick/timer로 유한 표시 갱신·억제 후 유휴·즉시 재표시를 확인하며 synthetic delegate/직접 Refresh를 쓰지 않는다. Editor176 full UBT32.78초와 집중3개가 Success·경고/실패0·native0이다. 최종 성능 주석만 보정한 Editor177 full5.24초와 Game50 full32.23초도 exit0이며 native3개는 동작이 같은176 근거를 재사용한다.

이는 무한 유휴 polling 및 전체 membership 수집 반복을 제거한 실행 구조 개선이다. GAS의 `GetActiveGameplayEffect(handle)` 자체는 현재 엔진에서 선형 조회이므로 finite 갱신은 최악에 UI 수×전체 active 수의 내부 검색을 여전히 가질 수 있다. 모든 내부 탐색/O(N)을 제거했다고 쓰지 않는다. native RecomputeGameplayEffectStartTimes는 time delegate 없이 StartWorldTime을 보정할 수 있어 별도 종료시각 cache로 치환하지 않고 현재 GAS 값을 계속 소비한다. native3개는 standalone/NullRHI 모델 수명 근거이며 새 원격 복제·화면/부하·FPS/CPU 개선량의 증명은 아니다.

변경은 StatusEffects header/cpp와 기존 CompactHUD/StatusBindingReentry test4파일이다. Config·에셋·반영/wire 필드는 바꾸지 않았다. 근거는 `Saved/Validation/Continuation_20261014/MMOFoundation_20261101_StatusEvents86/`와 Editor176/177·Game50 process.json이다. `Saved/ArchitectureBatch86StatusEvents/FinalSourceInputs.json`은85 selected66개가 변하지 않음을 확인한 뒤 이번4개를 추가한70개/SHA256 `f8073d2750502a611c30b906e6a3be4043f65187a1faee1a2de50ad8ec5667fe`다. 기존 배포물 Cook79/Development Stage82와 Shipping82 compile-only,83 RHI 한계는 그대로이며85–86차 packaged/Shipping 검증으로 바꾸지 않는다. 전체 MMORPG·운영 서비스·목표 규모/혼합 부하와 A03/A06/A07/A08/A09/A10 잔여 경계를 유지하고 목표는 active다.

## 87–88차 제작 worker와 이관 응답의 실행 경계 — 2026-11-01

87차는 Handover callback의 실제 상관관계 결함을 수정했다. 이전에는 A callback이 반환한 B ID/같은 attempt가 B 레코드를 완료할 수 있었다. 보낸 ID/attempt를 값으로 고정해 일치한 응답만 받고, attempt별 shared atomic을 GT 전달 전에 선점한다. 모르는 ID/attempt는 선점을 소모하지 않는다. 중복 worker callback은 추가 GT task를 만들지 않으며 Send(false)도 같은 선점과 경합해 이미 queued된 정상 응답을 재시도로 덮지 않는다. 기존 lifetime revision·현재 state/attempt 검사는 유지한다. 새 queue/manager/RPC는 없다. 원인 수정과 중복 전달 제한이며 실제 분산 authority 이전 구현으로 세지 않는다.

88차는 실제 CraftingStation→SubmitEdit의 순수 제작 계획을 기존 profile worker 안으로 옮겼다. GT에서 기준 snapshot/잠금 집합·canonical recipe 값/digest·진행 규칙·quest/progression/fingerprint를 확정한다. worker는 기준 Encode→정확한 receipt 조회 후 새 요청에만 재료 탐색/정렬·소비·스택 병합·GUID 생성을 수행한다. GT에서 계획한 quest와 같은 Proposed/CAS로 합치고 Encode의 bounded reader 검증을 거친다. known receipt는 현재 recipe가 유효해도 새 제작 계획이나 출력 GUID를 만들지 않는다. 일반 명령의 Proposed Encode도 receipt 이후에만 수행한다. 새 task queue·thread pool·비동기 UObject 접근은 없다.

제작 전 기준 CaptureItems가 기존 ResolveItems로 가방/장비/정의/권위를 검증한다. 순수 planner는 장착/잠금 상태를 바꾸지 않고 모든 변경 수량을 캡처한 최대 stack으로 제한하며 GUID/order/capacity 불변식을 보존한다. 따라서 제작 후 같은 GT 전체 item 해석을 반복하는 경로만 제거했다. 확정 투영의 현재 정의 해석·아이템/성장/quest 검증과 원자적 상태 설치는 그대로다. Work/Gate 예약·원래 completion·OnDurable·FinishWriter/진척 tail·owner 퇴역/정상 shutdown drain 순서도 유지한다. GT snapshot/정의/quest 비용과 로컬 단일-profile 저장 한계를 제거한 것으로 쓰지 않는다.

Editor178 full direct UBT43.84초와 Game51 full61.40초는 exit0이다. 같은 고정4파일에서 이관12개·제작1개·채집1개·영속14개, 총28 native가 Success·경고/실패0·notRun0·native0으로 마감됐다. 실제 native 시간은600.47초다. 새 제작 시험은 첫 worker 계획 앞을 유한 barrier로 멈춘 뒤 live recipe output을 바꿔도 수락한 원래 output1이 같은 커밋으로 확정됨, 정상 recipe의 exact receipt 재요청은 계획0회, missing/capacity/쓰기 실패·lost ACK·Destroy/GC/재접속·복합 quest 원자성을 확인한다. 새 이관3개는 A/B 교차 ID·잘못된 attempt·퇴역 retry/cancel/종료, worker128 burst 및 worker 응답 queue 후 Send(false)를 확인한다. worker burst 시험은 최종 결과를 관측하며 GT enqueue 수 자체를 계측한 것은 아니다.

이번 공통 Persistence 필터에는 기존12,289회 실제 inventory 커밋과16,384회 synthetic bounded stream 장기 시험도 포함됐다. 간격이 긴 로그와 sandbox의 불완전한 process 조회만으로 정체를 단정하지 않고, 같은 native PID의 CPU 증가·새 sequence와 최종 report로 정상 진행/완료를 확인했다. dump/중단/재시작은 하지 않았다. 새 실패가 없으면 같은 장기 시험을 다시 반복하지 않는다. 해당 통과는 이 로컬 repository/명령 계약의 근거이며 운영 DB·멀티프로세스/CCU·성능 이득이나 완료 SLA가 아니다.

실행 원자료는 `Saved/Validation/Continuation_20261014/MMOFoundation_20261101_ProfileAndHandover8788/` 및 Editor178/Game51 process.json이다. 준비 후보/원본 지문은 `Saved/ArchitectureBatch87HandoverCorrelation/`, 제작 원본/빌드 입력은 `Saved/ArchitectureBatch88CraftWorker/`에 보존했다. 후자의 FinalSourceInputs.json은 selected73개/SHA256 `42338f708e724c285504aa16271a7b2ed4c22f151f0585fe8eb9b6de259a7ce1`이며 나머지86 근거 파일은 불변이다. Game51 exe SHA256은 `e5e275355ad2546e67084c2e6c751bfc23a0fe9c764d9499cc4c2216c60646e1`이다. Config·에셋·반영/wire 필드는 바꾸지 않았다. 기존 Cook79/Development Stage82·Shipping82 compile-only와83 RHI는 해당 버전 그대로이며87–88 새 배포 실행으로 바꾸지 않는다. 이관 source fencing·durable destination/receipt·unknown outcome 조회·active transport 교체/퇴역 transfer ID 재사용은 새로 인증하지 않았다. 서비스/목표 규모·합산 자원/혼합 부하와 전체 목표는 남는다.

### Spellblade 이주와 작업 순서 조율

사용자가 별도 GreatSword 모션매칭 채팅에서 이 채팅을 직접 연결해 Spellblade 이주와 두 작업의 조율을 요청했다. 원문을 확인했고 같은 cwd/저장소임을 확인했다. 위87–88 소스 동결·Editor178/native28/Game51 정상 마감과 빈 프로세스 조회 뒤 이주 채팅에 Source/Config/Scripts/Plugins/에셋·공통 문서 및 엔진/빌드/Cook의 다음 작업 소유권을 넘긴다. 그 채팅은 현재4개 diff를 보존하고 CraftingExecutionTests의 캐릭터 include/spawn만 자기 이주 범위에서 바꾼다. 일반 Greatsword 무기 자원과 과거 로그/지문은 구분해 보존하며, 저장 ClassId/definition fingerprint와 UClass/asset redirect가 별개임을 공유했다. commit/branch/index는 이 채팅에서 바꾸지 않았다. 이주 완료·지문/실제 상태 확인 전에는 여기서 새 공통 소스 변경이나 엔진 실행을 시작하지 않는다. 목표 active이며 사용자 pause나 전체 완료로 표시하지 않는다.
## 89–92차 실행 비용과 호환성 검증

2026-11-01 Spellblade 이주 채팅과 Source/엔진 소유권을 나눠 진행했다. 그 채팅의 새 이름/경로·native fixture·Core/PrimaryAsset redirect·protocol9와 안정 ClassId=GreatSword·기존 Basic/Greatsword.Starter를 보존한다. 해당 이주의 Content/SourceArt·전용 문서/CSV·Backup은 이번 독립 묶음에서 수정하지 않는다. 실제 PIE 파지/면·방향/공격·복귀 외형과 장식의 독립 흔들림은 별도 미완료다. 기존73개 지문은87–88 당시 근거이며 이주 후 현 workspace와 같다고 주장하지 않는다.

**91차 실제 변경과 완료한 집중 검증:** 만료된 authority Preparing 요청은 아직 Mover effect를 제출하지 않았으므로 다음 charged Advance 방문 전에도 목적지 WP 광고를 철회한다. local authority는 기존 Pawn predictor, nonlocal은 추가 source 없음으로 돌아간다. 논리 Finish·token·client 통지는 기존 world writer에 남기며 Queued·known native outcome·client Preparing/AwaitingLocalArrival은 유지한다. readiness는 반환 source가 실제 목적지 위치/반경·Activated·단일 zero-offset explicit shape인지 검사해 getter 도중 deadline을 넘은 local fallback을 다른 지역의 목적지 준비로 오인하지 않는다. 최종 native effect의 실제 시각 deadline fence도 유지한다. 이미 시작된 I/O 취소·완료나 합산 I/O/GPU/메모리 상한은 아니다.

이주 채팅의 EditorBuild4에서 두 파일을 직접 컴파일해 Succeeded37.62초를 확인했다. `Saved/SpellbladeMigration_20261101/Native/index.json`의 PreparedArrivalBoundaries/PreparedArrivalExecutionBudget는 각각 Success·entries0이며, 소유자가 session60669 실제exit0을 확인했다. 전체13개는12 Success+별도 무기 Skeleton warning1로 원본을 보존한다. 이후 그 채팅이 영구 Skeleton 저장 계약을 고친 NativeFresh4 Success/warning0·실제0은 그 이주 범위의 추가 근거다. 91차 두 Source 지문은 `Saved/ArchitectureBatch91ArrivalDemandDeadline/AppliedInputs.json`과 일치한다. 새 WP/RHI/원격/패키지·고속 다중 이동 부하 인증은 아니다.

**90차 완료한 실제 호환성 검증:** Source/Config/Scripts/Plugins/에셋 동결과 임시 엔진 슬롯을 명시적으로 인계받아 Entry listen/current Editor9와 보존 Stage82 packaged8 peer를 실행했다. 포트 open만으로 client를 시작하지 않고 실제 첫 deferred OBJ 출력 뒤에 시작해 원래 초기 tick 지연 경계를 확인했다. NetCL55116800/engine44/project1.0.0.0이 같은9 checksum3460139667과8 checksum1988226550의 Hello→invalid checksum→Upgrade 및 peer Upgrade→OutdatedClient를 확인했고 Login/Join·fatal0이다. `Saved/Validation/MMOFoundation_20261101/Protocol9Old8Admission1/verification.json`의13 checks가 모두true, native0×2·runner session25445 actual0·강제 종료/재시작0이다. source1213/runtime39 전후 JSON SHA256은 모두177193847e3c4553643a177702b2078a6c74498f2e4fac1c1c7019177265e1c9이고 구8 exe도 불변이다. receipt seconds는 순차 수집 경과시간이어서 client 실제 실행시간/성능으로 해석하지 않는다. 원문과 준비/분석은 Saved/ArchitectureBatch90Protocol9Admission/에 있다. current9 Game/Cook/Shipping/Server-target·정상 동버전 gameplay·auth/CCU/분산 권위를 증명하지 않는다.

**89/92차 Source 적용, 빌드·native는 대기:** 이주 채팅이 오프라인 소스 범위를 반환한 뒤 현재 원본4개 지문과 patch preflight를 확인하고 최소 diff를 적용했다. 89차는 실제 melee notify의 Kismet sphere wrapper와 ignore/object-type 변환 임시 배열을 동일 native SweepMultiByObjectType으로 바꾼다. TraceTag/stat·owner ignore·Pawn mask·simple collision·physical material/face policy와 local OutHits, authoritative pose/SSR·window/spatial revision·재진입/종료 계약은 유지한다. 기존 SpatialDiscontinuityTrace에 실제 wrapper 결과와 생산 notify의 GAS HitResult 순서·metadata/geometry·self/nonPawn 제외를 비교하고 기존 callback 중 teleport도 유지한다. capsule FaceIndex 비교는 복잡 메시 face-remap의 새 runtime 인증이 아니다.

92차는 capture당 최대2048개 Targets의 전체 임시 복사를 고정 count/index+현재 FTarget row 값 복사로 바꾼다. IsDead 전 row의 strong pin과 callback 직후 registry revision/active fence를 유지하므로 제거/추가·team 변경·종료에서 다음 row를 읽지 않는다. 기존 Values/CapturedActors와 immutable worker snapshots·queue/cadence/취소는 유지한다. NPCSnapshotRetired의 실제 IsDead callback 안에서 현재 row 제거→공개 API 재등록→GC, 그 capture의 worker 미제출과 다음 capture 정상 복구를 추가했다. 배열 압축/순서 변경이며 allocator relocation을 강제했다고 주장하지 않는다.

적용4개 실제 지문은 `Saved/ArchitectureBatch92NPCTargetCapture/AppliedInputs8992.json`이다. SpatialDiscontinuityTests의 Saved 후보 LF와 현재 CRLF 차이는 정규화 후 내용 동등성을 확인했고 기존 working-tree CRLF를 유지했다. 나머지3개는 후보 byte hash도 같다. scoped whitespace/원본 대비 최소 diff는 통과했지만 열린 검수 Editor18028 때문에 **아직 새 UBT/native/Game을 실행하지 않았다.** 그 세션에는 실제 PIE를 시작할 때 임시 visual profile·combat/공격·캡처를 수행하는 LiveQA observer가 등록되어 있고, CUA는 사용자의 Esc로 중단됐다. 사용자 사용 여부/정상종료 확인 전 세션을 재사용·조작·종료하거나 새 빌드를 시작하지 않는다. 소유권 반환 자체는 열린 사용자 세션 조작 허가가 아니다.

정상 종료와 빈 프로세스 확인 뒤 두 개선을 같은 direct UBT와 집중 trace/NPC snapshot·callback 회귀로 검증하고, 이주 후 Game target/배포 경계도 위험에 맞춰 이어간다. 현 Source는 이주 채팅의 EditorBuild6/NativeFresh4 및 앞90/91 검증 **이후** 변경되었으므로 그 근거를 새89/92 통과로 옮기지 않는다. 임시 배열/전체 복사 제거의 구조 개선이며 CPU/FPS/CCU 개선량은 미측정이다. A03 권위 이동 전환·A06 GT 전체 수집/worker 경쟁·A07 원격/인계·A08 hard closure·A09 실제 pose·A10 합산 자원과 미정 서비스/운영·전체 MMORPG 목표는 남는다.

사용자 확인 대기: 같은 검수 Editor18028·LiveCoding20300이 세 차례 연속 목표 턴의 실제 프로세스 조회에서 유지됐다. 독립적인4 cpp 적용·최소 diff/내용/지문·문서 링크283개/3235개(오류0, 과거 로컬 archive 부재12개) 검사는 마쳤으며 다음 필수 작업은 새 direct UBT/native이다. Esc 중단 세션의 사용 여부/정상종료 응답이 아직 없어 새 빌드를 시작할 수 없으므로 목표를 blocked로 전환한다. 완료나 사용자 요청 pause가 아니며 세션·소스·에셋을 임의 종료/변경하지 않는다. 응답 또는 외부 정상종료 뒤 목표를 재개하면 현재4개 실제 지문과 프로세스를 재확인하고 Editor179·집중 trace/NPC 회귀·Game52 순서로 이어간다. 새 Cook/배포·성능 주장 전에는 이주 후 현재 입력을 별도로 검증한다.
### 89/92차 사용자 재개와 최종 통합

사용자가 검수 에디터 종료·계속 작업을 명시했다. computer-use의 현재 반환 창을 확인해 정상 종료를 요청했고 LogExit/로그 닫힘 및 권한 있는 CIM의 Unreal/LiveCoding/UBT/worker0으로 확인했다. 강제 종료나 미저장 에셋 삭제는 없으며 등록된 임시 LiveQA observer도 그 프로세스와 함께 끝났다. 목표 도구도 active로 확인되어 앞 사용자 확인 blocked가 해제됐다. 이주 채팅의 Content/SourceArt·전용 문서/CSV·Backup과 실제 PIE 파지/방향·공격/복귀 미검증은 보존한다.

Editor179 full actual0 후 첫8 native는7 Success/1 Fail·native255였다. SpatialDiscontinuityTrace의 compound metadata 조건만 실패했으며 다른 필드 값이 없던 원 결과를 유지한 채 Editor180의 필드 진단을 실행했다. Diag1도 native255지만 실제/원wrapper 두 hit 모두 materialValid0/0·materialEqual1, actor/component/face/item/element/bone/flags/time/distance/depth와 모든 geometry delta0임을 확인했다. 즉 hit 동등성 차이가 아니라 즉시 생성한 transient material의 가용성을 항상 보장한다고 둔 fixture 전제였다. 설치 Chaos의 query-material snapshot은 새 solver 생성/정상 physics post-tick에 갱신되며, world 생성 뒤 신규 material은 그 즉시 query snapshot에 있다고 보장되지 않는다.

생산 코드에 timer/flush/fallback을 추가하거나 비교를 완화하지 않았다. fixture는 GEngine 공유 material의 공개 GetPhysicsMaterial()을 World/solver 생성 전에 호출하고 같은 material을 두 대상에 사용한다. 원wrapper가 실제 준비된 material을 반환한다는 조건을 추가했고 기존 actual material nonnull·pointer/metadata/geometry 동등성 조건도 유지해 최종 통과했다. 새 단계 대기나 private API는 없다. capsule fixture의 복잡 메시 face-remap 한계는 그대로다. 별도로 처음 후보가 owner를 query 생성자에 넘기면서 생긴 OwnerTag 차이를 찾았고, owner 없는 생성 후 AddIgnoredActor로 바꿔 원wrapper의 정책을 보존했다. 이 차이가 material 실패 원인이라고 주장하지 않는다.

Editor182 full actual0·수정8 native Success/경고0·native0 뒤 전체 Game52는 새 이주 테스트의 RerunConstructionScripts 호출2곳 때문에 C2039/native6으로 실패했다. 해당 테스트는 EditorContext이고 API도 Editor-only이므로 소유 채팅과 조율해 Project_JWeaponSkeletalVisualTests.cpp의 바깥 가드에 WITH_EDITOR만 추가했다. gameplay나 에셋을 수정한 우회가 아니다. Game53 full actual0, Editor183 full actual0 및 최종9 native Success/경고·실패/notRun0·native0으로 마감했다. 마지막9는 기존8과 실제 무기2파츠/재구성 Editor 테스트를 포함한다. 각 증분 시간은 해당 원 process.json에 있으며 빌드 시간을 frame 성능으로 사용하지 않는다.

최종 근거는 `Saved/Validation/Continuation_20261014/MMOFoundation_20261101_EditorBuild183/`, `MMOFoundation_20261101_GameBuild53/`, `MMOFoundation_20261101_MeleeNPC8992Final/`이다. 최초 실패 `MMOFoundation_20261101_MeleeNPC8992/`와 `MMOFoundation_20261101_MeleeMetadata8992Diag1/`, Game52를 그대로 보존한다. Editor182의8개 실행은 전후 Source/runtime 지문도 일치한다. 최종 전체 Source1213/runtime39는 `Saved/ArchitectureBatch92NPCTargetCapture/FinalWorkspace8992.json` SHA256 `10e41114e22fb556d2ced919bd695f2b7f6012525e1847e347196b2f7619f8a0`, Game53 exe는 `732416e8e02c21f8fe51a314384762744e6df2fac1aaad7d3ed34001c0d1a804`다.90 이주 후 기준과 비교하면 의도한4 cpp 및 Editor 가드1 cpp만 변경됐고 다른 Source는 보존됐다. 최초 AppliedInputs는 실패 당시 값이며 수정 후 최종 지문과 구분한다.

이 마감은 구조적 임시 할당/복사 제거와 수명/판정 보존의 근거다. 실제 CPU/FPS/CCU 개선량·혼합 부하, 새 current9 Cook/Stage/Shipping/Server-target·실제 PIE 외형 검수로 확대하지 않는다. 기존 Cook79/Development Stage82와90의 old8 거절·91의 world 경계 결과는 각각의 입력/범위로 유지한다. 다음 A07은 현재 Mount가 Iris stop/re-register될 때 기존 owner scope writer가 다시 깨는 실제 경계의 후보 검토이며, 아직 재현/수정이나 default-on 완료로 표시하지 않는다. 전체 목표는 active다.

## 93차 탑승 Actor 등록 수명과 소유자 복제 그룹

2026-11-01 기존 Mount→rider runtime-owner prerequisites 알림을 Iris start/stop에도 연결하고, PlayerCharacter는 authority에서도 이 알림을 구독한다. ASC 관측은 기존처럼 client 전용이다. authority는 같은 world의 현재 reciprocal Mount에 기존 scope Wake(false)만 예약하며, 실제 group 변경은 기존 post-actor writer의 primary driver/connection/full-handle 검증에 남긴다. stop callback에는 driver 식별자가 없어 직접 Withdraw/Destroy하지 않는다. 기존 EndPlay 구독 해제·strong pin·종료 가드와 모듈 의존성을 유지하고 새 manager/tick/polling을 추가하지 않았다.

새 opt-in `Run-SourceAuditNetwork.ps1 -MountOnly -MountReplicationRestart`는 기존 두 소켓 참가자의 원래18행 cohort 뒤에 같은 actor/possession/ASC tuple의 public Iris Stop/Start와 native group 관측을 연결한다. SetReplicates의 RemoteRole 변경 부수효과를 피하고 bReplicates/role·tuple·possession을 fixture에서 고치지 않는다. 원래 기본18행 조건은 유지하며 선택 실험은24행과 warm/stopped/restored/dismounted 네 phase를 모두 요구한다. deadline이나 기존 판정을 완화하지 않았다.

생산 변경 전 Baseline1은 실제 primary handle116→128의 complete/roundtrip1 뒤에도 group member116·ready0·pending0이20초 유지돼 실패했다. native0×3과 source/runtime 불변은 정상 종료 근거이며 semantic 성공이 아니다. 수정 후 Fixed1 및 관측 상세만 보강한 Diag1에서는 같은 group30098325511/groups_created1을 재사용해 member/current128·full/identity/member/ready1·pending0으로 복구했다. 그러나 두 실행의 **전체 선택 원격 검증은 case43 step2에서 실패 상태로 유지**한다. 클라이언트 새 Mount_1과 올바른 AP/SP role·PC/PS back-reference가 생겨도 rider component의 MountedMount는 이전 Mount_0를 유지했다.

이는 RemovedFromWorld로 일부 actor만 등록 중지하고 외부 same-pointer 참조를 유지한 경계다. 설치 엔진의 해당 stop flags는 정상 원격 Destroy를 요청하지 않으며, 일반 object property의 값 비교는 UObject pointer가 같으면 새 Iris ID만으로 dirty가 되지 않는다. PC/PS는 native Pawn OnRep back-reference 경로로 갱신돼 일반 component와 구분된다. ForcePropertyCompare는 legacy ActorChannel이고 ForceNetUpdate/push polling은 member 재양자화 보장이 아니다. 재Possess/null-set/role·ASC reset·private fragment 변조·새 wire incarnation 필드로 통과를 만들지 않았다. strict 실험과 원실패를 보존하며 전체 remote reference 복구·old ACK/retirement·travel은 미완료다. AlwaysRelevant fixture이므로 원격 표시나 요청한 inclusion을 거리 필터 효과·private allow-bit 증거로 확대하지 않는다.

Editor186/187와 Game54 full direct UBT는 실제0, 관련 탑승/교체/GC/종료9 native는 Success·경고/실패/notRun0·실제0이다. 기존 실제 기본 탑승/하차·공개 tuple/health·owner-only item/equipment cohort는 정상 및75±25ms lag/10% loss/5% duplicate에서 각각18행 authority+각 client18행, 전체 passed=true·native0×3·source/runtime 불변이다. 근거는 `Saved/Audit6850/Round3/Network/MMOFoundation_20261101_Batch93Mount{RegistrationBaseline1,RegistrationFixed1,RegistrationDiag1,Default1,DefaultLoss1}/`와 `Saved/ArchitectureBatch93MountRegistration/NOTES.md`다. 현 Source1213/runtime39는 `FinalWorkspace93.json` SHA256 `2b7380fad573e597c1e64fdc55243383ded3a38bad7d475caabe4fdc03add1e6`이며89/92 기준 대비 의도한5 Source와1 script만 달라 나머지는 보존됐다. Editor184의 최초 TObjectPtr 조건식 추론 컴파일 실패와 명시 APawn* 수정도 남긴다. 새 Cook/Stage/Shipping·성능/CCU 보장은 없으며 전체 목표는 active다.

## 94차 NPC 결과 FIFO의 값 소유권

기존 world NPC scheduler의 ready 결과를 UE TDeque의 private 값 record로 저장한다. 기존64개 outstanding 상한을 inline 용량에도 적용해 결과별 MakeShared와 RemoveAt(0)의 뒤쪽 배열 이동을 제거했다. world당64개 record의 고정 공간 비용을 갖는다. Agent의 공유 수명, worker 값 소유권, token/revision/age 검증과 queue/counter/bPending을 callback 전에 퇴역하는 순서는 유지한다. head를 로컬 값으로 move/pop한 뒤 소비하므로 callback의 producer 퇴역/queue clear/WorldEndPlay·GC가 현재 delivery 값을 무효화하지 않는다. 새 manager/queue 상한/스레드를 추가하지 않았다.

기존 공개 producer/Core service로 실제 두 query가 한 worker batch에 들어가고 ready2개가 준비된 뒤, 첫 소비 callback에서 peer와 마지막 producer를 퇴역하거나 WorldEndPlay하고 GC하는 회귀를 추가했다. private injection/friend나 가짜 결과 방송을 쓰지 않는다. Editor188 full direct UBT 실제0, 해당 새 경계를 포함한15 native와 기존96 NPC NativeBatch·Revalidation·RegistryChurnFairness3 native가 모두 Success·경고/실패/notRun0·실제0이다. 기존1ms collection/worker deadline을 완화하지 않았다.

입력/적용 지문과 설치 UE container 근거는 `Saved/ArchitectureBatch94NPCReadyQueue/{Inputs.json,AppliedInputs.json,EngineInputs.json,NOTES.md}`다. 이 Editor 검증 직후 Source1213/runtime39는 `VerifiedEditor94.json` SHA256 `a9c43eb302920138cb64c3c5f619c8e53b2b5f0e423f8bda07399557be4c8228`이며 Game54는93 입력이다. 이후 별도 Game55 full direct UBT도 실제0으로 통합했다. 최종 runtime을 포함한 FinalWorkspace94.json은 Saved에 보존한다. GT 전체 수집·worker 경쟁·FPS/CCU 개선량을 완료로 세지 않으며93의 strict 원격 참조 실패와 나머지 목표도 유지한다.

## 95차 월드 목적지 준비의 동시 admission

기존 world arrival writer의256 pending records·프레임64 visits/4 advances는 처리량 예산이며 동시 목적지 준비 허가와 다르다. WorldDefinition에 `MaxSimultaneousPreparedArrivals`를 추가했다. 유효 범위1..256과 runtime/DataValidation 검증을 두며 default256은 기존 허용량을 유지하는 호환값이다. 실제 하드웨어/부하에 맞춘 적정값이 아니다. 기존 asset을 바꾸거나 임의의 작은 운영 제한을 기본 적용하지 않았다.

기존 Entry가 정확한 token/component/request에 묶인 authority permit 한 개를 소유하고 O(1) 조회/허가/반환한다. local immediate와 deferred local/remote Advance는 같은 gate를 통과한다. 포화되면 같은 Preparing 요청·원래 deadline·기존 round-robin에 남으며 local은 현재 Pawn predictor, nonlocal authority는 추가 destination 없음으로 유지한다. read-only source query는 permit를 만들거나 반환하지 않는다. 실제 source getter·ready ACK·native Mover effect도 현재 permit를 요구하며 다른 world/퇴역 component의 허가를 사용하지 않는다. grant와 bStarted는 RPC 전에 확정하고 이전 Entry 참조를 callback 너머로 보존하지 않는다.

Queued/known·unknown outcome의 기존 관찰 수명을 유지하고 기존 terminal Finish→exact Unregister/Remove 또는 Stop에서 반환한다. Stop은 map을 먼저 분리하고 active count를0으로 만든 뒤 정리한다. policy 축소나 잘못된 새 정책은 신규 허가만 막고 이미 시작한 grant를 회수하지 않는다. 종료 통지는 현재 광고 permission과 구분해 기존 Request+bArrivalPreparationStarted 이력으로 판단한다. client는 기존 trusted ClientPrepare hint와 독립 AwaitingLocalArrival 수명을 유지한다. 이 예산은 authority 논리적 준비 허가이며 총 client source·native I/O/task·resident/GPU/Nav cap이 아니다. local immediate 호환 경로가 있어 strict FIFO/무기아도 보장하지 않는다.

Editor189와 첫 집중8 native는 실제0이었다. Game56은 stale generated header 때문에 실제6으로 실패했다. Source GENERATED_BODY는25행인데 Game 생성 헤더는22행이었다. 원인은 후보를 적용한 root의 Copy-Item이07:01 후보 수정 시각까지 보존한 반면 이전 Game55 UHT timestamp는07:02였기 때문이다. 설치 UHTExecution.cs1027은 header timestamp>saved timestamp로 감지한다. Source 후보를 적용할 때 실제 적용 시각이 기록되게 바이트를 쓰는 방식으로 수정했고 vendor/설정·캐시 삭제·헤더 강제 생성 옵션을 바꾸지 않았다. old generated headers/uhttimestamps·입력 시간·원 Game56 로그를 `Game56StaleHeaders/`에 남겼다. 이는 이 후보 적용 절차의 원인 수정이며 엔진 전체의 영구 결함 수정이라고 하지 않는다.

최종 Editor190/Game57 full direct UBT 실제0·world 집중8 Success/경고·실패/notRun0·native0이다. Game 생성 헤더25행과 새 reflected admission property의 실제 생성을 확인했고, 동일 명령의 Game58도 실제0/1.02초·Source/runtime 불변으로 재확인했다. 반복 빌드 시간은 게임 성능 수치가 아니다. 기존 default burst/frame/frontier, 실제 Entry/permit를 가진 native movement fixture, local/remote 포화·정책 변경·Queued 만료·GC·stale token·Stop을 검증했다. standalone fixture는 WP 준비 완료를 꾸미지 않으며 새 실제 MainWorld/remote/GPU/I/O 부하 검증으로 세지 않는다. 다음 실제 owner 이동 통합에서 현재 policy/permit 관측을 연결할 경계다.

원본/후보와 root 추가 guard·종료 이력·적용 시각은 `Saved/ArchitectureBatch95ArrivalAdmission/{Inputs.json,AppliedInputs.json,FinalAppliedInputs.json,Rationale.md}`로 구분한다. 최종 `FinalWorkspace95.json` SHA256 `2bfb9822c3fd5dc6b57784427e1e9f289733898ae5976fe51abc0c081e77a611`의 Source1213/runtime39는94 기준 대비 의도한7개 파일만 변경됐다. 새 Cook/Stage/Shipping은 없고93 strict 원격 참조 실패, A03/A06/A07/A08/A09/A10·운영 서비스와 전체 MMORPG 목표는 미완료/active다.

## 96차 현재 소스의 실제 MainWorld permit 수명

기존 non-Shipping world probe의 phase_samples에 authority active/peak permit·admissions/deferrals 네 값을 GetStats의 const 조회로 추가했다. standalone과 owner probe의 같은 관측 helper를 재사용하며 생산 요청·phase·pose·변환·ACK·deadline을 변경하지 않는다. 실제 query/수신/이동 writer는95차의 기존 경로다. Source 변경은 기존 fixture cpp/inl 두 개뿐이고 별도 runtime manager나 새 에셋을 만들지 않았다.

기존 A10 uncooked runner와55/83 standalone 판정 의미를 재사용해 현재 Editor191 모듈·MainWorld source map/DA·정상 BP GameMode/PC/Avatar에서 실행했다. 새 Cook/Stage/패키지를 만들거나 보존 Stage82/protocol8을 현재 소스로 주장하지 않는다. NullRHI·trace/스크린샷/HLOD 관측 없이 원래 normal ServerTryInteract handler→Portal→WP source→요청별 native Mover receipt→실제 floor/capsule 도착을 확인했다. standalone 처리이므로 실제 socket 입력 RPC 커버는 아니다.

`Saved/ArchitectureBatch96ArrivalRuntime/MainWorldNull1/Verification.json`은 passed=true, native0·timeout/강제 종료0이며34.406초에 종료했다. 실제 origin(-93200,-93200,2348.750)→S01 authored hint(6800,16800,7800)→floor/capsule destination(6800,16800,7897.999)이었다. Preparing1/Queued2는 현재 permit1·destination source at hint, Arrived4는 permit0·정상 source at Pawn이다. peak/admissions1·deferrals0이며 원점과 최종 readiness는 같은 provider identity를 엔진에서 소비하고 실제 geometry/충돌 조건을 만족했다. standalone에 없는 원격 restored_normal_source_consumption hash 동등성을 주장하지 않는다.

원래 fixture120초·engine SECONDS150을 유지했고 외부150초 관측이 만료되더라도 같은 PID 정상 종료를 기다리는 runner로 준비했다. 이번에는 만료되지 않았다. Source1213/runtime39와 MainWorld/HLOD·이주된 Spellblade BP/Weapon을 포함한 보호9개 asset SHA가 전후 동일하다. raw uncooked game 로그의 WorldPartitionHLODUtilities/HLODBuilderInstancingSettings import warnings와 optional profiler DLL 부재는 보존하며 이번 Portal/permit 성공을 새 HLOD/표현 검증으로 넓히지 않는다. GPU/Nav·메모리/physical I/O cap·고속/대규모 이동·FPS/CCU나 visual 품질 인증도 아니다.

Editor191와 Game59 full direct UBT 실제0이다. 최종 `Saved/ArchitectureBatch96ArrivalRuntime/FinalWorkspace96.json` SHA256 `e0344cb0148c946bbc1f5a7242d3af0c467f593fecfd689e8857df37e7a29fae`는95 기준 대비 의도한 두 관측 파일만 다르다. 원본/적용 hash와 영구 probe는 유지한다. 다음97차는 기존 strictdefault overlap을 바꾸지 않는 opt-in cap1 backpressure/실제 permit owner 퇴역/대기 owner 도착의 별도 검증을 Saved 후보로 검토한다. 목표는 active이며 전체 아키텍처 미완료다.

## 97차 실제 두 owner의 포화와 permit 인계

기존 두-owner probe에 명시적 non-Shipping cap1 분기를 추가했다. 기본 모드는 기존 client TryInteract·양쪽 Preparing/source/pending barrier·slot2 퇴역·native 도착·replacement/원격 engine-consumed hash 판정을 유지한다. cap1은 입력 RPC를 대신했다고 주장하지 않고, 실제 두 owner의 origin-ready 뒤 같은 GT callback에서 공개 RequestPreparedArrival 두 건을 등록한다. production writer가 실제 허가한 slot을 관측해 동적으로 선택하고 기존 public UnPossess/Destroy/RestartPlayer로 그 recipient를 퇴역시킨다. phase/role/pose/ACK·movement를 주입하거나 인위 대기하지 않는다. 기존 Bind/Current/BaseSource/Restored/NewAvatar와 run/slot/character/account/ASC/source/connection fence를 재사용했다.

cap1 튜닝은 서버 process의 live definition만 변경하고 원래 유효 정책을 기록·정상/실패 Finish에서 복원한다. 디스크 asset 저장이나 vendor/이주 원본 변경은 없다. 예상치 못한 다른 정책 writer가 있으면 덮어쓰지 않는다. 신규 flag는 기존 WorldSmoke+OwnerArrival+Overlap 모두 없으면 초기 실패다. 정책/원시 표본은 별도 world-arrival-backpressure.json에 최대8개 중 정확6점으로 기록하며 기존 overlap_observed=true를 대신하지 않는다. strictdefault와 cap1은 별도 의미의 검증이다.

Editor192 full direct UBT 실제0 뒤 현재 uncooked 소스/MainWorld에서 dedicated NullRHI server+실제 D3D12 offscreen client2개를 실행했다. `Saved/ArchitectureBatch97ArrivalBackpressure/Default1/Verification.json`과 `Cap1/Verification.json`은 모두 passed/nativeAllZero/semanticAllTrue=true·native0×3이며 Source/runtime/보호9asset이 전후 동일하다. 두 cohort 사이에도 같은 입력/바이너리/보호 에셋을 요구했다. default는 실제 입력 RPC·양쪽 준비와 slot2 퇴역/slot1 생존을 그대로 확인했다. cap1은 실제로 먼저 허가된 slot1을 퇴역시켰고 대기 slot2가 생존·도착했다.

cap1 원시6점의 (active,pending,admissions,deferrals)는 baseline(0,0,0,0)→registered(0,2,0,0)→blocked(1,2,1,1)→pawn_retired(1,2,1,1)→waiting_admitted(1,1,2,1)→terminal(0,0,2,1)이다. peak는 최대1, 여섯 점의 실제 정책은1이며 원래 정책 복원도true다. release와 재허가는 같은 production tick일 수 있으므로 중간 active0 표본을 강요하지 않는다. 대기 authority source는 허가 전 없고, 허가 후 실제 target·native receipt/물리 도착·client source/consumed hash 복원을 확인했다. 퇴역 client의 Preparing 표본은 default에서 관측됐지만 cap1에서는 Prepare/Finish가 병합돼 **미관측(false)**이었다. 이를 관측 성공으로 세지 않고 실제 Failed·새 avatar/epoch·base PC source·복원 hash/retired source를 검증한다.

reader peer 검토에서 final readiness를 receipt 위치/provider identity에 직접 연결하지 않은 구멍과 필수 admission_limit/previous_limit 누락을 찾았다. 두 실행의 원 RunnerSource/초기 Verification을 보존하고, live process 모두 정상 종료한 뒤 Saved pure reader만 강화해 동일 실제 raw를 재검증했다. 최종 Verification에는 strongerReadinessAndPolicyRecheck=true와 reader SHA256 `c38890d184273abd44ee4ade263d2cedffdff98a1ab5220a02b1fc58b01bbeef`가 있다. world capsule↔receipt 좌표만 capsule radius 안에서 연결하며 partition-local consumed 좌표의 raw equality는 강요하지 않는다. 실제 cap1 JSON 복사본의 origin-as-final·foreign provider·누락/잘못된 limit·누락/fraction/out-of-range previous policy7개는 각각 거부됐다. 이 합성 검사는 native/runtime 성공 추가 건수가 아니고 실제 engine 재실행도 없었다.

이후 Game60 full direct UBT 실제0으로 통합했다. 최종 `FinalWorkspace97.json` SHA256 `5ea312383d8ba19aad48fefc71ec1fbb065f8194f64e6c96f9d8826e9d34df2c`의 Source1213/runtime39는96 기준 대비 기존 두 fixture 파일만 달라 다른 Source를 보존했다. 새 Cook/Stage/Shipping·실제 auth/provider·Queued cancellation 전체·strict FIFO·physical I/O/메모리/GPU/Nav/고속/CCU 성능 인증은 아니다.95 production permit를 실제 원격 포화/퇴역/인계까지 검증한 범위이며 전체 목표는 active다.

다음 A12 검토는 기존25차 `Saved/Validation/MMOFoundation_20261026/Batch25PresentationDraft/SelectedNiagara_EffectTypeRead1/SelectedNiagaraScalability.json`을 재사용한다. 실제 Spellblade CPS→profile→soft Niagara와 vendor EffectType 참조를 확인했으며 단순히 EffectType이 없다고 세지 않는다. load/apply 예산과 warm spawn/active 비용은 별도이며 현재 효과의 effective settings/global FX budget·loop kill/resume·중요 경고 수명을 확인한 뒤 기존 UE scalability와 필요한 project-owned 콘텐츠를 실제 consumer에 연결한다. 아직 새 VFX 정책·에셋·Source 변경/실행은 하지 않았다.

## 98차 원격 warm 이펙트의 프레임 생성 예산

기존 remote weapon/cold-ready loop의 world FIFO를 재사용해 이미 로드된 원격 cosmetic도 생성 예산을 통과하도록 했다. 새 cue authoring 필드 `bUseRemoteSpawnBudget`는 true가 기본이며 기존 saved profile도 재저장 없이 이를 상속한다. local-controlled/viewed·preview와 제작자 false timing 예외는 즉시 경로다. remote loop는 기존 exact attack/path/revision의 PendingLoop를 유지하고 matching stop에서 철회한다. remote one-shot은 그 instant에 남은 credit을 얻거나 started-tag를 확정하고 생략하며 늦게 재생하지 않는다. live queue가 새 one-shot보다 우선한다. gameplay 판정·RPC/wire·NS/EffectType asset은 바꾸지 않았다.

기존4 apply·128 visits·queued soft1ms를 실제 GFrameCounter ledger로 공유해 반복/재진입 Tick이 같은 frame의 예산을 리필하지 못하게 했다. 첫 visit은 soft timer 선검사 때문에 영원히 진척이 없지 않도록 허용하지만4/128 하드상한 밖으로 나가지 않는다. callback 한 번은 soft1ms를 넘을 수 있고 immediate one-shot의 실제 시간은 그 queued timer에 포함하지 않는다. hard count는 생성 시도이며 active emitter/GPU·메모리 cap이나 적정 FPS/운영 수용량이 아니다. 새 manager/queue/worker·component Tick은 없다. 기존 native pool·강한 pin·generation·notify lease·취소를 유지한다.

적용 전 Editor192의 fresh D3D12 selected read는 Success1/자동화 경고0/native0·spawn0, Source/runtime 및 선택3asset 불변이었다. 실제 FX global budget은 disabled, Niagara use-global/culling CVars는 true/1이었다. 기존 vendor EffectType은 존재하지만 선택 NS 둘에 effective count/global cap은 없었고 Trail의2500cm distance override는 유지됐다. 전역 예산이나 vendor asset을 임의로 변경하지 않았다. 이 조회는98 동작 검증10건과 별도다.

Editor193/Game61 full direct UBT는 실제0이다. 같은 Source/runtime에서 Null5, 실제 D3D12 Effects1·Related3·fresh cold-class1의 총10 native가 Success·자동화 경고/실패/notRun0·네 process native0이었다. 실제5 warm default loop·Stop-before-apply/local-view 즉시 loop/one-shot, queued1+immediate3의 공유4와 별도 자연 frame의4/5 one-shot, 회복/다음 frame의 중복·생략 재생 차단을 검증했다. 기존100×12 pool/timing 회귀는 authored false로 그 목적을 유지하고 default true 경계는 따로 실행했다.300×2 장비와4096 포화·취소·재입장은 가짜 frame 갱신 없이 latent 자연 frame을 사용한다. 실제 saved Spellblade CPS/profile의 tag·soft path·loop 계약·default true와 실제 queue/stop 및 dirty 불변도 확인했다. Source1213/runtime39와 실행별 보호6asset은 전후 동일했다.

원본/후보·실제 적용 시각은 `Saved/ArchitectureBatch98RemoteWarm/{Inputs.json,AppliedInputs.json}`이며 root는 WriteAllBytes로 적용해 UHT timestamp를 보존했다. 최종 `FinalWorkspace98.json` SHA256은 `a7f41b98dd1953fd9321b2513f5a1b471fffcb560a5ec6877e974c64883ed9b0`이고97 대비 생산5·테스트2개만 바뀌었다. `RootVerification.json`과 각 실행의 raw/verification을 재사용한다. active FX·GPU 비용/quality·자동 camera-change flush·새 socket/Cook/Stage/Shipping은 미검증이며 배포물은 여전히 Cook79/Stage82다. Basic 무기 측정은 새 Spellblade200 실측으로 바꾸지 않고 파지/공격 방향·접촉 미완료도 유지한다. 다음은 `ActiveFXFollowup.md`의 프로젝트 소유 NS/EffectType 최소 연결·cull/resume/notify 수명 경계이며 전체 목표와93 strict 원격 참조 실패는 미완료다.

## 99차 프로젝트 소유 지속형 FX의 native 활성 정책

기존 실제 Trail profile의 soft reference 한 곳을 프로젝트 소유 `/Game/Effects/ProjectJ/Combat/NS_CombatTrail`로 연결하고 `NET_CombatTrail`을 생성했다. vendor System/EffectType·CPS와 다른 cue는 보존한다. UE distance significance·High·DeactivateImmediateResume·local 보호와 초기 authored effect-type count128을 사용한다. 기존2500cm distance override·fixed bounds·emitter/material/readiness·attach/notify 의미를 유지한다. 새 runtime manager/queue/worker·Actor별 tick은 없으며98 생성 admission과 기존 ManualRelease 수명이 이어진다.128은 데이터로 조정할 초기 한도이고 주기 사이 초과·local 예외·pool 보유가 있어 GPU/메모리 hard cap이나 적정 FPS/수용량이 아니다. resume는 명시적으로 소유한 persistent cosmetic에만 적용하고 finite one-shot/필수 경고의 의미와 구분한다.

Editor 전용 native 제작 commandlet은 기본 dry-run이다. typed9필드·정확한 looping cue/source·clean package·새 대상/sidecar·provider 상태를 검사하고 NET→NS 저장 성공 뒤 원 AP의 한 soft field만 게시한다. verified backup과 partial receipt를 남기며 기존 파일 overwrite/자동삭제·vendor 변경·Git/LFS 설정 변경을 하지 않는다. provider None은 격리 invocation에만 준다. default Dry1 native0·소수 한도 NegativeFraction1 expected1은 생성/저장/기존 에셋 변경0이었다. Apply1 native0은 새 NS262960bytes/NET5847bytes와 AP만 의도대로 바꿨고 native 전체 settings/override/bounds·cue snapshot 및 보호 파일·backup 검사를 통과했다. 별도 peer도 정확6개 경로에서 세 의도 변경과 원본 보존을 교차검증했다. 이는 전체 Content 전수 diff가 아니다.

초기 Editor194는 macro 인자 안 전처리 지시문과 UE5.8 shared JSON key 때문에 실제6이었다. root는 원 소스/로그를 보존하고 lambda를 macro 밖으로 옮기며 JSON key를 소유 FString으로 변환했다. legacy define·vendor·캐시를 바꾸지 않았고 Editor195/Game62 full0으로 확인했다. 첫 실제 -game/D3D12 Continuous1은 Stage0의30초 만료/native255였다. NiagaraWorldManager가 PostActorTick에서 지우는 cache를 latent가 Tick 밖에서 요구한 관측 오류였다. 실제 LP/viewport projection을 확인하고 테스트 local TG_PostUpdateWork의 읽기 전용 값 snapshot으로 옮겼다. cache/clock/FX tick을 주입하지 않고 camera/phase revision·frame age·object identity를 연결하며 모든 종료에서 observer를 해제한다. 원 대기30초/count2를 유지했다.

Editor196/Game63 입력의 vendor Continuous2/High1은 실제 게임 뷰/native0·자동화 경고0, 각30행/10phase였다. 같은 component의 거리 중요도 반전·cull/resume, culled 중 exact lease 종료·attack 교체·old notify·local linkage/복귀와 observer 해제를 native public state로 확인했다. debugger reason은 보조값이다. High의 실제 주기는0.25초였다. 이 테스트2와 실제 authored128을 혼동하지 않는다. culled actor 파괴를 별도 실험했다고 하지 않으며 기존98 종료/GC 근거와 관련 native cleanup 코드를 재사용한다.

저장 뒤 기존 saved-AP 기대값을 새 정확한 경로/정책으로 정리하고, Editor197/Game64 full0 입력 `feec5c626397536854ac03e7678579fa2b81ca94945fc9064cf0591c92df719a`에서 SavedConsumerRHI1과 ProjectHigh1을 새 프로세스로 실행했다. 각각 Success1·자동화 경고0/native0이며 보호7asset·Source/runtime이 전후 동일하다. 전자는 실제 NS/NET getter·bounds·dirty 불변·AP/CPS queue 소비/stop-before-apply와 기존 vendor100×12·98 cold/warm 회귀다. 후자는 실제 saved AP→project NS/type·effective128/High를 재로드하고 그 payload의 transient copy에 테스트2를 적용한30행/10phase 수명 회귀다. raw reader도 actual source identity·설정·fresh native view·반전된 거리·정확한 lease/map/pool 상태를 검증했다. 실제128 동시 시스템 압력이나 pixel/GPU 성능 검증은 아니다.

마지막에는 영구 제작 도구의 batch 전용 기록 경로만 `Saved/Authoring/NiagaraScalability/<run id>`로 일반화했다. Source delta는 Editor cpp 한 줄, runtime delta는 CharacterEditor DLL 하나이며 VFX/game/test 코드·runtime과 정책7asset은 그대로다. Editor198/Game65 full0과 GenericDry2 native0·새 대상 파일/수정0을 확인해 앞선 기능 검증을 그 범위로 재사용했다. 원 경로/runner/receipt·기능 지문은 보존했다. 최종 `Saved/ArchitectureBatch99ActiveFX/FinalWorkspace99.json` SHA256은 `c6e83baa3f12a2f7ed5e96f4780f618b8b6f6d15365b7ac62ae8fa4a8a9f868e`(Source1215/runtime39)이며98 대비 Editor 도구3개·기존 테스트1개만 다르다. `FinalAssets99.json`, `RootVerification.json`, `ApplyPeerReview.md`와 각 native 원문을 재사용한다.

새 Cook/Stage/Shipping·최신 배포 폐쇄·128 압력·GPU/VRAM·픽셀/near-warning 품질·전체 수용량은 미검증이다. 배포는 Cook79/Stage82이고93 strict 원격 참조 실패·미정 source engine/서버·DB·인증 및 전체 MMORPG 목표는 계속 미완료다. 기존 Basic200을 새 Spellblade 실측으로 재표기하지 않고 새 무기 파지/방향/접촉 미완료도 보존한다. 다음 잔여 작업은 기존 A03/A06/A07/A08/A09/A10/A11/A12 근거를 재사용하며 전수 감사를 다시 시작하지 않는다.

마감 diff-check에서 테스트 주석의 끝공백 한 개와 줄바꿈만 정리했다. `WhitespaceClosure.json`은 whitespace-normalized 원문 동일과 runtime 불변을 확인한다. 실제 built 지문13257c33…f48d와 최종 Source 지문c6e83baa…868e를 구분하며 이 비동작 변경으로 추가 빌드/RHI를 반복하지 않았다.

### 100차 NPC 수집의 Tick-local 저장 재사용

기존85/92/94차 근거와 실제 collect→worker→ready→callback 경로를 재사용해 production cpp 두 곳만 수정했다. NPC spatial query의 index 배열을 같은 Tick의 루프 밖으로 옮겨 용량만 재사용하고, Core의 완료 값을 Ready record로 move해 내부 결과 배열의 추가 복사를 없앴다. Query는 매번 Reset하며 Prepare는 index view를 동기 소비해 별도의 owning snapshot을 만든다. 성공 snapshot은 최대32개지만 준비 실패가 있으면 query 시도는 기존 방문 한도128에 도달할 수 있다. index Num은 target 한도2048 이내이며 배열 capacity에는 allocator slack이 있고 Tick 종료에 해제된다.

task의 const/shared GetResult 복사, worker의 독립 값, component별 Actor/위협 ID mapping, 현재 row pin/revision·authority·age·예약 퇴역/취소·실제 worker 수명은 유지했다. 등록부 revision은 위치 revision이 아니므로 다음 Tick까지 위치/결과를 캐시하지 않는다. 새 manager/thread·상한 변경·에셋/설정 변경은 없다.

Editor199/Game66 full direct UBT는 실제0이다. 같은 입력의 기존 집중13 native는 Success13·자동화 경고/실패/미실행0·프로세스 실제0이며 spatial 반복 입력·batch 결과·수명/재검증·등록부 변경·실제96 producer 공정성·callback GC/퇴역을 확인했다. 실행 전후 Source1215/runtime39와 기존99차 보호7asset은 불변이다. `Saved/ArchitectureBatch100NPCCollect/BuiltWorkspace100.json`, `Focused1/Verification.json`과 원 보고를 재사용하며 작은 변경 때문에 전체 감사·Cook·FX/GPU 회귀를 반복하지 않았다. 구조적 할당/복사 감소의 검증이며 CPU/FPS/CCU 개선량이나 전체 A06 완료는 아니다. GT 전체 수집·worker 경쟁·혼합 부하와 다른 잔여 항목은 계속 남는다.

### 101차 도착 유지의 무효 경로 취소

실제 NPCAction의 in-range/동료 도착 유지에서 PathToken이 이미0이어도 CancelPathAndMove가 경로 서비스의 최대64개 Requests를 검색했다. Submit은 wrap 때도0을 건너뛰므로 Cancel의 기존 GT 검사 뒤 token0만 즉시 반환하게 했다. nonzero 취소의 callback 해제·완료 전 물리 slot 보존·stale completion/Stop 처리는 유지한다. Mover 위치 writer·ASC/사망 heartbeat·물리 갱신과 기존 Mass 거절 가드는 변경하지 않았다.

Editor200/Game67 full direct UBT 실제0, 기존 IntentLifecycle·FollowRecipientLifetime·PathAdmission·LatePathCompletion·OutOfRangeDuringPathCooldown의5 native Success·자동화 경고/실패0·프로세스 실제0이다.100 대비 Source 변경은 NPCPath cpp 한 줄이며 전후 Source1215/runtime39·보호7asset 불변이다. 최종 `Saved/ArchitectureBatch101NPCMovement/BuiltWorkspace101.json` SHA256은 `aff040bab9b4aef7c2b684200574e4ba10124bb04d99bf0e500860344b134545`이고 원 보고는 `Focused1`에 있다. 새 테스트·timer·framework·Cook을 추가하지 않았다. 유휴 no-op 검색의 구조적 제거이며 Mover 저빈도/승격·혼합 부하/FPS/CCU는 미검증이다.

병렬 A08 검토는 기존41차 근거와 현재 soft payload·no-render/dedicated admission·취소/공유 lease를 재사용했고 추가 production 변경을 채택하지 않았다. Foley profile 내부의 hard attenuation은 profile 자체의 soft 진입 뒤에 있으며, Family의 hard Retargeter는 AnimProxy 포즈 node의 실제 적용/복원 소비가 있다. 정확한 dependency와 server pose/notify·소켓/판정 동등성 없이 서버 제외나 일괄 soft 전환을 하지 않는다. `Saved/ArchitectureBatch101AssetClosure/ClosureNotes.md`는 좁은 유지/보류 근거이며 전체 hard closure 검증이나 메모리 절감 근거가 아니다.

### 102차 NPC 결과의 수집부터 게시까지 만료

기존 MaxResultAge0.5초가 Core submit부터 시작해 GT 수집/외부 callback 지연을 빠뜨리고, age 검사 뒤 target eligibility 및 component threat ASC getter가 다시 지연될 수 있는 실제 호출 순서를 보강했다. 최초 observer/query origin/공유 target 값보다 앞의 CapturedAt를 기존 closure/Ready record로 전달한다. 준비가 이미 만료됐으면 Core에 제출하지 않고 현재 Agent/slot·context·registration·owner/authority가 정확히 일치하는 준비만 strong pin 상태에서 기존 semantic invalidation으로 퇴역한다. callback 뒤 map/slot 참조를 재사용하지 않고 LastScheduled/순회/cadence도 바꾸지 않는다.

Ready는 기존 pop→논리 예약 퇴역→검증 순서를 유지하며 QueueResults에서 나이만 보고 완료를 유실시키지 않는다. eligibility 뒤 실제 시각을 재검사하고, private ApplyResult의 선택 absolute deadline을 threat getter/current-fence 뒤·SelectedTarget/OnQueryCompleted 게시 전에 확인한다. 만료는 기존 InvalidateQueryContext로 종료한다. standalone/manual 요청은 기본인자0의 기존 경로를 유지한다. Core public Completion·DeliveryMilliseconds·0.25초 deadline·process-wide 실제 worker2개/물리 예약·수량 상한과 wire/에셋은 변경하지 않았다. callback 자체의 실행 시간을 선점하거나 gameplay commit 재검증을 대체하는 구조는 아니다.

OldestReadyResultMilliseconds는 이제 Tick 시작에서 **수집 시작 기준** 나이를 표본한다. DiscardedDecisions에는 미제출 prepared 만료도 포함하고 RejectedBatches는 기존 Core admission 실패 의미를 유지한다. AppliedDecisions는 기존 subsystem acceptance이며 component 내부 equipment/context/deadline 거절이 가능하므로 실제 게시 횟수와 동일시하지 않는다. 추가 시간 필드/manager/통계 체계를 만들지 않았다.

Editor201/Game68 full direct UBT 실제0, 같은 입력의 집중12 native는 Success12·자동화 경고/실패/미실행0·프로세스 실제0이다. 새 NPCSnapshotCaptureAge는 실제 life callback 준비0.55초의 submit 생략, 준비0.30초+실제 worker 완료 후 Ready0.25초의 총 age, 최종 eligibility0.55초의 만료와 각 fresh 회복을 실행했다. DamageThreatSnapshotAge는 실제 source ASC getter0.55초가 component 검증을 지연해도 게시하지 않고 이후 정상 action으로 회복하는 경로다. 기존10개는 batch math/실제 worker·수명/재검증/등록부 churn·prepared supersede·ready retire/GC를 재사용했다. 테스트 fixture의 one-shot ASC getter만 확장했고 production testhook·clock/timeout 조작은 없다.

Source1215/runtime39와 보호7asset은 실행 전후 동일하다. 최종 `Saved/ArchitectureBatch102NPCCaptureAge/BuiltWorkspace102.json` SHA256은 `b238a3c65c148b452d7a10ccb6b2f459a201e84db0f37c6f2196f8b64979adfb`이며101 대비 의도한7개 Source(생산4·기존 fixture/테스트3)만 다르다. `Original`, `AppliedWorkspace102.json`, `Focused1/Verification.json` 및 원 보고를 보존한다. 새 pre-submit 만료 정리 자체에서 peer 재등록/GC/world-stop을 섞는 조합은 exact fence/strong pin의 정적 peer 검토와 기존 관련 회귀를 재사용했으며 해당 분기만의 별도 재현은 하지 않았다. 전체 GT 수집/worker 경쟁·혼합 부하·FPS/CCU·GPU/Nav·실제 provider/서버·새 Cook/배포 등 기존 미완료는 유지한다.

### 103차 실제 렌더 군중과 NPC의 혼합 관측

기존 Rendered -Movement fixture의 authored Mover/animation/cloth와 NPCDecision.Start64/128·명시 observer/nodraw를 한 실제 standalone RHI world에 연결했다. 관측을 위해 실제 QueueResults의 완료 batch 수/누적 compute wall elapsed/Core submit→delivery3값과 opt-in 별도40열 npc_decisions.csv 및 기존 runner 스위치만 추가했다. 원래 frames.csv 스키마/행과 default-off/network observer의 실행 계약은 유지하며 새 worker/스케줄러/asset은 없다. client observer의0 통계는 server 부하 증거가 아니다.

Editor202/Game69 full0, 같은 Source/runtime·보호asset·실행 옵션의 Count1/32 두 RHI가 실제0과 원래 이동/피벗/점프/몽타주/복귀·local 품질 검증을 통과했다. Low1/High1의6556/2981 NPC행은 원 frames의 phase/trajectory_interval_end_frame과1:1이며 누적 counter 차분은 반복 last-value 편향을 피한다. standalone 현재 body route는 pose_routes.csv/manifest를 따르며 과거 Basic200을 재표기하지 않는다. High phase3 PNG는 cohort 존재 확인만 했고 최종 무기/접촉·pixel 품질 승인과 구분한다.

아래는 steady phase2–6 각각의 값 범위이며 합친 전체 percentile이 아니다.

| 화면 authored 캐릭터 | frame간격 P95 ms | 최신 관측 NPC full Tick P95 ms | 완료 batch 계산 평균 us | Core 전달 평균 ms |
| --- | --- | --- | --- | --- |
| 1 | 7.46–8.63 | 0.37–0.53 | 26.7–34.1 | 6.47–7.16 |
| 32 | 15.29–19.99 | 0.67–0.91 | 28.1–31.2 | 14.40–16.44 |

NPC 계산량은 같은 producer64/target128이지만 실제 수학 입력·batch형태나 캐릭터별 표현 품질까지 동일한 A/B는 아니다. NPC timing은 sampler 시점의 최신 완료 Tick, 원 gt_ms는 GGameThreadTime 표본이며 GT critical path가 아니다. batch compute는 wall elapsed·Core 전달에는 프레임/GT polling이 포함되므로 차이를 순수 worker 대기로 해석하지 않는다. 각 steady window의 discard/reject 증분은0이다. 두 실행은 수용량/FPS 보증이나 반복성·열/운영 부하 시험을 대체하지 않는다.

High steady1648행 모두 result_visits16이고1641행은 collect/apply<1ms였다. 연속 frame증분1마다 applied증분16/완료batch증분1이며 outstanding−32×core_pending의 ready 잔여 보수 하한이 항상 양수다. 따라서 이 조건의 약975–1112 결정/s는16/frame에 제한된다. worker 추가보다 기존1ms 시간 예산 안에서 결과 count를 dispatch32에 맞추는 후보를 다음에 비교한다.32×128이 기존 ParallelFor threshold4096을 밟을 수 있으므로 compute/프레임·GT 비용/결과 age도 함께 본다. 결과 age는 순수 queue 체류 시간이 아니며 Core 전달/FPS 감소를 미리 보장하지 않는다.

`Saved/ArchitectureBatch103NPCExecution/BuiltWorkspace103.json` SHA256은 `805c71c1aec609d76bc6e9a701412c14cdd1f55b47c1a404533bce4376586ff6`(Source1215/runtime39)이다. Original/Candidate/Inputs·Schema·Manifest와 두 원 실행을 보존하고 `Comparison1.json`은 frame/counter와 Source/runtime/asset·전체 설정 및 Count/output 경로만 정규화한 argv 동등성을 확인한다. 최초 reader의 설정 동등성 누락은 같은 raw를 강화해 해결했으며 trace/해상도 변조를 거절하는 것도 확인했다. A10의 다음 gate는 sole-demand source 퇴역 후 실제 WP load/unload tail/residency 안정화이며, 공유 목적지 취소를 unload로 오판하거나 Portal에 global Nav-ready를 강제하지 않는다. 전체 provider/서버·A03/A07/A08/A09/A10/A11/A12 및 새 배포/규모/최종 시각 품질은 계속 남는다.

### 104차 결과 적용 상한 후보와 열린 프레임 비용

103의 실제16/frame 포화·남은 ready·1ms 여유 근거로 header 한 줄의 MaxResultsPerTick를 MaxQueriesPerDispatch(현재32)에 맞췄다. 이는 time budget 안에서 최대32개를 처리할 수 있다는 count bound이며 항상32개/정확히한job 전체를 처리한다는 보장은 아니다. 기존1ms soft budget·Outstanding64·실제worker2·pending8·FIFO/round-robin/LastScheduled·취소/GC/나이 계약과 threshold4096은 유지한다. 새설정/manager/worker/테스트를 추가하지 않았다.

Editor203/Game70 full direct UBT 실제0, 기존 NativeBatch/Revalidation/Membership/RegistryChurn와 prepared supersede/ready retirement/GC/captureAge/threat age9 native는 Success9·자동화경고/실패0·실제0이다. 같은Count32+NPC64/targets128/observer/nodraw의 High1 RHI는 native0·원래 이동/피벗/점프/몽타주/복귀·local조건을 통과했다. steady1558행 중837행이16개를 넘겨 처리했고 각phase의 captureAge P95가약60–78→31–50ms로 줄었다. 하지만phase5 frameP95가19.993→25.319ms로 증가해 다른변수 없이 같은후보를High2로한차례재확인했다. High2도native0/workload성공,1636행중767행이16을 넘겼고 ageP95가약30–46ms로 줄었지만 phase5 frameP95는23.462ms로 여전히 높았다.

| 지표(공격 phase5 steady) | 기존16 | 후보32 High1 | 후보32 High2 |
| --- | --- | --- | --- |
| subsystem acceptance/s | 974.8 | 1004.1 | 1035.6 |
| ready capture age P95 ms | 78.43 | 50.15 | 45.84 |
| 최신관측 NPC full Tick P95 ms | 0.760 | 0.934 | 0.851 |
| frame간격 P95 ms | 19.99 | 25.32 | 23.46 |
| 완료batch 계산 평균 us | 30.0 | 36.8 | 32.6 |

**성능판정은열려있다.** 응답age 감소는반복됐지만전체FPS개선을입증하지못했고프레임tail증가원인도확정하지못했다. count변경은현재WIP후보로유지하며 `Evaluation104.json`은 contract verifier의passed와성능수용을구분한다. 실제NPCaction/Nav/전투listener 없는합성productionproducer이므로raid복잡callback비용이나원격client/서버수용량을대체하지않는다. GPUcounter·GT표본·NPC최신Tick/CPUcritical path·batch wall elapsed의의미는103을유지한다.32-query가4096 subdivision경계를밟을수있는실행형태변화도열린원인후보다. 수치를맞추려고worker/threshold/softbudget을연달아조절하지않고다음105에서기존도구로combined GT대기·Cloth/Mover/작업스케줄링/observer경로를좁게관측한다.

`Comparison1/2.json`은각old/new16/32 bound·명시한header전체바이트1줄변경·source/runtime/보호48artifact·설정/정규화argv를검증한다. 최초분석기는provenance에보존한tracked삭제의null digest를잘못.lower해분석만실패했고, tombstone을유지하는수정뒤같은raw를재검증했다. 원실행과Comparison1의exact reader를보존한다. High2전외부docs-only commit으로HEAD가7800→ed3478로갱신됐지만전체Source1215/runtime39와보호48artifact/별도FX7은비트동일이었다. HeadTransition104.json/PostCommitWorkspace104.json으로이메타변경만명시연결하며원Built/지문·공용provenance검증기를완화하거나불필요한rebuild를하지않았다.

BuiltWorkspace104.json SHA256은 `f1bcf14fb43b798ed4ea838c0f6965741c64a9bb689188b9999d0aa4c8d70e62`,현재HEAD의동일bits PostCommitWorkspace104.json은 `8df23d18f9b5fbbd1134e2587a83ab615d6d5b17c41c770d0f165943b9cfbe13`이다. 검증프로세스는모두정상종료했고새Cook/Stage/Shipping·메모리/규모·provider/서버·전체A06및다른MMORPG잔여를완료로세지않는다.

### 105차 실제 CPU 대기와 결과 상한 채택 판정

관측기 한 cpp에 독립 `ProjectJ_RenderedCrowd_Observer` CPU scope와 실제 Players.Num bookmark를 추가했다. 기존 Actor=200 literal을 Count32 근거로 사용하지 않는다. 게임 로직·worker·예산·solver 설정·assets는 바꾸지 않았다. Editor204/Game71 full0의32 trace와, header만 기존16으로 되돌린 Editor205/Game72 full0의 Control16 trace 모두 실제0·원래 RHI/workload/local 품질 검증에 성공했다. 기존102 나이/100 할당·복사 감소/103 누적 관측은 유지한다.

두 trace의 exact observer2645/2997개와원 CSV2645/2997행을 검증하고 실제 포함 Game Frame의 TimerId를 대응해 실측offset0을 확인했다. 예전+1offset이나 AuthoredCPU region·bookmark+2wall초를 가져오지 않았다. phase5 game-time2–7초의 실제260/289frame을 선택했고, Metadata/Phase5를 나눠 이 시간창의 GT/worker events만 내보냈다. CPU부담과계측상호작용이있으므로이trace를비계측FPS개선으로비교하지않는다.

32 trace에서 실제 `TG_LastDemotable → FSkeletalMeshComponentClothEndTickFunction_ExecuteTick → STAT_EndParallelClothTask → WaitUntilTasksComplete`가8,320회(32×260)였고GT 대기union2.266185초·프레임평균8.716ms/P95 10.513ms였다.그중99.90%가실제clothworker실행구간과겹쳤다.별도GT EndPhysics WaitForTasks는0.539175초다.All-thread WaitForTasks8.8447초의대부분인RHI/Render대기를GT물리CPU로합치지않는다.PhysicsParallelFor31.2초같은worker중첩합도framecriticalpath나OS실행CPU가아니다.NPC collect/apply는프레임평균.372ms/P95 .744ms,관측기자체.195/.236ms로구별됐다.이capture의주요GT대기경로를확인한것이지104차이의보편적인원인까지입증한것은아니다.

| 동일계측공격창 지표 | 후보32 | 대조16 |
| --- | --- | --- |
| actual Game Frame P95 ms | 23.499 | 21.198 |
| cloth ancestry wait union P95 ms | 10.525 | 9.302 |
| NPC collect/apply P95 ms | 0.744 | 0.762 |
| 관측기 P95 ms | 0.236 | 0.229 |
| ready capture age P95 ms | 45.895 | 84.450 |

cloth ancestry union은해당부모아래모든wait를합집합해위의정확한WaitUntilTaskscope보다약간크다.표의여러P95를서로더하지않는다. `MatchedTraceComparison.json`은동일trace/quality/argv/보호inputs·각actualBuilt·전체Source record의header1줄차이·Saved32→현재16 exact bytes를검증한다.수학입력과batch형태/실제iterations까지동일하다는뜻은아니다.

**결정:32를전역기본값으로채택하지않고현재Source는기존16을유지한다.** 응답age개선은보존하지만기존비계측2회및이번계측대조에서frame tail이높아진후보를지연숫자만보고채택하지않는다.104의WIP32기록은이결정으로대체된다.서버/목표부하에맞춘향후정책이나전체NPC성능문제를해결완료로세지않으며,추가설정행렬은돌리지않았다.전체A06의혼합수용량·실제NPCaction/Nav/전투listener와광범위반복성은남는다.

기존37/38/44 근거를재사용해clothouterpriority/solver/constraintgrain을분리했다.InnerParallelForBatchSize는직접PhysicsParallelFor에적용되지않으며globalparalleloff/priority튜닝을반복하지않는다.현재remote정책은base≤2/max≤4/substep1을모두설정하고native actual iterations는delta로조정돼local8–11/remote2–3은정상범위다.CSV의remote max는configured상한이아닌원격실제통계최댓값이다.따라서상한누락을추측수정하거나local품질을낮추지않았다.기존cloth실행budget의명시opt-in/defaultoff·native완료/취소/복원계약과향후다양한garment품질검증도유지한다.

원 trace는996,403,305bytes와대조trace를보존한다.초기Saved export helper의따옴표parser오류·역슬래시응답파일경로실패는원실패를보존하고AST/경로수정후같은trace만재분석했다.소스나엔진을바꿔로그를숨기지않았다.Profiler는CPU48thread/9023timer를완료했으나MemoryTag InvalidTag진단도남아있으므로전체분석오류0·memoryprofiling성공을주장하지않는다.프레임/observer/event계층검증의CPU범위만사용한다.

최종 `Saved/ArchitectureBatch105FrameTail/BuiltControl16.json` SHA256은 `5bbc1155f63957c3113b6a8b04127bfe632ce272ace599c94342f5338a3e3887`(Source1215/runtime39,HEADed3478)이고현재bits가정확히일치한다.32 capture의 BuiltWorkspace105.json SHA는 `7545092d7e55ec153855e21e4ae9ffa3129b4ca6608cbc178f756d8d1bbb571d`다.Window/FrameMap·ContextGTAnalysis/GTWaitContexts·WorkerIntervals/WorkerFindings·IterationContract와원exports를재사용한다.관련process는정상종료했고기존VFX7/각run보호artifact는유지됐다.새Cook/Shipping·운영server/provider/DB/auth·전체MMORPG와성능보장은미완료다.

사용자진행질문에는기존작업량가중약60%(55–65)/잔여40%를유지했다.현재환경에서진행가능한필수잔여40–80시간은구현/통합/검증의거친추정이며환경미정서비스/운영연동전체완료시간을뜻하지않는다.공통C++ 실행/수명/취소/복제/예산은다양한캐릭터·장비·스킬등이재사용하되새asset의호환·LOD/재질/Cloth/VFX제작예산·실제연결검증은필요하다.특정검증asset에만맞춘구조도,모든asset자동최적화완료라는주장도아니다.다음A10은103의sole-demand source퇴역후실제load/unload/residency경계를좁게재사용한다.

## 추가 성능 목표와 실행 범위 — 2026-11-01

사용자가 전달한 잠정 목표는 WQHD(통상2560×1440), 가능하면 에디터100FPS급(약10ms), 최대 약200개체 밀집과 달리기 약5배 이상의 빠른 탈것이다. 제품의 모든 조건에 적용하는 확정 합격선이나 달성 보장이 아니다. 다음 관련 대표 측정부터 실제 출력 해상도·screen percentage/upscaling·scalability·VSync/제한·실행 모드·하드웨어와 P95/P99/hitch·화면/전투 품질을 함께 명시한다. 표시 캐릭터·실제 NPC 시뮬레이션·복제 Actor·접속 client·서버·동시 공격 수는 구분하며200CCU로 바꾸지 않는다. 탈것 속도는 실제 달리기 값/단위·가속/회전·경로를 명시한 가정으로 관리하고 지금 production 속도를 변경하지 않는다. 과거 저해상도·NullRHI·다른 에셋의 근거는 보존하되 새 목표의 실측으로 합산하지 않는다. 패키징의 성능 개선도 같은 조건의 실행 모드 비교 전에는 전제하지 않는다.

기존205 카탈로그·A03–A12·10/19 상태와 잔여 근거를 재사용한다. 필수/권장/선택, 계약/실제 소비/통합, 통과/실패/미검증과 외부 의존성은 기존 항목 안에서 구분하고, 공통 기반 완료를 거래·우편·길드 등의 고유 규칙 완료로 세지 않는다. 새 전수 감사·중복 장부나 미정 provider의 가짜 운영 연결을 추가하지 않는다. 연결된 구현은 되돌릴 수 있는 묶음으로 통합하며 직접 위험한 권위·자산 보존·취소/수명 회귀를 우선한다. 양 target·Cook/패키징·전체 품질을 미세 항목마다 자동 반복하지 않고, 배포/직렬화/콘텐츠 로딩 경계나 필요한 통합 기준점에서 갱신한다. 성능 후보는 판정 질문과 비교 조건을 정하고 채택/기각 뒤 다음 필수 작업으로 진행한다. 현재106은 기존 Portal의 준비→공개 owner 퇴역→origin 복원→선택 native level tail을 최대2회/동일PID로 확인하는 CPU 범위이며 WQHD/GPU/100FPS 검증이 아니다.

## 106차 목적지 철회와 선택 native level 작업의 종료

기존96 standalone Portal probe에 명시 `-ProjectJAuditWorldResidency` 관측만 추가했다. WorldSmoke+PreparedArrival·standalone을 요구하고 원격/HLOD/다른 network variant와 혼용하면 실패한다. 기본96·HLOD 메서드·원격97 본문과 production streaming/permit/속도/품질 정책은 유지했다. 목적지21개 후보를 현재 consumed source들의 수요와 차집합해 고르고, weak cell/GUID와 첫 관측 뒤 weak native level identity만 유지한다. Loaded+Active DataLayer·target grids·shape 정책·partition transform을 검증하며 지원하지 않는 확장·범위 변경·overflow·다른 수요·관측 수명 종료는 성공으로 바꾸지 않는다. query128callback/source8/sample512로 관측을 제한하며 전체 hash 내부 작업량/물리자원 상한이라고 하지 않는다.

실제 `ServerTryInteract`→Portal→Preparing을 사용하고 **UnPossess→Destroy→RestartPlayer**의 공개 수명에서 PC/PS/source/session을 매 callback 뒤 재확인한다. 두 번째 요청은 동일 weak Portal을 재사용하고 퇴역했으면 실패한다. 목적지 source/permit 반환과 level의 `HasLoadedLevel`(PendingUnloadLevel 포함)·async request IDs·pending·ShouldBeLoaded·정상 Unloaded/Removed를 분리했다. 로딩 상태를 Unloaded로 표시할 수 있는 coarse cell state만으로 종료를 판정하지 않는다. native WP의 MaxLoadingStreamingCells를 읽기만 하며 새 physical ticket manager나 강제 flush/GC/언로드를 추가하지 않았다.

Editor206 full direct UBT는 **실제0,18.68초**다. 변경은 fixture cpp/inl 두 파일이며 consumer/직렬화/배포 변경이 없어 이번에는 Game/Cook/패키징을 반복하지 않았다. Game72/기존 Cook79·Stage82는 이전 입력으로 구분한다. `Saved/ArchitectureBatch106WorldResidency/MainWorldResidency1`은 **원래 PID20976·47.10초·실제0·timeout/강제종료없음**이다. 현재 MainWorld/정상 BP/Avatar/Mover/ASC를 사용한 NullRHI 실행이며 두 취소·원점 새 Pawn/새 ASC revision·normal source 복원·최종 permit/pending0을 통과했다.14개 raw 표본과 CSV의294개 cell 관측을 독립 reader로 대조했고 Source1215/runtime39·보호9asset 전후 불변이다.

| 동일PID cycle | 실제 전이 | 선택 native tail의 첫 전체 종료 | 관측의 한계 |
| --- | --- | --- | --- |
| 1 | 후보21 중4개 Loading, level async IDs13을 Preparing에서 본 뒤 취소 | 퇴역 약1.202초 뒤 | permit0인 첫 drain에서도4개/13IDs가 남았다. 실제 로딩 중 철회와 논리/물리 수명의 차이를 관측했다. |
| 2 | 후보21 중4개가 이미 loaded된 표본에서 취소 | 퇴역 약0.127초 뒤 | Loading은 미관측이므로 두 번째 in-flight I/O 취소 증명으로 세지 않는다. |

각 cycle은0.5초 이상 간격의 native 종료3표본을 확인했다.21개 모두를 실제로 로드한 시험이나 모든 맵/소스의 I/O 취소 보장은 아니다. native loading cap의 관측값은4였고 첫 cycle4개와 일치하지만 global async packages는 최대273으로 늘어, 셀 수 제한을 package/byte/메모리 hard cap으로 해석하지 않는다. 설치 엔진의 해당 gate도 non-server·비blocking 경로에 한정된다.

**프로세스 메모리 안정화는 미완료다.** 전체 관측 창의 UsedPhysical은 약69.91MiB, UsedVirtual은 약84.35MiB 증가했다. selected level tail 종료 뒤 남은 값이며 캐시/GC 대기/재생성/기타 작업의 귀속을 분리하지 않았다. 이를 셀별 비용·누수 원인·정상 안정 상태 또는 permanent leak-free로 단정하지 않는다. 두 cycle만으로 안정화를 만들기 위한 반복·강제GC·임의 허용 byte 기준을 추가하지 않았다. 합산 I/O/residency·GPU/texture/PSO·Nav·고속 이동/원경·실제 원격/운영 규모는 계속 열린 경계다.

native log는 Error/ensure/assert0,Warning24다.21개 EditorDataStorageUI·기존 HLOD utility/settings2개·PoseSearch index1개이며 뒤 세 메시지는96 원 log에도 있었다. 경고0이나 HLOD/애니메이션 품질 승인을 주장하지 않는다. 기존93 실패도 유지한다. 추가 source 확인에서 그 exact live same-pointer Stop/Start 직접 caller는93 fixture뿐이고 push dirty/poll은 unchanged UObject의 새 handle 재양자화를 보장하지 않아 우회 patch/matrix를 추가하지 않았다. 일반 travel과 old ACK/remote retirement는 별도 미검증이다.

최종 `BuiltWorkspace106.json` SHA256은 `575b8cbbfce364e940a0439311e69e89f14c419b87ab0aea3b508c2af1ab2166`이다. `MainWorldResidency1/Verification106.json`, 원 world-residency JSON/CSV·Process·Before/AfterSource/Assets 및 CandidateNotes/Inputs/StaticChecks를 재사용한다. 진단 중 일회 PS 비교의 live dictionary/JSON object 형식 차이는 직렬화 정규화 후 확인했으며 공용 provenance 계약을 바꾸지 않았다.105의 기본16 및 기존 실행 개선은 유지한다. 다음 빌드 번호는 Editor207/Game73이며 목표는 active다. 이번 좁은 gate를 전체 A10·전체 MMORPG나 새 잠정 성능 목표 완료로 계산하지 않는다.

## 107차 NPC 준비 수요와 클래스의 수명

현재 Mover의 Actor/component tick을 낮춰도 NP world service의 input→simulation→finalize는 계속 실행된다. 읽은 NP backend에는 live per-instance suspend/Hz API가 없고 StandaloneLiaison은 비네트워크 전용이다. 기존 Walking에는 static-floor 재사용도 있어 중복 idle cache를 추가하지 않았다. **A03 Mover 원거리 전환은 미완료**이며 Mass의 FindMover 거절과 CMC 전용 adapter를 유지한다. 현재 RegisterNPC/SetRoute의 생산 caller가 없는 것도 완료로 바꾸지 않는다.

실제 개선은 기존 WorldEncounter의 비동기 준비 수명에 연결했다. 이전에는 일정만 완료 시 재검사해, 플레이어가 떠난 뒤나 NPCClass가 바뀐 뒤에도 기존 요청으로 생성할 수 있었다. 기존2arg HasNearbyPlayer는 유지하고 TryQueryNearbyPlayer가 완전한 query 성공과 found를 분리한다. observer overflow/Build/HasAny 실패는 부재로 간주하지 않는다. 최대8개 준비 요청에서 기존1초 monotonic wake를 재사용해 알려진 부재·일정 종료를 취소하고, 미확정은 ready여도 원30초 deadline까지 보류한다. 미확정 ready만 local inline 목록으로 다음 wake를 늦추며 다른 ready/component 작업을 막지 않는다.2048 owner ring·1ms soft·spawn apply2·MaxPreparations8과 일반 component 준비 규칙은 유지한다.

취소는 consumer/callback을 분리하고 nonready handle/예약은 실제 ready callback까지 유지한다. 완료는 원ClassPath를 값으로 보존하고 현재 정의가 다르면 old exacttoken만 퇴역한다. 생성 전/FinishSpawning 뒤에는 world/권위/token/phase/revision·등록/ID·ClassPath/UClass·자기 NPC를 재검증한다. 이미 분리된 ready callback에서 등록이나 수요가 바뀌면 자기 미커밋 후보와 old tuple만 정상 cooldown으로 정리하고 새 token/phase/NPC는 되돌리지 않는다. 별도 manager/backend/스레드·속도·직렬화·에셋 변경은 없다. Active인 Encounter를 거리만으로 삭제하는 정책은 추가하지 않았다.

Editor207 full direct UBT는 실제0/25.52초였다. 첫 NativeBatch107_1은 기존 FairChurn/PreparationLifecycle 성공, 새 두 초기 Preparing 확인 실패·native255였고 원문을 보존했다. 테스트가 한 번의 World.Tick(1.1)로 초기 hash 시차를 모두 지난다고 가정한 문제였다. 엔진은 delta를 제한하며 이후 실제 관측의 max undilated는0.4, 해당 시나리오의 Preparing 진입은 game time0.4/0.8이었다. production4파일은 유지하고 정상 World.Tick(0.1) 최대16회의 관측과 진단만 보정했다. phase/token/NextUpdate/native ready/deadline·20초 한도를 주입하거나 바꾸지 않았다.

최종 Editor208 full direct UBT는 실제0/7.14초, NativeBatch107_2의 **WorldEvents4개 모두 Success·자동화 경고/실패/notRun0·native0**이다. NativeScheduleCancellation은 `ready_before=0 reservation_after=1`로 consumer 퇴역 뒤 같은 handle/예약 유지를 확인했다. 이는 ready callback 수신 전 예약이며 disk I/O가 실제로 진행 중이라는 별도 증명은 아니다. 같은 native World에서 observer 이탈/0.5초 cache 만료→spawn/revision/defeat알림0·클래스A→residentB 교체 거절·일반 component의 정상 완료→observer 복귀/기존 game-time cooldown→정상 single spawn→Active 부재 유지→EndPlay 정리를 확인했다. 기존 실제4spawn·최대8preparation/2apply 및256owner 공정성/퇴역도 공유 실행에서 통과했다.

진짜 observer overflow에서 readyunknown→30초 expiry, 생성 콜백의 모든 재진입 조합, 실제 원격/배포·FPS/CCU·WQHD/200 성능은 이번 실행 범위가 아니다. API의 invalid-query unknown/complete-empty 구분과 해당 branch의 수명 검토를 대규모 overflow 실측으로 바꾸지 않는다. 구조적으로 불필요한 생성과 이후 시뮬레이션을 막는 원인 수정이며 수량별 절감 ms나 전체 A03/A10 완료는 미측정이다.

원자료는 `Saved/ArchitectureBatch107NPCSimulation/{BeforeWorkspace107,BuiltWorkspace107,BuiltWorkspace107_2,AfterNative107_1,AfterNative107_2}.json`과 두 NativeBatch107 보고서·원/수정 test 후보에 있다. 최종 `BuiltWorkspace107_2.json` SHA256은 `b8b78c04476f7bbe58bb77a8ce9232f5c5ac9f1a26039465442e1a9f264e2bc5`(Source1215/runtime39)이며 실행 전후 동일하다. baseline106 대비 변경은 production4개와 기존 test cpp1개다. 관련 프로세스는 모두 정상 종료했다. Game72/Cook79/Stage82는 이전 입력이며 이번 변경에 새 Cook/패키징을 반복하지 않았다. 다음 Editor209/Game73, 목표active다. 다음 대표 관측은 기존 도구의 기본1280×720를 보존해 current Spellblade/WQHD/200 조건을 명시하는 준비부터 하며, 새 전수감사·설정 행렬·성능 보장으로 확대하지 않는다.

## 108차 대표 군중 관측과 출력 해상도 검증

사용자가 명확히 한 대로100FPS는 최적화 방향의 표현이며 하드 합격선이 아니다. 네이티브 실행과 충분한 그래픽 품질을 유지한 최적화를 지향하며, FSR·프레임 생성·최저 옵션으로 숫자를 채우는 목표로 바꾸지 않는다. 약200개체와100FPS를 결합한 고정 완료 조건으로 해석하지 않는다. 특정 수치를 달성할 때까지 반복 튜닝하지 않는다.108은 실제 비용의 기준선을 한 조건으로 확인하고 측정 도구의 요청값/실제 출력 혼동을 고친 범위다. production 렌더링·Cloth/애니메이션 품질·움직임 정책은 변경하지 않았다.

기존 Measure-RenderedCrowd.ps1에 optional ResolutionX/Y(기본1280×720 유지)와 ForceResolution을 추가했다. 첫 Baseline1은 native0/원 workload 성공이지만 PNG가888×500이어서 **WQHD 기준선으로 기각**하고 보존했다. 설치 엔진 GameEngine.cpp:413–417의 desktop-area 보정과 지원 `-ForceRes`를 확인해 사용했고, Forced1은 phase2/3 PNG IHDR2560×1440을 독립 확인했다. 도구는 항상 실제 두 PNG의 크기를 별도 viewport-output.json에 기록하고 ForceResolution에서 불일치하면 실패한다. 이는 측정 설정의 원인 수정이며 게임 FPS 개선이 아니다. 기존105 Trace1/Control16 PNG는 모두1280×720여서 그 대조의 출력 조건은 유지된다.

Forced1은 Editor208 실행 파일의 **uncooked -game·Entry 단순 카메라/조명·offscreen D3D12·200개 PlayerCharacter avatar·local player1·socket0**이다. AIController가 구동하는199개 로컬 authority avatar를 실제 원격 simulated proxy나 NPCDecision/Action/Nav 부하로 바꾸어 해석하지 않는다. CPU는 Ryzen7 9800X3D, 실제 선택 adapter는 Radeon RX9070XT/Adrenalin26.10.1이다. 시작 CVar readback은 각 sg 품질3, VSync0/MaxFPS0, AA method4, ScreenPercentage0/Desktop.Mode1·sg.ResolutionQuality0, DynamicRes.OperationMode0이었다. **출력 WQHD만 증명하며 내부 raster 크기·최종 upscaler·FSR/FG 미사용은 미검증**이다. 따라서 이 결과를 네이티브 WQHD 성능이나 Editor UI/PIE·실제200원격 client 성능이라고 부르지 않는다.

기존 Summarize-MoverCrowd.py의 첫1초·capture request 및 뒤2frame 제외, 공격은 동시200 montage 조건을 재사용했다. GT/RT/RHI/GPU는 지연될 수 있는 별도 지표이며 P95를 합쳐 frame 비용으로 만들지 않는다.

| 구간 | 표본 | 평균 frame ms | P95 ms | P99 ms |
| --- | --- | --- | --- | --- |
| 대기 | 121 | 54.10 | 55.86 | 56.98 |
| 이동/반전 | 110 | 59.36 | 62.64 | 64.95 |
| 점프/착지 | 110 | 59.52 | 64.00 | 67.14 |
| 동시 공격 | 38 | 78.29 | 85.35 | 98.36 |
| 복귀 | 122 | 53.99 | 56.10 | 58.87 |

공격 표본은 한 burst의2.876초이며 GT P9573.60ms/GPU18.98ms였다. 대기 GPU P95는54.99ms여서 모든 구간을 CPU 단독 병목으로 단정하지 않는다. 선택 창 최대frame은98.36ms·100/200ms 초과0이지만 startup·capture 제외와 짧은 구간의 결과여서 hitch-free 보장은 아니다. Native input·이동/점프/공격/복귀와 로컬 품질 관측663개는 원 fixture를 통과했고 두 이미지를 직접 확인했다. 먼 군중의 정지 캡처로 근접 의상·손발/그립·모든 콘텐츠 품질을 승인하지 않는다.

전체 프로세스 표본 peak는 WorkingSet9,639,219,200bytes/Private24,752,480,256bytes였다. startup을 포함한 서로 다른 시점의 peak이며 개체당 비용·VRAM·paging·누수로 귀속하지 않는다. 현재 pose_routes2,200행은11개 메시 경로를200회씩 공유하며 좁게 확인한 C++에서 메시 에셋 clone producer는 없었다. per-instance component/AnimInstance/cloth/render 자원의 bytes·반복 퇴역 후 회수는 미확정이다. Error/ensure/assert0·Warning225(초기 EditorStorage21/PoseSearch204)이며 PoseSearch는 frame0/2에만 관측됐다. 첫phase2보다10초 전의 경고지만 InProgress/Failed를 구분하지 않으며 이후 silence로 모든 DB의 index ready를 증명하지 않는다. 순수 readiness 관측 근거 없이 새 index build/asset 전수 검사를 추가하지 않았다.

Cloth는 현재 max-LOD geometry506,800/unknown0·200part이나 execution cap0/permission false를 유지했다. observer off의 qualified0은 native cloth 중단 증거가 아니다. 기존44/105와 ClothGate는 향후 구체적 후보의 실제 작업/품질 판단에 재사용하며 이전 Basic 예산을 새 기본값으로 옮기지 않는다.108 미달을 priority/iterations/grain/품질 설정 행렬로 확대하지 않는다. 다음은 기존 미완료인 소셜 revision tombstone 수명처럼 전체 MMORPG의 실제 공통 비용 경로를 진행한다.

두 원 run은 `Saved/Validation/WholeProjectAudit_20261013/MMOFoundation_20261102_WQHD108_{Baseline1,Forced1}`에 있고 원자료를 보존했다.48개 보호 artifact와 전체 source/runtime는 각 실행 전후 동일하며 두 프로세스는 자연 종료했다. 최종 `Saved/ArchitectureBatch108WQHD/FinalWorkspace108.json` SHA256은 `319e4217f11f8cd2d4e67188badd6e0500eafaea2609143b8f5cb4f07b962c09`(Source1215/runtime39)다.107 대비 수정은 측정 script1개여서 C++ 빌드·Cook·패키징을 하지 않았다. WQHD108Summary/SlowFrames·MemoryScope·PoseReadiness·ClothGate를 재사용한다. Editor208/Game72·Cook79/Stage82와 다음 Editor209/Game73을 구분하며 전체 목표는 active다.

## 109차 소셜 이력의 신규 ID 입장 제한

64차부터 남은 파티·길드의 장기 이력 문제를 현재 소비 계약 안에서 제한했다. 기존 GI 메모리 어댑터에 `MaxTrackedGroupsPerKind` Config 값(기본65,536·실행/제작 범위0–1,048,576)을 추가해 각 kind의 live+deleted ID를 함께 센다. 상한만큼 미리 할당하지 않는다. 새 Create는 authority/session/AlreadyMember 검증 뒤, namespace/번호 발급과 map·PlayerState·알림 변경 전에 기존 결과 enum 끝에 추가한 `HistoryCapacity`를 반환한다. 기존 enum 값0–8과 public API/Version1/복제 타입은 유지한다. trusted Restore도 기존 검증·동일 revision replay를 유지하고 **미등록 ID만** publication/미래번호 예약 전에 거절한다. UI·관계·owner roster의 writer와 기존 command consumer는 그대로다.

이 변경은 **메모리 어댑터의 등록 건수 제한(containment)**이다.17차에서 지양한 “단순 cap으로 장기 서버 수명을 해결했다고 간주”하는 접근을 완료로 바꾸지 않는다. 생성 이력이 한도에 도달하면 마지막 탈퇴 후에도 새 ID를 만들 수 없다는 가용성 비용을 명시한다. 제한 없는 누적에 대한 안전한 실패를 추가한 것이며, 운영 영속 membership·무한 생성/퇴역·메모리 bytes/FName pool의 hard cap을 완성하지 않았다. 기본값은 개발 어댑터용 임시 상한이지 realm/CCU 수용량이 아니다.

삭제 snapshot은 현재 더 높은 revision의 live 복원을 허용하고 Get은 exact tombstone을 반환한다. LastIssued는 발급 high-water일 뿐 삭제 watermark가 아니고, 같은 namespace 미래번호512의 아래에는 미발급 구멍이 존재할 수 있다. 외부 opaque ID도 받는다. 따라서 TTL/FIFO 삭제·번호 재사용·범위 압축 프레임워크를 추가하지 않았다. 기존 ID의 Join/Leave·높은 revision 복원·exact/equal/stale 동작은 full/shrink/zero에서도 유지한다. 운영 퇴역은 실제 producer의 ordered watermark 또는 versioned terminal-deletion/incarnation 계약이 정해져야 하며 provider/DB는 미정이다.

기존 GroupNameLifetime에 별도 transient instance의 작은 상한2→1→0과 fresh0만 추가했다. 원32churn·namespace 분리·future512→513·소진 본문은 그대로다. 실제 API로 tombstone+live 포화, typed Create 거절, foreign/future512 Restore 거절 전후 번호·map·projection·알림 불변, full에서 기존 가입/탈퇴·exact/equal/stale/higher revision 복원, kind별 독립·shrink/zero 비퇴역을 확인했다. clock/timeout·CDO·production hook을 바꾸지 않았다. 독립 core/test peer도 차단 사항이 없었으며 delegate를 strong PlayerState보다 먼저 해제한다.

Editor209 full direct UBT 실제0/23.49초, `NativeBatch109_1`의 Membership·GroupSnapshot·SocialSessionRevocation·GroupNameLifetime·PartyRosterExecution **5개 Success/자동화 경고·실패·notRun0/native0**,107+109 누적 Game73 full direct UBT 실제0/32.68초다. transient authority/projection 시험이며 새 원격 socket/UMG·공급자·장기 서비스 부하를 검증한 것으로 세지 않는다. 기존 party 복제 근거를 재사용하고 새 Cook/패키지는 반복하지 않았다. source는 의도한 Social h/cpp와 기존 test cpp3개만 바뀌었으며 native 실행 전후 전체 source/runtime가 동일하다. 일회성 이름 비교 명령의 PowerShell join 우선순위 오류는 명시적 scalar 비교로 바로잡아 실제3개 변경과 후보 hash를 확인했고 공용 검증기는 유지했다.

원/후보·입력·consumer/peer 근거는 `Saved/ArchitectureBatch109SocialLifetime`에 있다. 최종 `FinalWorkspace109.json` SHA256은 `f7d238d4a4d2313258dabe18cd79de568f7d09826f2d6f207cae0db02cf491f5`(Source1215/runtime39), Game 통합에서 변경된 runtime은 Project_J.exe 하나다. 모든 관련 프로세스는 정상 종료했다. Cook79/Stage82는 이전 입력이며 전체 목표는 active, 다음 Editor210/Game74다. 다음110은 기존 A09 전투 판정의 반복 계산·조회·할당을 좁게 읽고 실제로 줄일 수 있는 작업만 묶는다. 새 전수 감사·판정 생략·추측 병렬화를 추가하지 않는다.

## 110차 SSR 요청의 충돌 조회 전 승인

기존 A09·89차 native melee sweep 근거와 실제 Notify→Melee/NPC ability→CombatHitValidation/SSR 경로를 재사용해, client SSR RPC의 작업 순서를 cpp 한 파일에서 정리했다. 이전에는 이미 확정한 target·오래된 sequence·domain 횟수 초과·기본 데이터 오류에도 world LOS가 먼저 실행될 수 있었다. 이제 기존 outer PlayerState command budget → pure data/actor/spatial 검증 → active key/tag/window/trace/길이 → duplicate/old sequence → 기존 rate/sequence 승인 → LOS → 공격자 history/대상 SSR 또는 기존 fallback → dedup 예약/피해 순서다. 적격 요청의 충돌·시간·공간·권위 검증을 생략하거나 worker로 UObject/물리 작업을 이동하지 않았다.

승인 시점의 의미를 명시적으로 변경했다. 기본 데이터 오류는 outer quota를 계속 소비하지만 domain count/sequence는 건드리지 않는다. 적격 요청이 기존 기본32/초 domain 한도를 통과하면 **벽에 막혀도** count와 sequence를 먼저 차지하므로 같은 blocked sequence가 비싼 조회를 반복하지 못한다. 초과 시의 기존 count 증가/sequence 미확정, world-time 창, component 수명 전체 fence와 Begin/End/ResetSpatial 비초기화는 유지한다. 복수 실패의 내부 로그 우선순위는 달라지지만 void Unreliable RPC·enum·wire·public 서명은 그대로다. NPC/authority 직접 판정은 이 변경의 client domain quota를 새로 소비하지 않는다.

기존 WeaponAttackLifetime 하나에 실제 visibility Box와 per-instance 작은 quota만 추가했다. 실제 query/production LOS로 벽을 먼저 확인한 뒤 duplicate/old sequence의 무변경 거절, blocked2회의 count/sequence 확정, 포화 거절, 공격 종료·공간 reset·새 window에도 fence 유지, 잘못된 self/NaN RPC의 domain 불변·실제 outer quota 소비를 확인했다. 정상0.1초 World.Tick으로 원1초 창을 지나고 벽을 정상 해제한 뒤 새 activation/기록/sequence의 RPC가 실제 세 번째 효과를 적용했다. 마지막은 공격자 sweep history와 NPC의 기존 bounding-box fallback 소비이며 target capsule rewind나 실제 socket 통신의 새 증거가 아니다. capsule 이력/회전·보간과 window/공간 경계는 같은 묶음의 기존 시험을 재사용한다.

첫 Editor210 full direct UBT는0/6.87초였지만 NativeBatch110_1은 기존8개 성공 뒤 새 natural World.Tick에서 **WorldContext 미등록 assert/native3·최종 report 없음**으로 실패했다. 최초 오류와 callstack의 test86행을 확인하고, 기존 Spatial fixture와 같은 실제 context 등록/해제만 test에 보완했다. core·clock·한도·검증 조건은 바꾸지 않았다. 최종 Editor211 full0/7.00초, 동일 조건 `NativeBatch110_2` **11개 Success·자동화 경고/실패/notRun0·native0**이며 전체 source/runtime는 두 실행 각각 전후 동일했다. 실패 원자료도 보존했다. 임시 command component/Box·GE delegate·world/context의 정상 종료를 독립 peer로 확인했다.

89차가 제거한 Kismet 임시 변환 배열을 다시 만들지 않았다. native sweep의 call-local OutHits는 gameplay event 재진입/취소/GC 때문에 shared scratch로 바꾸지 않았고, 실제 접촉 listener 의미를 바꾸는 선행 dedup도 추가하지 않았다. SSR fixed-cap capsule ring/binary search·기존 socket 한 번 해석·callback 뒤 execution 재검증을 유지한다. 이는 **불필요한 scene-query 작업을 앞단에서 제거하는 구조 개선**이며, FPS/절감 시간·전체 A09/대규모 전투 인증은 아니다. 실제 많은 공격의 pose·sweep·효과 총량은 여전히 남는다.

원/후보·peer·consumer notes와 두 실행 지문은 `Saved/ArchitectureBatch110CombatCost`에 있다. 최종 `BuiltWorkspace110_2.json` SHA256은 `c24772150a4900fe2059f5a43946f758efaab18c2cddc963087214368c13e81a`(Source1215/runtime39)다.109 대비 production cpp와 기존 test cpp2개만 달라졌다. 현재 검증 target은 Editor211이며 Game73/Cook79/Stage82는 이전 입력이다. 반영·에셋·직렬화/배포 경계 변경이 없는 cpp 묶음이므로 Game/Cook/패키지를 항목마다 반복하지 않고 다음 통합점에서 수행한다. 전체 목표active, 다음 Editor212/Game74다.

## 111차 기존 예약 수명 검토와 작은 후보의 보류

95–97/106/107의 기존 근거와 현재 연결만 좁게 읽었다. 동일 world의 WorldEvent async load는 consumer 취소 뒤에도 ready callback까지 최대8개 native 예약을 유지해 새 조기 재사용 결함을 확인하지 못했다. Arrival의 논리 permit0 뒤 실제 level tail은106에서 이미 관측한 서로 다른 수명이다. 성공 후에도 다른 source가 사용하는 level의 unload까지 논리 permit을 붙들면 영구 점유가 될 수 있어 새 shared manager나 물리 ticket으로 재라벨하지 않았다. 합산 I/O/bytes·새 world tail과 A10은 미완료다.

NPC의 도착 유지도101의 Cancel(0) guard 덕분에 새 world 요청/abort/callback을 만들지 않는다. invalid MoveId에서만 불필요한 strong 참조/Following 조회를 피하는 작은 후보는 남겼지만, 그 한 줄을 별도 빌드·회귀 묶음으로 만들지 않았다. A03 실제 Mover 저빈도 실행을 가짜 TickInterval/중립 입력으로 대체하지 않았다. Source/에셋/빌드 변경은 없고 `Saved/ArchitectureBatch111ResourceLifetime/{WorldNotes,NPCNotes}.md`를 이후 관련 변경에서 재사용한다.

## 112차 장시간 제작의 영속 예약과 수령

현재 즉시 제작/단일 profile 커밋을 장시간 작업에도 연결했다. 새 콘텐츠 세트·외부 provider·전역 job framework를 추가한 범위가 아니다. `DurationSecondsPerBatch=0`은 기존 즉시 동작과 digest bytes를 유지하고, 양수만 새로운 duration domain을 hash에 넣는다. 원 Recipe/공통 planner를 재사용하며 직접 즉시 Plan으로 양수 시간을 우회할 수 없다. batch당0–86,400초, 작업 전체 최대604,800초다. 한 캐릭터 최대8개 job·job당64 escrow행·live+escrow 전체4096행과 기존 profile65,536bytes를 함께 제한하며 상한만큼 미리 할당하지 않는다.

- **시작:** 기존 제작대의 authority/range/visibility를 검증하고 재료를 escrow로 옮긴 job을 한 CAS로 확정한다. 출력/퀘스트 진척은 지급하지 않는다. 전체 재료 인스턴스는 UUID를 유지하고 부분 stack만 별도 UUID로 나눠 live와 escrow가 같은 소유권 ID를 공유하지 않게 한다.
- **수령:** 원 start request가 job ID이며, receipt 조회 뒤 native worker의 실제 서버 UTC가 deadline을 넘었을 때 동결한 출력·job 제거·기존 제작 퀘스트 진척을 한 커밋으로 확정한다. 현재 레시피의 변경/registry 제거로 약정 수량을 다시 계산하지 않는다. 현재 출력 정의/version/stack packing이 호환되지 않거나 가방/문서 한도가 부족하면 전체 job을 보존한다.
- **취소:** 현재 레시피와 무관하게 canonical escrow 재료의 UUID/version/level/count를 환급하고 job을 같은 커밋에서 제거한다. 기존 locked/equipped stack에 합치지 않으며 bag order만 새 위치로 정한다. 환급이 불가능하면 job/재료를 그대로 두고 실패한다. restore/checkpoint는 출력 정의 부재만으로 전체 profile이나 유효한 환급을 막지 않는다.

시작은 제작대를 요구하고, 수령/환급은 캐릭터가 소유하므로 이전 제작대/Pawn의 위치·수명과 독립적이다. 기존 station은 같은 recipe/version의 job이 없을 때 시작하고, 있으면 새로 중복 시작하지 않으며 due이면 레시피 조회보다 먼저 기존 job을 수령한다. 모든 시간은 native worker의 UTC로 재검증한다. 로컬 개발 host의 시계 전진/후퇴는 완료 시각에 영향을 줄 수 있으며 분산/보안 시계를 완성한 것이 아니다.

**실행·표현 경계:** 새 `UProject_JCraftingJobsComponent`가 PlayerState의 server job writer이며 Snapshot은 checkpoint다. waiting Actor·전용 thread·Tick/timer를 만들지 않는다. native worker는 receipt miss 뒤에만 값 planner/UTC/UUID/encode를 실행하고 기존 gate·한 profile CAS·lease·내구 결과를 재사용한다. 모든 일반 edit/checkpoint/자동 진척도 jobs를 보존한다. `FProject_JPersistentItem`은 같은 이름/필드의 공통 Character 값 헤더로 이동해 소유권 타입을 중복하지 않았다.

owner-only component 조건과 bounded summary array를 함께 적용했다. 공개 `RequestCollectJob/RequestCancelJob`는 기존 PS quota와 session-bound submit/factory/admission을 사용하고 임의 request ID fallback이 없다. fresh not-due 수령은 IO 전에 거절하되 raw SubmitEdit는 job/recipe가 사라진 뒤에도 기존 receipt를 조회할 수 있다. 클라이언트 countdown은 게시 당시 remaining에서 수신 뒤 monotonic 경과를 빼는 advisory라 전송 지연만큼 보수적으로 늦는다. 서버 getter는 UTC이며 client 시각을 권위로 사용하지 않는다. unknown/missing/ended는-1,0도 표시상의 ready다. 동일 job 값/동일 weak world는 요약을 다시 계산하거나 알림/네트워크 갱신을 만들지 않는다. 게임의 pause/time dilation 시각과 UTC를 혼합하지 않았다.

전체 inventory/progression/quest/job 상태를 설치한 뒤 **첫 알림 전에 AppliedRevision을 확정하고 각 알림에 같은 session+exact revision을 검사**한다. callback 중 더 최신 진척 projection이 재진입하면 이전 호출이 남은 알림이나 revision을 되돌리지 않는다. 초기 Open은 원 bReady=false 입장을 유지했다. 새 전역 counter/별도 writer/가짜 completion으로 이 경계를 가리지 않았다.

**저장·호환:** 새 snapshot 기본 Format3은 jobs/동결 출력/escrow/정확한 int64 UTC 문자열을 저장한다.65536-byte reader/writer·구조·소유권 검증을 유지하며 실패 시 출력 인수를 보존한다. 명시적 Format2 read/write와 duration0 digest는 유지하고, legacy 문서의 jobs를 무시하지 않고 거절한다. Format1은 여전히 명시 ownership 이관이 필요하다. 기존 Format3 이관은 payload/revision/escrow를 그대로 보존하며 v1 대체 입력으로 job을 발명하거나 다른 캐릭터 escrow UUID를 재사용할 수 없다. `PersistentEdit`는 기존0–14를 유지하고15–17에 Start/Collect/Cancel을 추가했다. 새 default subobject/RPC/summary 때문에 realtime protocol은9→10이며 persistence format/마케팅 버전과 구분한다.

**실제 검증:** 첫 Editor212 full은 GetLifetimeReplicatedProps의 매크로 필수 parameter 이름 누락(C2065)으로 exit6/140.06초였다. 정상 종료까지 기다린 뒤 cpp/h의 parameter spelling만 보완했고 원 입력·실패 로그를 보존했다. Editor213 full direct UBT는0/14.77초, Game74 full direct UBT는0/75.08초다. `NativeBatch112_1` NullRHI의 **8개 모두 Success·자동화 경고/실패/notRun0·native0**이다. ProtocolAdmission, Crafting AtomicInteractionAndRecovery, ProfileCodec, Migration, QuestRuleCompatibility, LocalProfileLifecycle, ProgressAdmission, ProgressCapacityRecovery를 한 실행에 묶었다.

기존 crafting fixture에서 원 즉시/손실 응답/old-owner 종료/IO 실패를 보존하며 bounded pure 예약·포화·UUID·정확 due/환급/실패 원자성, 실제 제작대 시작·early RPC 거절·취소 환급, start/collect receipt replay, 무관한 tracking 커밋에서 job 알림0/요약 표본 불변, 진짜 detach/Pawn·PS 파괴/새 PS reopen의 deadline 보존, 자연2초 UTC 대기, recipe registry 제거 뒤 수령, 실제 station 수령과 진척3→4→5를 확인했다. raw 서버 함수/native authority 경로이며 새 실제 socket이나 완성된 job UI의 증거가 아니다. Recipe fixture는 UObject를 강하게 보유하므로 registry 제거를 실제 package unload/GC로 확대하지 않는다. timing/worker/clock을 주입하거나 테스트 조건을 완화하지 않았다.

현재 `FinalWorkspace112.json` SHA256은 `c0db1f09837f74997db435791bd095ee693b5b11214c1740bb79e9d811c006f8`(Source1220/runtime39)이며 원/후보/세 subagent peer·Correction1·native 전후 지문은 `Saved/ArchitectureBatch112TimedCrafting`에 있다.110 대비 의도한25개 파일(신규5)만 바뀌었고 native 전후 전체 source/runtime와 실제 canonical potion2asset은 동일하다. Game 통합에서 runtime 변경은 Project_J.exe 하나다. 관련 프로세스는 모두 정상 종료했다. Cook79/Stage82는 이전 입력이므로 새 저장/wire/Shipping·socket·UI·운영 provider 완료로 세지 않는다. 장시간 제작의 로컬 기반이며 생활 전체/모든 장기 활동·거래 원장/전체 MMORPG 완성이 아니다. 다음 Editor214/Game75, 전체 목표active다.

## 113차 장시간 제작의 클라이언트 소비와 요청 수명

112의 영속 제작을 기존 PlayerUI/HUD의 실제 native 제작 창에 연결했다. 최대8개 행을 job ID로 재사용하고 작업 변경은 delegate로 반영한다. 카운트다운·요청 대기는 기존 HUD의 창이 보일 때만 초당1회 이하로 갱신하며, job별 Tick·timer·worker·별도 manager는 추가하지 않았다. 레시피 표시를 위한 동기 로드 없이 현지화된 기본 행·수령/취소 버튼을 사용한다. 기존 입력·창·드래그·인벤토리 경로는 유지했다.

클라이언트는 컴포넌트당 마지막 요청 하나만 관측한다. 새 correlation GUID는 영속 명령 ID와 구분하고, Pending 중 중복 요청을 차단한다. 실제 monotonic10초 이후 getter는 Unknown을 반환하며 자동 재시도는 하지 않는다. 수락 전 거절은 Rejected, 확인된 저장/게시 완료는 Confirmed, 수락 후 false completion은 저장 부재를 확정할 수 없어 Unknown이다. 일치하지 않는 상관관계·job·action·소유 generation, 종료한 source와 이미 확정된 결과의 늦은 응답은 버린다. outer PS quota 소진은 reliable 응답 증폭을 피하기 위해 무응답으로 처리한다.

authority PS의 identity 변경은 CharacterId+증가 generation을 한 owner-only scope로 발급한다. 클라이언트 jobs identity는 그 scope의 RepNotify만 쓴다. 별도로 도착하는 PS identity에서 component generation을 추측하지 않는다. UI는 현재 PS character와 scope character가 같은 경우에만 표시하되 scope 도착 전에도 유효 source 구독을 유지한다. 행은 source/character/generation을 캡처하며 교체 시 퇴역한다. 서버는 요청·기존 설치·완료 callback의 동일 generation을 검사한다. SubmitJob도 admission/factory 각 callback 뒤와 제출 직전에 owner/identity/설치 수명을 재검사한다. scope 내부는 한 UPROPERTY 전체 복원 후 알림이며, PS·요약과의 강한 동시 일관성이나 최신 상태의 무지연 도착을 보장한다는 뜻은 아니다.

설치 UE의 기존 Iris StructNetSerializer가 이 flat GUID+uint64 전체를 복원·복사한 뒤 RepNotify를 호출하는 것을 확인했다. native NetSerialize와 필드 값이 동등하므로 공식 SupportsStructNetSerializerList에 이 타입 하나의 호환성을 명시했다. 전역 경고 설정이나 serializer/wire는 바꾸지 않았고 새 custom Iris serializer도 만들지 않았다. 첫 baseline의 해당 경고와 근거를 보존했으며 최종 loss 실행의 세 역할에서 해당 경고0이다. 필드·압축 의미를 바꾸면 이 선언을 재검토해야 하고 classic 복제 실행은 미검증이다.

실제 변경은 Source/Script18개(신규 native UI2·fixture inl1)+Config1이다. LocalProfile의 NM_Standalone guard, UTC/escrow/receipt-first/단일CAS, 원본 즉시 레시피·맵·아이템 에셋과 기존 상한은 유지했다. 소켓 fixture는 명시적 synthetic 요약만 설치하고 실제 no-provider Rejected를 검사한다. 운영 인증·원격 저장 성공을 흉내 내는 provider는 추가하지 않았다. 실시간 protocol11, Snapshot3이다.

검증은 묶어서 수행했다. 첫 Editor214는 HUD의 TObjectPtr에서 const auto*를 추론한 C3535/C2440로 UBT6 정상 종료했고, `.Get()` 한 곳 수정 후 Editor215 full direct UBT0(7.717초), Game75 Development full direct UBT0(39.327초)이다. 집중 native3개가 Success/자동화경고0/native0(15.912초): 실제 profile의 Pending→자연10초 Unknown→같은 요청의 늦은 Confirmed, 중복 차단·즉시 거절, authority A→B→A 새 설치, PS 파괴/reopen과 기존 자산/영속 경계를 확인했다.

실제 dedicated server+두 클라이언트 baseline와50ms 지연/2% 손실 각각 native0/0/0·timeout0이다. 기존 AOI5단계 뒤46–49에서 실제 foreign PS 관측을 전제로 raw 요약과 getter 모두 owner1/foreign0, 갱신·제거, no-provider Pending→Rejected, native PS 교체·종료한 source 호출 차단을 확인했다. synthetic 전송 시험이며 온라인 자산 확정이나 실제 늦은 packet 주입의 검증은 아니다. 별도 standalone RHI는 실제 Avatar.TryInteract와 native row button OnClicked 소비로 시작→취소/환급→재시작→자연UTC 수령의5행·4회 commit·최종 재료4/결과1/작업0/표시0을 확인했다. native0, 원본 에셋 불변,1280×720 PNG에서 한글 대기 행·수령 비활성/취소 버튼을 직접 확인했다. 이 출력 크기나 기능 시험으로 FPS·네이티브 raster·그래픽 품질 승인을 주장하지 않는다.

native·RHI 이후에는 이 타입의 Iris 호환성 Config 선언만 추가했고 Source/runtime 동일·기존 serializer 경로의 값 동등성을 별도로 확인했다. 최종 Config의 소켓 loss 시험 후 Game 통합은 Project_J.exe만 바뀌었다. 최종 Saved/ArchitectureBatch113CraftingClient/FinalWorkspace113.json SHA256 `7aac981bdd38d4cef3e21d03851525d92eeb7618befcee356748ea5c77f24382`(Source1223/runtime39, HEAD ed3478), 다음 Editor216/Game76, 엔진·빌드 프로세스0이다. Cook79/Stage82는 이전 입력이다. 전체 MMORPG 목표는 active로 유지하고, 다음은 기존 A09의 실제 전투 판정·스윕·효과 집계 비용과 상한 근거로 이어간다. 공급자 미정의 운영 저장·인증과 기존 A03–A12 미완료를 이 묶음 완료로 지우지 않는다.

## 114차 공유 전투 노티파이와 장비 표현의 반복 작업 제거

기존 A09·89·110의 실제 호출과105/108 관측 도구를 재사용해 두 실행 비용을 정리했다. 공유 melee notify는 유효 Begin마다 모든 참여 mesh의 weak map을 훑었다. 이제 기존 WeaponMotion/GroundContact와 같은 native PostGarbageCollect observer를 최초 Begin에 한 번 등록하고 BeginDestroy에서 정확히 해제한다. 공격 시작이 몰릴 때 P번의 전체 N행 검색을 반복하지 않는다. 수집된 mesh는 다음 공격을 기다리지 않고 GC 후 정리한다. DestroyComponent 등으로 먼저 무효화된 key는 GC까지 약하게 남을 수 있으며 UObject를 붙잡지 않는다. 같은 mesh의 중복 instance·현재 window 검사, End, canonical sweep·metadata·접촉별 event, call-local 결과 배열과 callback 후 execution 재검증은 유지했다. 프레임 stamp 초안은 적용 전에 버리고 기존 native 수명 패턴을 사용했다.

로컬 장비 style/skin pair setter의 실제 equip/unequip 경로는 먼저 combat 표현을 퇴역·prefetch하고 무기를 교체한 뒤 style RepNotify를 통해 같은 자원을 다시 준비했다. 기존 후반 combo/input/animation 반응만 작은 private helper로 공유하여 두 번째 퇴역·요청을 제거했다. 첫 retire-before-weapon과 callback 뒤 owner/world/current-pair 검증은 유지한다. 외부 RepNotify의 독립적인 정리·revision 검사도 그대로다. 이는 local pair의 중복 GT traversal·lease 해제/요청·generation 변경 제거이며 원격의 독립 property 알림 전체를 합쳤거나 물리 I/O가 절반이 됐다는 뜻은 아니다.

production은 위 두 경로와 기존 Begin/GC/Tick/sweep/event/authority/SSR/LOS/effect에 정적 CPU trace 구간을 연결한5개 파일이고, 기존 집중 test2개를 포함해 Source7개만 바꿨다. 새 파일·manager·worker·timer·dynamic per-actor trace label은 없다. Config·반영 payload·protocol11/Snapshot3·자산 저장·품질 정책은 바꾸지 않았다. 변경은 구조적 반복 비용 절감이며 FPS 병목의 원인 수정이나 전체 A09 완료로 세지 않는다.

Editor216은 full direct UBT0(134.910초)이다. 첫 native114_1은6 Success+1 warning/native0였고, 새 실제 Destroy/GC test의 Engine world context 누락이 최초 경고였다. 시험의 등록·해제만 보완하여 Editor217 full0(6.090초), 최종 RHI native114_2의8개 모두 Success/경고0/native0(2.114초)다. 첫 요청의 pool 필터가 잘못되어 빠진 것은 최종 정확한 GroupE.EffectsPoolLifetime으로 포함했다. 실제 GC 후 다음 Begin 없이 dead map2→1, normal End0, notify 자체의 GC·observer 퇴역을 확인했다. 실제 warm Niagara 경로의 요청 증가1·lease token 교체·live lease수 유지·무기 UObject identity·같은 pair의 정확한 noop·clear0과 기존 geometry/metadata·취소/GC·pool 회귀를 함께 검증했다. Game76 Development full direct UBT0(79.263초)다.

기존108과 같은 WQHD200/movement/local-player 조건에 trace만 추가한 실제 RHI 한 실행이 native0/원래 workload 성공, 관측 출력2560×1440이다.725개 CSV 행과 정확한 GT observer/TraceServices frame을 일대일 대응했고 보정 없는 raw/trace frame offset0을 확인했다. 그룹 R 입력을 발행한 이전 phase4 전환 frame부터 phase5 전체114행까지62.2048989–70.2664823초를 선택했다. 실제 actor·montage·gameplay-pose-demand peak가 각각200이다. 다만 새 melee/authority/SSR/effect 구간10개는 전체 trace에서 관찰되지 않았다. 이 대표 입력은 근접 판정·피해 처리량을 검증하지 못했고, 이를 비용0·200회 성공 피해·200 remote client로 바꾸지 않는다. 다음 작업은 실제 authored montage/notify와 소비 경로를 확인해 적격 근접 producer 및 다수 caster/피해 workload를 기존 fixture에서 검증하는 것이다. 새 전수 감사나 옵션 행렬을 시작하지 않는다.

같은 구간의 기존 cosmetic Spawn/Stop scope는 각200회다. Spawn은 inclusive14.009ms/exclusive1.483ms의 그룹 합계이며 진입 횟수가 실제 emitter 생성 성공을 뜻하지 않는다. 가장 긴 한 GT 진입11.123194ms의 callee rootCount1을 원 trace에서 분리했다. 그 안의 Activate→RequestCompile10.8683ms→NiagaraAsyncTask_DigestSystem9.1549ms는 중첩 inclusive 값이다. 설치 Niagara 소스의 Activate editor guard와 NiagaraEditor의 동기 digest 호출을 확인했으며 이 uncooked editor activation을 cooked 최초 비용·일반 steady-state·GPU·권위 전투 비용으로 외삽하지 않는다. asset이 pending compile이었던 구체 원인과 cooked 경로는 미검증이다. 이 결과만으로 새 warmup manager나 품질 저하를 추가하지 않았다. 첫 callee 추출은 분석 script 분기 오류로 command가 비어 실패했으며 그대로 보존하고 수정된 Cosmetic2의 정확한 시간 인자·root1을 사용했다. 게임 재실행 없이 같은 원자료를 읽었다.

원/후보·peer·첫 경고·보완·trace 구간 대응·정적 계측 동등성은 Saved/ArchitectureBatch114CombatExecution에 있다. 원 trace SHA256 F540EFF6B0A4F122C7C5F84B1CA61B0C9ACAB0FA05D5B418AF383649401B8D5A는 모든 추출 전후 동일하다. 최종 FinalWorkspace114.json SHA256 `b954b0d4823396d463a43ed4e5292cfeec896c3c02f165935a547455471cee40`(Source1223/runtime39), Game 통합에서 Project_J.exe만 바뀌었고 engine/build/analysis process0이다. 다음 Editor218/Game77이며 Cook79/Stage82는 이전 입력이다. 현재 그래픽 옵션의 출력 크기만으로 native raster/FSR·FG 미사용 또는 화면 품질 승인을 주장하지 않는다. 전체 MMORPG 목표와 기존 A03–A12·운영 provider·목표 규모의 잔여 작업은 계속 active다.

## 115차 다중 NPC 공격과 GAS 종료 통지의 수명

**실제 변경:** NPC GAS 활성화에서 `PrepareForAttack` 직전의 `CanCommitToTarget` 한 번을 제거했다. Prepare 자체의 같은 첫 검증, 이동 취소 뒤 revision/목표/LOS 재검증, GAS 발동·비용 이후·타격 시 검증은 그대로다. NPCActivate/OnHit/CommitTarget/Prepare의 static CPU scope4개를 추가했다. 기존 result16·시간/수량/경로 예산을 바꾸거나 worker에서 UObject를 읽지 않는다.

실제 단일 공격에서 자연 종료 listener가 `DeactivateNPC`→`ClearAbility`를 호출하는 동안 엔진 `NotifyAbilityEnded`의 후처리가 같은 spec을 다시 필요로 해 `lost its active handle` 오류가 발생했다. 프로젝트 ASC의 override에서 **Super 전체를 엔진의 `FScopedAbilityListLock`으로 보호**했다. 먼저 선언한 strong self pin은 잠금 해제의 pending 제거까지 유지된다. 종료 callback은 기존 grant를 PendingRemove로 표시하며 실제 제거는 최외곽 잠금 해제에서 완료된다. 이는 원인인 종료 순서 수정이며 새 timer/queue·fixture 우회·vendor 수정이 아니다. 기존 NPC action listener는 소유 flag만 내려놓고 UI listener는 기존 refresh를 예약하므로 callback 내 즉시 spec 소멸에 의존하지 않는다. ClearAll+Give의 동시 조합은 엔진의 clear-all 우선 계약을 유지하며 새 즉시 재부여·재활성화 보장을 만들지 않는다.

**기존 경로와 추가 소비:** 원 single-Cleave 구현·authored BP/activation/attack/montage/notify/GE는 그대로 두고 기존 NetworkLoadFixture/Measure-Network에 opt-in `-Animation -Cleave -CleaveFanout 32`를 추가했다(기본0).32 actual AI-owned caster·96 native victim·32 controller,160 exact observer handle을 상한으로 쓴다. ActivateNPC→실제 scheduler/action→GAS→native montage notify→물리 sweep→matching GE와 Health를 소비한다. 직접 notify 호출·피해 주입·fake completion/frame/role은 없다. 두 클라이언트는 기존 초기5 AOI 단계를 확인하고 새 authority 단계에서는 연결을 유지한 뒤 원 graceful 종료를 사용한다. 새32공격의 피해 전송/시각 표시를 확인했다고 주장하지 않는다. 자연 종료와 실제 pre-impact 취소, 수명/epoch 재검증, 정확한 등록 해제·lease/grant 회수·owned Actor 정리를 포함한다.120초 전체/25초 단계 제한과 production 예산은 유지한다.

**검증:** Editor218은 새 fixture의 local `Started`가 GameMode 멤버를 가리는 C4458로 정상 실패6/16.35초였다. 해당 local 이름만 `StartedCount`로 바꾼 Editor219는 전체 direct UBT0/14.25초이고 기존 NPC6 native는 모두 Success·자동화 경고0/native0이다. 첫 single-Cleave 실행은 기존 기능 검증·세 native exit0이었지만 위 GAS 오류가 있어 깨끗한 기준점으로 세지 않는다. 종료 순서 수정과 기존 GAS test 파일의 실제 완료 fixture/회귀를 묶은 Editor220은 전체0/111.19초, 최종 NativeBatch115_2는 GAS task·EndGrantRetirement와 NPC admission/intent/context/recipient/root-motion/bounded-cleave **8개 Success·자동화 경고/실패0·native0**이다. 실제 자연/취소 EndAbility callback에서 pending 제거·live 조회 제외·callback 신규 grant와 반환 뒤 정착/재사용을 확인했다. GC 중 ASC 파괴·동일 spec 재활성화·ClearAll+Give까지 검증한 것은 아니다.

같은 실제 single-Cleave 원 유발 경로를 SingleFixed로 다시 실행해 양쪽 client의 기존 피해/public Health·owner privacy와 세 프로세스 정상 종료0, **GAS 오류0**을 확인했다. 새 Fanout32는 첫 실행에서 세 프로세스 정상 종료0/검증 성공, 실제 montage/attack/동시 peak가 모두32였다. 자연 시작·종료32, group별 고유 물리 접촉3과 matching GE/Health 감소2(총96/64/64), 실제 취소 시작·종료32와 접촉/GE0·Health 보존, 정리1을192개 target 행으로 확인했다.32 요청을 peak로 추정하지 않았다. 부분 setup 실패·실제 world teardown 도중 late trace 종료는 이번 실행의 성공 근거와 구분한다. Game77도 전체 direct UBT0/62.70초이며 scoped diff check0이다. 새 Cook/Stage/Shipping은 수행하지 않았다.

**실제 CPU 근거와 한계:** setup/물리 준비 이후 서버만 public TraceAuxiliary로 CPU trace를 시작했다. 일반 tail은 제외하고 engine important metadata cache와 실제 OnConnection 완료를 확인한 뒤 공격한다. worker callback은 plain atomic만 캡처하며 UObject를 캡처하지 않는다. 세 role의 시작 인수에서 startup trace/nettrace를 제외했고 native Start/Stop·정리를 확인했다. trace1,347,693bytes SHA256 `8B306C74E79A3952ABCD75EA509ECCC31C7B6C6D45434614335D4180CA950D9B`는 Insights 추출 전후 동일하다. 한 분석에서 실제 NPC4+물리8 timer ID·GT 이벤트2880·trace GameFrame59의 연결을 확인했다. GC prune/SSRRequest metadata는 미관측이며 비용0이나 SSR 처리량으로 바꾸지 않는다.

전체 짧은 trace의 MeleeNotify288회 inclusive7.320ms, 그 안의 sweep288회1.842ms와 dispatch736회4.630ms, NPCOnHit736회2.749ms를 관측했다. 반복 event736과 고유 접촉96·실제 피해64는 서로 다른 값이다. 기존 접촉 이벤트의 다른 소비자·metadata 계약을 보호하고 notify 단에서 임의 dedup하지 않았다. NPCCommitTarget416회1.673ms, NPCActivate64회1.832ms이며 Prepare·LOS·GE 등 nested inclusive 값을 독립 비용처럼 더하지 않는다. 정상+취소 burst와 cleanup을 포함한 한 실행이고 변경 전 대조가 없어 FPS 개선량으로 계산하지 않는다.

GT의 큰 exclusive 범위는 기존30Hz 제한의 UpdateTimeAndHandleMaxTickRate1185.442ms, ABP_Player_Master_C224.847ms, SkinnedMesh Tick224.842ms, NP IndependentTick69.694ms였다. throttle 포함 server frame은 렌더 FPS가 아니다. ABP 범위에는 unscoped native update도 포함될 수 있으므로 이것만으로 BP graph/trajectory를 원인 확정하지 않는다. 다음은 기존 A03/A08과 해당 native update·pose/소켓·root-motion 소비를 좁게 연결하며, 단순 mesh freeze·품질 저하·full-body montage 중 무조건 skip으로 성공을 만들지 않는다. 운영 규모·실제 맵/렌더 품질·지연 보상·전체 전투 및 전체 MMORPG 완료와 구분한다.

최종 변경은 이전114 대비10개 Source/Scripts 파일(신규 inl1)이고 Config/에셋·protocol11/Snapshot3는 유지한다. 원/후보·두 correction·peer·native·실제 socket·작은 trace/추출·정적 지문은 Saved/ArchitectureBatch115CombatFanout에 보존했다. 최종 FinalWorkspace115.json SHA256 `f00320ee9aace5204061a04eb0815bec5ff2bd82c95ada4266074da114d6c5ba`(Source1224/runtime39), 최종 Game 빌드에서는 Project_J.exe만 바뀌었다. 엔진·빌드·분석 프로세스0, 다음 Editor221/Game78이다. 전체 목표는 active이며 A03–A12·운영 provider/인증과 목표 규모의 잔여 작업은 계속 남아 있다.
## 116차 애니메이션 스냅샷의 소유권 전달과 GT 비용 분리

**실제 변경:** Player와 일반 Character의 native animation producer는 완성된 local snapshot을 GT cache에 저장한 뒤, 마지막 사용에서 기존 proxy의 새 owning rvalue overload로 전달한다. Pending→worker snapshot 소비도 같은 revision fence에서 move한다. 두 deep value copy를 제거하되 lvalue 게시의 독립 복사·GT cache·BP-visible retained trajectory/public value getter의 독립 소유는 유지했다. 두 실제 호출자와 Pending의 모든 reader를 확인했다. move 뒤에도 source의 scalar TObjectPtr는 남을 수 있으므로 **소비 직후 Pending={}**를 수행하고 revision을 확정한다. 이전 active 값을 Pending에 swap해 퇴역 asset을 계속 pin하는 구조는 쓰지 않았다. 기존 양 슬롯 ARO·Initialize와 같은 revision의 재소비 no-op을 유지한다. 새 cache/manager·지연 프레임·품질/Hz/예산·판정 pose/socket/root motion·Config/에셋 변경은 없다.

이 변경은 값 전달의 불필요한 복사를 줄이는 구조 개선이다. 기존 TArray 대입은 capacity를 재사용할 수 있었고 move는 이전 buffer를 해제하는 위치를 바꾸므로, **heap 할당 두 번 제거·FPS 향상·115의224.847ms 원인 해결**로 주장하지 않는다. local trajectory 출력 생성은 여전히 존재한다. 기존 full Player NativeUpdate와 exact trajectory library call에 static CPU scope2개를 추가해 별도로 원인을 구분한다.

**검증:** 기존 SnapshotPublicationAndReinitialize에서 실제31-sample heap 주소가 local→Pending→Consumed로 같은지, lvalue producer 저장소 격리, 반복 consume의 값 보존, Pending/Consumed Chooser의 실제 GC 생존과 empty replacement 뒤 회수를 확인했다. 원 final-publication-before-decision·BP/public trajectory 독립 값·reinitialize/새 owner 경계는 유지했다. Editor221 전체 direct UBT0/15.69초, 관련 PlayerLocomotion·proxy GC·pose/montage 수명의 RHI native51은 모두 Success·자동화 경고/실패0/native0(2.727초)이다. Game78 전체 direct UBT도0/18.34초다.51 native는 전체 캐릭터/장비/골격의 시각 품질 승인이 아니며 새 Cook/Shipping을 수행하지 않았다.

같은 fixture/에셋/설정의 `MMOFoundation_20261102_Anim116Fanout32`를 한 번 실행했다. 서버와 실제 client2의 native0/원 검증 성공, 실제 montage/attack 동시 peak32, 자연 시작·종료32·고유 접촉96·matching GE/Health 감소64, 취소 시작·종료32·접촉/피해0·정리1과 GAS 오류0을 유지했다. authority target192행·Source/runtime/보호 에셋 전후 불변을 확인했다. 이32공격의 client 피해 전송·전체 MMORPG/수용량·화면 FPS를 검증했다고 확대하지 않는다.

**CPU 관측:** late server-only trace1,286,293bytes SHA256 `BCD58129EE85CF5181FABAA38FED46BF81123A4E90F52FADA3480F5E9C1DD55B`는 한 Insights 추출 전후 동일하다. 새 animation2/NPC4/physical8 timer ID와 GT 실제 이벤트5902를 확인했다. GC prune/SSRRequest는 미관측이다. exact Player NativeUpdate와 trajectory library는 각각1502회·inclusive133.342/124.300ms로 library가 전자의 약93.2%였다. library 범위는 history·prediction·출력까지 포함하므로124ms 전부를 prediction loop 또는 worker로 즉시 이동 가능한 비용으로 보지 않는다. 현 ABP_Master의 inclusive234.234/exclusive74.754ms는 새 native child scope를 제외한 값이다.115의 exclusive224.847ms와 직접 비교해 약150ms 개선이라 해석하지 않는다. snapshot 이동의 별도 전후 ms 효과는 측정하지 않았다.

분석 helper의 첫 frame join 가정은 실패로 보존했다. 실제 frame marker보다 앞선 animation entry4개(두 scope 각2)와 마지막 끝 시각이 무한인 열린 frame1개를 확인해, 원 이벤트를 버리거나 종료 시각을 발명하지 않고 별도 기록했다.57개 frame begin record이며5902개 중5898개가 해당 frame 구간에 연결된다. 이 보완은 기존 원 trace 재분석이고 게임 재실행·통과 조건 변경이 아니다. 기존30Hz 제한 대기1225.638ms를 포함한 server frame을 렌더 FPS로 바꾸지 않는다. 다음은 A03/A08와 현재 native solver의 GT/UObject/rollback-blackboard 의존성을 확인해 안전한 pure-value 계산 분리 또는 더 작은 기존 경로 개선을 선택하는 일이다. stock Mover/BP predictor를 그대로 worker에서 호출하거나 full-body montage를 이유로 trajectory/pose/history를 생략하지 않는다.

최종 Source5개만115와 다르며 신규 Source·Config·에셋·protocol11/Snapshot3 변경은 없다. 원/후보·peer·RHI·실제32 실행·정적 버퍼 소유권·raw trace/분석·frame 경계 보완 근거는 Saved/ArchitectureBatch116ServerAnimation에 있다. FinalWorkspace116.json SHA256 `3f2b491e473334da5835384514aa29667bdbf989f637431acd548c5e5d3876ff`(Source1224/runtime39), 최종 Game에서 바뀐 runtime은 Project_J.exe 하나다. 엔진·빌드·분석 프로세스0, 다음 Editor222/Game79다. 전체 목표는 active이며 A03–A12·운영 provider·목표 규모/실제 콘텐츠의 남은 작업은 유지한다.
## 117차 native 지상 설정 쓰기와 실제 예측 반복 경로

**실제 변경:** 공통 GroundMovementLibrary는 매 호출의 `RequestGameplayTag("Mover.AnimRootMotion")` 대신 같은 선언/정의를 가진 기존 native tag를 직접 사용한다. 프로젝트 GroundWalkingMode 계열은 좁은 library friendship을 통해 inherited protected float4개를 직접 쓴다. BP descendant의 native 부모 필드도 같은 주소이며 event dispatch는 바꾸지 않는다. 일반 USmoothWalking 호출은 cached FFloatProperty4개와 기존 schema check를 유지한다. 같은 동적 per-step 계산 뒤 네 값을 쓰며 MaxSpeedOverride·velocity input의 early return·slope/root-motion·회전 보정·기존 native spring solver 위치/상태를 유지했다. 새 setter API/parameter struct/cache/worker 경로·설정·에셋 변경은 없다. dictionary/property indirection을 줄이는 구조 개선이며 정량 FPS/CPU 개선량은 별도 미측정이다.

프로젝트 소유 Mover fork에는 전체 GetPredictedTrajectory와 정확한 예측 반복 구간의 static CPU scope만 추가했다. 반복은 시작 시 기존 exact native Flying parameter 후보가 있는지에 따라 NativeFlyingCandidateLoop/GenericLoop로 표시한다. 후보가 실행 중 invalid 상태에서 기존 fallback을 쓰는 규칙은 유지하므로 candidate 이름을 모든 step의 native 실행 보장으로 해석하지 않는다. query당 한 번만 계측하며 per-step trace/counter를 넣지 않았다. scope 제거·공백 정규화 시 원 함수와 정확히 같음을 확인했고 sample 수/Hz/solver/quality와 generic/BP/기반 이동/async 경로를 유지했다.

**검증:** 기존 NativeDecisionBoundaries의 대조 Mode를 실제 USmoothWalkingMode로 바꿔 새 native path와 같은 경로끼리 비교하지 않게 했다. 양수 delta의 비자명한 spring 결과·private reflected state 동등성을 보존하며 native tag identity·네 값·sprint·실제200→0 목표의 landing 감속·velocity 우회의 값/속도 제한 보존을 확인했다. 준비 중 첫 landing fixture의 idle 입력/목표 불일치를 원/수정 후보로 남기고 실제 감속 입력으로 고쳤다. Source 동작 우회나 delta0으로 통과시키지 않았다. Editor222 전체 direct UBT0/32.00초, 집중 RHI6 Success·자동화 경고/실패0·native0(0.211초), Game79 전체 direct UBT0/27.50초다. CommandAdmission·StrafeSprintCommandAdmission·NativeFlyingTrajectoryParams·NPC GroundRootMotion/IntentLifecycle를 함께 확인했다.

동일한 actual32 코호트 `MMOFoundation_20261102_Trajectory117Fanout32`는 세 프로세스 정상 종료0/검증 성공이다. 실제 peak32, 자연 시작·종료32·고유 접촉96·matching GE/Health64, 취소 시작·종료32·접촉/피해0·정리1/GAS 오류0과 Source/runtime/보호 에셋 불변을 유지했다. client는 기존 초기 AOI를 확인하며 이32공격의 피해 전송을 검증한 것은 아니다. 새 Cook/Stage/Shipping·전체 콘텐츠 시각 품질은 수행 범위 밖이다.

**실제 비용과 다음 판단:** late trace1,313,248bytes SHA256 `B5AAE054020D5475FD78D53BE160A22674F24B994D59B81E83D11210B730E7C3`는 추출 전후 동일하다. NPC4/animation2/물리8/전체예측·generic loop의 실제 ID와 GT entry8832를 확인했다.58개 frame begin record의 마지막1개는 열린 frame으로 남겨 끝 시각을 발명하지 않았다. PlayerGTUpdate1504회127.796ms, 그 안의 trajectory 전체 library117.649ms→GetPredictedTrajectory115.215ms→GenericLoop110.146ms는 중첩 inclusive 값이다. NativeFlyingCandidateLoop·SSRRequest·GC prune은 미관측이다. generic loop는 exact Flying 최적화 밖의 GenerateMove를 사용하는 경로이며 반드시 BP VM이라는 뜻은 아니다. 고정 내부 sample 수나 모든 mode의 적격률도 추정하지 않는다.116과의 전체 비용 차이는 호출 수·관측 구간·계측이 달라 source 변경의 causal ms 이득으로 계산하지 않는다.

기존 thread-safe phase의 실제 GameThread/Foreground scope도1504회이며 Worker/ParallelEvaluation은 미관측이다. FApp과 mesh dispatch의 dedicated 기본 gate 및35차의 지원 `-useperfthreads` Off/On/On2 근거를 재사용했다. 현재 baseline 설정은 유지했으며, 다음118은 같은32 조건에 기존 runner의 **ServerPerformanceThreads 하나만** 사용해 실제 worker dispatch와 정확성을 확인하는 단계다. 이 옵션만으로 NativeUpdate의 GT trajectory 생성 자체가 worker로 이동하지 않는다. stock predictor는 live mode/gravity UObject·rollback-blackboard·native/BP GenerateMove를 사용하고 ground bridge도 mode/spring 상태를 쓰므로, 함수 전체를 worker에 옮기지 않는다. pure Flying subset만으로 이번 hot generic 경로를 해결했다고 하지 않으며 전체 A03/A08/A09도 미완료다.

최종116 대비 Source3개와 Plugin1개만 바뀌었다. 원/후보·landing fixture 보완·peer·빌드/native·실제32·trace/분석·scope-only 동등성은 Saved/ArchitectureBatch117TrajectoryExecution에 보존했다. FinalWorkspace117.json SHA256 `89d4352f9b0bcdeafaf64febb19d60eece109a0c880b6bca61f870868d492877`(Source1224/runtime39), 최종 Game 빌드에서 바뀐 runtime은 Project_J.exe 하나다. 엔진·빌드·분석 프로세스0, 다음 Editor223/Game80이다. Protocol11/Snapshot3·Config/에셋과 이전 Cook79/Stage82는 유지하며 전체 MMORPG 목표와 운영 provider/목표 규모의 잔여 작업은 active다.
## 118차 지원 서버 프로필에서의 실제 worker 실행

**진행과 범위:**117의 구현·집중 회귀·동일32 실제 전투와 generic 예측 경로 계측을 재사용했다. 현재 Source/Config/에셋을 바꾸거나 빌드·일반 회귀를 반복하지 않고, 기존 runner의 `-ServerPerformanceThreads`만 추가한 `MMOFoundation_20261102_Trajectory118PerfThreads`를 한 번 실행했다. 이는35차에서 검증한 지원 process startup `-useperfthreads` 정책이다. 엔진의 dedicated 기본 FApp gate를 실행 중 강제 변경하거나 pool·CVar·품질·Hz·root-motion/visibility 설정을 추가하지 않았다.

**실제 정확성:** dedicated Editor host와 실제 client2 모두 native0/원 검증 성공이다. 실제 montage/attack peak32, 자연 시작·종료32·고유 접촉96·matching GE/Health 감소64, 실제 pre-impact 취소32·접촉/피해0·Health 보존, 정리1/GAS 오류0을 유지했다. Source/runtime/보호 에셋도 전후 불변이다. 두 client는 초기 AOI를 확인하며 새32공격의 피해 전송을 승인한 근거는 아니다. 이 결과를200연결·서버 수용량·Shipping 배포나 client FPS로 확대하지 않는다.

**입력 비교:** OFF117과 ON118의 source1224·선택 에셋·실제 UnrealEditor-Cmd exe와 관련 runtime38 항목은 일치한다. 양쪽 client 인수는 unique log/output/UserDir 경로 외 정확히 같고, 서버 인수의 유일한 기능 차이는 `-useperfthreads` 하나다. 중간 Game79 빌드로 미사용 Project_J.exe만 달라 full39 vector 동등성은 false로 남겼다. old executable 복원이나 공용 provenance 예외를 만들지 않았다. 비교와 양쪽 manifest 지문은 Saved/ArchitectureBatch118ServerThreads/InputComparison118.json에 있다. 이 정책은 다른 FApp 소비자에도 영향을 주므로 애니메이션만의 개입으로 인과 해석하지 않는다.

**실제 worker 증거:** server-only late trace1,432,865bytes SHA256 `DE9350411C6726E19B33D1E8ACE0430046A32A6B5275C7583C34A4C1FF23BC33`는 내보내기·분석 전후 동일하다. 기존 phase1 marker1436회가 실제 비GT worker4개에서 실행됐다: Foreground Worker #0(ID6)490, #1(ID7)606, Background Worker #0(ID8)175, #1(ID9)165다. GT(ID2)는91회다. phase2의 IsRunningParallelEvaluation1436/Foreground91은 별도 branch 관측이며 각각 실제 비GT/GT와 대조했다. thread-safe라는 함수 이름만으로 worker라고 판정하지 않았다. 이 marker는 관측한 native phase의 위치이며 모든 actor/pose/graph 비용이나 전부의 병렬화를 뜻하지 않는다. 전체 NativeUpdate1527회125.092ms와 trajectory1525회119.301ms, generic loop1525회114.309ms는 여전히 GT다. nested 시간을 더하거나117과의 다른 구간/호출 수를 정량 FPS 개선으로 세지 않는다.

**분석 도우미 보완:** 첫 Insights export 프로세스는0이었지만 helper가 frame60행을 CPU로 검사해 identity false/60을 기록했다. 설치 TimingExporter.cpp의 FrameEnumerator505–558/612–623은 CPU의 TimingEventFilter를 적용하지 않아 no-thread-filter export에 합성 GameFrame을 포함한다. 원 helper·실패 Verification·CSV를 보존했다. 같은 원자료의 정확한 `(ThreadId,TimerId,name,start,end)`와 FrameThreadId16777216/depth1·Frame metadata를 대조해 frame60개만 분리했고, 실제 CPU phase3054개는 모두 ID/name/thread/label이 일치하며 unknown0이었다. 마지막 frame의 열린 +inf 끝도 그대로 보존했다. 분석 차원 분류 수정이며 게임플레이/성능 설정·원자료·CPU identity 조건을 바꾸지 않았고 UE/Insights/게임을 재실행하지 않았다. Correction1Outputs/Verification.json SHA256 `5f30843b493c813e4de04cd4f05c7c1bb703565427fdaab8ff274e3b149934e9`가 실제 재분석의 성공 근거다. future PowerShell helper는 구문 검사만 받았고 수정 후 실행 검증은 하지 않았다.

**다음 경계:** 실제 엔진 animation worker 경로가 이 전투 demand와 공존함을 확인했지만, live mode/gravity/rollback-blackboard/BP 호출을 포함하는 원 predictor 전체를 worker로 이동하지 않는다. 다음은 기존 지상 spring 계산을 중복 없이 하나의 값 수학 구현으로 분리하고 실제 mode가 이를 사용하는 경계를 검토한다. generic SyncState의 존재만으로 immutable/no-UObject packet이라고 할 수 없으며, owner/source/history·GT tuning writer와 필수 public 값·GC 수명도 별도로 해결해야 한다. A03/A08/A09와 전체 MMORPG 목표의 남은 범위는 active다.

PostRun118.json은 현재 Source1224/runtime39가 FinalWorkspace117 SHA256 `89d4352f9b0bcdeafaf64febb19d60eece109a0c880b6bca61f870868d492877`와 정확히 같음을 확인한다. 새 full snapshot·빌드·Cook/Shipping을 중복 생성하지 않았다. 관련 운영 기준도 기존 PerformanceOptimizationFoundation에 반영했다. 엔진·빌드·분석 프로세스0, 다음 Editor223/Game80은 아직 사용하지 않았다. Protocol11/Snapshot3·Config/에셋과 이전 Cook79/Stage82의 입력 구분은 유지한다.

## 119차 공통 지상 spring 계산과 값 소유권 경계

**실제 변경:** 프로젝트 소유 Mover fork의 SmoothWalking 수학을 기존 GroundMovementUtils의 단일 `ComputeSmoothWalkingMove`로 옮기고 실제 mode가 이를 호출한다. 입력은 매 step의14개 tuning float·시간·목표 속도/회전·초기화 조건이며, `FSmoothWalkingStateView`는 동기 호출 동안만 빌린 기존 spring 다섯 필드의 참조다. helper 내부는 수학만 수행한다. mode의 기존 zero-delta/default-sync guard, private state 생성→blackboard 읽기→new/stale 및 role 판정 순서와 초기화 조건을 유지했다. SimulationTick·private reflected state의 경로/Clone/NetSerialize/reconcile/interpolate·117의 동적 tuning writer·BP event dispatch는 바꾸지 않았다. 별도 solver 복제·새 queue/pool·prediction sample/품질 변경은 없다.

view는 소유 packet이 아니다. GT mode의 view를 worker에 capture해서는 안 되며, worker는 자신의 다섯 값으로 그 안에서 view를 만들어야 한다. generic SyncState도 무조건 값 소유라고 간주하지 않는다. 현재 FMoverDataCollection의 명시 대입은 deep clone이지만 implicit copy constructor는 내부 shared pointer를 공유할 수 있다. 전체 collection의 copy 계약을 임의 변경하지 않고 이번 수학 경계에서 collection/UObject/blackboard를 제외했다. 초기 Saved-only V1 후보는 적용하지 않았고 V2가 기준이다. live owner·gravity·mode/BP·history를 사용하는 원 predictor는 여전히 GT다.

**독립 reference와 유지 이유:** production 변경 전에 기존 mode에서12개의 고정 입력을 실행해 출력과 실제 private spring NetSerialize 바이트를 채취했다. 기존 rollback blackboard의 이전 frame 유효/만료·new/stale/retained 상태, 두 facing spring 경로·가감속·외부 영향·deadzone·zero delta를 포함한다. 영구 `Tests/Data/ProjectJSmoothGroundReference.inl` SHA256 `ff6fa106e363b6e92ce4bf35231b8e859defac749c21841f20eb94005ec34fb8`는 그 결과이며 삭제 대상 일회성 캡처가 아니다. 최종 테스트에서 reference 기록/파일 쓰기/소스 해시 검사 경로는 제거했고 입력·codec·비교 기준은 유지했다. 원 mode와 현재 mode·직접 GT helper·기존 ThreadPool의 한 배치에서 실행한 helper를 비교한다. worker closure는 소유한 native 값만 받으며 직렬화/테스트 단언은 GT 복귀 뒤 수행한다. 실제 비GT 실행, vector1e-8/quaternion1e-6 비교와 spring 직렬화 바이트 일치를 확인했다. 이는 실제 production trajectory handoff의 증거와 구분한다.

**검증:** Editor223은 테스트에서 protected derived cleanup을 직접 부른 C2248로 정상 실패6했다. 같은 객체를 public base virtual을 통해 정리하도록 고쳐 동적 cleanup을 유지했고 원 실패를 보존했다. Editor224 전체 direct UBT0/4.96초와 변경 전 reference1 Success·경고0/native0 이후 production/compare-only 후보를 적용했다. Editor225 전체 direct UBT0/15.07초, 최종 관련 RHI57 Success·경고/실패/not-run0/native0(2.872초), Game80 전체 direct UBT0/24.20초다. 매 회 source/runtime 지문을 보존했다. 원 spring schema/직렬화·일반 Ground/RootMotion·명령 admission·애니메이션 게시/GC·NPC 취소/수명을 함께 확인했다. 이동한 수학의 기존 trailing whitespace만 정리한 기록도 남겼으며 계산식/순서는 바꾸지 않았다.

같은 실제32-caster 지원 서버 실행 `MMOFoundation_20261102_Ground119PerfThreads`는 host/client2 모두 native0/원 검증 성공이다. 실제 montage/attack peak32, 자연 시작·종료32·고유 접촉96·matching GE/Health 감소64, pre-impact 취소32·접촉/피해0·정리1/GAS 오류0을 유지했다. source/runtime/보호 에셋은 전후 동일하다. 기존 client AOI 확인을32공격의 피해 전송·200접속 수용량 검증으로 확대하지 않는다.

**관측과 다음 경계:** late trace1,377,897bytes SHA256 `491650700B9C41B41EE80F2369C2D1C1FB045B0BC874F8BB237C7A1A8A752560`는 추출 전후 동일하다.118에서 수정한 exporter를 실제 실행해 합성 frame60행과 CPU phase3060행을 정확히 구분했고 CPU identity 오류0이었다. 실제 thread-safe phase는 비GT worker4개1441회/GT89회이며 별도 parallel-evaluation 분기도 같은 lane과 대조했다. NativeUpdate1530회122.825ms→trajectory library117.331ms→전체 예측115.659ms→generic loop112.539ms는 중첩 inclusive이고 여전히 GT다. 다른 실행과의 ms 차이를 인과적 성능 향상이나 화면 FPS로 계산하지 않는다. 다음은 hot generic ground 예측의 live 접근·collection 복사와 마지막 tuning writer를 보존하면서 소유 값 경계를 실제 소비 경로까지 확장할 수 있는지 좁게 확인한다. 새 전수 감사나 unsupported BP/기반 이동의 강제 worker 호출을 추가하지 않는다.

최종 변경은 기존 Source/Plugin3개와 신규 영구 테스트 cpp/reference2개다. 원/후보·reference 채취·cleanup 보정·peer·빌드/RHI·실제32 실행/trace는 Saved/ArchitectureBatch119GroundSolver에 있다. FinalWorkspace119.json SHA256 `27d5642a17a3a39496f840296b581091119dbf720d7885251a7dc749676ba114`(Source1226/runtime39), 최종 Game 빌드에서 Project_J.exe만 바뀌었다. 엔진·빌드·분석 프로세스0, 다음 Editor226/Game81이다. Config/에셋·Protocol11/Snapshot3와 이전 Cook79/Stage82 구분을 유지하며 Shipping/전체 품질·A03–A12·운영 provider와 전체 MMORPG의 잔여 작업은 active다. 충분한 품질에서 최대한 최적화하는 것이 목표이며 에디터100FPS와 특정 인원수를 결합한 하드 합격선은 두지 않는다.

## 120차 실제 지상 예측의 요청별 준비와 값 계산

**실제 변경:** hot generic 예측의 반복 live 접근을 줄이는 실제 consumer를 연결했다. 기존 MoverComponent의 동기 GT prediction-blackboard 수명 안에서 요청당 한 번 native extension을 호출하고, 프로젝트 Mover는 canonical GroundWalking 계열에서 입력·live 설정·바닥/중력·spring seed·기반 자세를 소유 값으로 수집한다. 별도 executor/manager/pool을 추가하지 않았다. 기존 SimpleWalking 입력 투영, 프로젝트 gait/감속/회전 준비와 post-solver 제한을 공통 값 함수로 옮겨 **실제 이동 모드와 예측이 같은 수학**을 소비한다.119 spring 구현도 그대로 재사용한다. inner loop에는 UObject·collection·blackboard·기반 component 참조가 없고 기존 전체 sample 수/간격·transform/velocity/derivative/time을 계산한다. 현재는 같은 GT 호출에서 계산하며 production worker handoff 완료로 세지 않는다.

상위 모드가 이전 MaxSpeedOverride로 이번 desired velocity를 만든 뒤 다음 상한을 쓰는 순서와 마지막 MaxSpeed/Acceleration/Deceleration/TurningStrength/FacingSmoothingTime을 보존했다. Nav velocity는 원래처럼 spring·네 tuning writer·회전 제한을 우회하며 root-motion 속도도 유지한다. history·visual-root transform·gravity 복구·EndPrediction·resampling은 기존 경로를 재사용한다. fallback에서도 sample 작성 전 중복 world getter만 합치고 GenerateMove 뒤에는 state를 다시 읽어 mutable 확장을 보호했다. native UHT는 원래 C++ implementation을 직접 호출하므로 collection deep copy를 전부 제거했다는 주장은 하지 않는다.

**범위와 기반 좌표:** native parent가 정확한 프로젝트 GroundWalking이고 두 생성 event가 native로 해석될 때만 준비한다. 사용자 BP override·별도 native subclass·async backend·optional query·unknown modifier/tag provider는 기존 실행을 유지한다. bone이 없는 실제 Box/Capsule/Sphere/StaticMesh component는 등록·수명·동일 world와 자세를 확인해 입력 base와 Sync base를 독립 캡처한다. 미지원/custom/skeletal/socket base는 기존 경로다. 첫 sample은 Sync의 **기존 cached base**를 사용하며, 첫 advance 이후 current base 자세를 쓰는 원 setter와 같은 ToLocal→ToWorld/Rotator 왕복을 매 단계 유지한다. nonuniform scale은 원 NoScale 계약을 그대로 따른다. no-input intent는 raw base intent와 첫 cached/이후 current quaternion의 차이를 유지한다. world-space spring 다섯 값에는 base 회전을 적용하지 않는다. 특정 에셋 경로별 예외나 기반 해제/mesh 고정은 없다. runtime `ProjectJ.Mover.PreparedGroundPrediction` 기본1,0은 원 경로이며 non-Shipping read-only query counter는 실제 수락만 계측한다.

**발견과 보정:** 처음 전체 비교는57 Success/1 Fail/native255였다. velocity와 fallback은 일치했지만 spring6조건은 sample2부터 달랐다. blackboard의 HasEntry/TrySet 성공이 readable predictive receipt를 보장한다는 가정을 제거했다. 실제 timestamp가 읽을 수 없으면 기존 mode는 매 sample stale reset을 수행한다. 초기 reset을 먼저 읽고 모든 admission 성공 뒤 실제 TrySet→TryGet으로 후속 reset 정책을 캡처한다. BeginPredictionFrame은 real timestamp를 진행시키지 않으므로 callback 없는 이 동기 loop에서 그 결과가 유지된다. 원 EndPrediction 정리도 유지한다. fixture clock·허용 오차·입력 범위를 바꿔 실패를 숨기지 않았다. 컴파일 전 UObject guard를 올바른 RF flag로 고쳤고 Editor226의 최초 C2065는 기존 sample 타입 forward declaration으로 해결했다. 원 후보/실패는 보존했다.

**검증:** Editor227/228/229/230/231/232 전체 direct UBT 정상0, 최종 Game81 전체0/62.82초다. RHI Native120_2는58 Success·경고/실패0/native0였다. 기반 좌표 확장 뒤 Native120_3은58개 모두 통과/native0이나 query-only fixture를 실제 물리로 재개할 때 pose/pending 불일치 경고1을 남겼다. 테스트만 기존 public authority handover로 보정한 Native120_4는 집중2 Success·경고/실패0/native0이며, 생산 소스가 같은 나머지57개의 근거를 재사용했다. AfterNative120_4는 전체58 입력 이후 바뀐 파일이 테스트 cpp 하나뿐임을 확인한다. 원 경고도 보존했다. 전체 query 비교는 기존0/신규1의 실제 분기 counter·동일한 시작5개 writer·독립 nested 값·92/13 samples와60/37Hz·전 sample과 최종5개 writer·공개 physical/private spring/input·optional caller 불변을 함께 확인한다. 회전 base, nonunit scale, cached≠current 자세, 입력/Sync base의 독립성, no-input, 실제 backend 시간 진행도 포함한다.119 reference SHA256 `ff6fa106…34fb8`는 변하지 않았다.

**실제 적용과 CPU:** 첫 actual32 실행은 기능 검증/native0이었으나1491회 모두 generic이어서 새로운 native 실행을 요구한 Export1은 false를 유지했다. source상 PopulateMoverInput의 기반 좌표화와 물리 floor 계약을 확인해 지원 범위를 연결했다. 새 관측은 기존 Stage50의 물리 settle 뒤/late trace 전 실제 caster32개의 mode/native parent/event owner/base/input/modifier를 CSV에 한 번만 저장하며 실패는 기존 Finish(false)로 전파한다. 공격/품질/시간 제한은 바꾸지 않았다. 최종 `MMOFoundation_20261102_Ground120PerfThreads2`는 세 native0/원 검증 성공, 실제 montage/attack peak32·고유 접촉96·matching GE/Health64·취소32의 접촉/피해0·정리1/GAS 오류0이다.32개 실제 GroundWalking/native parent와 Box sync base를 확인했고 이 시점의 using_input_base는0이다. 이 한 시점 관측을 전체 trace의 입력 상태 보장으로 확대하지 않는다.

최종 trace1,359,989bytes SHA256 `D812FA5B463095F2B2FB83D269F294F724BD61D274E1FCDC1A9CF02B28DB513E`는 추출 전후 동일하다. Export2는 실제 native ground loop1506개를 요구해 통과했고, 전체 예측1506회49.142ms 중 native loop44.100ms였다. GenericLoop metadata는 important-event cache에 남지만 late window의 실제 generic event는 없다. Player NativeUpdate55.690ms→전체 trajectory library50.768ms도 중첩 inclusive다. 첫 실행의 전체 예측101.371ms/1491회·generic97.959ms와 비교한 **관측값**이며 동일 binary CVar A/B나 FPS/대규모 수용량의 정량 보장은 아니다. 반복 live 조회를 제거한 실제 실행과 CPU 관측을 구분한다. native thread-safe phase는 실제 비GT1419/GT87회였고 evaluation branch는 parallel1417·foreground89(그중 비GT2)로 별도 차원이다. 이 marker들을 같은 뜻으로 합치거나 trajectory가 worker로 옮겨졌다고 하지 않는다.

최종119 대비 Source/Plugin13개(신규 prediction cpp1)만 바뀌었고 Config/에셋·Protocol11/Snapshot3는 유지한다. 원/후보·보정·peer·모든 실패/빌드/native·두 실제 코호트·raw trace/분석은 Saved/ArchitectureBatch120GroundPrediction에 있다. FinalWorkspace120.json SHA256 `83317d562dbf19ceab125c4b63147ad71f3a22d43f95eb23dc46717ee4908cd0`(Source1227/runtime39), 최종 Game에서는 Project_J.exe만 달라졌다. 엔진·빌드·분석 프로세스0, 다음 Editor233/Game82다. 이전 Cook79/Stage82·Shipping/운영 provider·전체 콘텐츠 품질/목표 규모와 A03–A12의 잔여 작업은 완료로 바꾸지 않는다. 추가 worker job은 스케줄링 비용·현재 프레임의 GT 소비/게시·취소 수명을 함께 판단하며, 이 작은 계산마다 별도 task를 무조건 만들지 않는다. 전체 MMORPG 목표는 active다.

## 121차 장비 등록 재진입과 자산 소유권 인계

**실제 변경:** 기존 장비 visual load 완료가 RuntimeItems의 mutable row 참조를 SetMesh/부착/RegisterComponent 너머 유지하던 경계를 고쳤다. 등록은 실제 native/Blueprint animation initialize 콜백을 실행할 수 있다. 새 코드는 기존 binding·visual revision·exact token·item/path/pose 정책과 owner/world/service를 캡처하고 self/owner/definition/source/asset/candidate를 호출 동안만 강하게 보유한다. 매 확장 가능한 경계 뒤 현재 row와 source/asset/attachment를 재조회하고 마지막에는 원 resolver로 현재 follower/socket 선택을 다시 확인한다. 오래된 candidate만 파괴하며 새 generation의 메시·token은 건드리지 않는다. 같은 old request의 준비 실패는 token을 먼저 떼고 반환한 뒤 기존 bounded retry를 사용한다. 새 상태·manager/pool·공장 test hook은 추가하지 않았다.

성공한 등록 뒤 RuntimeItems에 메시를 게시하고 token0→captured service Release 순서로 로더 소유권을 끝낸다. 실제 장비 컴포넌트의 cooked에도 존재하는 `USkinnedMeshComponent::SkinnedAsset` UPROPERTY가 자산을 보유한다. 따라서 장착 완료 장비는 로더의2048 global/64 owner lease와 resident 유지 순회를 계속 점유하지 않는다. 한도·native load 실제 완료 전 예약·apply 예산은 유지한다. 공유 service의 Tick은 Group/Asset/Owner와 moved callback을 보유하므로 그 callback 안의 Release도 안전하다. 이후 재장착 때 warm streamable handle을 다시 만들 수 있는 tradeoff는 있으며, 이 변경을 메시 unload·GPU memory 또는 FPS 감소로 주장하지 않는다.

**유지한 경계:**78/101의 hard closure 근거를 재사용했다. Family의 실제 retarget node 설치·원 graph defaults 복원과 GC pin은 필요해 보존했다. UIKRetargeter의 hard IK-rig와 soft/editor-only preview mesh는 서로 다르며, hard retargeter라는 이유로 preview 메시 전체가 게임에서 즉시 로드된다고 하지 않는다. 서버 gameplay pose·notify·소켓/무기·GAS grants·공개/비공개 복제·retry 한도·cloth 품질은 이번 소유권 인계와 별개다. Unreal MCP는 연결되지 않아 현재 source/설치 엔진 코드와 기존 직접 UBT/native 경로를 사용했다. A08 전체 완료나 새로운 async retargeter loader의 필요성을 만들어내지 않았다.

**실제 재현:** 영구 C++ fixture는 기존 owner에 consume-once callback을 두고, 임시 Engine SkeletalCube 복제본의 native post-process 초기화가 이를 호출한다. 자산 파일·시계·전역 callback을 바꾸지 않고 기존 shared loader의 진짜 warm completion을 자연 engine frame에서 기다린다. 기존 생산 코드인 Editor234의 NativeBatch121_BeforeFix는 callback4조건을 실제 실행해7개 수명 단언 실패/native255를 기록했다. 해제/파괴 후 등록된 orphan1개가 남았고, 동일 item A→B→A의 새 row에 old candidate가 게시되어 replacement lease1이 남았다. 정상 적용도 lease/group/token1/1/1을 계속 보유했다. 실패 뒤 orphan 정리는 단언 후 수행해 원 실패를 숨기지 않았다.

개선 뒤 동일 callback과 추가 handoff 단언을 실행한 Editor235의 RHI NativeBatch121_1은14 Success·경고/실패/not-run0/native0(1.637초)이다. 정상 lease/group/token0/0/0과 setup strong pin 제거 후 실제 GC에서 컴포넌트의 asset 생존, 실제 unequip 파괴를 확인했다. 공개 BindNull·A→B→A·DestroyComponent의 callback은 각각1회, retired candidate의 등록 잔류0이며 새 revision3의 자기 메시1개만 남고 lease0이었다. 슬롯/grant 원 ASC·commit/aggregate retirement·privacy projection·retry·modular pose·공유 loader pressure/resident/churn 회귀를 함께 확인했다. 테스트가 보유한 메시 참조로 GC 성공을 대신하지 않았다. 원 동기/LeaderPose/headless 본문도 유지했다.

Game82 전체 direct UBT0/20.81초 뒤 같은 Editor 실행 파일/DLL의 NullRHI 집중2개는 Success·경고/실패0/native0(0.021초)이다. 렌더링 불가에서 요청/토큰/시도/타이머/늦은 메시 생성이 생기지 않는 기존 계약을 확인했다. Game 빌드에서 달라진 runtime은 미사용 Project_J.exe 하나이며 full39 runtime의 전후 동등성으로 오해하지 않는다. Editor235 전체 direct UBT도0/7.45초다. Editor233은 backup-preserve guard로 테스트 적용이 중단된 뒤 실행된 unchanged no-op이며 baseline 증거로 쓰지 않는다. 원 guard 실패/빌드/native 원문은 보존했다. 새 Cook/Stage/Shipping·전체 장비 시각 품질/부하·특정 메모리 절감은 검증 범위 밖이다.

최종120 대비 생산 cpp1과 기존 테스트 cpp/header2개만 달라졌다. Config/에셋·protocol11/Snapshot3는 그대로다. source/후보·peer·baseline/future의 좁은 단언 차이·실패/성공·Game/NullRHI·지문은 Saved/ArchitectureBatch121EquipmentHandoff, retarget 소비 보존 근거는 Saved/ArchitectureBatch121RetargetClosure에 있다. FinalWorkspace121.json SHA256 `c2b886d5d1432514fda9ca4bb096ce1ad93f99be99635720fbf7518e196937e1`(Source1227/runtime39), 최종 NullRHI 전후 입력도 일치한다. 엔진·빌드·분석 프로세스0, 다음 Editor236/Game83이다. 이전 Cook79/Stage82와 A03–A12·운영 provider/서버·전체 MMORPG 목표의 잔여 경계는 유지하며 목표는 active다.


## 122차 준비된 이동의 실제 적용 직전 검증

기존 A10/106 근거를 재사용한 좁은 소비 경계 보강이다. Portal의 준비 완료·queue 전 합법 배치와 실제 Mover 적용 사이에 지지물/충돌이 바뀔 수 있었다. 기존 코드의 실제 native effect로 지지물 비활성화·Pawn 채널 변경·새 장애물·기록된 위치와 다른 target·지지물 파괴를 재현하니 다섯 경우 모두 먼저 이동하고 나중에 Failed로 정리됐다. 새 전수 감사·I/O manager·전역 Nav gate는 도입하지 않았다.

기존 도착 후 검사에서 고정 중심/명시 quaternion의 지면 trace·현재 이동 규칙 walkability·캡슐 overlap을 private 값 검사로 공유했다. 도착 후에는 실제 캡슐 위치와 기록 목표 사이의 기존 거리 검사를 유지한다. 실제 effect는 같은 root/capsule·request·Queued·authority·ASC epoch·permit·deadline을 확인하고 **원 TargetLocation이 기록된 ArrivalPlacement와 같은지 및 현재 목적지 지면/충돌이 합법인지**를 native teleport 직전에 검사한다. 목표를 다시 선택하거나 유효한 다른 위치로 조용히 바꾸지 않는다. 정상일 때 기존 native FTeleportEffect를 호출한다.

현재 요청의 기하 거절은 Actor/Output/blackboard를 바꾸지 않고 실제 FTeleportFailedEventData 한 개를 기존 event 목록 뒤에 추가한다. 기존 처리 callback의 exact generation/pawn/mover 확인→known false→기존 중앙 Advance의 Failed→source/논리 permit 반환을 그대로 사용한다. effect 안에서 직접 완료·큐 변경·새 timer를 만들지 않으며 stale/retired/deadline 거절은 이전의 미확정 경로를 유지한다. 이는 GT 적용 경계의 재확인이고 물리 월드 전체를 원자적으로 예약한다는 뜻은 아니다. native TeleportTo의 최종 충돌 처리는 유지한다.

Editor236 full direct UBT0(6.94초)의 tests-only 원 구현은 NativeBatch122_BeforeFix native255/1실패·25개 관련 단언 실패를 보존했다. 기존 양성/epoch 사례에는 실제 합법 floor를 제공했고 다섯 음성은 실제 collision scene을 queue 조건 이후 변경한다. 수정 Editor237 full0(17.15초) 뒤 NativeBatch122_1의 월드9개는 Success·경고/실패0/native0(3.73초)이다. 다섯 음성 모두 applied0/moved0·native failure·기존 Output/event 보존·Failed/permit0과 새 세대에 대한 이전 receipt 무효를 확인했다. baseline/final 단언은 같으며 test setup의 deprecated3인자 호출만 zero angular velocity를 명시한 지원4인자 API로 바꿨다.

기존96 runner를 그대로 재사용한 Saved/ArchitectureBatch96ArrivalRuntime/Post122FixedTarget1은 실제 MainWorld/Portal→WP 준비→native Mover→도착을 native0/32.34초, Verification.passed=true로 통과했다. Preparing/Queued permit1→Arrived0, 같은 provider와 normal predictor 복원, 전후 Source/runtime·보호9개 asset 불변이다. Game83 full direct UBT도0(20.26초)이며 Game 빌드에서 변한 runtime은 Project_J.exe 하나다. 새 RHI 품질·원격 socket·Cook/Stage/Shipping·GPU/Nav/HLOD·물리 I/O/메모리 안정화나 FPS/CCU 수용량 인증은 하지 않았다.

최종121 대비 생산 h/cpp2개와 기존 테스트 cpp1개만 달라졌다. Config/에셋·protocol11/Snapshot3는 유지한다. 원본/후보·peer·실패/성공과 입력 지문은 Saved/ArchitectureBatch122ArrivalCommit에 있다. FinalWorkspace122.json SHA256 `68aa65f4c69ce828b4fa61bca7bf2e3f623fe439a6507c63e84285a76a3fc03e`(Source1227/runtime39), 다음 Editor238/Game84다. 이전 Cook79/Stage82 및 A03–A12·실제 운영 provider와 전체 MMORPG의 미완료 범위는 그대로이며 목표는 active다. 에디터100FPS는 충분한 품질에서 최대한 최적화하자는 지향점으로 유지한다.


## 123차 실제 거리 AOI의 원격 퇴역과 재진입

기존 A07/93/106 검토를 재사용했다. isolated live RemovedFromWorld Stop/Start를 정상 거리 이탈로 간주하거나, 그대로인 외부 UObject 참조를 억지로 dirty 처리하지 않았다. 현재 생산 코드에 해당 특수 재등록 호출자는 없어 private fragment·강제 null/set·재Possess·새 manager를 추가하지 않았다. 대신 아직 직접 검증하지 않았던 정상 native Grid AOI 이탈/원격 객체 종료/재진입을 실제 dedicated server와 두 socket 클라이언트에서 연결했다.

기존 AuditNetwork fixture와 runner에 opt-in MountSpatialReentry만 추가했다(MountOnly 필수, Restart와 배타). 이 모드의 rider/mount는 처음부터 spatial이며 기본18행/별도 strict24행 검증 본문과 기본 모드 동작을 유지한다. 추가 Case44는 실제 remount→외부 관측자의 native detached view→기존 foreign rider/mount의 Destroyed EndPlay·weak liveness 종료·live iterator0·bridge mapping 해제→view 복귀 후 같은 server handle에 새 client weak incarnation·public reciprocal/PS-owned ASC/attributes·개인 상태 복원→기존 owner dismount RPC 순서다. public weak HasSameIndexAndSerialNumber로 구분하며 GetUniqueID는 진단값일 뿐 재사용 불가를 가정하지 않는다. foreign 새 Avatar의 자연스러운 로컬 epoch 재바인딩을 관측하고 actor/role/ASC를 수동 복구하지 않는다.

기존18행과 별도3역할×4행의47열 CSV를 모두 검사한다. 서버 handle은 native full comparison/실제 object roundtrip으로 확인하며, authority owner group의 ready/identity/member/요청 inclusion·같은 group/created 수와 owner-only inventory/equipment를 연결했다. raw stored camera cache/target/child connection을 기록하지만 이를 private Iris consumed-view getter나 remote ACK 전체 종료로 설명하지 않는다. native RemapDynamicObjects는 읽기만 하며 지원하지 않는 설정은 실패한다. 새 RPC·timer·엔진 수정·품질 저하·per-actor 작업 pool은 없다.

첫 Spatial1은 기존18행과 native0×3를 유지했지만 Case44 step1/20초에서 실패했다. reader의 검증된 Name 변수와 내부 name의 대소문자 충돌도 별도로 고쳤고 원 runner/raw 및 derived ReaderReanalysis를 보존했다. 진단만 추가한 SpatialDiagnostic1도 같은 실패를 반복했다. 실제 server target은 camera였지만 client target과 server cache는 원점이었다. 설치 Iris가 클래스 CDO로 기본 spatial filter를 고르는 계약에 따라 작은 native fixture camera의 CDO에 replication/AlwaysRelevant를 명시했다. Camera1은 실제 client camera 전달·far cache를 확인했지만 server cache가 계속 near여서 실패했다. stock PC가 UPawnMovementComponent 이동을 기준으로 camera send를 요청하는 차이이며, 생산 PC와 기존 NetworkLoadController는 이미 RequestMoverCameraUpdate를 UpdateCameraManager→Super 단계에서 사용하고 있었다. AuditController도 **같은 기존 공통 호출과 단계**를 재사용하도록 연결했다. 좌표 직접 쓰기·manual ServerUpdateCamera·새 RPC·deadline 증가로 통과시키지 않았다. 이 두 수정은 검증 호출자 보완이며 새 전역 MMORPG 네트워크 결함 수정으로 확대하지 않는다.

최종 Editor242 full direct UBT0(16.99초)에서 SpatialMoverCamera1은 실제 native0×3/원18행 각역할/추가12행/Source-runtime 불변으로 통과했다. foreign mount UObject57578→60493, rider60000→60446를 관측했고 server public handle120/60 및 local full handle 계약은 유지됐다. 숫자 차이 대신 weak identity/실제 EndPlay와 매핑으로 판정했다. SpatialLoss1의 LagLoss 설정(75±25ms, loss10%, dup5%)·역순 slot1 선접속도 native0×3/12행 통과했다. Default1은 새 모드 없이 기존18행 각역할/native0×3를 통과했다. 복사한 실제 CSV의 reader4검사도 실제값/재사용 가능한 diagnostic ID 허용, far행 누락/새 incarnation 판정 누락 거절을 확인했다. 합성 ID 재사용 검사는 실제 native 재사용 관측이 아니다.

Editor238/239/240/241도 full0이며241의 earlier PlayerTick 후보는 실행 전에 실제 production camera 단계로 교체했다. 최종 Game84 full direct UBT0(20.28초)이고 바뀐 runtime은 Project_J.exe 하나다. 새 전체 native 회귀·Cook/Stage/Shipping·실제 travel·멀티서버·대규모 관계별 비용/FPS·GPU 품질·외부 운영 provider를 인증하지 않았다.93 strict 특수 재등록 실패와 old remote ACK/history 범위는 유지한다.

최종122 대비 기존 fixture cpp/h와 기존 runner 총3개만 달라졌다. Config/에셋 편집·protocol11/Snapshot3 변경은 없다. 후보/원본/peer/실패/성공과 지문은 Saved/ArchitectureBatch123MountSpatial 및 Saved/ArchitectureBatch123SpatialLifetime, 실제 socket raw는 Saved/Audit6850/Round3/Network/Batch123*에 있다. FinalWorkspace123.json SHA256 `27abcbbac36ed22307a72d6bcf0e5db13b9cd5d93c4b3fa8f86ae33472b1aaed`(Source1227/runtime39), 다음 Editor243/Game85다. 이전 Cook79/Stage82와 전체 MMORPG의 A03–A12·환경/운영 잔여는 유지하며 목표는 active다.


## 124차 native 예측 조회와 실제 이동 튜닝의 격리

119/120과 실제123 입력을 재사용해 기존 애니메이션 worker로 지상 예측 값 계산을 옮기는 경계를 확인했다. closed loop는 이미 UObject-free지만120의 끝부분이 예측에서 얻은 MaxSpeedOverride/Acceleration/Deceleration/TurningStrength/FacingSmoothingTime를 실제 Mode에 다시 썼다. Directional의 끝 튜닝은 변하는 미래 속도·방향에 의존해 GT에서 한 번 계산한 값으로 대체할 수 없었다. 별도 per-actor task/새 manager, 같은 루프를 GT/worker에서 중복 계산하거나 한 프레임 늦은 commit을 도입하지 않았다. Velocity만 쉽게 이전하는 새 packet도 실제 eligible 비용 근거 없이 추가하지 않았다.

실제 정상 물리 경로는 OnPreSimulate가 승인된 속도 cap을 먼저 설정하고 GenerateGroundMove가 나머지 값을 실제 solver 전에 다시 쓴다. 따라서 직접 GenerateMove만 부른 차이를 authoritative 이동 버그로 확대하지 않았다. 대신 **같은 물리 상태/명령/시간에 대해 이어지는 예측 조회끼리 간섭하지 않아야 한다**는 요구를 실제 API로 재현했다. 현재 생산 PoseSearch predictor/engine debug caller는 Samples만 소비하고 마지막 forecast 값을 live mode에 commit하라고 요구하지 않는다.120의 값 쓰기 보존 기록은 당시 동등성 리팩터링 근거로 유지하되 원하는 writer 소유권으로 간주하지 않았다.

수정은 canonical native TryPredictTrajectory의 마지막5개 writeback을 제거한 것뿐이다. foreach 계산의 Params/State·spring 수학·기반 좌표/Euler 왕복·sample density·입력/태그 admission·BB prediction receipt/End lifetime·실제 물리 writer와 generic/custom BP fallback은 바꾸지 않았다. 미래 계산의 튜닝은 소유 값 안에 남고 이 native query는 입장 당시 live5값을 그대로 보존한다. fallback 전체나 공개 API 전체가 read-only라는 보장은 아니며 실제 worker 실행 이전도 아직 하지 않았다. protected hook 주석은 새 forecast 소유 계약에 맞췄다.

Editor243은 추가 fixture의 기존 FirstDifferent 지역 변수와 새 이름 충돌 C4456/exit6을 보존했다. 새 변수명만 FutureFirstDifferent로 고친 Editor244 full direct UBT0(8.45초)의 tests-only 원 구현에서 NativeBatch124_BeforeFix는1실패/native255·관련12단언 실패였다. cap225/가속611/감속733/turn9/facing.37이 한 번 조회 뒤165/500/300/8/.4로 남았고, 같은 physical frame의3회 중2·3번째 미래는sample1부터 달랐다. 실제 OnPreSimulate 포함 다음 물리 출력은 처음부터 query0/1/3에서 같았으며 admitted_delta=0이었다. admission을 뺀 raw direct 진단의 delta.54616을 실제 이동 결함으로 보고하지 않았다.

수정 Editor245 full direct UBT0(96.30초) 뒤 NativeBatch124_1은 기존 관련 로코모션/ground reference/command admission/flying/root-motion/proxy 수명 묶음58개 Success·경고/실패/notRun0/native0(3.39초)이다. query0/1/3 모두live5값과 반복 Samples가 같고 raw/admitted_delta=0, 정상 admitted velocity/angular 및 실제5writer 결과는 baseline과 같았다. 같은 초기 상태의 legacy/prepared sample 비교·기존 허용 오차·실제 native counter·private physical spring/input 검사를 유지했다. 기존120의 끝 필드 기대값만 native는prequery 보존, unsupported fallback은legacy 보존으로 명시적으로 바꿨으며, landing20000 내부 연산은 legacy 값과 Samples 동등성으로 계속 확인한다. baseline/final 차이는 별도 patch로 남겼다.

Game85 full direct UBT도0(66.11초)이며 Game 통합에서 바뀐 runtime은 Project_J.exe 하나다. 이번에는 복제/저장 schema나 새로운 자산/graph 연결이 바뀌지 않아 이미 검증한 socket/trace/Cook를 반복하지 않았다. 새 FPS/CCU·물리 충돌/전체 시각 품질·worker scheduling·fresh Cook/Stage/Shipping 인증은 하지 않았다. 다음 worker 경계는 owning history/current/visual/gravity·BB GT 마감, montage/BP fallback, 같은 frame의 graph 전 계산, 완료 snapshot의 GT 품질 mirror/source rebind 수명까지 함께 해결해야 한다.

최종123 대비 생산 prediction cpp1·기존 테스트 cpp1·hook 주석 header1만 바뀌었다. Config/에셋·protocol11/Snapshot3는 유지한다. 원본/후보/peer/모든 실패와 실제 성공은 Saved/ArchitectureBatch124PredictionIsolation에 있고 FinalWorkspace124.json SHA256 `5859c142e7a52c9c7907655527b8045eaeb57e5c4abb6b39c351c81351d89d5c`(Source1227/runtime39)다. 다음 Editor246/Game86이며 이전 Cook79/Stage82와 A03–A12·운영 provider/전체 MMORPG의 잔여 범위는 그대로다. 목표는 active다.


## 125차 같은 프레임의 애니메이션 worker 지상 예측

119/120의 native 커널과124의 query 격리를 그대로 사용해 실제 PlayerAnimInstance→Proxy 소비를 연결했다. Mover의 원래 prediction context 준비/중력 저장·복원/EndPrediction 순서를 공유하고, GT capture는 native admission·private spring 값·기반 좌표·visual root·입력/튜닝을 UObject 없는 소유 값으로 만든다. generic/native flying query의 기존 동작과 BeginPredictionFrame 위치는 유지했다. PoseSearch history/current 생성은 원래 GT public API 순서를 지키며 future resample은 기존 predictor와 하나의 static 계산을 공유한다. 포크 엔진 외부나 설치 엔진 파일은 수정하지 않았다.

Proxy의 기존 owning snapshot publication에 packet을 함께 옮긴다. Character NativeTS의 최종 publication 소비 뒤 Player NativeTS가 동일 프레임 계산·future velocity/turn을 완료하고, native 상태 결정·BlueprintThreadSafe/graph가 그 값을 읽는다. 추가 per-actor Task/새 scheduler/GT 중복 계산·다음 프레임 commit은 없다. 기존 pending/active 슬롯과 proxy별 재사용 scratch를 쓰며 실제 forecast/hz·graph quality·물리 simulation 빈도를 줄이지 않았다. GT BlueprintUpdateAnimation 구현, 별도 native parent/custom predictor, warped montage와 native ground 미지원 상태는 전체 기존 동기 경로를 유지한다. 포화 source generation에서는 새 defer를 거절한다.

GT PostUpdate는 completed==consumed==published revision과 현재 source generation이 일치할 때만 완료한 history/future 품질 mirror를 복사한다. update-only 완료에도 연결하며 proxy의 graph 출력은 소유 상태로 남긴다. source 교체·NativeInitialize/Uninitialize·proxy Initialize에서 pending/active/completion을 퇴역시킨다. SourceBind가 proxy barrier 전에 generation을 바꾸므로 이전 task 완료가 새 source에 반영되지 않는다. 완료 전 GT policy는 이전 완료 forecast를 읽고, worker는 현재 프레임 graph 입력을 완성한다. 임의 GT BP가 미완성 future를 읽게 하는 식으로 admission을 넓히지 않았다.

Editor246은 새 proxy scratch의 FTrajectorySampleInfo 완전 타입 include 누락(C2027/C2338 계열)/UBT6으로 정상 종료했다. 필요한 private header include만 보완한 Editor247 full0(15.73초), 테스트 통합 Editor248 full0(9.34초), Game86 full0(74.17초)이다. 기존 두 테스트를 확장한 NativeBatch125_1은 RHI58 Success·경고/실패/notRun0/native0(3.13초)이다. 실제 ThreadPool 값 계산의 모든 sample·private spring/input·mode5·중력 복원·물리 floor 보존과 source Destroy/실제 GC 후 동일성을 확인했다. Character native producer 두 instance의 CVar0/1 두 프레임 31sample·derived 값·동일 publication 완료·GT PostUpdate mirror·중복 소비/종료 퇴역을 비교했다. 이 수동 NativeTS fixture 자체는 GT에서 실행하며 실제 엔진 worker 증거와 구분한다.

실제 MMOFoundation_20261102_AnimWorker125는 기존 dedicated server/두 socket client 및 authored32-caster 시나리오를 재사용했다. native0×3·timeout0, peak32/접촉96/GE64·Health64, cancel32의 접촉/피해0, cleanup1·source/runtime 및 보호 asset 불변이다. 1,434,956B의 완료 trace SHA9e7365f9…2956c4b는 UnrealInsights native0으로 불변 export됐다. 정확 CPU timer/thread identity·synthetic GameFrames 제외 후 새 forecast Worker1416회/GT87회, outer non-GT1416회와58 server frame을 관측했다. NativeTS의 evaluation 분기와 실제 thread lane은 따로 검증했다. outer/label 중첩 시간은 합산하지 않는다.

이 capture의 GT trajectory capture/history scope는1503회 누적8.271ms, worker forecast label은1416회50.519ms, GT forecast label은87회4.932ms다. 이는 각 scope의 관측값이며 이전 binary의44.100ms loop와 통제된 A/B 개선율이나 화면 FPS로 비교하지 않는다. export의 바깥 PowerShell LASTEXITCODE 확인이 stale1을 반환했으나 native process receipt0·trace 불변·Export1 Verification passed=true를 확인해 재실행하지 않았다. 새 source·asset·품질/FPS 인증으로 오류를 감추지 않았다.

124 대비 생산15파일(신규3)과 기존 테스트2개만 변경했다. Core 패킷/커널·Mover context/resample과 실제 Player/Proxy caller가 연결되어 새 미사용 framework가 없다. Config/에셋·protocol11/Snapshot3는 유지한다. 원본/후보/peer/실패/최종 증거는 Saved/ArchitectureBatch125AnimationWorker에 보존했다. FinalWorkspace125.json SHA256 `946dee1e1d599cca8f81b851b76ca493504deaa7e0d0bfa71b398eb17b0237e5`(Source1230/runtime39), Game에서 추가 변경된 runtime은 Project_J.exe 하나다. 다음 Editor249/Game87이다. authoritative Mover/Mass/AI LOD·GT NPC 전체 수집/worker 경쟁·혼합 critical path·GPU/메모리/고속 스트리밍·전체 remote 규모/운영 provider·fresh Cook/Stage/Shipping은 여전히 남는다. 목표는 active다.


## 126차 공유 VFX notify의 참가자별 수명과 제작 검증

기존98/99 생성 예산·프로젝트 NS/NET와 공통 component를 재사용했다. shared UAnimNotifyState의 NotifyBegin이 참가자마다 전체 weak mesh map을 다시 훑어 같은 asset의 동시 시작에서 중복 검색이 늘었으며, MeshLeases 참조를 BeginCueLease→PlayCue→Niagara 생성/Activate 콜백 너머로 보유했다. matching End/Begin이 맵을 제거·재할당하거나 같은 ID를 다시 시작하면 이전 반환이 새 실행을 덮거나 별도 token을 획득할 여지가 있었다. original runtime의 강제 crash 재현을 주장하지 않고 실제 코드 경계와 지원 native activation을 검증했다.

Begin은 해당 mesh의 stale 실행만 검사하고, 외부 acquire 전에 Token0+단조 Claim을 게시해 in-flight duplicate Begin을 막는다. callback 뒤 맵을 새로 조회해 같은 claim/presentation·mesh/owner/world/등록 상태·cue·current token만 확정한다. End는 먼저 정확한 실행을 분리하고 외부 EndCueLease를 호출한다. acquire 중 End/교체/GC가 일어나면 이전 호출은 자신의 반환 token만 종료하며 새 same-ID claim을 바꾸지 않는다. 기존 Super Begin/End 순서·cue 내용·component/ManualRelease pool·복제/게임플레이 권위는 유지한다. generation 포화에서는 새 acquire를 거절한다.

전역 weak 정리는 기존 Melee/WeaponMotion의 lazy PostGC+BeginDestroy 해제 수명을 재사용한다. 설치 UE5.8 GC.cpp는5892–5898 hash/GC lock 해제,5937 GIsGarbageCollecting=false 뒤5943 PostGC를 방송한다. 살아 있는 shared notify는 dead mesh entries를 먼저 전부 분리하고, 살아 있는 presentation/owner/world를 pin한 뒤 exact token을 반환해 고아 loop도 끝낸다. 콜백을 넘어 map 참조를 보유하지 않으며 BeginDestroy에서는 delegate 해제/weak storage 퇴역만 한다. active animation asset이 notify를 보유하고 attack/owner가 component를 종료하는 원 계약은 유지한다. notify 자체가 독립적으로 unreachable이고 owner만 생존하는 조합을 새로 보장하거나 별도 deferred ticket을 만들지는 않았다.

제작 검증은 기존 AP IsDataValid의 명시 Editor LoadSynchronous 경계에만 추가했다. bLooping=false의 one-shot Niagara는 scalar cue token이 있어도 ActiveLoopingCues에서 실제 component를 추적하지 않아 matching notify/attack End가 늦은 resume를 퇴역시키지 못한다. 따라서 EffectType의 DeactivateResume/DeactivateImmediateResume/PauseResume를 거절한다. 이 reaction은 플랫폼 전역이며 현재 quality의 culling-off를 안전 근거로 쓰지 않는다. owned loop·non-resume2종·기존 EffectType 없는 system은 유지하고 runtime 기본 type 강제 대입이나 vendor/asset 변경은 없다. 내부 Niagara loop 유무를 이름으로 추측하지 않는다.

Editor249 tests-only full UBT0(7.74초)의 원 검사는3 resume 조합에서6개 단언 실패/native255였다. 실제 saved Trail은 이미 bLooping=true/project NS+NET/reaction3으로 정상이며 오류를 숨기려고 바꾸지 않았다. 최종 Editor250 full0(25.13초) 후 NativeBatch126_1은 실제 D3D12 RHI4 Success·경고/실패/notRun0/native0(2.47초)다. 기존 native fixture의 public pool prime/ReleaseToPool/reuse→실제 OnComponentActivated UFUNCTION 콜백이1/2/3회 발생해야만 통과한다. duplicate Begin, End-before-return, End→같은 attack tag 새 인스턴스→same ID Begin+fullGC 및 교체 token의 matching End를 확인했다. delegate를 수동 Broadcast하거나 production 가짜 hook을 넣지 않았다.

같은 검사에서 살아 있는 notify/owner/presentation과 owned 보조 mesh32→16→0 fullGC를 수행했다. 다음 Begin 없이 dead map/token만 사라지고 정확한 생존 token·최종 loop/pending/recovery 종료·실제 weak storage 해제를 확인했다. 생성/회수의 기존100×12 pool 및 presentation recovery,5 reaction×one-shot/owned lifetime·nullType·실제 saved AP 전체/NS/NET dirty 불변도 함께 통과했다. 보호7asset hash와 Source/runtime은 실행 전후 불변이다. Begin의 전체 map iterator 제거와 분리된 NotifyBegin/NotifyEnd/WeakMeshPrune scope는 작업 경계의 근거이며 wall-time 복잡도 벤치마크나 FPS/GPU 개선율로 바꾸지 않는다.

Game87 full direct UBT0(21.35초), Game 통합에서 추가 runtime 변경은 Project_J.exe 하나다.125 대비 기존 Source6개(Notify 생산h/cpp·Profile Editor 검사cpp·기존 testcpp2/fixtureh1)만 바뀌었고 새 runtime manager/Actor tick/production class·Config/에셋·protocol11/Snapshot3 변경은 없다. 원본/후보/peer/원 실패·최종 증거는 Saved/ArchitectureBatch126Presentation에 보존했다. FinalWorkspace126.json SHA256 `985e5335a2f23df0be3f393a323b74444fa85fb69961d8c0d6093ff78ff73939`(Source1230/runtime39), 다음 Editor251/Game88이다. 기존 실제128 pressure·GPU/VRAM·near warning/전체 시각 품질·혼합 critical path/CCU·Cook79/Stage82 이후 fresh 배포 및 A03–A12/미정 운영 provider의 잔여와 전체 active 목표는 유지한다.


## 127차 공유 NPC 대상 수집의 예산별 재개

기존85/92/100/102의 한 pass 공유 spatial snapshot·행별 pin/revision·값 worker·0.5초 원age를 재사용했다. 이미 target 위치는 producer마다 중복 수집하지 않았으므로 새 cache를 덧붙이지 않았다. 남은 CaptureTargets의 최대2048행 loop는 개별 IsDead callback 사이에서1ms budget을 확인하지 않아 예산을 소진해도 전체 registry를 끝낸 뒤 query/Core 준비를 계속했다. 이번 변경은 이 실제 GT 실행 경계에 대한 cooperative continuation이며 authoritative Mover simulation 빈도나 새로운 AI 콘텐츠 구현이 아니다.

기존 Scheduler 안에 optional 미완료 수집 하나만 보유한다. 원래 target registry revision/count/order·cursor·최초 pre-observer CapturedAt과 native 값/weak actor 배열을 유지하고, 각 행은 local 값+strong pin으로 IsDead를 호출한 뒤 serial/revision/count/index/수명/age를 재검증해 append한다. member/row 참조는 외부 callback을 넘지 않는다. 실제 soft budget 소진 시 다음 Tick에 suffix를 이어가며 완성 전에는 spatial query·Component.BeginSnapshot·LastScheduled/PendingToken·Core 예약을 만들지 않는다. 기존 ready drain/action 우선, ring·dispatch32/Outstanding64/결과 적용16/Core worker2는 유지한다.

완성 index를 만들다 budget을 소진한 경우 동일 timestamp의 built 상태를 보관해 다음 pass에서 다시 만들지 않는다. 실제 소비 때 spatial/actor 소유권을 Tick-local로 옮기고 member를 비운 뒤 extensible provider/per-agent 준비를 실행한다. 이후 registry 변경은 원 batch fence로 거절한다. 변경/Stop/마지막 agent 퇴역·0.5초 초과는 data-only 상태를 폐기하며 worker 결정 수를 늘리지 않는다. 새 CaptureDeferrals/Expired/Invalidated·pending/ready/cursor/count/age를 기존 GetStats와 NPCDecision.Control status의 Summary 끝에 연결했다. 이전40열 CSV/reader·Console 명령·Tick/manager·Core scheduler는 그대로다.

원 runtime cpp SHA4b2559d8…727bc를 유지한 채 새 header 선언과 집중 test만 넣은 Editor251 full0(33.20초)에서 NativeBatch127_Before는2실패/13단언/native255였다. 실제2ms prefix life callback 뒤 suffix callback2개·position read3개·index build1·Core batch1과 NPC/Core 예약 합2가 관측됐다. 새 pending/cursor 선언만으로 원 실행을 바꾸지 않았고 이 기준 입력/바이너리 지문은 별도 보존했다.

최종 Editor252 full0(6.77초) 후 NativeBatch127_1은 관련29개 Success·경고/실패/notRun0/native0(7.34초)다. 실제2ms callback이 관측된 Tick은 prefix read1/suffix0/build0/query0/Core0이고 다음 pass는 prefix 값을 다시 읽지 않고 suffix만 완성했다. prefix 실제 위치100→500 변화와 보관값100을 구분해 기존 worker→FIFO→동일 source 결과까지 확인했다. registry 재등록+fullGC·producer Stop/새등록·World EndPlay·slice 사이.55초 만료에서 과거 자료를 재사용하지 않고 fresh 결과가 회복한다. 임의 clock/worker queue 주입이나 테스트용 public 실행 hook은 없다.

기존102 age 검사는1Tick 즉시 제출 전제만 bounded continuation pump로 바꿨다. 원 pre-observer timestamp는 갱신하지 않아 .30초 최초 준비+.25초 consumer 대기는 여전히500ms를 넘고, final eligibility의.55초도 거절한다. 준비만 만료된 case0은 CaptureExpired1/DiscardedDecisions0, 실제 제출된 case1/2는 Discarded1이며 각각 fresh callback1로 회복했다. fixture OnLifeQuery는 MoveTemp로 한 번 소비하므로 반복 sleep 우려는 원본 확인 후 철회했고 불필요한 강제 reset을 넣지 않았다. 기존 registry/registration/GC·spatial math/ownership·실제 Task 경로도 통과했다. 2048 producer fixture는 의도된 퇴역1개 제외 serviced2047, submitted2051/root batches345/discarded4이며96 fixture는95/96/3/1이다. 수동 service pumping의 기능/공정성 근거이며 CCU나 렌더 frame 수용량 인증은 아니다.

1ms는 callback 사이에서만 확인한다. IsDead 한 번이나 bounded index Build는 선점하지 못하며, 수집 좌표는 여러 시점의 advisory 관측이다. 원0.5초와 live membership/life/range/threat/최종 deadline 검사가 안전 경계다. 지속적인 registry churn/느린 collection은 완성을 지연시키거나 만료시킬 수 있으며 stale publish·timestamp 리셋으로 이를 숨기지 않는다. 새 FPS/전체 CPU 개선율·혼합 cloth/animation/AI critical path 또는 hard deadline/liveness를 선언하지 않는다.

Game88 full direct UBT0(28.03초), 추가 runtime 변경은 Project_J.exe 하나다.126 대비 Source4개(기존 Scheduler h/cpp·기존 status cpp·기존 test cpp)만 바뀌며 Config/에셋·protocol11/Snapshot3는 유지한다. 원본/후보/peer/모든 원 실패·최종 증거는 Saved/ArchitectureBatch127NPCCapture에 있다. FinalWorkspace127.json SHA256 `7313e6b24faf5c77195857a301e68f6b3df01ddc46c3c17371f512bfc6a4af04`(Source1230/runtime39), 다음 Editor253/Game89다. 이전 Cook79/Stage82와 A03–A12/미정 운영 provider/실제 품질·수용량의 잔여를 유지하며 전체 목표는 active다.

## 128차 저장 형식의 단일 검사와 미지원 필드 보존

기존 ProfileRules는 저장된 quest의 stable ObjectiveId·Kind/Event/Target·definition version·count/schema/reward를 비교하며, Gather/즉시 Craft/timed Collect는 이미 Proposed.Quests와 자산을 같은 receipt/CAS에 넣는다. 공유 목표·offline 진행의 occurrence/참가/범위 규칙이 정해지지 않은 상태에서 새 fanout 계층은 만들지 않았다. 공통 배포 기준점을 준비하며 확인한 실제 차이는 정상 Snapshot.Decode가 부모 object의 알 수 없는 필드를 허용하는 반면 migration의 KnownSnapshotFields만 별도 JSON parse로 거절한다는 점이었다. 정상 Open 뒤 Encode하면 미지원 값이 소실될 수 있었다.

기존 Decode의 root(Format1/2/3 각각6/7/8)·progression5·quests2·quest state4/5·objective2 필드 수를 required typed field 검사와 함께 닫았다. 이미 엄격한 item/escrow8·job10·frozen output5와 기존 business bounds/64KiB 제한은 그대로다. 전부 local Result에서 검사한 뒤 한 번만 Out으로 옮기는 계약을 유지한다. migration Inspect와 ownership manifest의 중복 KnownSnapshotFields 및 두 호출을 없애고 같은 Decode를 재사용한다. manifest의 JSON reader/writer는 실제 소비가 있어 유지한다. 형식 번호·writer bytes·운영 provider·public API는 늘리지 않았다.

원 runtime reader 두 cpp의 지문을 유지한 Editor254 full0에서 codec의 알려지지 않은 부모 필드7종은14단언 실패였다. 실제 공개 GrantExperience19를 정상 커밋·종료하고 canonical payload를 확인한 뒤 owned scratch DB의 root에 future_account_currency 하나만 추가한 새 Open도5단언 실패했다. 이전 구현은 지원하지 않는 문서를 설치하고 편집/checkpoint를 받아 원 payload/revision을 바꿨다. NativeBatch128_Before는2 Fail/native255(0.288초)이며 이 실패 입력/바이너리를 보존했다. 첫 Editor253의 C2248은 기존 private DatabasePath test setup 접근이 원인이었다. 기존 friend 패턴에 해당 test class 하나만 추가해 바로잡았고 public setter·reflection 우회·빌드 옵션 변경은 하지 않았다.

최종 Editor255 full direct UBT0(5.81초) 뒤 NativeBatch128_1은 관련18개 Success·경고/실패/notRun0/native0(417.68초)이다. Format1 legacy/Format2 AND/Format3 AND+job의 canonical decode→encode bytes와 reject 시 전체 sentinel 불변을 확인했다. 실제 새 Open은 지원하지 않는 payload를 거절하고 진행/아이템/장비를 부분 설치하지 않으며 SubmitEdit/공개 XP/checkpoint를 받지 않았다. worker의 동일 profile key 재조회와 offline Inspect로 원 payload/revision 보존을 확인했다. 정상 session metadata의 무기록성이나 전체 DB read-only를 주장하지 않는다.

기존 inventory/stream/runtime-frequency 장기 회귀도 함께 정상 종료했다. 반복 stream의 관측13,312건에서 records2/receipts65/405,504bytes를 유지했고, 실제 checkpoint의 무변경 반복 및 이동 알림/XP 커밋 경로도 통과했다. 이는 해당 scratch 시나리오의 보존/상한 근거이며 모든 콘텐츠·운영 부하나 영구 자원 안정성의 인증은 아니다. 알려지지 않은 raw JSON 중복 key를 새로 탐지한 변경도 아니다. 지원할 새 필드에는 명시적인 형식/이관이 필요하다.

Game89 full direct UBT0(29.59초), Game 통합에서 추가 runtime 변경은 Project_J.exe 하나다.127 대비 Source4개(기존 decoder/migration cpp2·test friend header1·기존 test cpp1)만 바뀌고 Config/보호7asset·protocol11/Snapshot3는 유지한다. 원본/후보/peer/첫 오류/원 실패/최종 native와 지문은 Saved/ArchitectureBatch128ProfileSchema에 있다. FinalWorkspace128.json SHA256 `edbbe7398ea0eb1bbaf2207e33c92ceeb6409087af535e45fe782bb5bb7a7810`(Source1230/runtime39), 다음 Editor256/Game90이다. 다음 작업은 최신 공통 Cook/Stage와 정상 새 프로세스의 pending crafting 복원→공개 collect 한 번/반복 거절이다. 이전 Cook79/Stage82·공유 목표/offline 정책·대량 FastArray·운영 DB/인증·A03–A12 및 전체 MMORPG의 잔여를 완료로 바꾸지 않으며 목표는 active다.

## 129차 최신 공통 패키지와 실제 프로세스 재접속

이전 Cook79/Stage82는 현재 입력의 배포 검증으로 쓰지 않았다. 최신 Editor256/Game90 full direct UBT0 뒤 ThirdPerson/Engine Entry/Crafting/Gathering/MainWorld5개 명시 맵을 Batch129Cook1로 새로 Cook했다. native0·오류/경고0이고 Source/runtime·보호 에셋은 불변이었다. 새 Stage1의 Game90은 직접 빌드 바이너리와 일치했다. 실제 GatherCraft는 phase15/native0까지 완료했지만 early-frame FindConsoleObject500회 경고2개 때문에 기존 엄격한 runner에서 실패했다. 실패 패키지·로그·프로필을 보존하고 warning whitelist는 넣지 않았다.

125차의 기존 GT-only TryPrepareNativeTrajectory에서 a.PoseSearchTrajectory.UseWarpedMontage와 ProjectJ.AnimationQuality.SourcePolicy.Enabled를 매번 이름으로 조회하는 두 곳을 nullable static IConsoleVariable pointer로 바꿨다. 등록된 주소만 재사용하고 null은 재조회하며, GetInt와 montage/품질 gate는 매번 원래대로 평가한다. 기존 private CVar writer·기본값·flags·worker Compute에는 변화가 없다. 두 소유 모듈은 필수 의존성이며 정상 native process 수명/NoLiveCoding으로 확인했다. 강제 unregister(false)·임의 모듈 unload/Hot Reload 객체 교체를 보장하는 새 manager는 만들지 않았다. Editor257/Game91 full0 뒤 Stage2의 같은 원 GatherCraft trigger는694frame까지 phase15/native0·오류/경고0으로 통과했다. 관측된 중복 검색의 원인 수정이며 FPS/전체 CPU 개선율을 측정한 것은 아니다.

기존 crafting fixture에 명시적인 SeedPending/RestoreCollect와 run GUID/절대 witness 경로만 추가했다. 기본 fresh·TimedUI 경로/2초 recipe/45초 제한과 recipe 복구를 유지한다. Seed는 정상 bench TryInteract로 실제 escrow-backed job을 커밋한 뒤 종료한다. Restore는 같은 고립 UserDir의 다른 실제 PID에서 normal Open 결과를 먼저 관측하며, 재료/recipe/quest 주입이나 marker를 이용한 상태 설치를 하지 않는다. 공개 progression·quest/objective·bag/equipment UUID와 slot/bInBag·원 job/UTC/escrow/frozen output을 bounded493byte 비교 값으로 확인한다. witness는 closed11 fields/최대128KiB·base64/decoded64KiB 제한을 가지며, profile 형식이나 권한 자료가 아니다. 이름은 문자열로 기록하고 lookup용 순서만 정규화한다.

첫 Seed는 native0이지만 success0/phase200이었다. 정상 fresh avatar의 시작 무기는 이미 durable profile에 slot1/bagfalse로 저장되는데 새 witness가 빈 장비를 가정한 오류였다. 실제 고립 SQLite read-only 조회의 revision4/Health4/starter1/job1과 원 시작 장비 로그를 확인했다. 장비를 지우거나 생산 Open을 바꾸지 않고, 기존 ValidatePersistentEquipment와 backend CaptureItems의 UUID별 bag/slot merge를 재사용해 비교 범위를 바로잡았다. 상한/ValidateJobState와 기존 ItemValue의 slot/bInBag 표현을 유지했다. 두 번째 Saved runner의 계약 문서 version2 검사 누락도 엔진/프로필 생성 전 한 줄만 바로잡았고 원본·실패 자료를 보존했다. 테스트 전용 public API나 fallback으로 검증을 우회하지 않았다.

최종 Editor258 full direct UBT0(13.74초)/Game92 full0(19.94초) 뒤 Stage3 native0이다. Stage2/3은 반영 class/default/asset 변화 없이 각각 조회 캐시와 선택 fixture의 cpp 본문만 바뀌었음을 지문으로 확인하고 동일 Cook1 에셋을 재사용했다. 새 Game은 별도 Stage로 배치했고 이전 출력을 덮어쓰지 않았다. 최종 패키지는48파일/3,349,327,928bytes, native exe SHA256 `8bf1c627348fb286c1d7858a2aff0ab536bde5c640ba5c31723ee34bba8a4171`로 직접 Game92와 같다. 같은 최종 패키지의 GatherCraft phase15/native0·오류/경고0(692frame)도 다시 확인했다.

PendingRestore2의 실제 PID23336/18732는 각각 seed201/restore213 native0·오류/경고0이다. job61194228…00000002의 원 Started639291957391120000/Due639291957411120000 ticks와493byte 소유 값이 정상 Open 후 일치했다. revision4·Health4/Mana0/job1을 먼저 확인한 뒤 공개 새 세션 collect만 admission하고, 확인된 최초 적용에서 revision5·Health4/Mana1/job0으로 바뀌었다. 같은 job의 새 수령 요청은 거절되고 revision5/수량을 유지했다. 두 번째 프로세스에 transient recipe를 재등록하지 않았으며 원 deadline을 현재 시간으로 갱신하지 않았다. source/runtime/보호16asset·native/container 지문·witness SHA가 유지됐고 모든 native handle을 종료 전 보유했다. 세션 metadata는 정상 기록할 수 있으므로 DB read-only·crash recovery·이전 세션 request replay 증거로 부르지 않는다.

같은 Stage3의 실제 listen server+별도2client에서 Quests와 CraftingJobs를 분리 실행해 각각 native0×3/timeout0을 확인했다. packaged listen의 local host1개는 기존 participant 계약대로 제외했다. Quests는 기존 실제 owner-only 진행/추적·아이템 복제 검증이다. CraftingJobs의 server8/client각4행은 합성 owner-summary의 실제 소켓 투영·실제 no-provider Rejected·source 퇴역이며 운영 자산/온라인 영속 저장은 아니다. Blueprint UI 버튼과 취소의 기존113 근거는 유지하되 이번 새 두-process collect를 UI 클릭 증거로 바꾸지 않는다.

128 대비 Source3개(생산 trajectory cpp1/기존 crafting fixture h·cpp2)만 바뀌었다. 새 runtime manager/tick/task pool·Config/에셋·protocol11/Snapshot3 변화는 없다. 최종 source1230/runtime39·보호16asset·패키지48파일 전체가 검증 전후 일치했고 엔진/빌드 프로세스는 정상 종료했다. 원본/후보/peer/첫 실패들·모든 빌드/패키지/실행과 지문은 Saved/ArchitectureBatch129Deployment, 실제 socket raw는 Saved/Validation/GroupD_20260910/Batch129Packaged*에 있다. FinalWorkspace129.json SHA256 `0990e477781994b1cbb54d71d37eaa15fa8c42bd27bc04c6353e3a9a04a3dac4`, 다음 Editor259/Game93이다. 이 기준점은 해당 Development 맵/호출 경로의 통합 근거이며 Shipping·전체 맵/모든 콘텐츠·운영 DB/인증·GPU/Nav/CCU/FPS·A03–A12의 잔여와 전체 목표는 active다. 다음은 기존 NPC/BP의 실제 소유권을 확인한 중립 이동 실행 연결이며 Mass/NetworkPrediction 저빈도 완료로 세지 않는다.

## 130차 일반 NPC의 목적지 실행과 동일 Actor 전환

기존 NPCAction은 hostile scored pursuit 또는 Companion의 actor follow만 소비했다. null attack의 combat activation을 중립 이동으로 해석하거나 follower를 강제로 전투용으로 바꾸지 않았다. 기존 BP_NPC_Test 한 개와 native 부모의 저장 없는 조회에서 AIControllerClass는 표준 AIController, AutoPossessAI는 PlacedInWorld임을 확인했다. 새 spawn의 자동 possession을 가정하지 않는다. 첫 조회의 Python MRO 끝 null class 오류는 unknown/skip으로 바로잡았고 두 번째 native0 조회의 Source/runtime/BP SHA·dirty0을 확인했다. graph0은 지원되지 않은 반영 조회의 결과이며 BP event가 없거나 실행되지 않는다는 증거가 아니다.

기존 NPCAction의 native StartNavigationTo는 absolute<=1e9의 fixed world nav-feet 위치와 반경1..2000 하나만 접수한다. 같은 component가 이미 실행 중이면 덮어쓰지 않고 거절한다. 별도 target Actor·hostile scoring·attack grant·Tick/manager/worker pool을 만들지 않았다. 기존 action ring의32 update/128 visit/0.75ms와 path64 pending/16 physical/4 dispatch/8 delivery/2초 개별 요청 deadline을 재사용한다. 입구 접수 거절에는 기존 backoff를 쓰고, 접수된 경로의 실패/만료/불가·RequestMove 거절·owned move 실패는 false로 퇴역한다. 실제 native HasReached ExactLocation과 동일 radius를 쓰며 Move Success만으로 도착을 선언하지 않는다. 현재 suspension record는 scored actor만 복원할 수 있으므로 location intent를 명시적으로 거절한다.

기존 action/intent/ASC epoch와 controller/move/path token을 최종 콜백까지 검증한다. virtual ASC/life/nav 조회 뒤 peer를 다시 확인하고, path-ready의 goal/start는 각 한 번 캡처한 뒤 현재 세대를 검사한다. PathFollowing delegate에도 captured controller+action revision을 넣어 과거 broadcast가 새 lease에 적용되지 않게 했다. 진짜 reach 결과도 세대 검사 후 사용한다. 도착·취소는 scheduler/path/move/context/token을 먼저 분리하고 navigation completion만 guard 밖에서 한 번 호출한다. callback이 새 명령을 시작해도 이전 cleanup은 그 상태를 건드리지 않는다. 기존 FollowStopped의 dismiss 시점은 유지한다.

실제 NPC helper가 이 경로를 소비한다. ASC를 바꾸지 않고 기존 BeginPlay/PossessedBy 초기화를 사용한다. 시작 claim을 첫 virtual getter 전에 예약하고, 성공 token/current 판정 전에는 사용자 completion을 게시하지 않는다. 기존 active action/hostile registration/brain/foreign move는 거절한다. 필요한 native action만 생성·등록하고 미접수로 끝난 새 미소유 component는 정리한다. 직접 생성한 controller만 기록하며 normal stop/도착 후에는 같은 NPC/ASC의 명시적 combat activation이 재사용한다. 최종 EndPlay에서 그 controller의 pawn이 null 또는 정확한 이 NPC인 경우만 파괴한다. APawn의 정상 Pawn→Controller Owner를 유지하고 반대 Owner를 추가하지 않아 순환을 만들지 않는다. 외부 controller나 다른 pawn으로 이전한 controller는 보존한다.

WorldEncounter는 default-off bUseInitialNavigation/고정 목적지/반경과 제작 검사를 제공한다. 기존 preparation/class/revision tuple에 설정을 포함하고, callback 가능한 NPC setup 후 기존 demand/schedule을 다시 확인한다. admission 실패는 해당 spawn의 기존 rollback/cooldown으로 끝낸다. 접수 후 Active/SpawnRevision은 유효한 spawn과 명령 접수이며 도착 성공이 아니다. 이후 이동 취소를 EnterCooldown/defeat에 연결하지 않으므로 같은 NPC의 명시 Stop→전투 승격을 방해하지 않는다. 기존 생명/Destroyed/Encounter 수명은 계속 기존 주체가 처리한다.

첫 Editor259 full direct UBT6은 local Controller와 상속 APawn::Controller 이름 충돌 C4458 한 개였다. 이름만 RetiredController로 바꾼 Editor260 full0 뒤 NativeBatch130_1은18 Success/1 Fail/native255였다. 실패는 새 helper 호출 전 transient Game-world의 Behavior Tree가 시작되지 않은 준비 단계였다. UWorld::CreateWorld(Game)의 기본 AI-system 미생성과 BT manager/InstanceStack 계약을 설치 엔진 소스에서 확인했다. 해당 테스트에 public CreateAISystem과 실제 manager 관측만 추가하고 stack callback의 scope 정리를 보강했다. 보호 flag/fake IsRunning·sleep·생산 동작 완화는 없다. 원 오류·실패 입력/실행과 Source/runtime 불변을 보존했다.

최종 Editor261 full direct UBT0(7.04초) 뒤 NativeBatch130_2는19 Success·테스트 경고/실패/notRun0/native0(9.69초)이다. 실제 Recast 우회 경로의 point 수>2와 Mover nav-feet 변위>1200cm·native reach를 확인했다. 이전 dispatched intent 취소→다른 목적지 도착과 새 token·실제 foreign RequestMove 보존·명시 취소/EndPlay를 한 cohort로 확인했으며 NPC SetActorLocation으로 이동을 흉내 내지 않았다. 별도 계약은 callback 전 완전 퇴역/재진입·ASC metadata refresh와 실제 recipient epoch 교체·실제 running native BT 거부/보존·같은 Actor/controller/action의 neutral Stop→기존 NPCActivation의 scoring/grant 전환과 정확한 해제를 검증했다. Encounter의 실제 spawn/접수·invalid 설정 거절·stop 후 동일 spawn/revision 유지·최종 자기 controller/예약 퇴역도 확인했다. 이 nav-less Encounter fixture를 실제 도착 근거로 쓰지 않으며 기존 추적/follow/NavData 퇴역/WorldEvents 회귀도 통과했다.

Game93 full direct UBT0(65.33초), 마지막 Game 통합에서 추가 runtime 변경은 Project_J.exe 하나다.129 대비 Source9개(기존 생산 h/cpp6·기존 테스트 cpp3), Source1230/runtime39이며 native 검증 전후 Source/runtime·보호17asset은 불변이다. Config/에셋·protocol11/Snapshot3는 유지한다. 원본/후보/peer/조회·첫 오류/실패·모든 실제 결과는 Saved/ArchitectureBatch130NPCExecution에 있고 FinalWorkspace130.json SHA256 `d79968e2b9d248e2532ed287df3eddca8896d168c9c5da4869e1e967bf12e2f2`다. 엔진/빌드 프로세스는 정상 종료했고 다음 Editor262/Game94다. 이는 실제 단일 NPC 실행 adapter의 근거이며 Mass/NetworkPrediction 저빈도·원격 이동·production-map Nav/혼합 물리·GPU/CCU/FPS·전체 AI 콘텐츠 완료가 아니다.129 Cook1/Stage3는129 입력만 검증하며130의 새 Cook/Stage는 하지 않았다. A03–A12/운영 provider와 전체 MMORPG 목표는 active다.

## 131차 실제 NPC 전투 활성화와 정의 그룹 준비

기존 NPCActivation의 ability grant·batch scoring·action lease를 새로 만들지 않고 실제 NPC helper와 선택 Encounter 생성 경로에 연결했다. StartCombatActions/StopCombatActions/HasCombatActions는 자신이 소유한 활성화만 다룬다. controller 준비는130의 중립 이동과 같은 private 경로를 사용하고, 등록된 외부 brain/move/action/activation을 덮어쓰지 않는다. 두 시작 요청은 setup guard를 공유하되 request serial은 분리해 unowned StopNavigation이 전투를 취소하지 않게 했다. 직접 생성한 controller는 같은 NPC에서 재사용하며 최종 EndPlay에서만 정확한 소유권으로 해제한다.

Activation은 첫 virtual ASC getter 전에 bChanging/strong pins를 예약하고 callback 뒤 소유자·component·epoch를 재검증한다. 새 미소유 component의 등록/접수가 실패하면 해당 생성물만 정리한다. 검토 중 내부 ActionToken을 Activation ID로 노출하면 다른 새 Action의 첫 번호가 같아 이전 helper가 새 외부 활성화를 취소하는 반례를 찾았다. Activation component-local 단조 serial 하나를 추가하고 기존 ActionToken은 내부 exact-action 해제에 유지했다. GetOwnershipToken/DeactivateNPCIfOwned는 활성화 serial을 사용하며 Actor는 활성화 serial과 캡처한 Action token을 따로 검증한다. 외부 재활성화·같은 ASC의 새 epoch·이전 helper Stop이 새 grant/등록을 침범하지 않는다.

WorldEncounter는 CombatDefinition soft reference와 authoritative team을 선택적으로 받는다. 기본 null은 기존 동작을 유지하고 initial navigation과 동시 선택/음수 team은 거절한다. 기존 WorldEventRegistry의 준비 슬롯 하나가 class+definition 두 경로를 같은 StreamableHandle로 요청한다. 이미 resident인 정의도 handle이 보유하며 ready callback은 기존 apply budget에서만 실행한다. public component 준비 API·8개 물리 예약/2개 적용/1ms·취소/timeout 수명은 유지한다. 요청 당시 class/definition path/team과 완료의 typed strong definition·spawn tuple·현재 demand를 확인한 후 helper를 호출한다. 이후 명시적 전투 해제는 Encounter defeat가 아니며 보상/새 전투 phase 규칙을 추측해 추가하지 않았다.

Editor262 full direct UBT0(80.62초), 관련 NativeBatch131_1은24 Success·테스트 경고/실패/notRun0/native0(10.21초)이다. 실제 no-controller NPC의 생성/등록·GAS grant·scoring/action·Stop/restart/ASC epoch·중립/전투 명시 전환·EndPlay, 실제 BT/외부 action 거부와 동일 Activation의 서로 다른 Action A/B가 같은 raw token을 갖는 반례를 확인했다. Encounter는 실제 group preparation의 resident 정의를 외부 strong pin 해제+native GC 후에도 유지하고, 정의 교체의 옛 요청/설정 충돌을 거절하며 producer 등록/grant와 stop/최종 퇴역을 검증했다. 기존 Recast/NavMover 이동·동료 follow/retirement·어그로 lifecycle/age도 통과했다. 이 검증은 전투 capability의 실제 연결이며 새 보스 공격/기믹·전체 phase·규모나 FPS 측정이 아니다.

Game94 full direct UBT0(53.26초), Game 통합에서 추가 runtime 변경은 Project_J.exe 하나다.130 대비 Source10개(기존 생산8·기존 테스트2), Source1230/runtime39·보호17asset 불변이다. Config/에셋·protocol11/Snapshot3와 기존 입력을 유지했으며 새 Cook/Stage는 하지 않았다. 원본/후보/peer/충돌 보강·실제 실행/지문은 Saved/ArchitectureBatch131NPCCombat, FinalWorkspace131.json SHA256 `255754eaf8577a31b1217272c5d2f4f757c84c3ed278c0f679dfd276bf12b802`다. 다음 Editor263/Game95이며 전체 목표는 active다.

A07은 기존 strict93 실패 코호트 하나와 current mount 관계3파일·기존106 지원 API 분석을 재사용해 미완료 엔진 gate를 특정했다. 새 handle/scope가 회복돼도 외부 replicated MountedMount 참조가 이전 remote actor에 남는 반례다. unchanged UObject의 재양자화를 ForceNetUpdate/일반 dirty로 보장할 근거가 없어 수동 참조 수리나 새 incarnation framework를 넣지 않았다. 선택 UE/Iris의 old remote 퇴역/remap·외부 reference invalidation 계약 또는 검토 가능한 source-engine 변경이 필요하며 정상123 AOI pass와 구분한다. 근거는 Saved/ArchitectureRequiredCloseout/A07Notes.md이고 이를 전체 복제 완료로 세지 않는다. 다음은 기존 A10 실제 수명/준비/자원 경계의 독립 마감이다.

## 132차 만료된 client 스트리밍 준비 수요 철회

기존95–97/106/122의 실행·수명 근거와 현재 Portal→shared arrival→WP provider 경로만 좁게 재사용했다. Preparing deadline 뒤 client의 ACK와 Advance는 이미 진행을 거절하지만, 독립 GetStreamingSource는 authority만 만료를 검사해 다음 scheduler 방문까지 목적지를 계속 광고할 수 있었다. authority 예외를 제거해 양쪽의 uncommitted Preparing 수요를 같은 deadline에서 철회한다. 조회는 request/phase/Entry/permit을 변경하지 않고 local predictor로 복귀하며 다음 정상 writer가 terminal 정리를 담당한다. Queued의 알려진 native outcome과 client AwaitingLocalArrival의 늦은 완료 정책은 유지한다. 새 state/RPC/manager/physical ticket·강제 GC/eviction은 추가하지 않았다.

기존 PreparedArrivalBoundaries에39줄을 추가해 비권위 local-controller 분기의 정상 목적지→만료 predictor·동일 identity·query 중 논리 상태 보존·늦은 Awaiting demand·실제 unregister 정리를 확인했다. 기존 authority 만료/queued receipt·수요 admission/공정 예산도 유지한다. public SetRole과 deadline을 구성한 NM_Standalone 분기 회귀이며 실제 client socket/replication/WP load를 실행한 증명은 아니다. 기존97 실제 owner 연결 증거와 분리한다. Editor263 full direct UBT0(7.42초), NativeBatch132_1 관련6 Success·테스트 경고/실패/notRun0/native0(0.167초), Game95 full direct UBT0(12.56초)다.

131 대비 Source2개(생산cpp1/기존 테스트1), Source1230/runtime39·보호17asset 불변이고 native 실행 전후 지문은 같다. Game 통합에서 추가 runtime 변경은 Project_J.exe 하나다. Config/에셋·protocol11/Snapshot3는 유지하고 새 Cook/Stage는 만들지 않았다. 원본/후보/peer·role-fixture 한계·실행 원자료는 Saved/ArchitectureBatch132StreamingLifetime 및 Saved/ArchitectureRequiredCloseout/A10Notes.md에 남겼다. FinalWorkspace132.json SHA256 `5d28c376055370136caad14fa4ce7787bd46ef2c473ad3dbd78149a91169e11c`, 다음 Editor264/Game96이다.

만료 목적지 광고의 조건 오류를 고친 것이며106의 process 메모리 증가 원인을 고친 것은 아니다.106 standalone은 이미 authority fence 대상이며 selected native level tail 종료와 process residency는 별도다. 소유 allocation/package/cache 근거 없는 재현·강제 정리, 인간 Portal에 전역 Nav-ready 조건을 추가하는 변경은 하지 않았다. A10의 합산 I/O/residency·GPU/texture/PSO·실제 콘텐츠/고속 이동 및 A07 엔진 수명 gate는 남는다. 다음 필수 혼합 자원 작업은 기존 A06/A12 근거를 재사용한다. 현재 EffectsPoolLifetime의 active 시험은 saved128 정책을 읽지만 transient count2를 실행했으므로 actual128 압력 증거로 세지 않는다. 별도 경고 의미/콘텐츠를 발명하지 않고 기존 fixture에서 저장된 정책의 bounded native pressure 경계를 우선 확인하며 전체 목표는 active다.

## 133차 실제 saved128 효과 압력과 풀 퇴역

기존99의 count2·126의 notify/pool 수명 근거를 재사용하고, 기존 영구 EffectsPoolLifetime의 별도 명시 opt-in `ProjectJTestNiagaraSavedPressure`를 추가했다. 원 count2/default/metadata 경로는 유지한다. 실제 saved Project NS/NET·High0.25초·2500cm·distance significance/DeactivateImmediateResume 정책을 변경 없이 사용하며 생성 시도 예산과 active 정책을 격리한다.132개 실제 remote lease/component를 같은 빈 Entry host에 두고 기존3anchor+129개 bounded 배경으로 카메라 반전에 따른 native count-cull/resume을 관측한다. native cached view/실제 world-tick·frame/revision·owner/position·고유 component/lease·실제 cull 원인 flag를 확인한다. 게임 정책·품질/CVar·자산·새 manager는 추가하거나 바꾸지 않았다.

첫 Editor264/Pressure133_1은 활성 단계와 개별 culled stop/attack 퇴역 뒤 최종 검사에서 만료됐다(native255/1실패). terminal 관측 조건을 더 진단한 Editor265/Pressure133_2도 같은 최종 단계에서 실패했으며 views1/camera-match1/projection1/IDs32/132를 남겼다. 뷰 캐시가 사라졌다는 초기 가설은 배제했다. 실제 엔진 FNCPool::Reclaim은 MaxPoolSize 내에서만 FreeInPool로 보관하고 초과분은 PoolingMethod None→DestroyComponent로 퇴역한다.132개 모두 살아 있는 pool component여야 한다는 fixture 가정이 원인이므로 정상 풀 잔류와 초과 파괴를 구분했다. 불필요한 view 예외는 되돌리고 원30초·실제 camera/view·활성 단계의132고유 ID/lease 검사는 유지했다. pool 한도를 늘리거나 component 생존을 강요하지 않았다.

최종 Editor266 full direct UBT0(8.42초), Pressure133_3은 실제 D3D12/local viewport host native0/1 Success·테스트 경고/실패/notRun0(5.52초)이다.9point/1188행·38열의 원시 자료를 별도로 대조해132개 고유 component에서128 active/4 count-cull·다른 cull 원인0·모든 current remote distance<2500, 카메라 반전 후 같은 component의 복귀, old stop lease와 attack 교체를 확인했다. 최종 lease/active/scalability/tracked cue는0이며 실제 asset MaxPoolSize32에 맞춰32개 FreeInPool+100개 정상 파괴=132 퇴역이다. 이 과정의 살아 있는 수량·과거 peak132·파괴 slot의 ID0/sentinel을 구분하고 invalid UObject를 역참조하지 않는다. pool 잔류32는 leak나 미해제 active lease가 아니다.

Source는 기존 테스트cpp1개만 변경했고 production policy·Config/에셋·protocol11/Snapshot3와 보호17asset은 유지했다. Native 전후 Source1230/runtime39 지문이 같으며132 대비 runtime 변경은 UnrealEditor-Project_JCharacter.dll 하나다. 새 Game/Cook/Stage 없이 기존 Game95/129 패키지를 해당 옛 입력 근거로 보존한다. 원 후보/두 실패·수정 가설과 폐기·peer/engine 계약·최종 실행은 Saved/ArchitectureBatch133NiagaraPressure에 보존했다. RawVerification133.json은 원 행별 관측을 검증하며 FinalWorkspace133.json SHA256 `f4a9b5327daf77854135c57fe7f35e39b5601035eefbab18e86afdb3a151d25a`, 다음 Editor267/Game96이다.

이것은 저장된128 active 정책과 별도 idle pool 수명의 실제 검증이며 새 제품128 보증·GPU/VRAM 비용·경고 보호·혼합 부하/FPS·CCU·전체 A12/MMORPG 완료가 아니다. current cue 계약은 cosmetic이므로 bUseRemoteSpawnBudget 해제나 local 예외를 중요한 remote 경고 보호로 세지 않는다. 다음은 Saved/ArchitectureRequiredCloseout/A06A12Next.md의 기존 대표 mixed runner/105 좁은 export를 사용해 최신 animation worker·NPC 수집·Mover/cloth·render의 실제 critical path를 확인하는 필수 묶음이다. 새 전수 감사·32 적용 예산 행렬·같은 count2 반복은 하지 않으며 외부/게임 규칙 의존성과 전체 목표는 active다.

## 134차 플레이어 검색 입력 준비와 혼합 실행의 실제 비용

사용자가 NPC는 모션매칭을 사용하지 않는다고 명확히 했다. 이번 준비 대상은 기존 RenderedCrowd가 실제로 생성하는 BP_Spellblade 플레이어 Avatar의 source Chooser/PoseSearch 입력이다. NPCDecision64/대상128은 별도 값 기반 판단 경쟁 입력이며64개의 전체 시각/NPC Nav·전투 구현 또는 NPC MM 부하로 세지 않는다. 기존108의 startup warning 관측을 readiness 완료로 쓰지 않고, Source의 실제 Ground/Air Chooser→PoseMatch column→MotionMatch/fallback 소비와 설치 API를 재사용했다.

기존 fixture와 측정 진입점에 Editor standalone 명시 opt-in을 추가했다. 각 플레이어에 실제 PlayerAnimInstance consumer가 있어야 하며 source mesh/asset/leader·instance/class·roots·정적 FEvaluate/Nested/Asset 결과와 PoseMatch row/fallback DB/schema를 확인한다.64table/4096canonical row/column128/8192PoseMatch 방문/128DB 상한 안에서 수집하고 unknown/dynamic/unloaded 결과는 준비 완료로 세지 않는다. 각 DB의 NewRequest는 준비 작업을 시작/취소할 수 있는 API이며 순수 조회가 아니다. unique DB당1회만 발행한 뒤 다음 자연 프레임의 ContinueRequest 명시 Success까지 기존≥8game초 warmup에 머문다. Failed/입력 변경은 실패하고 측정 중 새 index 요청·동기 로드/Wait/GetSearchIndex·노드 private 접근·강제 GC는 없다. 실제 source 입력과 보조 PP/linked class/node 관측을 분리하고 준비 시간은 admission 시각에 확정한다.

Editor267/268의 기존 CSV 이름 충돌·const schema 연결 오류와269의 직접 Chooser 링크 누락을 보존했다. 고유 변수명·const identity key·schema 소유 DB와 package pin으로 수정하고 기존 Editor private 의존성에 Chooser를 명시했다. Editor270 full0 뒤 Mixed134_1은 native0지만 result0이었다.6DB 준비 뒤 정상 공격 보조 레이어가400→600 consumer로 늘었는데 전체 metadata를 freeze해 과도하게 거절했다. 이름 예외 대신 실제 source candidate identity와 auxiliary 관측을 나눴고, 새 source/MM 소비자·root/DB 변경은 계속 거절한다. 원 admission 관측·최신 관측·source 고정 subset을 따로 기록한다. 최종 Editor271 full direct UBT0(16.64초), Game96 full direct UBT0(24.08초)다.

Mixed134_2는 실제 D3D12/Editor-game·플레이어200·NPC판단64/대상128·이동/점프/공격·local player·추적의 대표 실행 하나다. native0/result1/검증true,738개 원 frame/NPC 표본과 실제 observer event가1:1로 대응한다.6DB 모두 New1/Continue1 Success, 준비0.1134106초 뒤 frame103/phase2 admission,200 source consumer/835 membership·패키지 dirty 원상태를 확인했다. 이 실행의 aux는 admission 때 이미600개였으며 이전 실패에서의 늦은 증가를 새 실행이 다시 강제로 재현한 것은 아니다. scope는 source_player_chooser_input_candidates이며 실제 선택 횟수·private effective key·모든 candidate disk hash·전체 animation readiness를 증명하지 않는다.2560×1440 PNG와 실제 body/movement는 확인했지만 native 내부 raster/FSR·FG 상태와 전체 시각 품질 승인으로 확대하지 않는다.

실제 frame map으로 phase5 game-time2..7의75프레임(73.2072352–78.2207632)을 추출했다. 그중 rendered200/montage200 공격 조건은32연속 프레임511–542(73.2072352–75.7662513)이며 나머지 복귀43프레임과 분리했다. 공격 CSV frame P50/P95/P99는79.518/86.561/86.915ms이고 GPU 카운터는 지연 표본이다. 같은32프레임 GT 실제 CPU scope의 EndPhysics WaitForTasks32회0.6734055초, ClothEnd WaitUntilTasksComplete3479회0.3706353초를 부모 스택으로 확인했다. 이는 대기 wall이며 OS 실행 CPU나 서로 중첩된 timer의 합계가 아니다. NPC 계산 batch wall 평균은 수십 μs이고 Core 전달과 ready age는 훨씬 크므로 전달 지연을 계산 CPU로 오인하지 않는다. cap32를 다시 올리거나 전역 scheduler/cloth 품질 설정을 바꾸지 않았다.

현재 공격은200개 모두 exact native FlyingMode/native GenerateMove·warped_montage0을 사용했다. 같은32프레임의 NativeFlyingCandidateLoop는6400회0.1665424초, 평균5.20ms/frame GT exclusive다. 전체75프레임에는 기존 ground 준비 worker210회도 실제 non-GT lane에서 관측됐다. WorkerPhase의27811행 중 Game75는 독립 frame CSV exact tuple로, Rendering76은 metadata Frame Tracks 및 설치 exporter의 index/name/depth/overlap 계약으로 분류했다. 나머지27660행은 실제 CPU timer/thread identity를 확인했다. 합성 Frame.Index와 CPU TimerId의 숫자 충돌을 같은 차원으로 읽지 않고 nested scope도 합산하지 않는다. 다음135차는 이 실제 native Flying 소비를 기존 owning packet/pure worker에 연결하는 최소안이며 물리 simulation/Mass 저빈도나 NPC MM을 도입하는 작업이 아니다.

변경은 기존 C++ fixture h/cpp·Editor Build.cs·측정 script4파일이다. native/분석 전후 Source1230/runtime39와 보호17asset 및 해당 측정 manifest 입력은 유지됐다. 최종 Game 통합에서 추가 변경은 Project_J.exe 하나이며 Config/에셋·protocol11/Snapshot3·129 옛 패키지는 유지하고 새 Cook/Stage는 하지 않았다. 실행/실패/원시 CSV·trace/제한된 export·입력/검증은 Saved/ArchitectureBatch134MixedReadiness와 실제 Mixed134_1/2에 보존했다. FinalWorkspace134.json SHA256 `113658f18e4aa01729ad518051c64313661c852bac05d992f87fa9880ec52708`, 다음 Editor272/Game97이다. 이는 추적이 켜진 단순 장면의 준비·실행·인과 근거이며 전체 MMORPG/FPS/CCU·128FX 혼합 압력·mandatory warning·서비스/소스 엔진 잔여의 완료는 아니다. 전체 목표는 active다.


## 135차 native Flying 값 계산과 성능 채택 경계

기존 Mover의 exact UFlyingMode/default input·sync/no-base·ReuseFlyingParams1 admission과 BuildFreeMoveParams를 공유한다. 새 `FMoverNativeFlyingTrajectory`는 free-move/초기 pose·velocity·step·샘플/visual transform 값만 소유한다. GT prediction context·gravity·blackboard Begin/End와 physical SimulationTick은 유지하고 native sample→move→advance 수식을 동기/owning 경로가 함께 사용한다. worker는 UObject·mode·input collection·blackboard·CVar를 읽지 않는다. 기존 PlayerAnim packet에 Ground/Flying 구분만 추가해 이력15/예측15·같은-frame 계산·revision/source-generation 퇴역과 GT PostUpdate를 재사용했다. custom/BP/base/optional/warped 등 미지원 경로는 동기 fallback이며, malformed/evolved-invalid 값은 부분 결과를 게시하지 않고 기존 invalid-frame 경로로 실패한다. NPC 모션매칭·물리 갱신 빈도·새 manager/module·품질/Config 변경은 없다.

기존 raw92-sample fixture에 canonical GenerateMove(policy0) 동등성과 지원/거절·원 input/sync/private spring 보존을 연결했다. 서로 다른 입력/튜닝/이동 모드로 원천이 바뀐 뒤에도6개 owning packet을 한 worker batch로 계산해 원 결과와 비교한다. 정상 packet 대조 후 NaN/잘못된 count의 명시 실패·출력 정리도 확인한다. 기존 player producer fixture는 실제 Flying source→두 프레임 이력/파생 값 worker 계산→NativeTS→PostUpdate/retirement를 확인한다. 이 테스트의 명시 ThreadPool 값 작업과 실제 ABP animation task 실행은 별도 증거다. 최종 Editor273 full direct UBT0(6.408초), RHI NativeBatch135_2는58 Success/테스트 경고·실패0/native0(2.861초), Game97 full direct UBT0(68.942초)다. 앞선 측정 Editor272/native58 입력도 보존한다.

측정 버전 Mixed135_1은134 성공 실행과 같은 명령/품질·200 Player Avatar+NPC 판단64/대상128·이동/점프/공격·실제 RHI/추적이며 native0/result1/검증true720행이다.6DB New1/Continue1 Success·준비0.116493초/frame91 admission을 확인했다. 실제 observer↔CSV↔GameFrame 일대일 map으로 phase5 game-time2..7의67프레임을 선택했고, 실제 montage200 공격29프레임과 복귀를 분리했다. worker 파일의 Game67/Rendering68은 별도 Frame Tracks 차원으로 검증하고 나머지37110행은 실제 CPU timer/thread를 대조했다. Flying worker6000회(공격5800회)는 실제14개 non-GT lane에서 관측됐고 기존 GT NativeFlyingCandidateLoop는 해당 창에서 미관측이다. nested outer/worker scope는 합산하지 않으며, 서로 다른 경계·표본의 역사 GT 시간과 worker 시간을 뺀 값을 CPU 절감으로 쓰지 않는다. reader는134의75/32프레임·GT 양수2406476행/worker27660행과 기존 시간·부모 스택을 재현했다.

그런데 공격 frame P95는134의86.561ms에서102.115ms로 나빠졌으며 GT 예측 interval의 감소를 전체 개선으로 채택하지 않았다. 같은135 소스/바이너리에서 기존 DeferGroundPrediction0만 명시한 대조1회는 native0/result1/검증true730행, 공격31표본 P95 95.596ms였다. 두 실행의 전체 provenance와 품질·NPC·cloth 조건은 검증하고 명령 차이1개를 명시했으며 완전히 같은 명령의 반복 결과로 위장하지 않았다. 공격 worker 실행에는 prepared Ground가 없지만 대조0은 다른 단계의 Ground 배치도 바꾸므로 그 한계가 있다. 실제 GT 대기는 EndPhysics와 ClothEnd 부모이며135의 전체67프레임 WaitForTasks1.596709초/WaitUntilTasksComplete0.920918초다. 이는 대기 wall이고 원인·OS 실행 CPU가 아니다. PhysicsParallelFor·cloth 비용과 실행 간 변동이 커 추가 배치가 원인이라고 단정하거나 스레드 수·priority·grain·oversub/품질을 임의 변경하지 않았다.

최종 채택 보호는 기존 `ProjectJ.PlayerTrajectory.DeferGroundPrediction`의0=동기/1=Ground-only 기본/2=Ground+Flying 명시 검증이다. 기본1의 Flying 동기 출력과 명시2의 실제 pending/완료를 최종 회귀로 확인했다. 이는 기본값의 회귀 노출을 제한하는 조치이며 physics/cloth 원인 수정·Flying 성능 승인·전체 MMORPG 완료가 아니다. 측정 BuiltWorkspace135와 AfterMixed135_1/GTControl1, 최종 guard의 FinalBuiltWorkspace135/AfterFinalNative135/FinalWorkspace135를 구분한다. 최종 guard 뒤 새 군중 성능을 측정한 것은 아니므로 앞 수치를 최종 기본값의 새 결과로 쓰지 않는다. 최종 C++8파일(생산6/기존 테스트2), Source1231/runtime39·보호17asset·각 측정 content 입력 불변, Game 통합의 추가 runtime 변경은 Project_J.exe다. FinalWorkspace135.json SHA256 `7a3e1c8e5de829435b80caaa050824ec99859788d8c3a1abae518dc94b59a1e4`, 다음 Editor274/Game98이다. 원자료·비교·reader/기본값 결정은 Saved/ArchitectureBatch135FlightForecast에 보존했다. 새 Cook/Stage·원격 CCU·시각 품질/내부 raster/FSR·FG·운영 provider/소스 엔진 완료 주장은 없으며 A03/A06/A07/A08–A12 잔여와 전체 active 목표는 유지한다. 다음136차는 기존 GT trace의 Calc SkelMesh Bounds/물리 소유 경로만 좁게 재사용해 실제 중복 작업 여부를 확인한다.

## 136차 native bounds 비용과 공유 안전성 경계

134/135의 기존 공격 창32/29프레임과 부모 스택을 재사용했다. 클로스 시뮬레이션이 없는 아홉 파츠의 primary Calc SkelMesh Bounds exclusive 합은133.426/143.005ms, 프레임당 약4.17/4.93ms다. cloth tick의 별도27.156/24.751ms와 이미 제외한 nested ClothCalculateBounds를 중복 합산하지 않는다. native marker에는 유효 cache의 빠른 반환도 포함되므로 호출 수만으로 중복 invalidation 결함을 단정하지 않았다. 확인한 project 모듈러 경로에는 매 Tick bounds 강제 갱신이나 leader 재결합 writer가 없었다. PhysicsParallelFor 시간에도 실제 kernel 계산이 포함되므로 cloth 전체 시간을 스케줄링 오버헤드로 보지 않는다. 설치 엔진의 지원 CVar 범위만 검토했고 전역 grain·priority·oversub·solver·cloth 품질은 변경하지 않았다.

실제 RegisterObservedPlayer 관측의 기존8열은 그대로 두고 public asset/physics/leader/transform/cloth metadata13열을 추가했다. Bounds136_1은 실제 Count1/이동/local player/close view/플레이어 PoseSearch 준비에서 native0/result1/검증true,9703frame행·11pose행/21열이다. 같은 component의 기존8열은135와 일치한다. 아홉 파츠는 own PhysicsAsset이 없고 상대변환 identity·bounds scale1·native leader/parent/fixed flags0이며, Cloak는 자체 PhysicsAsset과 cloth metadata가 있어 분리했다. 원본은 숨겨진 Budgeted component의 Complete mesh와 PA_Mannequin이다. 의상 metadata는 LOD 전체의 바인딩 존재를 뜻하며 현재 LOD의 실제 시뮬레이션 완료가 아니다.

기존 fixture에 명시 opt-in LeaderBoundsProof를 연결했다. 실제 플레이어 월드의 WorldPostActorTick에서 phase2..6/age2..7을0.1초 간격으로 표본화하고, 최대32파트/320batch/10240쌍 안에서 원본 native child CalcBounds와 잠시 켠 native leader flag 결과의 box·sphere 포함 관계를 비교한다. 지원 identity·수명·상태를 검사하고 병렬 평가 중에는 기다리지 않고 건너뛴다. 각 쌍의 flag와 cache invalidation은 scope 종료에서 복구한다.52열 raw CSV와 완료/coverage/복구 JSON을 작성하고 runner/독립 분석이 실제 행을 대조하며, 빈 표본이나 쓰기 실패를 성공으로 세지 않는다. 초기 Proof1은 LoadMap 중 Untitled 월드 정리가 미결합 검증을 종료해0쌍이었다. 실제 플레이어가 존재할 때만 월드를 소유하고 그 월드 정리만 소비하도록 원인을 수정했다. 강제 timer/reset으로 덮지 않았다.

수정 후 Proof2는 native0로 완료했지만 qualification false다.9파트×5phase×50쌍=2250쌍/250batch 모두 원본 범위 포함에 실패했고 equivalent0이다. 최대 box 좌표 차98.602cm/반경 차33.049cm, 최소 box margin−86.829cm/sphere margin−81.337cm이며 flags는 모두 복구됐다. 숨겨진 원본의 native CalcBounds는 정적 imported bounds를 선택하지만, 원래 child는 원본 PhysicsAsset의 동적 bounds를 사용한다. 따라서 native leader flag·강제 parent 표시·fixed bounds·임의 padding을 채택하지 않았다. 같은 프레임 아홉 원본 child 결과의 최대 차5.684e−14cm는 별도 안전한 재사용 후보의 근거이며 cache 수명이나 모든 pose/vertex의 안전성 증명이 아니다.

최종 Editor276 full direct UBT0(5.628초), Game98 full direct UBT0(20.958초)다. 앞선 Editor274/275와 첫 실패도 보존했다. C++/script4파일, Source1232/runtime39·보호17asset·각 native manifest의 content 입력 불변,135 대비 runtime 변경은 Editor module/Game exe다. 원시 행·비용 소유자·실패·월드 수정·독립 검증은 Saved/ArchitectureBatch136BoundsCost와 실제 Bounds136_1/Proof1/Proof2에 보존했다. FinalWorkspace136.json SHA256 `951f7585a40029b63e843d2627a2b9c9401a511b0f3ebd579271feacd58a15d4`, 다음 Editor277/Game99다. 유효 월드 결합 뒤 조기 정리와 주입한 디스크 쓰기 실패는 runtime 미검증이며 source 검토만 했다. 새 Cook/Stage·200명 capture·FPS/CCU·전체 시각 품질 승인은 없다.137차는 정확한 현재 selected-body 값으로 원본 child native local cache를 재사용하는 최소 구현을 검토한다. NPC 모션매칭·새 물리 solver·글로벌 manager를 추가하지 않으며 전체 목표는 active다.

## 137차 원본 물리 bounds 재사용과 채택 조건

기존 Modular component의 CalcBounds를 확장하고 Budgeted source가 한 프레임의 단일 엔트리를 소유한다. 이미 유효한 native child cache는 key 작업 전에 Super로 반환한다. 지원 경로만 실제 PA 선택 순서의 body identity/GUID/name/consider/presence와 child GetBoneTransform(Identity)를 owning 값으로 캡처해 오차0으로 비교한다. child mesh pointer나 bone index가 다르다는 이유로 공유를 막지 않지만 현재 child identity는 계산 전후 검증한다. miss는 원래 child Super가 만든 native local cache만 게시하고, hit는 수신 child의 native local-cache 필드에 같은 값을 넣어 그 child의 정확한 query transform을 Super로 적용한다. 숨겨진 leader의 정적 CalcBounds나 새 AABB solver를 사용하지 않는다.

GT·정확한 기존 component class·Game/PIE·native local-cache1·include-location0·clean/no-cloth/no-own-PA와 유한 입력만 지원한다. active cloth metadata뿐 아니라 잔류 native simulation instance도 제외한다. 미지원 조건은 원래 Super를 유지한다. source/world/owner/asset/PA/프레임과 현재 body 값이 key이고 source의 weak bone revision이나 Finalize epoch를 포즈 불변으로 가정하지 않는다. source 등록·해제·mesh/PA 교체·animation rebind·종료에서 퇴역하며 <=64 body 값의 lazy TArray 한 개만 유지한다. 정상 재사용은 용량을 보존하고 수명 종료는 해제한다. native BodySetup invalidation/GUID·dirty/immutable definition 계약을 API에 명시했으며 통보 없는 raw geometry 편집은 지원하지 않는다. 플레이어/NPC의 애니메이션 선택 정책·물리 갱신 빈도·에셋·Config는 바꾸지 않았다.

기존 authored-pose 테스트 파일에 실제 hidden Complete source와 서로 다른 Lower/Sho child를 연결했다. native0 대비 miss/hit, caller query transform, 같은 프레임 public ApplyEditedComponentSpaceTransforms의 A→B→A와 실제 bounds가 달라지는 양성 대조, child 교체·source 등록/animation/physics 재초기화, include-location/nonuniform/ownPA/unregistered/detached fallback을 검증한다. private ResetToRefPose를 호출하거나 노출하지 않았다. Editor277의 inherited Bodies shadow/protected API 접근 오류는 변수명과 source 내부 유효성 검사로 수정했다. Native137_1의 단일 실패는 Constructor history를 ReplaceCurrent로 교체할 수 없는 native API 계약이었다. 원래 rank를 명시하는 Set으로 값·우선순위를 함께 복원했고, 이전 실패를 보존했다.

기존136 paired proof를 공통 흐름으로 재사용했다. SharedPhysicsBoundsProof와 LeaderBoundsProof는 배타적이며 기존52열 뒤에 mode/control/복원/actual counter8열을 붙인다. 공유 비교는 native leader flags0/0·공유 캐시0/1·모든 쌍 동등성·포함 관계와 실제 hit를 요구한다. 비교용 native invalidation은 geometry proof에만 있고 일반 성능 실행에는 없다. 최종 Proof2는 실제 이동/공격/점프 phase2..6의9파트×5phase×50=2250쌍/250batch, 좌표·반경 최대 차0·위반0·hit2000/miss250·flags/CVar 값과 rank 복원 성공이다. Cloak는 ownPA/cloth로 제외했고 별도 품질을 낮추지 않았다. 범위는 원래 native box/sphere의 보존이며 모든 vertex/WPO·새 콘텐츠 품질 승인이 아니다.

초기 readonly delegated counter의 typed TAutoConsoleVariable 생성자가 지원하지 않는 AsVariableInt를 조회해 startup handled ensure를 만들었다. 성능 실행의 정상 native/result와 이 로그 오류를 구분했다. 직접 RegisterDelegatedConsoleVariable + unregister(false) RAII로 등록 원인을 수정하고, hotpath는 같은 GT relaxed atomic 포화 증가/읽기 전용 GetInt를 유지했다. 최종 Editor280 full direct UBT0(15.655초), Game100 full direct UBT0(11.331초), RHI Native137_3은53 Success·테스트 경고/실패0(2.803초)이다. 수정 뒤 Native137_3/Proof2 두 새 프로세스는 해당 ensure 없이 정상 종료했다. 모듈 hot reload/주입한 disk 실패/모든 GC·월드 교체 조합은 별도 runtime 미검증이며 정상 process/world 종료 근거를 그 범위로 확대하지 않는다.

진단 등록 수정 전 Control0/Candidate1은 같은 Source/runtime·200 Avatar+별도 NPC 값 판단64/대상128·WQHD viewport·기존 품질의 한 쌍이다. manifest 차이는 run 경로와 공유 캐시0/1이며 NPC 모션매칭 부하가 아니다. 실제 phase5 montage200 공격26/28표본의 frame P95는106.297→102.861ms지만 phase2/3은 증가하고 다른 구간은 혼재한다. 최종 수정 버전의 새200명 결과나 전체 FPS 개선으로 세지 않는다. 기존135의 metadata→observer/frame map→한정 GT export를 재사용해 key/guard를 포함한 외부 구간을 검증했다. 실제 공격9파트 외부 평균은7.3429→7.8612ms/frame, Cloak까지 포함한 비중첩 합은13.3896→13.7444ms/frame이다. native9 CalcSkel exclusive는5.2486→0.7071ms/frame이지만 Key4.5864·Miss자체0.1530·Hit자체0.0947ms/frame을 빼놓으면 잘못된 이득이 된다. 외부 구간은 bounds 외 작업도 포함하므로 순수 bounds 함수 시간으로 부르지 않는다. exporter 경계 밖1/2행은 원시 행과 strict identity/nesting을 유지하고 정확 교집합0으로 계산했다. 이 비교를 마감하고 기본값 ProjectJ.Bounds.SharedPhysicsCache=0을 유지한다. 정확도/수명 검증이 있는 명시 실험 경로는 보존하되 다른 물리 자산에도 별도 비용 검증 없이 활성화하지 않는다. 추가 미세 튜닝이나 외부 engine priority/grain/cloth 품질 변경은 하지 않는다.

변경은 production5파일·기존 테스트1·기존 fixture/runner4로 총10파일이다. Source1233/runtime39·보호17asset 불변, native 전후 지문 일치, 최종 Game 통합은 exe만 추가 변경했다. FinalWorkspace137.json SHA256 `d69e422f18a01a17d497a6622b1cfb5238d492479c30e64f79ae51087571d903`, 다음 Editor281/Game101이다. 최초 후보·컴파일/우선순위 실패·등록 원인 수정·원시 결과·성능 비교는 Saved/ArchitectureBatch137SharedPhysicsBounds와 실제 Bounds137 실행들에 보존한다. 제한된 trace export는 기존 Saved/ArchitectureBatch135FlightForecast/Bounds137*에 있다. 새 Shipping/Cook/Stage·원격 CCU·전체 MMORPG 완료 주장은 없으며 기존 필수 잔여와 전체 active 목표는 유지한다.

## 138차 NPC 비모션매칭과 Mover 실행 연결

NPC는 모션매칭을 사용하지 않는다는 사용자 기준을 실제 BP 연결에 적용했다. 선택한 BP_NPC_Test는 Player_Master를 기본 AnimClass로 가지고 있었다. classic MM node0만으로 비모션매칭이라고 판정하지 않고 native Player driver/PoseSearch history1을 함께 확인했다. 기존 Unarmed로 단순 교체해도 실제17.74m 이동·속도800cm/s·64회 bone finalize에 BlendSpace 입력0이었다. 공개 그래프 관측은 그 템플릿의 ACharacter cast/CharacterMovement acceleration·falling 의존성을 확인했다. 실제 NPC는 APawn/Mover 계열이라 이 owner 계약을 만족하지 않는다.

공통 UProject_JCharacterAnimInstance의 기존 GT 생산/proxy 게시 경로에 유효한 동일 owner/world의 Mover Air/Input 관측을 연결했다. graph는 기존 GraphCombatSpeed/Direction과 현재 proxy의 Falling/Velocity 값 getter를 읽는다. worker에서 Actor/MovementComponent를 조회하지 않으며 player의 별도 생산 경로는 바꾸지 않았다. 소비자가 없는 새 HasMoveInput getter는 제거했다. 새 ABP_NPC_Mover_Unarmed는 공통 native 부모를 사용하고 원본 Unarmed의 skeleton·동작·BlendSpace·DefaultSlot·Control Rig를 보존한다. ShouldMove는 원래 strict 비교와 실제 임계값0.010000을 유지하면서 속도만 사용한다. simulated proxy의 로컬 입력/CMC acceleration을 필수로 요구하지 않는다. 낙하 전이에 쓰는 Velocity_Z 분리 핀도 public schema로 보존했다. 새 NPC runtime subclass/전역 manager/모션매칭이나 강제 pose/notify는 추가하지 않았다.

기존 AssetMigrationLibrary의 선택 템플릿 제작 함수는 기본 dry-run이며 기존 경로 덮어쓰기를 거절한다. 새 복제에만 reparent/reader10곳 교체·옛 EventGraph/변수 퇴역·SkipSave 컴파일·원본과 보존 그래프 검사를 하고 그 target 하나만 명시 저장한다. 원본 Unarmed/Player_Master는 그대로다. 생성물의 새 프로세스 메타데이터는29nodes, classic MM/PoseSearch/player-driver0, main graph/실제 Cleave slot·skeleton 호환을 확인했다. 마지막으로 BP_NPC_Test의 기본 AnimClass만 새 graph에 연결했다. 이는 모든 골격/NPC 콘텐츠를 일괄 변환했다는 뜻이 아니다.

실제 동일 NPC/controller가 Recast5point 경로17.74m를 이동하고 BlendSpace/pose 변화, 자연 Cleave montage·notify·GAS 피해1회, 재승인 후 impact 전 취소·몽타주 drain·정책 복원·마지막 명령의 EndPlay 퇴역을 통과했다. raw contact는3회이며 공유 authority 계층이 피해를1회로 제한했다. 초기 시험의 raw contact==1 가정은 실제 NotifyTick/ServerHitActors 계약과 맞지 않아 관측 후 수정했다. 피해는 정확히1회, 자연 종료 뒤 contact 수와 취소 뒤 health 불변, 잘못된 단계의 event 실패는 유지했다. 별도 기존 snapshot/재초기화/수명 RHI5개와 최종 NPC/Nav/동료26개가 실패 없이 완료됐다.26개 중1개는 프로토타입의 optional CombatVFX PresentationComponent 부재 경고를 포함하며 숨기지 않았다. VFX 재생·접지 시각 품질·원격 실제 NPC 복제·64개 full-NPC 혼합 부하·FPS/CCU 증명은 아니다.

최종 Editor290/Game101 full direct UBT0(7.545/60.822초), Windows Batch138NPCCook1 native0/46.803초·오류/경고0이다. Entry+NPC 두 에셋과 정상 구성 의존성의 Cook으로1706packages가 처리됐고 두 경로가 ReferencedSet/registry에 포함됐다. 새 Stage/Shipping/패키지 실행은 하지 않았다. 별도 OS-Temp 출력에도 정상 Zen Windows oplog는 재작성되므로 이전 Cook의 Zen index를 독립 보존·재사용 가능하다고 주장하지 않는다. 기존129 Stage 패키지는 이번 대상이 아니다.

Source8파일, Source1234/runtime39, 기존 보호17asset 중16개 불변이며 나머지는 명시 변경한 NPC BP다. NPC 원본17304b47…9d2877은 첫 Apply backup에 보존했고 현재 BP는e04de4b8…c34b78c7, 새 graph는a447b539…07be34a다. 초기 positive-control/추상 requester/include/분리 핀/접촉 가정 실패와 실제 근거는 Saved/ArchitectureBatch138NPCExecution에 유지한다. FinalWorkspace138.json SHA256 `0112eb3118ecbd0f07ef87d9097aeaff20d0c4900ab4e3d877a479bc67257e3e`, 다음 Editor291/Game102다. 전체 목표는 active이며 다음은 기존 A06/A12의 실제 NPC 표현·처리 예산과 혼합 경계를 재사용해 마감한다.

## 139차 실제 비모션매칭 NPC 혼합 실행과 미완료 판정 경계

기존 RenderedCrowd 기본 동작은 유지하고 NPCCombatCount0–32 선택 경로에 실제 authored NPC32와 별도 passive native ASC 수신자32를 연결했다. 플레이어200과의 분모를 구분하며 기존 GAS grant/activation·공유 판단/action·authored montage/notify·실제 GE와 animation budget을 소비한다. 플레이어 관측 등록으로 NPC를 영구 GameplayPose에 묶지 않고 실제 공격 demand가 보호를 요청한 뒤 자연 종료·몽타주 drain 후 원래 budget으로 복귀한다. actor/mesh/anim/ASC epoch/controller/grant·등록·late callback·취소/정리와 clean package를 함께 검사한다. 새 manager·예산 튜닝·MM·강제 pose/명중/LOD·새 콘텐츠를 추가하지 않았다.

**생산 advisory 수명 수정.** 실제 결과 age515.7356ms가 기존500ms를 넘었다는 이유로 semantic context invalidate가 발생해 정상 공격까지 취소됐다. NPCDecision과 TargetScoring의 기존 ResetPendingQuery를 재사용해 owner/target/authority/context가 현재인 단순 만료만 pending을 퇴역시킨다. fresh receipt나 새 타깃을 만들지 않으며 hard revocation·등록/장비/권한·기본3초 intent lifetime의 취소는 유지한다. 실제 worker/FIFO·.55초 경과와 기본 lifetime 회귀37개를 통과했고 이후 관련 선택 회귀에서도 유지했다. query deadline/서비스 예산을 늘리는 우회가 아니다.

**실제 이전 pose와 시간창.** 기존 Begin의 current-pose seed와 같은 queued pass 첫 Tick이 동일 점을 읽는 문제를 확인했다. HitValidation의 활성 공격 수명에 기존 BoneFinalized delegate 하나와 최대64 notify-point 값을 연결했다. 현재 지원 범위는 전체 montage notify64개 이하에서 직접 queued/forward continuous인 이벤트다. exact mesh/asset/anim class/instance ID·직전 native frame/bone revision·현재 pose source/world·attack/spatial generation·socket/tip offset·authored trigger metadata를 검증한다. 실제 이전/current world point 사이 직선 segment를 현재 미소비 시간과 authored window의 교집합으로 자르고 기존 단일 sphere sweep/GAS dispatcher와 SSR history를 그대로 소비한다. 엔진의 active NotifyEvent는 복사본이므로 원주소가 아닌 source montage/instance/Notify 객체/시간창으로 확인한다. 정상 blendout도 같은 native instance ID로 추적한다.

지원 밖 이벤트는 기존 경로를 유지하고, temporal 처리를 시작한 뒤 불연속/순간이동/수명 교체에는 낡은 점을 연결하지 않는다. 기존 NotifyEnd 원문과 callback 후 window/spatial fence는 유지했다. 새 Tick/task·전역 manager·전체 bone 복사·강제 animation evaluation·곡선 bake·반경/시간창/에셋 변경은 없다. 이는 실제 두 pose의 선형 구간 보존이며 미관측 곡선 전체, 임의 section jump/reverse/복잡 retarget/ControlRig 또는 root-motion의 과거 시간 매핑 보증은 아니다. A09의 그 경계는 계속 남는다.

**관측 교란 수정.** fixture GE-to-self delegate의 첫 ASC가 recipient가 아니라 source인 엔진 계약을 바로잡고 실제 bound recipient의 Health를 읽는다.139_9/10은 자연 종료/보호/budget 복귀32개를 확인했지만 명중 NPC2/12개로 엄격한 gate가 실패했다.139_10 raw704행·observer704개·GameFrame705개를 기존 trace에 매핑해 실제 frame473394.149ms 아래 PNG 압축266.150ms와 FlushRendering42.271ms를 확인했다. 다음 delta394.158ms가 hit window266ms를 건너는 원인이었다. 모든 NPC의 LOD2·filterNone/chance1·suppression0과 history도 따로 확인했다.

같은 촬영 프레임·PNG/sRGB/opaque alpha/default compression을 유지하며 엔진 ImageWriteQueue에 immutable BGRA 한 장만 전달했다. exact 현재 viewport/request 동안 자기 delegate만 연결하고 기존 listener/foreign request·queue 포화·픽셀 크기 오류를 거절한다. owned raw 최대64MiB·한 future, nonblocking enqueue와 GT IsReady/Get true로 phase2–6의5회/mask124를 확인한다. teardown은 자기 callback만 해제하고 값만 소유한 task는 엔진 queue가 drain하며 늦은 파일이 실패 결과를 승격하지 않는다. NPCCombatCount0 기본 동기 경로는 그대로다.139_11은 저장5회/모든 NPC 수명은 통과했지만 실제 피해18개여서 gate 실패를 유지했다. PNG 교란의 원인 수정과 일반 melee hitch 복원을 구분한다. readback/Flush와 queue 실패·foreign viewport 교체/write 도중 teardown의 native 검증은 남으며 코드 수명 검토로 완료를 대신하지 않는다.

**검증과 가정 정정.** Editor303의 첫 새 native hitch는 .600000083→.918752015, world delta318.751931ms에서 공개 nonzero history가 두 실제 pose를 [.643019795,.908752024]로 자른 독립 기대값과 정확히 일치했다. 이후 피해1을 가정한 검사는 contact0/GE0으로 실패했다. V3는 동일 실제 capsule의 stationary identity/크기를 확인해 clearance+40.4199994cm라는 비접촉을 증명하고 첫 공격의 피해0/Health1000 불변을 요구했다. 같은 NPC/target/definition을 정상 public readmit/new grant로 한 번 더 공격해 raw4/GE1·자연 종료, 다음 preimpact 취소·기존 EndPlay/소유권 정리를 확인했다. 배치·반경·시간창·pose를 바꾸거나 RenderedCrowd의 전원 명중 조건을 낮추지 않았다. 원래 정상 사례도 그대로 통과했다.

Native139_2의 나머지35개 성공과 동일한 생산4파일을 유지한 마지막 Native139_3의2개 집중 성공을 합쳐 고유36개 사례가 통과했다. 두 focused 사례의 optional NPC VFX 컴포넌트 부재 경고는 숨기지 않았다. 실패했던 V2 보고와 최초 probe/가정 실패도 보존한다. 관련 Source 지문과 기본 사례 역변환 동일성 검사를 근거로 같은 전체 회귀를 반복하지 않았다.

**최종 실제 혼합 실행139_12:** 플레이어200+실제 non-MM NPC32+별도 passive recipient32, strict success/native0/result1. NPC32 전부 quiet budget→동시 montage peak32→실제 GE1→자연 종료/GameplayPose 보호→원래 budget 복귀, raw55/GE32/Health320 감소,64actor 파괴/64mesh unregister·dirty 불변을 확인했다. 단일 queue의 PNG5회 future true·mask124·pending0이며 phase2/3 실제 출력은2560×1440이다. source/runtime provenance도 실행 전후 일치한다. 현재 입력의 결과이며 native 내부 raster/FSR/FG 상태·모든 시각 품질·경로 이동/원격 NPC·VFX/GPU·FPS/CCU 인증이 아니다.

최종 Editor304/Game104 full direct UBT0(7.577/41.871초), 생산 후보의 Editor303도 full0/48.027초다. Source/Scripts14파일·Source1236/runtime39·보호20asset 불변, FinalWorkspace139.json SHA256 `8bd720b48cc25d7a4876151db263313e15816c9e28efc07ef90eb30464938588`이다. SourceCheckpoint139는 앞선 부분 실패 시점이며 최종 완료 지문과 구분한다. 원자료·frozen 후보/peer·실패/성공·기존 trace의 한 프레임 export는 Saved/ArchitectureBatch139NPCPopulation과 실제139 실행들에 있다. 새 Cook/Stage/Shipping은 하지 않았고138 Cook/129 Stage의 기존 입력 범위를 유지한다. A08의 NPC authority load chain은 소스에서 새 결함을 확인하지 못했으며 선택한 serialized AssetRegistry 의존 근거가 다음 좁은 작업이다. A09 일반 곡선/물리 시간 매핑·A10 실제 준비/GPU/자원·A07 선택 소스 엔진/운영 provider 의존성과 전체 목표는 active이며 다음 Editor305/Game105다.

## 140차 선택된 NPC 자산의 직렬화 로드 경계

139차에서 통과한 실제 NPC 경로와 기존41/78/138 검토를 재사용해 activation·능력 BP·attack definition·montage·NPC BP·전용 AnimBP, 정확히6개 package만 AssetRegistry.scan_files_synchronous로 확인했다. object load/compile/save·재귀 closure나 전체 에셋 검색을 script에서 요청하지 않았다. Entry/NullRHI의 isolated Editor read는 정상0이며 direct hard edge36/soft0·출력 절단0, 선택 파일과 기존 보호20asset·전체 Source/runtime 지문이 실행 전후 일치했다.

activation→GA_NPC_PrototypeCleave→attack→AM_Spellblade_LMB1/GE_TestDamege와 NPC BP→공유 Mover walking mode/MannySimple/ABP_NPC_Mover_Unarmed의 실제 연결이 있다. 전용 graph는 BlendSpace/일반 jump·fall·land·idle 클립, skeleton과 FootIK rig를 참조한다. 이 한 경로에서 pure Niagara/표현 payload의 새 hard edge는 확인하지 못해 기존 authority pose/notify·metadata를 유지하고 source/asset 수정은 추가하지 않았다. 이름이 PlayerMovement인 공통 Mover 모드나 MM_ 접두어 클립만으로 잘못된 NPC MM 사용을 판정하지 않으며138의 실제29nodes/MM·PoseSearch·player-driver0 근거를 재사용한다.

raw Editor registry의 GameplayAbilitiesEditor/ControlRigDeveloper/AnimGraph 등의 /Script 참조는 cooked/runtime 로드나 resident byte 증명이 아니다. 같은 자산의138 Cook 성공도 현재 runtime 메모리0을 뜻하지 않는다. 이 결과는 선택 direct chain만 확인한 것이며 전체 A08 closure·서버 headless 상주 메모리·모든 콘텐츠 품질을 닫지 않는다. 근거 없이 all-soft/bundle framework나 서버 pose 제거를 추가하지 않았다. 원자료와 script 지문·native0/Source 보존 검증은 Saved/ArchitectureBatch140NPCDependencies에 보존했다(SelectedPackageEdges140.json SHA335745ac…7a7e1c8). 현재 Source 기준점은 FinalWorkspace139 그대로, 다음 Editor305/Game105이며 전체 목표는 active다. 독립적인 다음 작업은 기존106/132와 현재 portal 목적지의 실제 collision/navigation readiness 소비를 좁게 연결하는 것이다.

## 141차 Portal 실행 전 생존 자격과 실행 후 결과 수명

기존106/122/132와 현재 Portal 소비를 좁게 재확인했다. 이 진입은 정지·grounded·on-foot Player용이며 실제 WP 완료/비공허128cell·walkable floor/capsule·고정 목적지 native commit을 소비한다. flying/AI/companion의 Nav 목적지 계약이 아니므로 전역 Nav/GPU gate를 추가하지 않았다. 남은 작은 경계인 준비 후/적용 전 생존 상태 변경을 기존 native consumer에서 보강했다.

현재 same request/PC/Pawn/PS/ASC epoch와 native Mover/updated component가 유효한 상태에서, native FProject_JPreparedArrivalEffect의 placement eligibility 및 Super 적용 직전에 현재 PS의 AttributeSet/finite positive Health/State_Dead 부재를 검사한다. 부적격이면 Actor/SyncState를 바꾸지 않고 기존 FTeleportFailedEventData를 붙여 normal event processing→weak exact-generation receipt→기존 Failed terminal writer로 처리한다. stale binding은 기존 no-event 거절을 유지한다. source identity/queued source permission·client 분기·origin Portal unload·fixed target/geometry/deadline·queued 이후 grounded/velocity/combat soft policy·generic teleport는 바꾸지 않았다.

첫 Saved 후보는 공통 IsArrivalCurrent(false)에 생존 조건을 넣었지만, root 검토에서 이 함수가 RecordArrivalOutcome과 중앙 writer에도 쓰인다는 영향을 확인해 적용 전에 거절했다. 그 후보는 이미 native 이동이 적용된 다음 Health/Dead가 바뀌면 실제 SUCCESS receipt를 버릴 수 있었다. 최종 V2는 binding/receipt와 실행 자격을 분리해 공통 identity/receipt/source/Advance 원문을 유지했다. 새 field/state/API/manager/timer·결과를 흉내 낸 직접 Finish는 추가하지 않았다. V1과 거절 근거는 Saved에 보존한다.

기존 PreparedArrivalBoundaries에 네 경우만 연결했다. 공개 ASC health setter 또는 loose State_Dead 변경 뒤에도 실제 same PC/Pawn/PS/ASC incarnation과 적법한 목적지 geometry가 유지되는지 확인한다. 적용 전에는 실제 native Apply가 이동/SyncState 변경 없이 FAILED event1을 생성하고, 실제 OnEventProcessed 전에는 미확인·후에는 실패 receipt·중앙 writer 뒤에는 Failed/정확한 Entry·permit 반환이어야 한다. 적용 후/이벤트 처리 전 생존 상태가 바뀐 경우에는 실제 SUCCESS event/receipt와 geometry가 유효한 Arrived 결과가 유지돼야 한다. source query가 Entry/permit를 직접 퇴역하거나 life 변경만으로 기존 queued identity를 지우지 않는 것도 확인한다. 테스트는 자기 tag1회/health 값만 공개 API로 복구하고 가짜 포인터·clock·role·결과를 주입하지 않는다.

Editor305/Game105 full direct UBT0(8.080/13.333초), 기존 World/WorldEvents11개 NullRHI native0/11 Success·경고/실패0(3.927초)이다. 실제 life4경계는 동일epoch4의 native event1을 통해 실행 전 Failed(phase5), 실행 후 Arrived(phase4)로 끝나고 request/Entry/permit가 반환됐다. shared source/결과 전달/예산·월드 이벤트 등의 기존 회귀도 함께 통과했다. 의미 있는 현재 변경 경계만 실행했으며 같은139 혼합 장면을 반복하지 않았다.

생산cpp1/기존시험cpp1, Source1236/runtime39·보호20asset 불변, FinalWorkspace141.json SHA256 `216fedd719ebc868d8472210481c7366dfc738deae08fd79b347c8a5478bd66a`이다. 원자료·두 후보/peer·V1 거절·실행/보존 검증은 Saved/ArchitectureBatch141ArrivalLife 및 NativeBatch141_1에 있다. 이 시험은 실제 native 이동을 호출하는 값/물리 경계이며 전체 death ability/animation·새 socket/WP residency·GPU/Nav/메모리 귀속·Cook/Stage/Shipping/FPS/CCU 증명이 아니다. public health setter가 nonfinite를 clamp하므로 missing/NaN을 만들려고 private payload를 손상시키지 않았다. 운영 provider/소스 엔진·A07 strict 수명·A09 전체 곡선/물리 시간·A10 실제 자원 및 A06/A12 혼합 표현 등의 잔여는 유지한다. 다음 Editor306/Game106이며 전체 목표는 active다.

## 142차 혼합 이펙트 수명 근거와 전체 통합 gate 미완료

기존133 saved128 압력과139 실제 NPC 혼합 경로를 재사용했다. 명시 MixedFXResources 옵션만 local200+Movement+실제 non-MM NPC32+passive recipient32에 합성 cosmetic owner132를 더한다. 이132개는 full animated NPC/공격 콘텐츠 수에 합치지 않는다. meshless native MOVE_None·collision-disabled 소유자, transient CharacterMesh loop wrapper와 기존 attack/cue tag를 사용한다. 원 NS_CombatTrail/NET_CombatTrail·128/High/2500cm·pool32, 일반 remote creation budget4, 기존 camera/FOV·몸체/NPC 품질을 유지하며 기본 count0은 asset load/tick 등록 없이 반환한다. 새 manager/scheduler·quality fallback·NPC 모션매칭은 추가하지 않았다.

생산 getter GetCueLeaseComponent는 GT에서 exact current token→tag→loop component를 weak로 관측한다. pending은 null일 수 있고 inactive count-cull도 관측할 수 있으므로 IsActive로 필터하지 않는다. 종료/공격 교체 뒤 old token은 같은 pool component가 다른 owner에게 재사용돼도 null이다. 새 state/UFUNCTION·로드·활성화·권한 기능은 없으며 기존 CueLeaseOwnership의 실제 activation/reentry/GC 사례를 확장해 RHI1 Success·경고/실패0/native0으로 확인했다. loop 내부 상태를 복제한 별도 자원 manager를 만들지 않았다.

fixture의 TG_PostUpdateWork tick은 실제 Niagara cached view/projection/distance와 bounded 값만 읽는다. phase3 normal admission 뒤 실제132 distinct/current lease와 scalability 등록을 기다리고 phase5 실제 player/NPC montage와 active/count-cull 동시 관측을 요구한다. component 열은 registered-qualified, distinct는 실제 mapped pending도 포함한다. global budget 통계는 Stats.Frame/current flag를 함께 기록한다. phase6 exact stop 뒤 invalid/pool/noncohort reuse를 구분하며 새 owner가 빌린 component는 중단하지 않는다. 관측 tick과132 소유 actor를 NPC baseline 정리보다 먼저 해제한다. CSV30열/최대65536행, checkpoint16개 이내이며 RHI texture/process 통계는 checkpoint에서만 읽는다. 이는 전체 장면 수치이고 FX 증분 VRAM/총 GPU allocation이 아니며 unknown 값은 유지한다.

최초142_1은 준비 완료 선대기가 첫 Activate를 막아 on-demand compile을 시작하지 못했다. 설치 NiagaraSystem의 pending request/IsReadyToRun과 NiagaraComponent의 normal Activate→PollForCompilationComplete 계약을 확인해 선대기만 제거하고 native 비동기 activation/retry를 재사용했다. 직접 compile/Wait/강제 Tick·시간/예산 변경은 없다. 등록 전 실제 mapped component는 identity를 보존한 pending으로 관측한다. 두 번째142_2는 첫 실제 component 거리440.8355703cm에서 시야 검사를 실패했다. native mesh의 relative Z=-90 때문에 Actor 기준(400,-100,-66)이 실제 효과 기준(400,-100,-156)이 되었고, 기존 FOV65·16:9의 vertical half extent143.3408087cm를12.659cm 벗어났다. V3는 native CDO의 root-relative mesh offset으로 합성 owner spawn 위치를 보정하고 FinishSpawning 뒤 실제 mesh 위치/시야를 재확인한다. 메시 offset·camera·의도한 효과 분포는 바꾸지 않았다. 두 실패와 첫 원본/후보/peer를 보존한다.

최종 MixedFX142_3은 native0, 실제 component132/current lease132, active128/count-cull4와 phase5 실제 montage overlap19행을 기록했다. 이128/4는 관측값이며 같은 EffectType의 다른 cue도 공유하므로 고정 분할 조건으로 강제하지 않는다. CSV456행/30열, shared frame attempts 최대4, exact stop 다음 native 관측에서 old weak component132개 모두 invalid·old lease0, 소유 actor132 파괴/cleanup·dirty 불변이다.32개 pool 잔류를 증명하지 않았고, receipt retirement_seconds7.975는 의도된 phase6 유지 시간을 포함한 stop→fixture cleanup 시간이다. native 반환 자체가8초 걸렸다는 뜻이 아니다. checkpoint의 editor_compile_outstanding은 기본 VM/request 조회이며 별도 GPU compile 완료 증명이 아니다. 실제 준비 상태는 phase3 첫 compile request pending→132 관측 시 ready/request 없음으로 바뀌었다. phase2/3 PNG는2560×1440이고 기존5회 캡처 파일이 있다.

**전체 strict 결과는 success0으로 유지한다.** NPC32 전부 실제 montage·자연 종료·GameplayPose 보호·budget 복귀와64 actor/mesh cleanup을 확인했지만 raw41/GE25/Health250 감소였다. NPC9–15는 raw0/GE0이며 LOD2/suppression0·history found4/open3·진단 chord clearance +1.087905…+1.087907cm였다. hit/miss 모두 최대 montage advance .1476496초가 관측돼 그 값만으로 새 취소/누락 결함을 주장하지 않는다. 현재 실제 pose 사이 단일 chord는 미관측 곡선 전체를 복원하지 않으며, sampled clearance만으로 전체 공격 곡선이 target을 지났는지도 확정할 수 없다. 피해를 강제하거나 반경/위치/시간창/gate를 낮추지 않았고 성공할 때까지 같은 장면을 반복하지 않는다. A09의 선택 authored path와 actual root/world 시간 매핑을 이어갈 근거이며 A06/A12 전체 qualification은 닫지 않는다.

최종 Editor309/Game106 full direct UBT0(7.470/42.757초), 앞선 Editor306/307/308도 정상0이다. Source/Scripts7파일·Source1237/runtime39·보호20asset 불변, 혼합 manifest와 최종 Source 동일, 이후 Game106으로 runtime Project_J.exe1개만 재빌드됐다. FinalWorkspace142.json SHA256 `348c051a6373dd93e1ce7cba95325a411030fe82bb407f9f2fffbdcff4e57b57`, 근거는 Saved/ArchitectureBatch142MixedFX/Verification142.json과 세 실제142 실행에 있다. 엔진 toolset Python startup 오류 등 기존 실행 로그를 지우거나 전체 로그 무경고로 보고하지 않는다. 새 Cook/Stage/Shipping·particle/ribbon draw·mandatory warning·FX GPU/VRAM 증분·FPS/CCU 인증은 없다. 운영 provider/소스 엔진·A07/A08/A09/A10/A12 잔여와 전체 목표는 active이며 다음 Editor310/Game107이다.



## 143차 선택 gameplay 궤적과 NPC 데이터 실행

**실제 변경.** 기존 AttackDefinition의 기본 동작은 보존하고 선택 `MeleeTrajectory`를 추가했다. 편집기 C++ 도구가 실제 RAW sequence를 120Hz 절대 montage 격자와 정확한 window 양 끝에서 평가해 2..64개 mesh 공간 키로 만든다. metadata/raw motion/retarget/mesh/socket 입력과 출력 키를 함께 stamp하며 등록 Data Validation이 현재 source와 비교한다. runtime은 활성 공격의 실제 GAS montage 간격·두 물리 root 관측값과 고정 MeshToRoot로 구간을 만든다. translation lerp/shortest quaternion slerp를 사용하는 **별도 authored gameplay geometry**이며 현재 화면의 blended/IK·물리 unsampled arc를 복원하는 기능은 아니다. source soft identity는 추가 hard visual payload를 만들지 않는다.

공유 notify는 cursor를 query/callback 전에 소비하고 전체 구간을 먼저 수집한다. actor별 첫 LOS 적격 contact에 해당 authority segment를 선택해 기존 GAS/중복 피해/취소 경로를 호출한다. 첫 geometric contact만 고르면 앞쪽 벽 뒤의 contact가 뒤쪽 적격 contact를 막아 dispatch 분할에 따라 결과가 달라질 수 있어 LOS를 먼저 확인한다. callback 뒤에는 exact instance/token을 재조회하며 낡은 map 참조를 유지하지 않는다. 켜진 데이터가 무효이면 legacy로 조용히 바꾸거나 NotifyEnd에서 보충 피해를 만들지 않는다.

SSR은 기존 wire RPC를 바꾸지 않고 같은 authority dispatch-time packet의 소유 구간들에서 접촉과 해당 구간 origin의 LOS를 확인한다. packet63구간·ring128·전체 retained geometry192로 제한하고 오래된 record 전체를 퇴역시켜 용량도 반환한다. 같은 timestamp의 open→atomic→close는 마지막 publication이 기준이다. legacy single-segment reader는 유지하며 atomic packet을 잘못된 하나의 chord로 반환하지 않는다. target은 기존 ClientTimestamp의 capsule이므로 움직이는 target의 연속 시간 CCD를 구현한 것은 아니다.

native Mover sync와 실제 root 일치·frame/bone/spatial/source fence를 유지한다. authority 또는 실제 locally controlled autonomous만 admission하고 simulated는 제외한다. 양의 고정 균일 scale과 동일 mesh 상대변환, 기반이 있으면 동일 유효 component/bone과 **변하지 않은 게시 기반 좌표계**를 요구한다. 기존 혼합 바닥이 Movable이므로 any-base 거절 대신 게시 값의 연속성을 사용했다. 실제로 움직인 기반·warp·resimulation·async backend·section jump/reverse는 계속 미지원이다. 기존 GAS→Mover montage bridge를 그대로 사용하며 새 writer/manager/task를 만들지 않았다.

NPCActivationDefinition의 선택 AttackDefinitionOverride를 기존 grant SourceObject로 전달했다. weak GAS SourceObject를 보완하기 위해 정확한 grant 수명의 captured data를 UPROPERTY로 보유한다. Stop은 이전 값을 local pin으로 이관하고 자기 handle/token/참조를 먼저 분리해 reentrant 새 activation을 지우지 않는다. null은 기존 CDO fallback이며 active A를 편집된 B로 바꾸려면 명시 Stop→Start가 필요하다. 원본 PrototypeCleave 공격/activation과 NPC non-MM/플레이어 MM 그래프는 유지하고 새 canonical 공격/activation 참조2개만 만들었다. 기본 fixture는 원본을 유지하고 명시 `-NPCAuthoredPath`가 실제 helper→grant→ability를 소비한다.

**제작·보존.** public baker는 clean persistent target만 허용하고 selected mesh/sequence의 지원 API로 필요한 컴파일 완료를 기다린다. 최초 protected Sequence.IsCompiling 호출의 C2248은 공개 selected API로 수정했다. source준비 실패 CanonicalDry143_1의 정확한 하위 조건은 기록되지 않았으므로 mesh compile이 유일 원인이었다고 주장하지 않는다. 정상 selected preparation을 추가한 뒤 Dry143_2가 통과했다. 콜백 전 target path/outer/package와 원래 trajectory/다른 필드를 고정하고 준비·설치·PostEdit/저장 뒤 재검사한다. rollback은 자기 exact field만 돌리고 package dirty를 지우지 않으며 disk rollback을 주장하지 않는다. private transient candidate builder는 기존 native fixture와 실제 제작이 공유한다.

**집중 검증.** Editor310 최초 실패와 Native143_1의20통과/1실패를 보존했다. 실패는318.752ms native tick이 window를 넘어선 동일 dispatch에 End까지 왔다고 가정한 시험이었다. 실제 queued Begin→Tick 뒤 다음 native dispatch에 End가 오는 관측에 맞춰 시험을 수정했고, 생산 기하·clock·반경·위치·window는 바꾸지 않았다. Native143_2와 최종143_3에서33구간·contact1/GE1·다음 dispatch close/재피해0를 확인했다. 고정된 실제 Movable 바닥 기반과 no-base 둘 다 동일 결과이며, 실제 Recast 도착·같은 actor의 취소/EndPlay·corrupted output stamp 거절도 포함한다. 최종143_3은2 Success/2 optional NPC VFX SuccessWithWarnings·실패0/native0, 원143_1의 영향을 받지 않은20개를 재사용한 고유24개 마지막 결과도 실패0이다. SSR 실제 authority RPC의 later-piece hit/LOS·중복 거절은 포함되지만 새 remote socket 실행은 아니다.

공개 Bake/Save는 새 참조2개에34키를 저장했고 기존20파일은 불변이다. 별도 CanonicalReload143_1에서 fresh preview와 저장 stamp/키가 같고 실제 등록 Validator가2개 valid·경고/invalid/skipped0, canonical validator 소비1을 확인했다. 최종 Editor316 뒤 CanonicalPublic143_1은 public 메모리 실행과 다음 dirty target 재진입 거절을 확인했고 파일22개는 그대로다. callback 도중 rename/foreign edit와 실제 SavePackage 실패 주입은 수행하지 않아 정적 보강과 정상 경로 검증으로 구분한다.

**실제 혼합.** Canonical143_1 한 번의 기존 조건(200 Player·non-MM NPC32·별도 passive32·합성 cosmetic owner132·2560×1440·Movement·LocalPlayer·기존 품질/예산)에서 strict gate가 통과했다. NPC raw48/GE32/Health320·각 GE1·자연 종료/GameplayPose 보호/budget복귀32·actor/mesh64 정리다. 관측 gameplay 구간 clearance는-76.785..-65.689cm이며142의 기본 live chord와 다른 명시 기하다.142의7miss를 동일 기하의 버그를 고친 증거로 바꾸지 않는다. FX CSV461행·active128/count-cull4·phase5 player/NPC montage 동시17행·shared attempts 최대4, old lease0·다음 native 관측에서 old component132 invalid·owner132 정리·dirty 불변이다. retirement_seconds8.006은 설계된 phase6 유지 시간도 포함하며 native 반환 자체가8초였다는 뜻이 아니다. 전체 editor process peak/private 수치를 FX 증분 메모리로 해석하지 않는다.

**통합·잔여.** Editor315/316 full direct UBT0(58.499/5.373초), Game107 full0(58.648초), 선택 Cook1708+platform skip7·오류/경고0(41.987초)다. Source/Scripts24파일(신규6), Source1243/runtime39·기존20/새2asset 불변, FinalWorkspace143 SHA256 `795f8feb3df414756cfbac3254f2e271c226895a159a4ba27bdab7769258c1a1`이다. mixed와 Built315 provenance가 같고 Cook는 같은 Source와 Game107을 사용했다. 이후 최종 editor-only public baker1파일 보강과 Editor module 변경을 구분하며 같은 최종 binary로 성능을 재측정했다고 주장하지 않는다. 원자료·처음 실패·후보·분리는 Saved/ArchitectureBatch143CanonicalMelee/Verification143.json에 있다.

새 Stage/Shipping·원격 autonomous/multi-piece socket·복잡 montage/이동 기반·particle/ribbon GPU·FX 증분 VRAM·전체 표현 품질·FPS/CCU·운영 provider/소스 엔진은 완료로 세지 않는다. A03–A12의 기존 잔여와 전체 목표는 active다. 다음은 특정 공격 예시를 더 반복하지 않고 기존 제작 validator의 선택 입력/결과가 실제 Cook 시작 전에 소비되도록 연결한다. 현재 일반 Cook wrapper는 DataValidation receipt를 검사하지 않으므로 source 변경 후 stale baked data의 배포 거절은 아직 별도 필수 경계다. 다음 Editor317/Game108이다.


## 144차 선택 제작 검증과 Cook admission 연결

**실제 변경.** 기존 Project_JAssetValidationCommandlet에 최대128개 explicit `/Game/Package.Asset` 선택을 추가했다. Main에서 empty/중복/형상/개수/누락 package를 거절하고 native 선택 hook은 전체 목록이 정확히 존재할 때만 게시한다. native registry metadata startup과 등록 validator·설정은 유지하며 전체 asset load/validation으로 확대하지 않는다. 기본 AssetType 경로와 기존 결과 정책은 유지하고, explicit 모드만 원 선택 수와 checked/requested/passed가 같고 skip/unable/warning/failed0·limitfalse여야 성공한다.

Run-ContinuationAssetValidation의 strict manifest 모드는 한 native process에서 이 선택을 소비한다. 기존 AssetTypes 모드는 그대로다. shared AssetValidation-Provenance helper는 manifest·선언/자동 package 입력 hash·native args/log/실제 종료·Source/runtime의 before/after/current를 연결한다. 대상 package의 실제 `.uasset`/`.umap` 하나를 자동 포함하고 전체 입력512개를 넘기지 않는다. duplicate/unknown JSON·경로 traversal/reparse·누락/모호한 payload를 거절한다. 이것은 선언 입력의 attestation이며 transitive asset closure의 자동 발견이나 검증 보장이 아니다.

기존 Packaging의 Cook/Stage 및 frozen Deployment의 Cook/Package는 manifest와 strict validation run을 필수로 받는다. out/scratch/native 시작 전에 검사하고 Stage/Package는 원 Cook 정상 완료와 compact admission7필드도 비교한다. native 종료 기록을 먼저 보존한 뒤 완료 재검사가 통과해야 admission을 게시한다. 사후 입력 변경은 성공 native exit를 지우지 않고 admission 미완료로 남긴다. 과거 partial/warning 또는 검증 없는 Cook를 성공으로 승격하지 않는다. 선택 manifest 자체는 Cook root를 추가하지 않으며 실제 map/AssetManager/AlwaysCook root는 기존 제작·로드 계약의 책임이다. [사용·범위](../../Maintenance/Change_Completion_Policy.md#선택-에셋-검증과-cook)에 JSON 형식과 명령을 반영했다.

**재현·검증.** 최종 Source의 selected-wrapper 모의49개는 기존11/strict16/admission10/manifest8과 map payload4개다. 소비자 모의14개는 missing/stale/mismatched recipe·null/nonzero/notExited·사후 입력 변화·원 Cook 누락/7필드 불일치·성공 publication을 검사했다. 실제 native 실행은 모두0이며 Stage absolute native는 격리 copy에서 fail-fast로 바꿔 실수로 실행되지 않게 했다. Root의 최종 consumer Source는 같은 byte/의미이므로 해당14개 근거를 재사용했다.

V1 실제 AssetSelection144_1은 선택2/입력6·valid2·실패/경고/skip/unable/limit0/native0이었다. actual missing 및 whitespace로 달라진 recipe는 Cook output/scratch를 만들기 전에 거절했고 Saved 소유 manifest는 원 bytes로 복원했다. V1 Cook1도 정상0이었다. 이후 generic target 자동 입력이 `.uasset`만 가정해 존재하는 `.umap`을 거절하는 실제 helper trigger를 합성 package bytes로 재현했다. 특정 map 예외 없이 정확한 두 filename 중 존재하는 하나를 선택하도록 보완하고 같은 원 trigger가 통과하는 것을 확인했다. 둘 다 존재/둘 다 없음은 native 시작 전 거절하며 실제 UWorld/WP loading의 증거로 세지 않는다. 초기 Saved mock의 배열 grouping 오타와 원 실패도 보존했다.

그 보완 때문에 도구 Source 지문이 달라져 기존 V1 기록을 최신으로 재사용하지 않았다. 최종 AssetSelection144_2의 native 선택2·입력6·valid2·경고/skip/unable/limit0/native0와 최종 Batch144ValidatedCook2의 Cook1708/platform skip7·오류/경고0·actual HasExited/exit0·완료 후 지문 재검사/admission publication을 확인했다. 두 대상은 기존 AssetManager/AlwaysCook 범위이며 Cook ReferencedSet에 package 이름도 있다. 같은 조건을 성공할 때까지 반복한 것이 아니라 실제 파일 선택 결함 이후의 Source/도구 identity를 새로 검증한 것이다.

**보존·잔여.** Editor317 full direct UBT0(7.643초), runtime C++ 변경0이므로 Game107을 재사용했다. Source/Scripts8파일(신규 helper/consumer test2)·Source1245/runtime39·보호22asset 불변이다. Editor module1개만 바뀌며 최종 Source/runtime는 최종 native의 before/after와 같다. FinalWorkspace144 SHA256 `01edc9dc4dbac16d0c80f81e53d6701d8b1948330865df10493424156740acad`, 이전 V1 Final/Built snapshot과 실제 근거는 Saved/ArchitectureBatch144CookValidation/Verification144.json에 보존했다. 신규 manager·서비스·게임 예산/품질 조정·NPC MM·플레이어 graph 변경은 없다.

실제 성공은 selected native와 Packaging Cook 경로다. Stage/Package와 frozen Deployment의 native 성공, 실제 map/WP external actor, plugin/engine package 직접 선택, 전이 의존성의 완전한 입력 선언, 임의 concurrent writer에 대한 filesystem lock, 새 packaged/Shipping 실행·FPS/CCU·GPU/VRAM·미정 운영 provider/source-engine은 미완료다. A03–A12 및 전체 목표는 active다. 다음은 최신 공통 패키지의 실제 Cook root·Stage·로드 소비를 기존129/138/143/144의 유효한 근거와 합쳐 확인하며, 새 전수 감사나 동일 군중 실행을 요구하지 않는다. 다음 Editor318/Game108이다.


## 145차 최신 공통 패키지와 실제 소비 연결

현재144 Source/Game107을 기존 공통5map에 통합했다.144 Cook2는 Entry 전용이며 crafting/gathering/MainWorld는 참조집합에 없었다. NPC BP·비모션매칭 graph·canonical 공격/activation은 이미 exact 참조집합에 있어 추가 hardcoded prototype Cook root나 새로운 archetype을 만들지 않았다. 기존129의 정확한5개 map과 정상 AssetManager/AlwaysCook closure를 사용했다.

**실제 통합.** 기존 AssetSelection144_2 native2/입력6의 같은 strict 기록·현재 Source/runtime를 소비한 Batch145CommonCook1은 Cook3183+platform skip7·오류/경고0·정상 native 종료와 post-check를 통과했다. 같은 Cook identity의 Batch145CommonStage1도 AutomationTool native0·post admission 통과다. 새 inner main exe와 현재/검증 Game107의 SHA256은 `88666835395c25f9eed072f545499f7b68eac0e68d1ab038339407c2052d77d5`로 같다. 패키지는48파일/3,353,873,272bytes이며 inventory는 완료된 모든 native 실행 후 작성했다. 첫 실행 전후 전체 바이트 불변 비교로 주장하지 않는다.

Source fixture를 추가하지 않고 기존 packaged Count1/NPCCombatCount1/AuthoredPath/Movement/LocalPlayer로 실제 reference 정의→typed source grant→정확한 non-MM graph→native 구간/피해→자연 종료·budget 복귀·exact 정리를 검사했다.34키/stamp 일치, raw23/GE1/Health10·자연종료1·GameplayPose 보호/복귀·actor/mesh2 정리·dirty 불변·native0다. raw contact는 dispatch 사이 반복 가능하며 GE1과 구분한다. cooked host에 미지원인 PlayerPoseReadiness나 FX132·새 trace/품질 플래그를 추가하지 않았다.143의 엄격한200 Player/NPC32/FX132는 그 입력의 기존 근거로 재사용했다.

같은 패키지의 퀘스트와 제작은 각각 기존 별도 listen+2client/100 meshless AOI probe/NoTrace 소켓 cohort로 통과했다. 두 cohort 모두 actual native0×3·timeout0·바이너리 불변·source 지문 일치다.100 probe를100개 실제 animated NPC로 세지 않는다. 퀘스트는 owner-only 진행/추적과 foreign data 거절, 제작은 synthetic job 요약/실제 no-provider 거절/source 퇴역이다. 운영 provider·영속 자산·새 canonical remote multi-piece 전투나 dedicated production server를 검증한 것은 아니다.129의 정상 재접속/생활 전체는 이전 입력의 유효한 근거로 보존하고 같은 시나리오를 다시 실행하지 않았다.

**보존·잔여.** Source/Config/에셋 변경0·새 C++ 빌드0이며 Source1245/runtime39·보호22asset 모두144와 같다. FinalWorkspace145 SHA256 `01edc9dc4dbac16d0c80f81e53d6701d8b1948330865df10493424156740acad`, 입력·Stage/container lineage와 개별 native 결과는 Saved/ArchitectureBatch145CommonPackage/Verification145.json을 따른다. 새 MainWorld RHI/연속 HLOD·물리 I/O/resident/GPU/Nav·고속/동시 이동·전투 원격 canonical/소스 엔진 수명·Shipping·FPS/CCU·운영 DB/auth/서버는 미완료다.

145 시작 추정12–20시간(중심16)에서 실제 공통 패키지 묶음2–4시간을 제거해 현 환경 필수 잔여10–16시간(중심13)으로 갱신했다. 원래 범위·품질은 유지하고 신규 전수 감사나 선택 확장으로 종료점을 자동 연장하지 않는다. 다음은 A10 준비/복제 수명과 남은 성능·자산 로드 경계를 기존 근거로 마감한다. 전체 목표는 active, 다음 Editor318/Game108이다.
