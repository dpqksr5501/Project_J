# 애니메이션 제작 가이드

| 작업 | 문서 |
| --- | --- |
| 캐릭터 임포트·스켈레톤 리타깃 설정 | [휴머노이드 리타깃 표준](HumanoidSkeletonRetargetingStandards.md) |
| 이동 프로필·DB 확장 | [데이터 기반 이동 확장](DataDrivenLocomotionExtensionGuide.md) |
| 몸체 보정·Idle 부착·오른팔·왼팔·공격 구간·복귀 | [무기 파지와 손 접촉 통합 가이드](Weapon_Hand_Contact_System.md) |

## 손 접촉을 설정할 때 읽는 순서

1. [몸체 보정 절](Weapon_Hand_Contact_System.md#body)의 몸체 프로필과 손바닥 소켓 설정.
2. [정상 부착 절](Weapon_Hand_Contact_System.md#mount)의 무기 DA와 Idle 목적지.
3. [보조 손 절](Weapon_Hand_Contact_System.md#secondary)의 왼팔 입력 및 기존 Two-Hand Grip IK 구간.
4. [진단](../Diagnostics/README.md)에서 무기·손·팔꿈치 측정 방법 확인.

몸체 프로필은 해부학·보정, 무기 프로필은 Grip·부착·접촉 기능, 몽타주 스테이트는 파지 구간을 담당한다. 개별 몸체·무기 조합의 도달 범위와 추가 물리는 별도로 검수한다.
