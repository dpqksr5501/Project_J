# 전투

전용 서버 근접 공격은 라이브 소켓의 `GameplayPose`와 검증된 베이크 공격의 `MontageTiming`을 분리한다. 라이브 경로는 본 갱신으로 소켓을 읽고, 베이크 경로는 `OnPostTickPose`에서 캡처한 Mover 루트와 몽타주 시각을 사용한다. 두 경로의 요청 종료와 기존 정책 복원을 회귀로 확인했다. SpellbladeKit은 기본 `Light01`을 포함한 활성 근접 공격 26개, 타격 창 28개에 베이크를 적용했다. [공격 데이터와 전용 서버 검증](../Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md#스펠블레이드-실전-공격-전체-베이크) · [베이크 제작 절차](../Animation/Authoring/Spellblade_CombatKit.md#근접-타격-궤적-베이크)

실제 R 도중 빠른 RMB 연계와 공통 마우스 수명, Avatar 교체·원격 입력의 최신 증거는 [2026-10-13 전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)를 읽는다. 입력 grace나 클립별 예외를 추가하지 않고 공통 press/release 계약을 수정했다.

늦은 Controller 도착에 따른 GAS ActorInfo cache 누락과 전투·탑승 레이어 자원 수명 공통화는 [구조 개선 후속](../Review/Audits/Project_J_Structural_Improvement_2026-10-13.md)을 읽는다. 전투 판정·예측·능력/콤보 상태는 기존 owner가 유지한다.

## 기준·설계

| 문서 | 범위 |
| --- | --- |
| [전투 애니메이션 통합 가이드](Architecture/Combat_Animation_System.md) | 상체·FullBody 선택, 이동 정책, 레이어·콤보·입력·첫 공격 작성 |
| [전투 확장 기반](Architecture/ScalableCombatFoundation.md) | 서버 권한과 확장 계약 |
| [VFX 구조·연결](Architecture/CombatVFXArchitecture.md) | 전투 이펙트 책임과 작성 방법 |

## 제작

- [무기 메시·텍스처 제작 규격](Authoring/Weapon_Asset_Standards.md): BaseColor/Normal 최대 2K, ORM 최대 1K, 원본 보존·스트리밍·예외·검수 기준.
- [대검 공격·콤보 작성](Authoring/SpellbladeCombatAuthoringGuide.md).
- 손 접촉 구간과 왼팔 ABP는 [양손 파지 가이드](../Animation/Authoring/Secondary_Hand_Contact.md).
- 직업·전직 데이터 연결은 [게임플레이 제작](../Gameplay/README.md).

## 구현·후속 기록

- [Combat Strafe 구현](Reports/CombatStrafe_Implementation_2026-08-04.md).
- [Combat Strafe TIP 구현](Reports/CombatStrafe_TurnInPlace_Implementation_2026-08-12.md) → [TIP 순간 튐 후속 분석](../Animation/Diagnostics/Project_J_TIP_Visual_Pop_Trace_2026-09-24.md).
- [발도·납도 원샷 복구](Reports/CombatIntro_StaleOneShot_Resolution_2026-08-12.md).
- [초기 Combat Strafe 인계](../Handoffs/CombatStrafe_2026-08-03/CombatStrafe_Handoff_2026-08-03.md).
