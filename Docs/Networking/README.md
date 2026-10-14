# 네트워크

현재 후속 구조·수명·실제 데이터 조사는 [심화 감사](../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](../Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md)에 기록한다. DS01–18의 실제 변경·계약 검토·미검증을 구분한다. 최종 전체 회귀 332개·Game/Cook과 기록한 통합 검증을 수행했으며, CPU 일부 증가와 packaged RHI trace 종료 실패는 남아 있다.

[전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)의 `Measure-Network.ps1 -Animation -CombatInput`은 실제 데이터의 R→RMB를 소유 예측·서버·simulated proxy에서 대조한다. 200 native NPC, 200 표시 몸체와 실제 소켓 접속 수를 각각 기록한다. Party/Guild 거리 정책 객체는 현재 실제 Iris filter adapter로 설치돼 있지 않다.

애니메이션의 스냅샷·표시·검색 경계와 실제 소켓 후속 검증은 [2026-10-13 파이프라인 리팩토링](../Animation/Architecture/Animation_Pipeline_Refactor_2026-10-13.md)을 함께 읽는다. 이 변경은 복제 이벤트 형식과 이동 권위를 유지한다.

Shift 달리기 SavedMove와 테스트 지연 설정의 최신 수정은 [달리기 이동 예측·커서 검증](../UI/Cursor_And_Sprint_Prediction_2026-10-10.md)을 읽는다.

| 종류 | 문서 |
| --- | --- |
| 수집 절차 | [네트워크 기준선 수집](CaptureGuides/ProjectJ_Network_Baseline_Capture_Guide_2026-09-04.md) |
| 측정 기준선 | [네트워크 결과](Baselines/ProjectJ_Network_Baseline_Results_2026-09-04.md) |
| 복제 구조 | [원격 TIP](Architecture/ProjectJ_Remote_TIP_Replication_2026-09-05.md) · [이동 원샷](../Animation/Locomotion/RemoteOneShotReplication.md) |
| 검증 기록 | [Iris/FastArray 검증](Validation/ProjectJ_Iris_FastArray_Validation_2026-09-05.md) |
| 계획·도입 게이트 | [원격 TIP 계획](Planning/ProjectJ_Remote_TIP_Replication_Plan_2026-09-05.md) · [Iris/AOI 확장 게이트](Planning/ProjectJ_Iris_AOI_Scale_Gates_2026-09-05.md) |

기준선의 NetDriver·Iris 활성 여부·클라이언트 수·관측 조건은 당시 기록이다. 새 실행은 해당 설정과 증거를 다시 확인한다. 네트워크 실측 원본은 [벤치마크](../Benchmarks/README.md)에서 함께 찾는다.
