# 네트워크

2026-10-19 장비 복제는 owner-only 상세 FastArray와 관찰자용 슬롯/정의 FastArray로 분리했다. 관찰자에게 인스턴스 ID·수량·잠금·가방 순서를 보내지 않으며, 두 표현의 변경 권위는 서버의 기존 장착/해제 commit 하나다. PS의 공통 명령 예산은 장비·가방·소모품·퀘스트·SSR 입장에서 공유하고 도메인 제한은 별도로 유지한다. [현재 전체 권고 판정과 검증](../Review/Architecture/Project_J_MMO_Remaining_2026-10-19.md)을 참고한다. 이 변경을 custom Iris 파티/길드 adapter나 계정 인증 구현으로 해석하지 않는다.

같은 작업의 로컬 영속 저장은 Windows Development 단독 opt-in 범위다. 순번 창64·checkpoint 최신1과 원자적 확정, v1 이관 도구가 구현돼 있어도 네트워크/Shipping 저장을 활성화한 것은 아니다. 최종 기본2clients/NPC100·LagLoss/ReorderLoss와 저장/재시작 결과는 [공유 검증 요약](../Review/Architecture/Project_J_MMO_Validation_2026-10-19.json)에 있으며, Editor-host 서버 모드를 독립 Server 바이너리나 운영 CCU 검증으로 세지 않는다.

[200명 전원 표시·로컬 품질 보존 계획](../Performance/Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md)은 시각 예산과 복제 필요성을 별도로 판단하고, 서버 판정·예측·SSR·재관련 검증을 포함한다. 200개 몸체 표시와 실제 200개 연결은 별도 목표/측정이며, 현재 Iris 정책 계산기를 live adapter로 해석하지 않는다.

같은 계획의6차는 원격 시각 몸체LOD와 에셋 호환 경계만 바꾸고 canonical 판정 pose/socket·CMC/SavedMove·GAS/SSR·복제 빈도/AOI를 유지했다. 최종 서버+client2/NPC200/lag50ms/loss3%의 기존 animation/combat 회귀를 새로 통과했다. 200표시 몸체 GPU 절감은 서버 성능/200소켓 달성의 근거로 사용하지 않는다.

10월 15일 첫 적용은 애니메이션 관측만 보완하고 전역 significance·이동 권위·복제 payload·판정을 변경하지 않았다. 실제 Iris 서버+client2·NPC200·50ms/3% loss·parallel net에서 원격 의미 이벤트와 R→RMB authority/owner prediction/remote target/end 3 trial이 통과했다. [현재 결과와 범위](../Performance/Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md#10월-15일-첫-적용과-검증)를 참고한다. WAN·200접속 검증은 아니다.

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
