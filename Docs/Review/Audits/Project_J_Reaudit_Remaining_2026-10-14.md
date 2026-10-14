# Project_J 재감사 — 상세 결과와 남은 문제

이 문서는 [재감사 변경 기록](Project_J_Reaudit_2026-10-14.md)과 [상태·실행 지도](Project_J_Reaudit_Map_2026-10-14.md)의 후속 장부다. 요청 범위는 전체 시스템이다. 파일 목록, 본문 읽기, 호출·소유권 대조, 실제 수정, 실행 검증을 합쳐 전수 정밀 완료라고 계산하지 않는다. 자동화 통과와 MCP 조회 성공도 모든 에셋·실행 분기의 무결성을 뜻하지 않는다.

## 중단 후 추가 개선

| ID | 분류·근거 | 실제 조치 | 검증과 한계 |
| --- | --- | --- | --- |
| RA20 | 잠재 GC 위험 보완. Mass의 Tick/Unregister/Stop이 외부 actor callback 뒤에도 실행되지만 manager Owner는 weak이고 이전 종료 시험은 fixture가 bridge/NPC를 pin했다 | 실행 중인 subsystem과 소유권 callback의 NPC를 GT 범위에서 pin. 저장소 종료 지연과 worker join 계약 유지 | MassGCBuild1 직접 UBT 269.61초 성공. 실제 demote/unregister callback에서 engine world 종료 후 fixture pin을 내려 GC한2경로와 기존 non-GC2경로가 MassGCFocused1에서 clean·exit0. 수정 전 GC 충돌 재현으로 주장하지 않음. 초기 focused fixture의 별도 processor root는 최종 검증 전 내려 제품 보유 계약을 더 엄격히 시험함 |
| RA21 | 같은 수명 계약을 TargetScoring, VisualAsset, NPCPath, NPCDecision, PresentationBudget에서 대조. 외부 완료/적용 callback 뒤 subsystem의 queue·stats·scope guard에 접근 | GT delivery/step의 subsystem 및 callback 참가자만 pin. worker 캡처·token/epoch·취소 완료·FIFO/admission 계약 유지 | CallbackGCBuild2 272.55초 성공, CallbackGCFocused1 20 clean·경고/실패/미실행0·exit0. scoring serial, visual expiry, 실제 no-nav 실패 전달에서 world 종료→GC→늦은 완료 차단 검증. decision/presentation의 모든 강제 GC 조합은 개별 재현하지 않음 |
| RA22 | MM 예산 접근자 3개가 Source/Plugins/Scripts/Config에서 선언·정의 외 호출 없음. UFUNCTION·virtual·반영 필드 아님 | Hidden/Mid/Far 개별 getter 제거. 공통 GetEffectiveAnimationBudgetSettings와 실제 cadence 소비자는 유지 | CallbackGCBuild2와 focused 회귀 통과. 실행 비용 개선 수치로 주장하지 않음 |
| RA23 | Editor-Cmd의 -game/-server에서 EditorOnly Toolset Python 초기화가 누락된 editor UClass를 찾음. 각 원격 시험에 45개 오류 행 | 원격 검증 wrapper에서 engine가 지원하는 DisablePlugins로 EditorToolset/AutomationTestToolset/ToolsetRegistry를 해당 game/server 실행에만 제외. 일반 Editor/MCP 설정 유지 | 기존 Network4–6은 통과했지만 오류45행을 그대로 보존. 새 wrapper 실행 검증 대기. 엔진 plugin 자체를 수정한 것으로 기록하지 않음 |
| RA24 | 실제 EarlyTransition notify는 RequestAuthoredEarlyTransition을 호출. 이전 Begin/End native 함수는 호출자가 없는데 depth 필드와 snapshot OR 분기는 남음 | Begin/End·depth·초기화·OR 분기 제거. 현재 held clip·gait·snapshot revision이 전이 권한을 소유. Blueprint getter는 worker snapshot 값을 계속 읽음 | 현재 최종 직접 UBT 진행. 기존 AuthoredConditionAndOwnership 및 전체 회귀 결과를 추가할 예정. 현재 정상 클립에서 버그를 재현한 것으로 기록하지 않음 |
| RA25 | GAS TryActivateAbilitiesByInputTag는 선언·정의만 남은 비반영 native API. 실제 입력은 AbilityInputTagPressed/Released와 실행 component를 통과 | 미사용 직접 활성화 loop 제거. input pressed/released와 GAS 활성화·예측 경로 유지 | 최종 UBT·기존 입력/GAS/원격 회귀 검증 대기. 공개 확장 port들을 일괄 삭제한 작업이 아님 |

