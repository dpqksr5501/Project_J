# 전체 시스템 상세 대조와 결함 처리 — 2026-10-15

> 최신 사용자 범위 변경: [소스·아키텍처 정리](Project_J_Source_Architecture_Cleanup_2026-10-15.md)가 현재 우선 목표다. 아래 전체 감사의 성능·운영·품질 전수 해결은 보류하며 기록은 보존한다.

전체 감사는 미완료다. [최신 검증](Project_J_Continuation_Audit_2026-10-15.md), [소스 범위 장부](ContinuationEvidence_2026-10-14/CoverageLedger.json), [분리된 작업 대기열](ContinuationEvidence_2026-10-14/ContinuationWorkQueues.json)을 함께 사용한다. `source_review_track`은 실제 읽은 함수·header·inline·caller·권위/소유권 범위이고, `defect_execution_track`은 재현·수정·회귀 상태다. 소스 대조가 끝난 범위를 에셋/실행/환경까지 검증 완료로 승격하지 않는다. 대조 결과는 읽기 전용 서브에이전트 3명이 수집했고, 수정·MCP·빌드·실행·저장은 주 작업자만 수행한다.

| 시스템 | 이번 명시 소스 대조 | 발견·남은 문제 | 필요한 검증 |
|---|---|---|---|
| 탑승·비행 | Mount runtime23파일의 함수/header/inline·기존 회귀, Player summon/interaction·mounted layer/snapshot caller | attach 퇴역·replicated A→B·phase tag 재진입 실제4실패 재현/수정·집중24통과. clone→새session·보호phase 서버하차·native dead 공급·중첩 복원 별도 | 관련24집중 23clean/기존warning1 통과; controller 교체 재현/수정 포함, 실제 두 rider 복제·GC·ABP/손/cloth 품질 |
| Backend·인계·지속 상태 | Gateway/connection/manager/loopback·MMO foundation·PS/GameMode, Base/Player 지정 level·snapshot caller | 음수 큐·비유한 설정/zero-delay retry, Revision0 owner 승인. 새PS/DB/auth/fencing adapter 설치 증거 없음 | 잘못된 입력/동기 재시도·초기화실패/legacy 호환 집중, 동일identity 새PS 이관·실서비스 환경 |
| Inventory·Equipment·grants | component/manager/runtime·AbilitySet/ASC·definitions/types·WeaponPresentation main·layer leases와 실제 calls | consume/bag callback 수명, lock 중 item 소실·unlock receipt·legacy RPC, default basename grant, spawn identity·alpha·linked lifecycle | 공통 OnGive/apply/end·notification 교체/GC와 item/slot 보존, 원격 FastArray·제작/시각 |
| Quest·Social·UI/HUD | Quest/Social/PS projection·UI model/profile/status/map/icon·HUD/window/widget·interaction의 명시 전체 cpp/header | release/availability·press/drag origin, failed UV, retired social/current interactor. 기존 지급commit·projection GC는 재사용 | 동일index 새press/다른source·BP callback·GC, 실제 paint/focus/capture/DPI/Widget BP·owner-only RPC |
| NPC·AI·Mass·Path | NPC/attack/Mass4쌍/Path/Core worker 및 Action resume/timer·Decision 확장 | NPCAttack activation callback, numeric tuning. 실제 Mass bridge·stats consumer 설치 미확인 | 실제 montage cancel/restart·GC, authored nav/BT/GA·opt-in pursuit/Mass 성능. 기존 cancel/physical slot·retirement 회귀 재사용 |
| GAS·플레이어 전투 | GAS12파일·skill execution/router·Sprint/Toggle/Melee·hit/SSR/state/presentation·두 notify | input lease→activation epoch, Sprint handle 후게시·Melee/NPC transaction, notify map/hit batch | 실제 cancel/restart/GC·prediction key·foreign/new lease 보존, 기존 Run/Sprint/Land/SSR·원격 전투 |
| Animation·MM·retarget | locomotion main/split/header·proxy/snapshot/search/continuity/trajectory·복제/ABA·IK/facing의 명시 범위 | 종료 timer lease·상하위 Jump/Land receipt, nullable Cycle, 반환 Chooser 순환 | stream-out/새전환·GC, null/정상 companion·bounded chooser, 실제 ABP/PSD/schema/curve/socket·close-up |
| 제작·Editor·Cook | bundle/menu/Persona/helper/validator/migration/graph installer·Config 소비, experiments 여섯 내부·test 제작 flag 경계 | Escape는 값복원 아님, preview root/child 좌표, migration partial-save/path·bundle 승인 범위. 비활성 GPU poll race·시험 취소 cleanup 후보 | 실제 editor transaction/저장실패·복구/graph compile, 정확한 soft dependency 로딩. configured Cook≠CookAll/DataValidation |
| 기반·Controller·network | InputLease/Message/Core asset services/Controller/filter/prioritizer/diagnostics·significance 등록과 실제 caller | significance Update 설치 못찾음·역점수 다중view/worker UObject, custom Iris/helper·pool은 실제 설치와 구분 | 실제 관찰자 이동·GT snapshot→계산→GT apply, 실제2socket/200NPC와 200connections 분리 |
| CPU·렌더·부하 | 기존 NullRHI ABBA/trace·D3D12 ABBA 원자료 재사용 | CA16 helper 비용 감소와 전체 CPU/렌더 차이 해결은 다름. 새 significance 설치 후보의 원인 기여 미확정 | 같은 조건 반복/교대·단계 trace, processCPU/world tick·FPS/GPU·표시 수/실제 socket 수·opt-in 시스템 분리 |

