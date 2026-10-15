# Project_J 계속 감사 — 23시 경계 재개 기준점

**전체 감사는 미완료다.** 사용자 요청에 따라 오늘은 검증한 통합 기준점을 보존하고 남은 전체 범위를 이후에 계속한다. 이 문서는 기존 인계를 대체하거나 사용자 변경을 덮지 않는다. 재개할 때 현재 Git/프로세스/산출물 SHA부터 다시 확인한다. 새 commit/push/merge/컴퓨터 종료는 별도 최신 요청이 필요하다.

[시스템별 실제 변경·검증·성능·남은 범위](../../Review/Audits/Project_J_Continuation_Audit_2026-10-14.md) · [구조·소유권 지도](../../Review/Audits/Project_J_Continuation_Structure_Map_2026-10-14.md) · [명시 범위 장부](../../Review/Audits/ContinuationEvidence_2026-10-14/CoverageLedger.json) · [원자료 사본/SHA](../../Review/Audits/ContinuationEvidence_2026-10-14/EvidenceIndex.json)

## 실제 Git와 보존

기준점 HEAD **855a1c8e111c94a5dffdd08ad05c68bdea2accec**, branch `codex/runtime-retarget-profiles`를 실제 확인했다. 인계 전에 요청한 기준점 commit은 이미 존재한다. 이후 commit/push/merge/종료를 하지 않았다. 오늘 변경은 working tree에 있다. 시작한 10/14 사용자 인계의 미커밋 요청문 삭제를 `InitialUserChanges.patch`와 현재 해당 파일의 Git diff로 직접 대조해 동일함을 확인했다. 그 파일을 수정하지 않았다.

`Saved/Validation/Continuation_20261014/FinalCheckpoint1`의 실제 source/Editor/보존/Content/Git/process 확인을 먼저 읽는다. 13:47 UTC 확인은 latest runtime Source/Config/descriptor636개 mismatch0, Editor9 mismatch0, 보존13 일치, Content Git changes0, 호스트 CIM 관련 active process0이다. 문서/계측·인계 파일을 뒤에 정리했고 그 뒤 runtime C++/Config/에셋은 바꾸지 않았다. 마지막 확인은 별도 종료 기록을 따른다.

OTM/Strafe·Run/Sprint·단발/MM 복귀·Land·측면 후보·전투·장비·리타깃과 지속 곡선 **0.35초·100cm·3도·85%**를 유지한다. native fixture는 actual BP/DA/ABP를 읽거나 transient clone만 사용했으며 asset save0이다. `(1)` package3개를 무용한 복제로 삭제하지 않았다.

## latest compiled 기준점

`Saved/Validation/Continuation_20261014/CA01_13_Integrated1`: frozen source694(당시 Scripts 포함)/Editor9/보존13. `EditorSet`에 complete9파일을 보유한다. 이후 Scripts/문서 변경은 컴파일된 runtime 변경과 구분한다.

| 단계 | 실제 기록 |
| --- | --- |
| 마지막 Editor directUBT | RewardTransportFixBuild1 정상 성공281.33초 |
| 수정별 집중 회귀 | IntegratedFocused1 **38 clean**, fail/warn/notRun/inProcess0 |
| 통합 전체 회귀 | WholeContinuation5 **373=353 clean+20 warnings**, fail/notRun/inProcess0,667.413초. 이전362 모두 포함/새11. 경고20 이름/message 정확히 동일 |
| Game directUBT | CA01_13_GameBuild3 정상 성공210.50초, Source/Config/descriptor636개 frozen 일치 |
| Cook | CA01_13_Cook3 exit0,cooked1361/inc0/platform skip7/error0/Cue warning1. configured Windows default map+Entry+AssetManager/AlwaysCook closure. CookAll 아님 |
| 패키지 | CA01_13_Package4 exit0. actual native exe와 direct UBT Game SHA 동일 |
| 최신 cooked exe SHA | **26098d789b055b9e3e54d7832311d391e8d08e24216aa6960e652bcfe37e8cea** |
| cooked 실행 | CookedCombat2 단독3cycle pass; CookedUI1 두 해상도1000행/24buff/4window/50-step 각600frames pass/logError0; CookedRender1 actual rendered body200/R/복귀 pass |
| 원격 | CA01_13_Remote1 Editor host server+real socket2·NPC200·lag40/loss2,AOI/FastArray/animation/CombatInput passed,process3 exit0,timeout0/Python startup error0. 200socket/독립 nativeServer/모든 신규 statGE 복제 완료가 아님 |