CallbackGCBuild1의 첫 오류는 새 fixture가 기존 private ExpireForTest를 호출할 friend 등록을 빠뜨린 C2248이다. 빌드를 끝까지 정상 종료한 뒤 시험 클래스만 등록했다. 제품 충돌이나 dotnet 예외로 분류하지 않는다. 원래 실패 로그도 보존한다.

## 완료된 RA19 통합 근거

RA19 고정본은 WholeLifecycle1 351개(331 clean·20 warning), 실패·미실행0·exit0이다. GameBuild3 200.39초 성공, Cook2 1361개 cooked와 플랫폼 제외7개·0error/1warning·exit0, Packaged2 stage 36.52초·exit0이다. Cook의 경고는 기존 GameplayCueNotifyPaths 미지정이다.

CookedDamage2의 실제 cooked 공격 데이터/GE/중복 타격 차단, Reaudit_Combat2의 실제 입력·GAS·montage·notify·해제 3회, Reaudit_UI2의 1280×720/1920×1080 동적 부하, Reaudit_Render2의 실제 표시 body200, Network4–6의 실제 접속2+NPC200 반복은 정상 종료했다. 추가 RA20–25 소스의 결과로 이 자료를 계산하지 않는다.

UI2는 각600 numeric frame에서 초기120을 제외했다. UI GT 평균/p95는 1280×720 0.5514/0.7539ms, 1920×1080 0.5619/0.7974ms다. 패키지 동작 확인이며 교대 A/B 성능 개선 증명은 아니다. Render2의 실제 동시200 montage 182프레임은 평균24.7965ms(40.33FPS), GPU10.2964ms; 종료 후240프레임은14.5442ms(68.76FPS)다. 실제 소켓 접속은0이다. 캡처는 멀리서 본 부하 장면이며 관절·의상·모든 클립 품질 검증이 아니다.

Network4–6의 각5단계 connection count는2, 세 프로세스 exit0, timeout0이다. 각 실행에서 client2개×actor2개×trial3개인12 관찰 묶음 모두 R·target·end를 관찰했다. lag50ms/loss20%의 localhost 시험이다. NPC200을 원격 접속200으로 계산하지 않는다. 상세 raw/manifest/process/log/CSV는 Saved/Validation 아래 실행별 디렉터리에 있다.

## 미해결·추가 검증 장부

