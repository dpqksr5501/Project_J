# 성능 설계·수집·결과

2026-10-14 추가 구조 변경의 기준은 [재점검](../Review/Audits/Project_J_Reaudit_2026-10-14.md)이다. RA01–13 전후의 전체 8개 Editor 모듈+modules를 맞춘 A1/B1/B2/A2 교대 CPU 비교 64행을 완료했다. 조건별 240틱 준비/600틱 측정·2회 반복이며 평균 증가 +2.40–8.59%가 남아 있다. 별도 단계 trace와 원인 조사를 계속하며 성능 완료로 판정하지 않는다. 실제 D3D12의 동시 200 montage 구간은 38.14FPS, GPU10.31ms였고 지속 공격이나 A/B 개선율은 아니다. 아래 기존 측정을 새 RA 변경본의 수치로 사용하지 않는다. UI·원격·CPU wrapper의 종료 근거도 보완했다.

현재 후속 구조·수명·실제 데이터 조사는 [심화 감사](../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](../Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md)에 기록한다. DS01–18의 실제 변경·계약 검토·미검증을 구분한다. 최종 전체 회귀 332개·Game/Cook과 기록한 통합 검증을 수행했으며, CPU 일부 증가와 packaged RHI trace 종료 실패는 남아 있다.

[2026-10-13 전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)는 같은 조건의 200명 교대 실행과 단계별 trace, 실제 RHI의 200개 authored 몸체 검증을 추가한다. NullRHI world tick, 렌더 프레임/GPU, 실제 원격 접속 수는 서로 다른 측정이다. 이전 리팩토링/리타깃 CPU 증가를 이번 입력 결함 수정의 통과로 해소 판정하지 않는다.

2026-10-13 후속 [200명 애니메이션 런타임 최적화](../Animation/Planning/Animation_Runtime_Optimization_2026-10-13.md)는 MM 데이터베이스 적용의 전체 그래프 탐색을 기존 캐시로 대체하고, 공격 시작·관측 시간·tick 원자료를 보완했다. 해당 timer의 국소 CPU 감소와 전체 World tick 비교의 편차·최대값 한계를 구분한다.

2026-10-13 파이프라인 구조 변경의 200명 CPU 전후 비교와 실행 경계는 [애니메이션 리팩토링](../Animation/Architecture/Animation_Pipeline_Refactor_2026-10-13.md)에 기록한다. 아래 10월 12일 시각 예산 off/on 결과와 다른 비교다.

다른 채팅에서 후속 작업을 시작할 때는 [200명 애니메이션 최적화 인계](../Animation/Planning/Animation_Optimization_Handoff_2026-10-13.md)를 읽는다. 현재 main 기준·품질 보호·네트워크/병렬 소유권·CPU 과제·원자료·실행 명령을 모았다.

## 최근 애니메이션 구현과 비교 — 2026-10-12

[200명 시각 애니메이션 예산](../Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md)에 실제 BP·장비·리타깃 몸체·의상 물리 구성의 기능 off/on 비교를 기록했다. 같은 바이너리, NullRHI, 조건별 120틱 준비 + 120틱 측정, 2회 반복이며 ABA on 평균 World tick은 근거리 13.61→11.40ms, 원거리 13.78→10.69ms다. 동시 공격은 19.76→19.58ms로 거의 같다.

기존 AnimInstanceProxy·엔진 병렬 평가를 유지하면서 평가 작업량과 주기를 줄인다. NPC/Mass 작업 큐에 애니메이션을 새로 제출하지 않는다. 로컬·전투·단발·급회전·화면 복귀 보호, 실제 보간 검증과 복구 CVar를 함께 기록했다. 200명 렌더 FPS·GPU 개선이나 200개의 소켓 접속 검증을 뜻하지 않으며, 원자료는 문서가 가리키는 로컬 `Saved/Validation`에 있다.

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

## 재감사 현재 측정과 한계 — 2026-10-14

최신 production RA25 package3은 실제200 표시 body·동시 montage167프레임에서27.1149ms(36.88FPS),GPU10.6211ms였고 복귀219프레임은15.8822ms(62.96FPS)였다. Render2보다 낮은 값이다. 단순 light/camera·D3D12 offscreen·실제 socket0이며 교대 비교/열·스케줄 통제가 없어 개선·코드 회귀를 확정하지 않는다. NullRHI full-binary A/B/B/A64행의 일부 CPU 증가와 별도16 traced region의 반대 방향은 미해결이다. [현재 검증과 원자료 범위](../Review/Audits/Project_J_Reaudit_Verification_2026-10-14.md)를 읽는다. 원격은 실제2socket+NPC200 반복이며200socket으로 계산하지 않는다.
