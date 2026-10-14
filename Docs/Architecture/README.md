# 아키텍처

현재 후속 구조·수명·실제 데이터 조사는 [심화 감사](../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](../Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md)에 기록한다. DS01–18의 실제 변경·계약 검토·미검증을 구분한다. 최종 전체 회귀 332개·Game/Cook과 기록한 통합 검증을 수행했으며, CPU 일부 증가와 packaged RHI trace 종료 실패는 남아 있다.

## 책임과 확장 기반

| 주제 | 시작점 | 내용 |
| --- | --- | --- |
| 런타임 | [런타임 책임](Runtime/README.md) | 캐릭터·컴포넌트·서비스의 실행·수명과 후속 결과 |
| 콘텐츠·MMO 기반 | [확장 기반](Extensions/README.md) | 모듈·능력·저장 계약, 콘텐츠 카탈로그와 검증 범위 |
| 전체 계획 | [계획](Planning/README.md) | 실행 로드맵과 보류된 MMORPG 시스템 |

## 다른 주제의 구조

- [2026-10-13 실제 구조 개선](../Review/Audits/Project_J_Structural_Improvement_2026-10-13.md): 중복 자원 수명·기본값·표시 helper와 책임 분리, private 미사용 제거, 현재 owner/Avatar의 GAS metadata 계약.
- [2026-10-12 애니메이션 병렬 경계](../Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md#기존-병렬-아키텍처와의-관계): 기존 Proxy·엔진 평가 활용, GT 시각 정책과 NPC/Mass 병렬 계산의 구분.
- [애니메이션](../Animation/README.md): Motion Matching, 상체 합성, 런타임 리타깃과 손 접촉.
- [전투](../Combat/README.md): 이동 정책, 공격 합성, 서버 판정과 VFX.
- [네트워크](../Networking/README.md): 복제 구조, 기준선, Iris/AOI 도입 게이트.
- [성능](../Performance/README.md): 실행 비용, 수집 절차, 기준선과 확장성 검증.
- [전체 구조 검토](../Review/Architecture/README.md): 시점별 감사·제안과 후속 문서.

각 구조 설명의 기준 위치를 해당 주제로 통일한다. 변경 보고서·측정 데이터는 해당 기준 문서에서 연결한다.
