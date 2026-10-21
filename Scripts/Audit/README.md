# 계속 작업 검증·근거 도구

이 폴더는 제품 실행 경로가 아니라 검증과 당시 기록의 생성 근거를 보존한다. 파일 수·장부 생성 성공을 전체 소스/에셋 정밀 검토 완료로 해석하지 않는다.

- 재검증: `Run-ContinuationBuild.ps1`은 direct UBT를 사용한다. `Run-ContinuationTests.ps1`과 에셋/입력/레벨/notify 검증기는 고유 run 이름과 해당 read/verify 스크립트를 함께 사용한다. `Run-AssetArchitectureValidation.ps1`의 Reload는 `Verify-AssetArchitecture.py`를 호출하고 Cook는 명시한 에셋 경계를 검증한다.
- 비교·분석: Analyze/Compare/Export/Summarize와 Game/패키지/버전 capture는 기존 측정 근거를 재사용하기 위해 보존한다. CPU/GPU 문제 해결 완료를 뜻하지 않으며, 보류한 성능 실험을 자동으로 재개하지 않는다. 과거 `Run-BalancedCPU.ps1`의 RA13 binary 복원을 최신본에 적용하지 않는다.
- 근거 재사용: `Publish-ContinuationEvidence.py`는 제한된 근거의 사본·지문을 게시하고, `Verify-HistoricalComparison.py`는 보존한 과거 비교 입력을 확인한다. `Capture-CA14To16Verification.py`는 실행 이름을 인자로 받는 통합 체크포인트 검증기다. 서로 다른 보존·재검증 역할이 있어 유지한다. 기존 결과를 현재 상태의 합격 근거로 자동 승격하거나 과거 해시를 덮어쓰지 않는다.

## 2026-10-19 일회성 생성기 정리

특정 세션의 시작 상태·고정 CA 결과·직접 작성한 검토 장부를 기록하던 다음 9개는 퇴역했다. 출력은 이미 보존돼 있으며, 실행 코드·Source·Config·CI에서 호출하지 않는 것을 확인했다. 현재 게임·에디터의 실행 도구가 아니고, 현재 소스에 재실행하면 과거 기준점과 충돌하거나 기존 장부를 덮어쓸 수 있다.

- `Capture-AssetCleanupBaseline.py`, `Capture-CA14Verification.py`, `Capture-Resume20261015.py`
- `Finalize-AssetArchitectureCleanup.py`, `Finalize-ContinuationCheckpoint.py`
- `Record-ContinuationReview.py`, `Record-ParallelReview.py`, `Reconcile-ContinuationInputEvidence.py`, `Start-Continuation.py`

퇴역 전 버전은 Git 커밋 `e8fe6d0d`의 원래 경로에서 복구할 수 있다. 로컬 원형·SHA256 목록은 `Saved/Maintenance/ScriptRetirement_2026-10-19`에 보관했다. 과거 결과 JSON과 SourceHashes 등 역사적 지문은 수정하지 않았다. 그 안에 남은 퇴역 파일명은 당시 입력 기록이며 현재 실행 연결이 아니다. 회귀 테스트·실제 자산/네트워크 검증기와 분석·비교 도구는 유지했다.

세션 전용 MCP 호스트·호출기·조회 준비·일회 graph 수정 스크립트 7개는 커밋 전 제거했다. 원본은 무시되는 로컬 Saved 보관소에 남기고 SHA와 제거 이유는 [커밋 전 정리 기록](../../Docs/Review/Audits/Project_J_Commit_Preparation_2026-10-15.md)에 기록한다. 실제 회귀 시험, 제작 validator, 테스트/샘플 BP·DA, 실패 및 성공 원자료는 유지한다.

모든 native 실행은 프로세스 겹침을 확인하고 정상 종료까지 기다린다. 빌드·Cook·검증을 중단하지 않는다. 현재 실행 인자와 출력 경로를 먼저 읽고, 기존 run 폴더를 재사용하지 않는다.

2026-10-20 GASP 작업 후 `Saved`의 완료된 일회성 이행·MCP 세션 스크립트 265개는 `Saved/StorageCleanup_20261020/RetiredSessionScripts.zip`으로 보관하고 실행 위치에서 제거했다. `Scripts`의 현행 GASP 회귀·클립 감사·제작·네트워크·분석 도구는 유지한다. 큰 과거 원자료는 인접 `.gz`로 무손실 보관했으므로 기존 경로를 입력으로 요구하는 분석에는 먼저 해제한다. 클립 감사기는 압축된 receipt도 기존 근거로 판정하며 새 `AUDIT_LABEL`을 요구한다. 과거 이행 스크립트는 복원하더라도 현행 에셋에 그대로 재실행하지 않는다. 범위와 검증은 [용량 정리 기록](../../Docs/Maintenance/Storage_Cleanup_2026-10-16.md)의 10월 20일 절을 따른다.

동일 정리에서 `Scripts/Editor`의 완료된 Foley 이행·누락 복구·6클립 EarlyTransition 감사/복구 래퍼 4개도 퇴역했다. native API·실제 애니메이션·영구 회귀 테스트는 유지했고, 해당 [애니메이션 진단 기록](../../Docs/Animation/Diagnostics/)에 복구 위치와 재사용 제한을 남겼다.

정리 후 기존 Python 회귀 16개·자산 검증 래퍼 모의 11조건·provenance 비교 6조건이 통과했다. 퇴역 파일의 실행 코드/시작 설정/CI 참조와 신규 문서 링크 오류는 0개다. Source·Config·Content의 Git 내용 변경은 없으며, C++ 빌드·에디터 실행은 필요하지 않아 수행하지 않았다. 이 결과는 과거 native 검증을 새로 실행했다는 뜻이 아니다.