첫 Insights 분석이 아직 running일 때 다음 분석은 guard로 시작 전에 차단됐다. 그때 먼저 시작한 CookedCombat1은 분석과 겹쳐 성능/PSO 비교에서 제외하고 원본·`ValidationOverlapReceipt.json`을 보존했다. 두 실행 모두 정상 종료까지 기다렸다. Insights 종료 후 단독 CookedCombat2를 재검증했고 cooked-combat guard에 Insights를 포함했다. **어떤 실행 중 빌드도 중단하지 않았다.** 기존 ProgramData 반복 거절로 멈춘 build만 앞선 사용자의 한정 종료 허용으로 처리했고 그 허용은 소진됐다.

## 시스템별 진행

실제 추가 수정 CA01–13: follower authored tick 대여/반환, 무기·단발 numeric admission, AbilitySet effect level, native removable 장비 stat GE, operation GC/InputLease/Message, 장비 definition grant identity, active Router/Move reentry target/revision, Editor facing installer whole-plan/rollback/compile, handover actor/session/value packet/finite clock, shared notify post-GC retirement, Quest reward tracking rollback, server timing tuple/loopback finite admission. 각 실제 원본 실패·최종 변경 이유·보존 이유는 본문/장부의 해당 ID를 따른다. 강제 reset/timer/특정 key·clip 예외로 새 증상을 가리지 않았다.

명시 범위 검토 후 유지: CR01 Foundation의 close/완료/원래 replay receipt·미설치 port, CR07 Core scoring 값 worker/GT current epoch·advisory commit, CR09 UObject 없는 future pool policy. CR11 NPC 모든 관련 component/scheduler/path/attack 함수·header/inline 및 opt-in sandbox를 묶어 대조했고 engine actual completion 슬롯/owned move/attack·Mass suspension 계약을 유지했다.

624 source rows/22 review units는 작업 범위이며 전수 완료 수가 아니다. 각 행의 whole-file/whole-project 완료 주장은 false다. 모든 source 함수/inline/native/reflection/BP/실제 호출·제작·검증·배포·실패가 아직 닫히지 않았다. 시스템별 지정 검토와 실제 수정의 완료를 전체 시스템 완료로 확대하지 않는다.

## 성능 원자료와 미해결

`CA_CPU_Integrated_ABBA1`: RA27 대 latest complete9 A/B/B/A,NullRHI body200/socket0,repeat2,warmup240/measured600,sim60,budget2ms,64행,latest9 복원. world tick delta Near off/on **+7.521/+3.375%**, Attack **+5.174/+6.333%**, Far **+4.783/+2.325%**, Hidden **−0.049/−0.084%**. Near/Attack ABA off 관찰량은 동일. ABA on bone/visual 수 일부는 다름. B2 drift 존재. 전체 OS CPU 누적은 ProcessCpu.json이며 setup/warm/측정/종료를 포함해 native window·thread wait와 다르다.

`CA_CPU_Integrated_Trace1`: 별도120/120 pair,20 region. actual PCA/KNN111113→110910,nodeUpdate478992→478376. world3840 incl59.518182→62.279522초. worker/physics/wait 증가·new role/demand 호출은 관찰했지만 old timer 부재를 비용0으로 계산하지 않고 nested instrumentation의 exclusive 재분배를 구분한다. 실제 검색이 늘어난 것은 아니다. 다음 원인 조사는 matched 부모 scope·per-operation role 호출·snapshot/worker/물리·wait/환경을 더 분리해야 한다. worker/wait 누적을 critical path나 OS active CPU로 합치지 않는다.

