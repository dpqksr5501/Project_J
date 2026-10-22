# 성능 설계·수집·결과

**발전 순서:** 기존 예산·LOD 최적화 → Mover·Manny/native 전환 → 이번 정책 소비 복구·추가 최적화다. 이번 비교도 기존 최적화가 적용된 기반에서 수행했다. 전환 자체의 동일 조건 성능 증감은 미측정이며, 과거 GPU 감소와 이번 frame 감소를 연결한 누적 수치를 만들지 않는다. [단계별 구조와 비교 범위](Reports/ProjectJ_Mover_200_2026-10-22.md#이전-최적화--mover-전환--이번-최적화)를 먼저 읽는다.

**동시 공격 결정:** 이번 저빈도 시각 갱신·IK 생략 최적화를 적용하지 않고 기존 매 프레임 평가와 제작 LOD·물리 품질을 유지한다. 활성 montage는 blend-out까지 보호한다. 공격·복귀 측정은 회귀 관찰용이며 성능 개선의 채택 근거가 아니다.

원자료의 작은 요약·CSV는 문서에 유지하고 상세 로그·캡처·대표 trace는 [Git LFS 압축 보관소](../../Archives/Validation/README.md)에서 공유한다. 바이트 동일 사본을 통합하고 원본 해시·복원 방법·퇴역 범위를 함께 기록한다.

**2026-10-22 현재 Mover 재검증:** source 품질 정책 호출 누락·회전 좌표계 불일치를 수정하고 200명 실제 이동/반전·점프/착지·공격/복귀를 재측정했다. 동일 바이너리 ABBA의 유휴 평균 **18.19→16.12ms**, 이동 **21.05→19.42ms**이며, 동시 공격 **30.10→30.08ms**와 tail 편차도 함께 공개한다. [현재 → 결과·실행 검증·원자료](Reports/ProjectJ_Mover_200_2026-10-22.md)를 시작점으로 읽고 아래 LOD 및 과거 CMC 결과와 합산하지 않는다. 내부 궤적 예측은 기본 60Hz를 유지한다.

**2026-10-16 현재 결정:** 검토한 GreatSword_Woman의 LOD0–2를 채택하고 LOD3–4 추가는 보류한다. 로컬LOD0·200명 전원 표시·원본 물리와 서버 판정을 유지한다. 공통 제작 설정은 다른 에셋에도 재사용할 수 있지만 생성·예외 검수·품질 승인은 에셋별로 필요하다. [현재 결정표와 단계별 근거](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md)를 기준으로 아래 과거 측정을 읽는다.

최신 [7차 기록](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-7차--새-캐릭터의상장비의-공통-lod-제작)은 새 에셋의 기존 LOD를 보존하고, 누락된 일반 표면 메시를 공통 native 설정으로 생성·검증하는 Editor 제작 경로다. [사용법과 Cloth·헤어·morph 예외](../Animation/Authoring/Mesh_LOD_Authoring.md)를 따른다. 최종 RHI13개·200명 표시/로컬LOD0·실제2연결 네트워크 회귀를 통과했으며 새 성능 개선율을 측정한 작업은 아니다.

[6차 기록](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-6차--다른-에셋-호환성과-실제-몸체-lod)은 에셋 교체·Cloth/외부 제어·몸체 역할 충돌의 공통 경계를 보완하고 실제 원격 body LOD를 적용했다. 동일 바이너리 교대 시험의 GPU는 유휴10.526→6.188ms, 동시200 montage 행11.922→7.421ms였지만 전체 frame/CPU 이득은 확정하지 않는다. 로컬LOD0·전원 표시·원본 물리를 보존하며 다른 에셋의 LOD/Cloth 제작·품질 검수까지 자동 완료로 주장하지 않는다.

[5차 기록](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-5차--물리-복제의-초기화-동등성과-비용-판정)은 물리 복제 비교의 비대칭 초기화 순서를 고쳤다. 단순 복제·축소본의900표본 동등성은 통과했지만, 실제91→44 actor의64행 비계측 변화는 대부분1% 안팎이었다. trace의 대조 scope 변동도 남아 제작 적용은 하지 않았다. Editor 동등성 회귀만 추가하고 기존 runtime/에셋을 보존했으며 JSON `fifth_pass`에 근거를 통합했다.

2차는 같은 [200명 계획](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-2차--공통-비용과-실제-소비-확인)에 통합했다. 유휴·이동·회전/착지까지 공통 조회를 정리하고 64행 비계측 A→B→B→A를 비교했으나 전체 CPU 개선은 미확정이다. 실제 표시 몸체 LOD1개, Far 정책과 retarget IK 노드 소비 불일치, 물리3개의 서로 다른 담당 뼈를 확인했다. 최종 Editor/Game·RHI34개·200명 표시/local1011 samples가 통과했으며, 상세 근거는 요약 JSON의 `second_pass`를 따른다.

현재 목표는 **총 200명 전원 표시·로컬 품질 유지**다. [2026-10-15 계획과 첫 적용](Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md)은 기존 예산 subsystem의 거리 관측 활성화, 로컬 중복 계산 제거, 200명 실제 렌더·로컬 보호·원격/전투 검증을 기록한다. 단일 렌더 유휴 표본은 15.978→12.712ms였지만 전원 공격은 30.499→30.607ms였고 50명 trace의 CPU 증가도 남아 있다. 전체 개선·회귀 해소로 판정하지 않는다. [실행 조건과 source/DLL/asset·원자료 지문](Reports/ProjectJ_200_Player_Optimization_2026-10-15.json)을 함께 읽고, 아래 과거 실행과 합산하지 않는다.

동일 계획의 3차는 원격 retarget IK 생략 후보를 검증한 뒤 **미채택**했다. 같은 바이너리 off/on/on/off 64행에서 Far 비용 절감이 입증되지 않아 후보 코드·옵션은 제거하고 2차 runtime을 보존했다. 기능/같은 입력 포즈 통과와 전체 성능 이득을 구분하며, JSON의 `third_pass`는 현재 적용 결과와 후보 측정의 해시를 별도로 보존한다.

4차는 기존 trace의 의상별 비용과 새 Editor 진단의 retarget op·실제 physics actor를 확인했다. physics 데이터 축소는 단순 복제도 포즈 동등성을 통과하지 못해 **적용 보류**다. 읽기 진단만 기존 시험에 남겼고 runtime·에셋·서버/네트워크 경계를 유지했다. JSON의 `fourth_pass`와 계획의 4차 절에 새 실행·재사용 근거를 구분했다.

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

- [오픈 월드 레벨 설계와 확장 계획](../Gameplay/Planning/Open_World_Level_Design.md): 고속 비행·팝인·원경·식생 검증 계획과 16절의 에디터 숲 고정 시점 CSV 비교. 패키지·전체 월드·원격 MMO 성능 합격이나 기존 200명 최적화 완료를 뜻하지 않는다.
- [확장성 검증 계획](Planning/ProjectJ_Scalability_Profiling_Validation_Plan_2026-09-03.md): 인원 단계와 CPU/GPU/메모리/네트워크 회귀 게이트.
- [보류된 최적화](Planning/DeferredOptimizationArchitecture.md): 도입 조건과 후속 계약.
- [시스템 현대화 작업 기록](Reports/ProjectJ_Systems_Modernization_Refactor_2026-09-08.md): 초기 진단부터 적용·측정·병합까지의 원본 이력.
- [애니메이션 실행 감사](../Animation/Planning/ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md) · [네트워크 수집](../Networking/README.md).

로컬 복제 캐릭터 수, 실제 원격 플레이어 수, 실험 fixture 수를 구분한다. 서로 다른 조건의 개선율을 합쳐 전체 게임 성능으로 해석하지 않는다.

## 재감사 현재 측정과 한계 — 2026-10-14

최신 production RA25 package3은 실제200 표시 body·동시 montage167프레임에서27.1149ms(36.88FPS),GPU10.6211ms였고 복귀219프레임은15.8822ms(62.96FPS)였다. Render2보다 낮은 값이다. 단순 light/camera·D3D12 offscreen·실제 socket0이며 교대 비교/열·스케줄 통제가 없어 개선·코드 회귀를 확정하지 않는다. NullRHI full-binary A/B/B/A64행의 일부 CPU 증가와 별도16 traced region의 반대 방향은 미해결이다. [현재 검증과 원자료 범위](../Review/Audits/Project_J_Reaudit_Verification_2026-10-14.md)를 읽는다. 원격은 실제2socket+NPC200 반복이며200socket으로 계산하지 않는다.
