# 심화 감사 원자료

이 폴더는 [심화 감사](../Project_J_Deep_Structure_Audit_2026-10-13.md)의 이동 가능한 증거다. `EvidenceManifest.json`에 원래 Saved 경로·크기·SHA256을 기록한다. 성공 보고서와 실패 재현/컴파일·process 종료 로그를 함께 보존한다. 파일 없음이나 fixture success만으로 전체 검증 통과를 뜻하지 않는다.

- `BaselineManifest.json`: 앞선 사용자·감사 변경을 포함한 시작점의 텍스트 파일 해시. 전체 원본과 8개 Editor 모듈/게임 바이너리는 Saved의 Baseline/BaselineBinaries에 별도 보존한다.
- `SourceReviewLedger.json`: 이름으로 특정한 계약 검토와 목록 수집만 한 파일, 시작점 이후 변경 여부를 구분한다. 파일 전체 수동 검토 비율이나 compiler call graph가 아니다.
- `SourceChangeManifest.json`, `SourceChangesFromDeepBaseline.patch`: SI 이후 보존 기준선과 비교한 Source 54개(기존 44개·신규 10개)의 실제 차이. tests/tools를 포함하며 기존 사용자 변경은 이미 기준선에 들어 있다. HEAD 전체 diff와 구분한다.
- `Regression`: 원본 automation index, 실행 인자, 로그. 테스트의 수와 warning·실패·미실행을 별도로 확인한다.
- `Build`: 최초 오류를 포함한 실패와 최종 정상 종료 로그. UBT는 직접 실행했고 진행 중인 빌드는 중단하지 않았다.
- `NativeAuthoredAssetsVerified.json`: Chooser 20개/327 result 행, PSD 53개/486 entry, schema 6개에 대한 읽기 전용 public C++ export. compiled search index와 모든 클립의 품질 검사로 확대하지 않는다.
- `Asset*Coverage`, `PoseSearchFieldCoverage`, `MCP`: MCP 조회 상태와 개별 의존성·실제 필드·graph pin 원응답. coverage의 Saved 상대 경로는 EvidenceManifest의 original로 대조한다. 지원되지 않은 필드는 검증된 값으로 처리하지 않는다.
- `AttackAsset*`: 공격 정의 10개의 수정 전후 값·원본 SHA·필요한 개별 save 결과. 공유 GE와 원래 정상 LMB1, 레벨은 변경/저장하지 않았다.
- `CPUComparisonRows*`, `CPU/*`: 두 ABBA 세션 128행, 매 tick JSON·모듈/source provenance, 별도 A/B trace의 16개 region CSV. AttackBurst는 한 번의 R과 종료 후 tick을 분리해 해석한다.
- `Runtime`: Cooked 전투·UI·200개 실제 몸체 렌더의 원로그/캡처/process 종료. UI 2해상도는 상태/갱신 automation이며 이번 실행의 UI screenshot 품질 검사는 아니다. trace 실행 실패는 NoTrace 성공과 별도로 보존한다.
- `Network`: 3회, 실제 독립 서버와 socket client 2개 및 native NPC 200개. 200개 실접속 자료가 아니다. 기존 Toolset Python 초기화 오류도 원로그에서 지우지 않는다.
- `RawTraceManifest.json`: 용량이 큰 utrace의 로컬 Saved 경로·SHA256. 실제 raw trace는 로컬에 보존하며 portable export CSV만으로 전체 trace를 대체했다고 주장하지 않는다.

NullRHI CPU, 렌더 FPS/GPU, 200개 표시 캐릭터, 실제 별도 socket client 수는 서로 다른 범위다. trace timer의 누적 exclusive/inclusive 시간과 critical-path wall 시간을 구분한다. 모든 함수·BP 이벤트·클립, live backend/DB·native Server target·200개 실접속을 보증하는 자료가 아니다.