`CA_Render_Integrated_ABBA1`: same camera/quality,Editor-host D3D12 offscreen1280×720,rendered200/socket0,one R burst. full200 mean frames A1/B1/B2/A2 **30.602/31.954/29.893/38.160ms**. aggregate−10.054%지만 run variance가 크므로 개선 확정이 아니다. GPU 별도12ms 전후/delayed. 이전 packaged Render2/3과 직접 합산하지 않는다. grid PNG로 모든 hand/cloth 품질을 검증할 수 없다. hardcoded 옛 CA01–03 label은 companion IntegratedPerformanceContext.json의 actual manifest로 정정해 읽고 원본을 덮어쓰지 않는다.

CA11 direct native notify의50/100/200 actual BP+R Motion/synthetic Ground 방문량1225/4950/19900을 확인하고 GC 경계로 옮겼다. native GC2→1/live token/last End/notify unroot 회귀 통과. 이 국소 수정과 원래 전체 CPU 증가 해결은 다르다. 기존 CPU fixture는 **LMB intent**이며 R workload라고 한 옛 표현을 정정했다. parent montage15 public read의 Motion8/9 state·Ground0은 해당 subset만 뜻한다.

**원래 DS→RA13 CPU 증가와 Render2→3 차이는 미해결이다.** 오래된 RA13을 복원하는 `Saved/Validation/Reaudit_20261014/Run-BalancedCPU.ps1`을 latest에 그대로 실행하지 않는다. `Scripts/Audit/Run-ContinuationComparison.ps1`은 시작 당시 complete9를 보유/복원하며 process가 남으면 교체를 거절한다.

## 다음 계속 작업

1. Git/process/frozen636 source·Editor9/패키지 SHA/user handoff diff부터 확인. 진행 중 Unreal/UBT/dotnet/LiveCoding/MSBuild/shader/Insights가 있으면 정상 종료 대기. directUBT only. 새 커밋/전송/병합/종료 권한을 추정하지 않는다.
2. **CR12 source 계약 후보**를 실제 native fixture로 묶어 재현: class StartingLevel/owner MaximumLevel/Initialize·Set·Capture·Restore 범위 불일치, Player TryApply의 teleport 뒤 void level setter 거절 미전달/pose-only 성공 가능. 아직 새 runtime 실패 재현/수정이 없다. persistent level·pose·receipt 권위를 결정한 후 원천 계약으로 수정한다. 필요한 trajectory/SSR 보상 유지.
3. **CR11/CA11 callback 수명**: Activation CancelAbility 뒤 같은 weak ASC 재사용, BP IsDead·selected-target multicast GC, notify Super/attachment/native cleanup GC/reentry·누락 End. source 후보를 실제 재현하고 scoped 참가자/논리수명/current ownership/atomic publication을 구분한다. 지금 통과한 GC fixture로 이 조합을 완료했다고 표시하지 않는다.
4. queued old-device callback origin, signed semantic sequence overflow, 모든 executor/ASC/prediction epoch/registry GC, Quest/Progression grant/restore callback 수명, 실제 DA map target·class 정책 등을 연계 시스템 단위로 계속 대조한다.
5. 후보에 제한하지 않고 모든 module/source/header/inline/actual native/reflection/BP entry, Config·DT·Chooser·PSD·IA/IMC·ABP/DA/CDO/instance override와 failure/authoring/test/deployment closure를 닫는다. catalog205행 의미·서비스 설치/GameplayCue closure·audio/cloth/접근성·모든 몸체/장비/클립은 미완료다.
6. 호환되는 재현·수정을 묶어 빌드하고 각 수정은 집중 회귀. 전체/Game/Cook/package는 다음 통합 기준점에서 필요한 경우 실행. 이미 검증된 native/asset 근거를 재사용하되 버전이 다른 증거를 latest 통과로 승격하지 않는다. 진행률 대신 시스템별 지정 검토·실제 수정·남은 검증을 간단히 보고한다.

환경/접근 미검증: live HTTPS/auth/durable DB/distributed authority/WAN, installed engine native Server target, 실제200socket, protected Python property는 공식 public API로 읽을 수 없는 부분, 전체 실제 BP/리그/시각 품질. 서비스를 설치하지 않은 구조와 접근 제한을 구분하고 우회하지 않는다. Unreal MCP callable capability가 없었던 범위는 public Editor Python/native fixture로만 확인했고 MCP 전수 사용을 주장하지 않는다.