| 항목 | 구분 | 근거와 다음 확인 |
| --- | --- | --- |
| 200명 CPU 일부 증가 | 확인된 미해결 측정 결과 | RA13 full binary A/B/B/A 64행에서 일부 평균 증가가 남음. 별도16 trace region은 반대 방향이며 ABA/MM 실제 수행량도 달라 원인 확정 불가. NullRHI CPU, rendered FPS/GPU, 공격 후 복귀, 실제 접속 수를 구분한다. 새 GC 보완을 CPU 해결로 계산하지 않는다 |
| LowerBody review installer의 실패 원자성 | 잠재 위험 | InstallLowerBodyInMasterForReview가 upper 설치 뒤 lower topology를 확인. 일부 실패는 이전 upper 변경을 남길 수 있음. 실제 Master의 경로 제한을 보존한 transient graph/transaction 회귀와 전체 rollback 설계가 필요. 실제 에셋 파손 재현·수정 완료 아님 |
| 비정상 제작 숫자 | 잠재 위험 | WeaponMotion key/time/transform·blend/IK alpha, OneShotInput cancel-angle/reselect tuning은 모든 runtime/제작 validator가 같은 finite 계약을 갖는지 추가 대조 필요. 이번 NaN movement/ground contact 수정과 실제130 DataAsset 통과는 이 모든 입력을 보증하지 않음 |
| follower 교체 시 동일 정책 | 잠재 위험 | fallback follower의 changed-policy 조건이 같은 정책을 가진 새 mesh에 적용을 생략하는지 별도 actor/mesh 교체 회귀 필요. RA08 resolver/cache 개선을 모든 교체 경로의 증명으로 사용하지 않음 |
| GameplayCue 경로 | 확인된 경고·제안 | 실제 Cue asset registry closure를 확인한 뒤 검색 경로를 좁혀야 함. 임의 폴더 지정으로 경고만 숨기지 않음 |
| 전체 에셋 제작·시각 품질 | 미검증 범위 | MCP 관련 연결·native authored/validator·Cook 검증과 모든 Skeleton/클립/Chooser 행/PoseSearch index의 수동 품질 검토는 다름. 가까운 손/발/의상·cloth·audio·모든 장비/몸체 조합은 전부 검증하지 않음 |
| 실서비스 네트워크·200접속 | 미검증 범위 | localhost2접속, memory repository/loopback handover, catalog/port는 실제 HTTPS backend·지속 저장·분산 권위·WAN·독립 Server target·200socket 검증이 아님 |
| 모든 source 함수/헤더/시험 분기 | 미완료 검토 범위 | 장부607개와 구현 본문 추가141개·명명된 계약 대조를 전 함수 전수 정밀 완료로 승격하지 않음. low-reference 토큰 후보도 반영/override/확장 계약을 대조하기 전 미사용 코드로 확정하지 않음 |

확인된 결함은 native 재현·실제 호출·상태 소유권을 근거로 수정한다. 잠재 위험은 재현 여부와 검증 범위를 먼저 기록한다. 제안은 설치·운영 중인 기능처럼 표시하지 않는다. 사용자 리타깃/Body/RTG/공격 DA 교정, 정상 OTM/Strafe·Run/Sprint·단발/MM 복귀·Land·측면 후보·전투·장비·지속 곡선 0.35초·100cm·3도·85%를 보존한다. commit·push·merge·컴퓨터 종료는 별도 최신 요청 없이는 실행하지 않는다.

## RA25 전체 검증에서 추가 관찰한 문제

WholeRefinement1은352개(331 clean·21 warning), 실패/미실행/inProcess0·665.64초·exit0이다. 재감사20개는 모두 clean이며 strict Mass GC에서 별도 processor root도 내렸다. 새 경고1은 StrafeForward.ProductionData의 PoseSearch SearchIndex가 index14의 누락 entry를 참조한다는 메시지다. 원래 production Cycle은70 entry, Forward와 시험의 transient Expected는14 entry다. 시험은 원래 Cycle을 DuplicateObject한 뒤 Expected의 entry를 제거/추가하므로, 복제된 derived index/normalization 관리가 비교 시험에 끼어드는지 조사 중이다. 실제 저장된 Cycle 에셋 손상으로 단정하거나 DDC를 지우지 않는다.

Network7–9는 모두 실제2접속·NPC200·세 process exit0·timeout0으로 통과했다. EditorOnly plugin 제외 후 Python startup 오류는 실행당45행에서15행으로 줄었지만 ToolsetRegistry의 PythonTestRunner 오류가 남았다. 부분 개선이며 완료가 아니다. engine는 공식 DisablePython 실행 플래그를 제공한다. game/server host의 Python 자체가 필요한지 확인하고 이 실행에만 제외하는 후속 검증을 진행한다. 일반 Editor/MCP는 유지한다.

## 검증 계약 보완 RA26/27 — 진행

RA26은 원격 game/server host에 공식 DisablePython 플래그를 적용한다. EditorOnly plugin 제외만으로 ToolsetRegistry 초기화 오류15행이 남았으므로 Python 시작 자체가 없는 native 실행 환경을 명시한다. 일반 editor/commandlet/MCP 구성은 바꾸지 않는다. 새 실행 검증은 진행 전이다.

