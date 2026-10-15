# 계속 작업 검증·근거 도구

이 폴더는 제품 실행 경로가 아니라 검증과 당시 기록의 생성 근거를 보존한다. 파일 수·장부 생성 성공을 전체 소스/에셋 정밀 검토 완료로 해석하지 않는다.

- 재검증: `Run-ContinuationBuild.ps1`은 direct UBT를 사용한다. `Run-ContinuationTests.ps1`과 에셋/입력/레벨/notify 검증기는 고유 run 이름과 해당 read/verify 스크립트를 함께 사용한다. `Run-AssetArchitectureValidation.ps1`의 Reload는 `Verify-AssetArchitecture.py`를 호출하고 Cook는 명시한 에셋 경계를 검증한다.
- 비교·분석: Analyze/Compare/Export/Summarize와 Game/패키지/버전 capture는 기존 측정 근거를 재사용하기 위해 보존한다. CPU/GPU 문제 해결 완료를 뜻하지 않으며, 보류한 성능 실험을 자동으로 재개하지 않는다. 과거 `Run-BalancedCPU.ps1`의 RA13 binary 복원을 최신본에 적용하지 않는다.
- 역사적 생성기: `Capture-AssetCleanupBaseline.py`, `Capture-CA14Verification.py`, `Capture-Resume20261015.py`, `Finalize-AssetArchitectureCleanup.py`, `Finalize-ContinuationCheckpoint.py`, `Publish-ContinuationEvidence.py`, `Reconcile-ContinuationInputEvidence.py`, `Record-ContinuationReview.py`, `Record-ParallelReview.py`, `Start-Continuation.py`, `Verify-HistoricalComparison.py`는 특정 날짜/HEAD/run을 전제로 한다. 새 실행용 범용 도구로 사용하거나 과거 검증 해시를 현재 상태로 덮어쓰지 않는다.

세션 전용 MCP 호스트·호출기·조회 준비·일회 graph 수정 스크립트 7개는 커밋 전 제거했다. 원본은 무시되는 로컬 Saved 보관소에 남기고 SHA와 제거 이유는 [커밋 전 정리 기록](../../Docs/Review/Audits/Project_J_Commit_Preparation_2026-10-15.md)에 기록한다. 실제 회귀 시험, 제작 validator, 테스트/샘플 BP·DA, 실패 및 성공 원자료는 유지한다.

모든 native 실행은 프로세스 겹침을 확인하고 정상 종료까지 기다린다. 빌드·Cook·검증을 중단하지 않는다. 현재 실행 인자와 출력 경로를 먼저 읽고, 기존 run 폴더를 재사용하지 않는다.
