# 원자료와 집계 — 2026-10-13

AutomationResults.json은 실제 index.json의 상태와 SHA256이다. 초기 무효 fixture·관측기 실패도 포함하므로 성공 수를 합산하지 않는다. CPUComparison.csv의 trace=true/false와 binary variant를 구분한다. 원본 CPU JSON에는 tick 순서의 전체 표본·공격 montage 수·관측 비용·품질/평가 카운터가 있다.

| 자료 | 내용·해석 경계 |
| --- | --- |
| [자동화](AutomationResults.json) | 보고서별 성공/경고/실패/미실행과 실패 test path, 원본 SHA256. 전체 회귀의 시점별 소스 구분 |
| [최종 소스](LatestSourceHashes.json) | Source·Config·Build/Target.cs·uproject 575파일. 마지막 서버 alias guard 포함 |
| [빌드/Cook/stage](BuildCookStageResults.json) | 원본 로그 SHA256·크기·결과/시간/최초 지원 한계. 접근 거부 실패도 보존 |
| [실행 스크립트](ValidationScriptHashes.json) | 저장소 검증 script와 로컬 실행·복구·trace export script의 SHA256 |
| [MCP 조회](McpEvidenceHashes.json) | 실제 연결을 조회한 JSON 원본 해시. 지원되지 않은 속성/graph 반환을 완전성으로 해석하지 않음 |
| [첫 CPU 비교](CPUComparison.csv) | f1 기준/첫 개선, measured360. trace 행은 비계측 평균에서 제외 |
| [리타깃 이전 CPU](PreRetargetCPUComparison.csv) | 848/이전 Body와 개선, measured120·교대64행. mixed unity 차이 있음 |
| [같은 non-unity CPU](MatchedNonUnityCPU.csv) | A1/C1/A2/C2, 각2회·8조건·120표본, 평균/p50/p95/max·관측/dispatch 비용64행 |
| [단계 trace](StageTimers.csv) | Insights region/timer count·inclusive/exclusive, tick당 정규화. worker 누적·중첩 scope를 wall frame에 합산하지 않음 |
| [원격](NetworkResults.json) | 무효 fixture·실제 unreliable 실패·최종 reliable 성공, 실제2접속+native NPC200. fixture control traffic 포함 |
| [초기 렌더 표본](RenderedCrowdFrames.csv) · [조건](RenderedCrowdManifest.json) | 실제 BP/body200의 D3D12 viewport·idle/R. CPU LMB workload와 다름; 전후 개선율 아님 |
| [전달 실행](DeliveryExecutionHashes.json) | 새 Cook/stage의 UI·실제 BP 렌더·전투 실행 결과/manifest 해시 |
| [휴대 원자료](PortableRawEvidence.json) | MatchedNonUnity/A1,C1,A2,C2의 tick 순서 cpu.json·manifest·arguments, DeliveryNetwork의 실제 client 관찰 CSV·조건·판정. 원본/복사 경로·SHA256 |
| [패키지 렌더 표본](CookedRenderedCrowdFrames.csv) · [재계산](RenderedCrowdStatistics.json) | idle/R의 전체 표본과 선형 보간 p50/p95. 캡처 요청 행 제외, 후속 캡처 비용은 남음 |
| [UI](CookedUIResults.json) · [idle PNG](CookedCrowd_phase2.png) · [R PNG](CookedCrowd_phase3.png) | 새 패키지의 두 해상도 UI·실제 BP200 표시. 연속 움직임·근접 손/발 품질 인증과 구분 |
| [기존 작업 보존](Preservation.json) | 사용자 인계 변경/Body 에셋 해시, 기존 브랜치/HEAD, Content 변경 없음 |
| [최종 산출물·에셋](DeliveryBinaryAndAssetHashes.json) | 전달 Editor8모듈·Game/staged executable·생산 Family/Body/ABP 해시. CPU 측정 C의 별도 non-unity 바이너리와 구분 |

원본 위치는 `Saved/Validation/WholeProjectAudit_20261013`(회귀·MCP·build/Cook/stage·렌더·보존 snapshot), `AuthoredAnimation_20261005`(CPU별 tick 순서 전체 JSON·utrace), `GroupD_20260910`(원격 각3프로세스 로그·trace·manifest), `EF_20260912`(cooked 전투), `WholeAudit_CookedUIFinal`(cooked UI)이다. 마지막 전달 소스는 `DeliverySourceSnapshot`이다. CPU 비교의 C 바이너리는 **FinalVerifiedBinaries**이며 마지막 서버 alias guard 이전의 전체 non-unity 산출물이다. 마지막 guard는 해당 CPU fixture에서 호출되지 않지만 정확한 측정 시점과 전달 소스는 구분한다.

재계산은 CSV의 variant/scenario/aba/repeat/sample 수를 유지하고 평균·분위수를 원본 tick 배열에서 얻는다. 서로 다른 창 길이·trace 여부·R/LMB 행동·RHI/NullRHI·접속 수를 합산하지 않는다. 앱 UserDir는 실행마다 새로 만들며 OS/driver 캐시 초기화가 아니므로 cold-cache 인증도 아니다.

큰 원본 로그·DLL·trace와 나머지 표본/PNG는 저장소의 로컬 Saved/Validation/WholeProjectAudit_20261013, AuthoredAnimation_20261005, GroupD_20260910 아래에 보존한다. Saved는 ignore이므로 새 clone에는 자동 전달되지 않는다. 위의 휴대 원자료·패키지 PNG·전체 frame CSV는 이 근거 폴더에도 복사했다. 최종 조사와 정확한 실행 목록은 [감사 보고서](../Project_J_Whole_Project_Audit_2026-10-13.md)를 따른다.