소스 경로 약칭 때문에 아직 장부에 연결되지 않은 행은 `UnmappedSourceReview.json`에서 실제 미검토와 기존 읽기 매핑 누락을 다시 구분한다. 파일 수·검토 단위 수·자동화 성공은 전수 정밀 완료율이 아니다. 테스트/실험 내부, 큰 Player의 범위 밖 함수, 모든 실제 에셋 graph·제작/검증/배포 및 환경·품질은 남은 대조다.

CA19 이후 최신 Source에는 추가 수정이 있다. 전체389·Game/Cook/package·combat3·socket2/NPC200의 완료 기준점은 `R15_CA17_18_Integrated1`/`R15_CA17_18_Verification1`이며, 이후 변경의 결과로 확대하지 않는다.

빌드 지연은 소스 변경 없는 `R15_IncrementalProbe1`의 실제386actions/253.20초에서 확인했다. Trace-level ActionGraph는 물리적으로 존재하는 shared PCH dep.json 두 개를 partition 미일치로 읽지 못하고 outdated로 판정하여 하위 action으로 전파했다. 설치 UE5.8의 dependency cache는 AppName 경로를 mount하고 해당 project shared PCH는 target 경로를 사용한다. 반복되는 ‘no existing makefile’ 한 줄만으로 clean build나 캐시 삭제를 단정하지 않는다.

공식 `-NoSharedPCH`의 작은 MMO 첫 전환8actions/5.70초→동일 조건0actions/.94초, Mount 첫 전환46actions에서 드러난 기존 UWorld 직접 include 누락을 보완한 뒤3actions/8.98초, 호환 수정10actions/20.02초를 확인했다. 전체 Editor 전환331→108→13→4actions에서 직접 include를 보완했고 정상 빌드13.63초 후 같은 소스/옵션 전체 target 재빌드는0actions/.93초다. 모듈 범위 결과와 full target은 별도 receipt로 유지한다. 최신 Editor9는 probe 전에 보존했고 엔진/캐시/ACL을 변경하거나 빌드를 중단하지 않았다. 오래된 UBA session 정리의 access-denied 로그를 우회하지 않는다.

유지하는 계약은 OTM/Strafe·Run/Sprint·단발/MM·Land·측면 후보·전투/장비·retarget와 지속 곡선 **.35초/100cm/3도/85%**, 원래 GAS/worker/GC·cancel/physical completion·server/prediction 권위다. 새로운 강제 reset·timer·특정 키/클립 예외로 결함을 가리지 않는다. 기존에 목적이 있는 authored fallback·UI debounce·suspension recovery timer는 별도로 유지 이유를 기록한다.

2026-10-15 추가 본문 대조에서 production Foley/PresentationBudget·Experiments 및 검증/제작 fixture의 정확 manifest를 `AdditionalReadScopes.json`으로 연결했다. 시험이 가진 실제 seam·명시 flag·cleanup/실패·품질 한계를 기록했으며 과거 Success를 현 소스 재실행으로 계산하지 않는다. CPU/Naturalness latent 폐기 cleanup, EffectsCooked 종료 시 임시 override 복구, RenderedCrowd 공격 성공조건, CookedCombat의 definition 존재를 hit-window로 오인한 assertion은 새 검증 도구 후보다. 기존 CA16 원자료에는 네 실행 모두 실제200 montage 구간이 있고 보고 평균도 재계산과 일치한다.
