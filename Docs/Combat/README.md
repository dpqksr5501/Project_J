# 전투

## 기준·설계

| 문서 | 범위 |
| --- | --- |
| [전투 애니메이션 통합 가이드](Architecture/Combat_Animation_System.md) | 상체·FullBody 선택, 이동 정책, 레이어·콤보·입력·첫 공격 작성 |
| [전투 확장 기반](Architecture/ScalableCombatFoundation.md) | 서버 권한과 확장 계약 |
| [VFX 구조·연결](Architecture/CombatVFXArchitecture.md) | 전투 이펙트 책임과 작성 방법 |

## 제작

- [대검 공격·콤보 작성](Authoring/GreatswordCombatAuthoringGuide.md).
- 손 접촉 구간과 왼팔 ABP는 [양손 파지 가이드](../Animation/Authoring/Secondary_Hand_Contact.md).
- 직업·전직 데이터 연결은 [게임플레이 제작](../Gameplay/README.md).

## 구현·후속 기록

- [Combat Strafe 구현](Reports/CombatStrafe_Implementation_2026-08-04.md).
- [Combat Strafe TIP 구현](Reports/CombatStrafe_TurnInPlace_Implementation_2026-08-12.md) → [TIP 순간 튐 후속 분석](../Animation/Diagnostics/Project_J_TIP_Visual_Pop_Trace_2026-09-24.md).
- [발도·납도 원샷 복구](Reports/CombatIntro_StaleOneShot_Resolution_2026-08-12.md).
- [초기 Combat Strafe 인계](../Handoffs/CombatStrafe_2026-08-03/CombatStrafe_Handoff_2026-08-03.md).
