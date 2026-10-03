# 성능 설계·수집·결과

## 측정 순서

1. [최적화 기반](Architecture/PerformanceOptimizationFoundation.md)에서 기존 예산·실행 정책과 후속 범위를 읽는다.
2. [프로파일링 수집](CaptureGuides/ProjectJ_Baseline_Capture_Guide_2026-09-03.md)으로 조건과 자료를 남긴다.
3. [통합 기준선](Reports/ProjectJ_Profiling_Consolidated_Summary_2026-09-06.md)과 [CPU 기준선](Reports/ProjectJ_Profiling_Baseline_Results_2026-09-03.md)을 비교한다.
4. [벤치마크와 원본](../Benchmarks/README.md)에서 workload·표본·재계산 방법을 확인한다.

## 계획과 변경 기록

- [확장성 검증 계획](Planning/ProjectJ_Scalability_Profiling_Validation_Plan_2026-09-03.md): 인원 단계와 CPU/GPU/메모리/네트워크 회귀 게이트.
- [보류된 최적화](Planning/DeferredOptimizationArchitecture.md): 도입 조건과 후속 계약.
- [시스템 현대화 작업 기록](Reports/ProjectJ_Systems_Modernization_Refactor_2026-09-08.md): 초기 진단부터 적용·측정·병합까지의 원본 이력.
- [애니메이션 실행 감사](../Animation/Planning/ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md) · [네트워크 수집](../Networking/README.md).

로컬 복제 캐릭터 수, 실제 원격 플레이어 수, 실험 fixture 수를 구분한다. 서로 다른 조건의 개선율을 합쳐 전체 게임 성능으로 해석하지 않는다.