RA27은 Forward production-data 시험의 불필요한 UPoseSearchDatabase 복제·entry 수정 경로를 제거하고 native entry 값 배열을 비교한다. 수정 전 ForwardIndexRepro1은1 clean·exit0으로 집계됐지만 시험 완료 뒤 index14 경고를 로그에 남겼다. 따라서 report clean을 로그 무결성으로 해석하지 않는다. 원래 Cycle70·Forward14와 Cook3의 canonical source cache key(3ba808...)를 기록했고, 경고의 transient 비교 key(8ff351...)와 구분했다. 원본 에셋의 실제 파손·production 충돌을 확정한 것이 아니다. 기존 async cache 상태가 있는 UObject를 비교 모델로 만들 필요가 없다는 호출·값 소유권 근거로 제거했다. canonical Cycle와 Forward index의 readiness·entry range·animation 연결을 직접 확인하도록 강화했다. 캐시 삭제·강제 reset·warning 기대값 억제는 추가하지 않았다. 빌드·새 프로세스 반복·통합 로그 확인 대기다.

RA25 패키지3은 GameBuild4 203.57초·Cook3 0error/1warning·35.94초 commandlet·정상 stage/실행이다. 실제 피해·전투3회·UI2해상도·200 표시 body 확인을 마쳤다. UI GT 평균/p95는1280×720 0.5737/0.8108ms,1920×1080 0.5900/0.8532ms이다. Render3 동시200 montage167프레임은27.1149ms(36.88FPS),GPU10.6211ms; 복귀219프레임15.8822ms(62.96FPS)이다. 이전 Render2보다 낮은 FPS이며 교대 비교/열·스케줄링 통제 없이 원인 또는 회귀로 단정하지 않는다. CPU·렌더 성능 해결 완료가 아니다.

추가 검토만 한 범위: project-module 의존 DAG, Core-only MMO 값 계약, editor 제작 module guard, 공유 notify의 per-mesh/per-instance token을 대조하고 유지했다. Begin에서의 전체 weak mesh 표 순회는 동시 시작 부하 후보로 기록했다. 전체 GC/notify 실패 조합·실제 메모리 누수나 CPU 증가의 원인으로 확정한 것이 아니다.

## 최신 확인 상태

권위 최신 결과는 [현재 검증 기록](Project_J_Reaudit_Verification_2026-10-14.md)이다. 앞선 대기/진행 문구는 해당 단계 기록으로 남긴다. RA27 최종 직접 UBT 266.99초 성공, 전체352개(332 clean·20 warning) 실패/미실행0·665.34초·exit0, 재감사20개 clean이다. ForwardIndexFixed1–3 새 프로세스 모두 clean·raw index 경고0, canonical70 index asset·5288 pose를 확인했다. 최신 network10–12 각 실제2socket·NPC200·3process exit0·timeout0·Python오류0, 각12 R/target/end 관찰 묶음이 완성됐다. generic automation clean을 전체 로그/에셋 무결성으로 승격하지 않는다.

일반 Editor MCP에서 Cycle/Forward schema·normalization·Master/Body native 부모·Body RetargetProfile 실제 연결을 읽기 전용 확인했다. private databaseAnimationAssets 조회 거절과 CDO parentClass 대상 불일치를 원응답으로 남기고, 공식 BlueprintTools.get_parent와 공개 property만 사용했다. entry/index 검증은 native 공개 API 결과로 구분한다. 에셋 저장0, 정상 editor exit0이다.

독립 Project_JServer.Target.cs는 실제 존재한다. ServerBuild1 직접 UBT는0.80초·normal completion·exit6, 최초 오류는 현재 engine distribution의 Server target 미지원이었다. 엔진 경로/파일 변경이나 우회는 하지 않았다. 기존 Editor-Cmd server-mode의 실제 소켓 시험과 native Server binary 운영은 다른 범위다.
