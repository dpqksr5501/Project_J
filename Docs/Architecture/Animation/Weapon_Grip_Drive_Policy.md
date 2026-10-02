# 무기 궤적과 손 파지의 소유권

갱신일: 2026-10-02. 접촉 복귀는 [Weapon_Contact_Recovery.md](Weapon_Contact_Recovery.md), 몸체와 노드는 [Guided_Hand_Contact.md](Guided_Hand_Contact.md)를 기준으로 한다.

## 런타임 규칙

| 상태 | 무기의 움직임 | 오른손 IK | 왼손 IK |
| --- | --- | --- | --- |
| 발도 Idle, VisualHand 공격 | 보이는 `WeaponSocket_Visual_R` | 기본 0. 이미 붙인 검을 같은 손이 다시 추적하지 않음 | WeaponGrip_L 추적 가능 |
| SourceAnimation 공격, 소스 주도 기본 공격 | 소스 `WeaponSocket_Greatsword_Combat` + 선택적 Motion Keys | WeaponGrip_R 접촉 추적 | 해당 공격 가중치로 WeaponGrip_L 추적 |
| 접촉 복귀 | 소스 무기 포즈에서 Visual 부착으로 블렌드 | 독립 목표와 남은 몽타주 가중치로 해제 | 기존 발도/양손 정책으로 전환 |
| 납도 | 선택한 등 소켓 | 납도 정책 | 납도 정책 |

공격 AttackTag와 프레젠테이션 상태는 기존 전투 경로에서 전달한다. 클라이언트는 활성 공격 정의와 실제 소스 몽타주에서 무기 소유권을 고른다. 무기·손 변환을 프레임마다 복제하지 않는다. 서버 판정은 기존 소스 메시의 히트 노티파이·검증을 유지한다.

비플레이어는 OwnerPresentationProfile/PresentationCombatStyle 어댑터와 활성 공격 이벤트를 제공해야 한다. 어댑터가 NPC의 모션 매칭·공격 복제를 자동 추가하지 않는다.

## Weapon Motion 스테이트 없는 공격

무기 DA에서 Supports Independent Motion과 Source Driven Montage Attacks가 켜져 있으면, `WeaponDrive = WeaponDefault`인 몽타주 공격은 Weapon Motion 노티파이 없이도 소스 소켓 궤적을 사용한다.

- 특정 공격의 손 주도는 AttackDefinition의 `VisualHand`.
- 특정 공격의 소스 주도는 `SourceAnimation`.
- `Override Grip IK`는 공격별 양손 가중치.
- Weapon Motion 노티파이는 구간의 키와 손 가중치로 활성 공격 기본 정책을 일시적으로 덮어쓴다.
- Motion Keys가 비어 있으면 추가 오프셋은 항등 변환이다.
- 노티파이가 끝나면 활성 공격 기본 정책으로 복귀한다.

스테이트 생략은 소스 궤적 선택에 대한 규칙이다. 다른 몸체의 손·팔·의상과 접촉까지 자동으로 같은 포즈가 된다는 뜻은 아니다. 구간별 오프셋/손 해제가 필요하면 노티파이를 작성한다.

## 전환

소스 진입은 현재 표시된 무기 포즈를 캡처하고 움직이는 소스 소켓으로 블렌드한다. 자동 진입과 노티파이 종료 후 활성 공격 복귀는 `Attack Entry Blend Seconds`를 사용한다.

`Use Contact Handoff`가 켜진 자동 공격 복귀는 `Follow Montage Blend Out`에 따라 outgoing 몽타주의 실제 기여 가중치를 사용한다. 현재 독립 소스 목표를 따라가며 끝에서 정상 Visual 부착과 오른손 Alpha 0으로 돌아간다. 이전의 즉시 KeepWorld 부착/IK 해제 설명을 대체한다.

노티파이 소유 또는 몽타주 미확보의 대체 복귀는 `Contact Recovery Seconds`의 유한 타이머를 사용한다. 0은 명시적 snap이다. 새 공격·장비/메시 교체·발도/납도·파괴는 수명에 맞게 복귀를 취소/정리한다. 상세 조건은 접촉 복귀 문서를 따른다.

## 에디터 설정

1. 소스 Drawn Socket Name은 소스 스켈레톤의 `WeaponSocket_Greatsword_Combat`, 보이는 Visual Drawn Socket Name은 `WeaponSocket_Visual_R`이다. 서로 다른 메시의 소유권을 구분한다.
2. 검의 `WeaponGrip_R/L`은 손잡이의 접촉점이다. 몸체 손목 위치로 옮겨 문제를 숨기지 않는다. 몸체의 `PalmGrip_R/L`이 실제 손바닥 접촉 위치·방향을 정의한다.
3. 몸체 DA `DA_HGP_Greatsword`를 런타임 ABP Class Defaults → Project J → IK → Config → Hand Grip Profile에 지정한다. Guided 노드 Primary Body Profile을 선택하고 기존 Right Grip 또는 RightWristTarget과 RightGripAlpha를 사용한다. Effector는 이미 변환된 컴포넌트 공간 손목 목표다.
4. 오른손 주도 구간의 왼손은 `bUsePrimaryHandSpaceGrip`가 true일 때 현재 오른손 포즈에 LeftGripInPrimaryHandSpace를 합성한다. false인 소스 주도 구간은 LeftGripLocation/Rotation 또는 LeftWristTarget을 사용한다. 해당 Alpha를 적용한다.
5. 그래프는 리타깃 → 손 접촉 → 의상/헤어 물리 순서다. 같은 팔에 FABRIK·Two Bone IK·Guided를 중복 적용하지 않는다. 현재 오른팔 Guided, 왼팔 미연결, RigidBody 3개 유지다.

Idle에서 검이 뜨면 Visual 소켓 부착을 조정한다. 공격 접촉만 어긋나면 Palm·몸체 Hand Offset·WeaponGrip을 확인한다. 현재 Idle 부착과 공격 접촉은 별도 작성하며, 공통 접촉 기준으로 Idle 부착을 자동 계산하는 개선은 아직 구현하지 않았다.

## 검증·진단

- 대표 몽타주를 스테이트 없이 실행해 소스 궤적 선택을 확인하고 VisualHand 예외도 확인한다.
- 진입/종료, 캔슬·연계, AO, 낮은 평가 빈도에서 소유권 순환과 접촉 전환을 확인한다.
- 오른팔부터 검사하고 양손 동시 접촉은 별도 검증한다.
- 서버 판정·원격 외형과 의상/장비 교체, LOD·가시성·텔레포트를 확인한다.
- 진단은 기본 off. 성능 측정은 로그·포즈 복사 진단을 끈다.

PIE에서 `ProjectJ.Presentation.GripTraceHz 60`, `ProjectJ.Presentation.GripTrace 1`을 입력하고 재현 후 `ProjectJ.Presentation.GripTrace 0`으로 끈다. 필요하면 GuidedIKTrace도 함께 켠다. Event는 소유권 전환, Timing은 몽타주 가중치와 포즈 시각, Arm은 도달 범위, WeaponContact는 실제 표시 검과 Palm 접촉을 기록한다.

유효하지 않은 planeValid=0으로 팔꿈치 flip을 단정하지 않는다. 복귀 중 독립 목표 오차를 실제 손–검 분리로 해석하지 않는다. frame·액터·메시·월드를 일치시켜 입력/솔버/최종 물리 단계를 비교한다. 정확한 필드와 측정 제한은 [측정 기록](Weapon_Grip_Trace_2026-10-02.md)에 둔다.
