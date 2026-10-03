# 아키텍처 문서

| 주제 | 읽을 내용 |
| --- | --- |
| [런타임](Runtime/Character_Component_Ownership_2026-09-20.md) | 캐릭터 소유권, 내부 수명·갱신, 리팩터링 기록과 검증 JSON |
| [콘텐츠 확장](Extensions/Extension_Foundation_2026-09-19.md) | 직업·전직·콘텐츠 확장 기반, 제작 도구, MMO 카탈로그 |
| [네트워크](Networking/ProjectJ_Network_Baseline_Results_2026-09-04.md) | 네트워크 기준선, Iris/AOI, 원격 TIP 복제 |
| [성능](Performance/ProjectJ_Profiling_Consolidated_Summary_2026-09-06.md) | 프로파일링 기준선, 확장성 계획, 시스템 현대화 기록 |
| [애니메이션](Animation/ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md) | 애니메이션 스레딩 감사 계획과 Runtime Retarget/Hand IK 구조 |
| [계획](Planning/ProjectJ_Mmorpg_Execution_Roadmap_2026-09-03.md) | 실행 로드맵과 보류된 시스템 |
| [구조 검토](Reviews/ProjectJ_Architecture_Audit_2026-09-03.md) | 시점별 전체 구조 감사·제안. 구현 여부는 후속 문서 확인 |

검증 JSON은 대응하는 보고서와 같은 폴더에 둔다. 실제 성능 실측 원본은 [Benchmarks](../Benchmarks/SystemsModernization.md)에 둔다.

## 손 접촉·무기 모션 — 2026-10-03

| 문서 | 내용 |
| --- | --- |
| [Guided 손 접촉](Animation/Guided_Hand_Contact.md) | 현재 구현, 몸체 DA 할당, 소켓 역할, 팔꿈치 튜닝, 보조 뼈·LOD와 검증 |
| [무기 접촉 복귀](Animation/Weapon_Contact_Recovery.md) | 몽타주 가중치 복귀, 타이머 대체, 수명·네트워크 소유권 |
| [무기 궤적·파지 정책](Animation/Weapon_Grip_Drive_Policy.md) | 노티파이 없는 공격, 손 주도/소스 주도 정책 |
| [Palm 정상 부착](Animation/Primary_Grip_Attachment.md) | Idle·공격 접촉 기준 통일, 기존 소켓 호환, 장비/몸체 보정 갱신 |
| [보조 손 접촉](Animation/Secondary_Hand_Contact.md) | 공격만 양손·항상 양손·한손 정책, 왼팔 Guided 핀 연결, 현재 포즈의 기준 뼈 공간 |
| [측정 기록](Animation/Weapon_Grip_Trace_2026-10-02.md) | FABRIK 비교, 팔꿈치 안정화 전후 실측과 해석 한계 |
| [외형 메시 소유권](Animation/Visual_Presentation_Mesh_Ownership.md) | 무기·의상 리더 선택과 상체 Reach 후속 계획 |

Palm·WeaponGrip 공통 기준의 Idle 자동 부착은 무기 DA에서 선택해 사용한다. 기존 Socket 모드는 호환을 유지한다. 상체 Reach와 손가락 보정은 후속 작업이다.
