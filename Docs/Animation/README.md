# 애니메이션

## 현재 플레이어 파이프라인

공용 소스 포즈에서 이동 Motion Matching과 전투 상체·몽타주를 합성한다. 임포트 몸체는 런타임 리타깃 뒤 몸체 비율에 맞는 Guided Hand IK를 적용하고 의상 물리를 평가한다. 무기 구동과 손 접촉의 책임은 별도로 설정한다.

| 목적 | 기준 문서 |
| --- | --- |
| 이동 상태·GASP 대응 확인 | [Locomotion](Locomotion/README.md) |
| Motion Matching 실행·원격·예산 정책 | [Motion Matching 구조와 후속 항목](Architecture/MotionMatchingNextSteps.md) |
| 외부 Blend Stack 복귀·worker 검색 예산 | [복귀 검색과 군중 예산 보완](Architecture/MotionMatching_Return_Crowd_2026-10-05.md) |
| Strafe 네 방향 Pivot·입력 취소 | [Pivot 이동 입력 기준 통일](Architecture/Strafe_Pivot_Input_Basis_2026-10-05.md) |
| 연속 Pivot·OTM/Strafe 중복 Start/Land 방지 | [일회성 동작의 요청 수명](Architecture/OneShot_Command_Lifetime_2026-10-05.md) |
| Start·착지 중 입력 변경·MM 복귀의 실행 흐름 | [기본 비활성 흐름 진단](Architecture/Start_Land_Input_Flow_Trace_2026-10-05.md) |
| 이동 착지 취소의 Idle 경유·연결 레이어 검색 완료 수정 | [착지 복귀와 연결 레이어 검색](Architecture/Landing_Return_Linked_Search_2026-10-05.md) |
| 실제 BP 군중 CPU·소켓 검증과 동시 공격 비용 | [실제 캐릭터 애니메이션 검증](Architecture/Authored_Animation_Crowd_Network_2026-10-05.md) |
| 리타깃 파이프라인과 배경 | [런타임 리타깃 구조](Architecture/Runtime_Retarget_HandIK_Architecture.md) |
| 무기 부착·파지·궤적·공격 종료 복귀 | [무기 파지와 손 접촉 통합 가이드](Authoring/Weapon_Hand_Contact_System.md) |
| 외형·의상 리더 선택 | [외형 메시 소유권](Architecture/Visual_Presentation_Mesh_Ownership.md) |

## 에디터 제작과 디버깅

1. [제작 가이드](Authoring/README.md): 몸체 DA, Palm·WeaponGrip 소켓, ABP의 오른팔·왼팔 연결.
2. [진단 기록](Diagnostics/README.md): 파지·팔꿈치·TIP의 로그와 수정 근거.
3. [계획](Planning/README.md): 파이프라인 제안과 스레딩 감사의 후속 범위.

현재 대검의 확인된 구성은 Idle 오른손 파지 → Two-Hand Grip IK 구간 양손 파지 → 왼손 해제다. 다른 무기는 보조 접촉 기능과 기본 가중치로 한손·항상 양손을 선택한다. 세부 설정은 [통합 가이드의 보조 손 절](Authoring/Weapon_Hand_Contact_System.md#secondary)에 모은다.

상체 전투 합성은 [전투](../Combat/README.md), 원격 복제 검증은 [네트워크](../Networking/README.md), 다수 플레이어 비용은 [성능](../Performance/README.md)에서 확인한다.
