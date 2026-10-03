# Palm과 WeaponGrip을 공유하는 정상 부착

갱신일: 2026-10-02. 기존 소켓 부착과 호환하면서 Idle·손 주도 공격·소스 공격 복귀의 파지 기준을 통일한다. [Guided 손 접촉](../../../../../Animation/Authoring/Weapon_Hand_Contact_System.md#body), [접촉 복귀](../../../../../Animation/Authoring/Weapon_Hand_Contact_System.md#recovery)를 함께 읽는다.

## 데이터 소유권

| 데이터 | 소유하는 값 | 공유 범위 |
| --- | --- | --- |
| Hand Grip Profile | 몸체 Palm 소켓, 실제 팔 역할, 손 접촉 Offset, 굽힘 안정화 | 같은 몸체를 쓰는 직업들이 공유 |
| Weapon Presentation Profile | 무기 Actor, 무기 Grip, 정상 부착 방식, 소스 공격 기본 정책, 납도 소켓 | 무기 외형/규격 |
| Attack Definition / Weapon Motion Notify | 공격별 손 주도·소스 주도, 접촉 Alpha, 구간별 Motion Keys | 공격/구간 |
| 소스 ABP와 전투 레이어 | 이동 Motion Matching, 상체 합성, 몽타주·AO | 공용 애니메이션 소스 |
| 몸체 런타임 ABP | 리타깃, 접촉 솔버, 몸체 추가 뼈의 물리 | 몸체 스켈레톤 |

새 DA 종류를 추가하지 않는다. 인라인 Calibration은 기존 콘텐츠의 호환 경로로 남긴다. 공유 몸체 프로필을 쓰면 인라인 값을 따로 맞출 필요가 없다. 직업별 ABP 이름이 남아 있더라도 몸체 보정을 직업 코드에 하드코딩하지 않는다. 모든 몸체 ABP/물리 에셋 통합은 이번 변경 범위가 아니다.

## 선택 가능한 부착 방식

기존 무기 DA의 **Weapon → Attachment → Drawn Attachment Mode**에서 선택한다.

- **Socket (Compatibility / Custom Mount)**: 기존 Drawn/Visual Drawn 소켓에 무기 Actor 루트를 항등 상대 변환으로 붙인다. 기존 에셋의 기본값이다. 특수 거치, 움직이는 무기 스켈레톤, 기존 작성 방식에 사용한다.
- **Primary Grip to Body Palm**: 몸체의 Primary Palm과 무기의 Primary Grip 및 몸체 Primary Hand Offset으로 루트의 손 뼈 상대 변환을 계산한다. Visual Drawn 소켓은 정상 파지 계산에 쓰지 않고 대체 경로로 남긴다.

납도는 계속 Sheathed/Visual Sheathed 소켓을 쓴다. 소스 공격의 Drawn Socket은 작성된 공격 궤적을 정의하므로 Palm 모드에서도 유지한다. 이름의 R/L 대신 Primary/Secondary 역할과 설정된 소켓·뼈를 읽는다. 공격 IK를 쓰지 않는 직업도 정상 Palm 부착만 사용할 수 있다.

## 계산과 소유권 전환

```text
Idle / 손 주도 공격:
  몸체 Palm 로컬 + Body Offset + Grip의 Actor 루트 상대 변환
  → 고정 Root-in-Hand 부착 → 입력 손 포즈가 무기를 움직임

소스 주도 공격:
  작성된 소스 소켓 + Motion Keys → 독립 무기 Grip 목표
  → 몸체 Palm 위치·방향에서 실제 손목 목표 역산 → Guided 팔 보정

복귀:
  독립 소스 목표로 손 IK 해제 + 같은 정상 Root-in-Hand 목적지로 무기 블렌드
```

게임 스레드에서 소켓 로컬 데이터와 프로필을 읽는다. 정상 부착을 이전 IK 손의 월드 위치로 보정하지 않는다. StaticMesh 소켓의 작성된 로컬 변환과 Grip 컴포넌트부터 Actor 루트까지의 상대 변환을 합성한다. `GetSocketTransform(RTS_Component)`도 엔진 내부에서 월드 변환을 거쳐 돌아오므로 정상 부착 보정에는 쓰지 않는다. 자식 무기 메시의 위치·회전·균일 스케일을 포함한다. 무기 루트 부착 스케일은 기존 Snap 규칙처럼 1이고, 자식 메시 크기는 유지한다. Grip 마커의 스케일을 상쇄하려고 무기를 줄이지 않는다. 공격 손목 변환도 무기 마커 크기를 손뼈 크기로 전달하지 않고 몸체의 현재 뼈 스케일을 유지한다.

발도, 정상 Refresh, 소스 모션 복귀와 Contact Handoff를 끈 기존 exit blend가 같은 목적지 계산을 사용한다. 복귀 목적지는 항상 항등이라고 가정하지 않는다. Palm 모드의 Primary IK는 정상 부착 중 항상 억제한다. 기존 `Allow Primary IK On Visual Attachment`를 켰더라도 이 모드에는 순환을 허용하지 않는다. 공격·복귀의 독립 접촉은 기존 Alpha/몽타주 가중치 정책을 따른다.

## 수명·비용·대체 경로

- 프로필 우선순위는 보이는 ABP Hand Grip Profile → 캐릭터 애니메이션 프로필의 공유 Profile → 인라인 Calibration → 기본값이다. Idle 부착과 공격 변환이 같은 함수에서 이 우선순위를 읽는다.
- 무기 Grip 소유 컴포넌트 검색은 기존 캐시를 사용한다. 정상 부착은 매 프레임 재계산하지 않는다. Retarget 인스턴스 등록 뒤 다음 애니메이션 조회에서 새 프로필을 반영한다.
- 게임 중 몸체/프로필 또는 무기 자식 변환을 바꾸는 호출자는 Weapon Presentation Component의 **Refresh Attachment Calibration**을 호출한다. Idle은 기존 Actor를 보정하며, 활성 소스 공격은 즉시 손 부착으로 바꾸지 않는다. 종료 경계에서 현재 몸체/프로필을 다시 읽고, 복귀 중 변경은 복귀 완료 뒤 반영한다. 장비 시스템의 기존 Refresh/파괴 수명도 유지한다.
- 자동 Palm 부착에는 실제 Palm 소켓이 필요하다. `Legacy Wrist Origin`은 이전 IK 목표 호환 정책이며 Palm 없는 정상 부착을 만들어내지 않는다.
- 없는 Palm/Grip, 잘못된 Hand 역할, 무기 루트 밖 Grip, 움직이는 SkeletalMesh Grip, 비균일·반전 스케일, 부모를 무시하는 Absolute 변환은 기존 Drawn/Visual Drawn 소켓으로 대체하고 부착 시 경고를 남긴다. 대체 소켓도 없으면 부착 실패를 기록한다. 움직이는 Grip이나 전단을 고정 부착으로 정확히 처리한다고 가정하지 않는다.
- 추가 컴포넌트 틱·RPC·프레임별 변환 복제를 추가하지 않는다. 전용 서버 외형 생성과 기존 타격 판정의 경계를 유지한다. 플레이어 Motion Matching/상체 합성/런타임 리타깃을 교체하지 않는다.
- 원격·URO·LOD·표시 예산·텔레포트의 기존 정책은 유지한다. 군중 성능, 모든 몸체·무기 조합의 시각 품질을 자동화만으로 보장하지 않는다.

## 지금 에디터에서 할 일

1. C++ 빌드 반영 후 에디터를 열고 무기 Presentation DA의 **Drawn Attachment Mode = Primary Grip to Body Palm**을 선택한다.
2. `Primary Grip Socket Name = WeaponGrip_R`를 유지한다. 소스 `Drawn Socket Name = WeaponSocket_Greatsword_Combat`, 납도 소켓과 기존 Visual 소켓도 유지한다.
3. 몸체 런타임 ABP의 클래스 디폴트에 `DA_HGP_Greatsword`가 할당됐는지 확인한다. DA의 Palm `PalmGrip_R/L`, Hand/UpperArm/Forearm은 실제 몸체 이름을 쓴다. Guided 노드 연결은 그대로 유지한다.
4. Idle와 공격의 공통 파지가 뜨면 몸체 **Palm 위치·회전** 또는 **Primary Hand Offset**을 조정한다. Palm에 검 원점을 프리뷰 부착한 모습은 Grip 정렬 결과와 다르므로 PIE에서 최종 접촉을 확인한다. 기존 Visual 소켓을 맞추는 방식은 Socket 모드에서만 정상 부착에 적용된다.
5. Idle → 평타 → Idle, 연계/캔슬, 납도, AO, 몸체·장비 교체를 확인한다. Weapon Motion 스테이트를 추가할 필요는 없다. 구간별 키나 손 해제에 필요한 스테이트는 유지한다.

실제 DA를 자동 저장하거나 일괄 변환하지 않았다. 새 모드는 기존 DA에서 명시적으로 켠다.

## 진단과 제한

기존 `ProjectJ.Presentation.GripTrace 1`을 사용한다. `[GripTrace][Attachment]`의 `context=AttachmentResolved`, `configured=PrimaryGripContact`, `reason=PrimaryGripContact`, `appliedPalmMount=1`과 실제 부착 Hand를 확인한다. `WeaponContact.calibratedErr`는 몸체 Offset 적용 후 실제 접촉 오차다. 복귀 중 독립 목표 오차와 구분한다. 성능 측정 전 진단을 끈다.

이 변경은 Idle·공격의 작성 기준과 복귀 목적지 중복을 해결한다. 팔꿈치 급변 완화는 기존 입력 포즈 굽힘 방향 유지/재획득과 몽타주 가중치 복귀가 담당한다. 팔 길이 밖 목표, 쇄골/가슴 Reach, 양손 동시 제약, 관절 제한과 손가락 포즈는 별도 문제로 남는다.

### 새 모드가 그대로 보일 때의 캡처

PIE 콘솔에서 한 줄씩 입력한다. 별도 DA나 그래프 변경은 필요 없다.

```text
ProjectJ.Presentation.GripTraceHz 60
ProjectJ.Presentation.GripTrace 1
ProjectJ.Animation.GuidedIKTraceHz 60
ProjectJ.Animation.GuidedIKTrace 1
```

Idle 3초 → 평타 2회 → Idle 3초 → 평타 2회 → Idle 3초를 재현한다. 공격을 시작하기 전 Idle에서도 애니메이션 조회가 기존 진단 틱을 깨운다. 종료 후 아래 두 명령을 입력하고 에디터 재시작 전에 `Saved/Logs/Project_J.log`를 보관한다.

```text
ProjectJ.Presentation.GripTrace 0
ProjectJ.Animation.GuidedIKTrace 0
```

| 기록/값 | 확인할 내용 |
| --- | --- |
| Attachment: configured, weaponProfile | PIE에서 실제 사용한 무기 DA와 요청 모드. 에디터에서 수정한 DA와 경로가 같은지 확인한다. |
| bodyProfile, calibration, characterProfile | 실제 몸체 DA와 VisualOverride/CharacterSharedProfile/CharacterInline/Defaults 우선순위. |
| reason=PrimaryGripContact | Palm 자동 정렬 계산 성공. 정상 부착에서 appliedPalmMount=1, parentMatch=1이어야 한다. |
| reason=ConfiguredSocket | 기존 Socket 모드 사용. |
| MissingPalm / InvalidPalmHand / InvalidPalmTransform | Palm 누락, 손 역할·부모 불일치, 무효 로컬 변환. |
| MissingWeaponGrip / SkeletalWeaponGrip / GripOutsideWeaponRoot | 무기 Grip 누락 또는 고정 부착 조건 불충족. |
| UnsupportedBodyScale / UnsupportedHandScale / UnsupportedWeaponChildScale | 목적지 몸체·손 또는 작성된 무기 자식의 비균일·반전·특이 스케일. 이전 부착의 Root 월드 스케일은 새 목적지를 거부하는 조건이 아니다. |
| UnsupportedWeaponGripComponent | 고정 StaticMesh 소켓의 작성 데이터를 제공하지 않는 Grip 컴포넌트. 특수 Grip은 Socket 모드 또는 별도 명시적 제공자가 필요하다. |
| SocketMountedWeaponChild / AbsoluteWeaponRoot / AbsoluteWeaponChild / InvalidContactTransform | 자식 소켓 부착, 절대 변환, 접촉 보정 변환 제한. |
| expectedParent/Socket, actualParent/Socket | 계산한 목적지와 실제 부착 비교. 공격·복귀에서는 실제 소유권이 달라지는 것이 정상이다. |
| mountErrorValid, worldErrCm, rotationErrDeg, relativeErrCm | 정상 부착에서만 목적지 오차를 비교한다. 공격·복귀의 -1은 측정 비적용이며 오류 증거가 아니다. |
| AttachmentCalibration | Palm 로컬·Body Offset·Grip-in-Root·예상/실제 Root 상대 변환·몸체 스케일·복귀 목적지. |
| AttachmentScale | 몸체·손·현재 Root 월드 스케일, Grip 자식 상대 스케일 및 작성된 Grip-in-Root 스케일을 소수 9자리로 기록한다. `uniform`은 접촉용 스케일 검사 결과이며 현재 Root 값은 관측용이다. |
| WeaponContact / GuidedIK | 실제 검–손 접촉 오차와 입력/솔버/최종 팔 변화를 구분한다. |

설정 진단은 최대 2Hz와 전환 이벤트에만 기록한다. 실제 포즈/접촉은 기존 요청 빈도를 따른다. 보이는 애님 인스턴스가 없어도 설정 진단을 먼저 남긴다. 로그는 게임 스레드의 관측이며 부착·프로필·IK 목표를 변경하지 않는다. 진단 off에서는 이 목적지 재계산과 문자열 생성을 수행하지 않는다.

추가 진단의 직접 UBT 빌드와 `PrimaryGripAttachment`, `VisualMeshOwnership`, `GuidedIKTraceIsolation` 테스트 3개가 성공했다. `Saved/Logs/AttachmentDiagnostics_Automation.log`에서 자동 정렬 성공과 누락 애님/기본 설정 대체 기록을 확인했다. 테스트는 진단의 관측 불변성과 Idle 진단 틱 시작을 검사하며, 실제 사용자 DA 연결 검증은 PIE 캡처로 수행한다.

### Idle 정렬이 적용되지 않은 실제 캡처와 수정

2026-10-02 23:18 저장된 사용자 로그의 마지막 PIE 구간에서 실제 무기 DA는 `DA_Greatsword_Presentation`, 몸체 DA는 `DA_HGP_Greatsword`, 요청 모드는 `PrimaryGripContact`였다. 그런데 발도 중 `UnsupportedWeaponRootScale` 또는 `UnsupportedHandScale`로 대체되어 `appliedPalmMount=0`, 실제 소켓은 `WeaponSocket_Visual_R`였다. 공격 전·복귀 후 Idle 접촉 오차는 약 2.243cm, 방향 오차는 약 35.714도였다. 기존 목적지에 대한 `worldErrCm=0`은 대체 Visual 부착이 정확하다는 뜻이며 Palm 접촉 성공을 뜻하지 않는다.

이전 소켓에서 상속된 Root 월드 스케일을 새 Palm 목적지의 허용 조건으로 검사하던 오류를 제거했다. Grip-in-Root도 작성된 로컬 데이터만으로 계산한다. 목적지 몸체·손 및 작성 변환의 스케일 검사는 공통 함수로 유지하되, float 포즈가 double로 변환될 때 생기는 수치 차이는 최대 축 대비 0.01%까지 허용한다. 실제 비균일·반전·영 스케일은 계속 거부한다. 기존 로그는 소수 2자리여서 `UnsupportedHandScale`이 미세 오차인지 실제 비균일인지 확정할 수 없으며, 새 `AttachmentScale` 로그로 구분한다.

확인할 성공 조건은 발도 Idle의 `reason=PrimaryGripContact`, `appliedPalmMount=1`, 실제 부착 Hand 및 `WeaponContact.calibratedErr` 감소다. 무기 Actor 원점을 Palm에 놓는 방식이 아니라 **검의 Primary Grip을 몸체 Palm 접촉점에 맞추는 방식**이다. 소켓 이름에만 매달리는 Visual 보정과 구분한다. 새 코드를 적용한 사용자 PIE 검증은 별도로 필요하다.

수정 후 직접 UBT 빌드 성공, 컴파일 경고 없이 종료 코드 0이다. `HandContactConversion`, `PrimaryGripAttachment`, `VisualMeshOwnership` 자동화 3개 모두 Success, 종료 코드 0이다. 로그는 `Saved/Logs/IdleAttachmentScale_Automation.log`. 이전 Root 월드 스케일이 (2,3,4)여도 동일한 Palm 목적지를 계산하고 재부착 후 접촉이 일치함을 검사했다. 수치 오차 수준의 몸체·손 스케일은 허용하고 실제 비균일 스케일은 계속 대체 경로로 보낸다. 기존 접촉·공격·복귀·진단 관측 불변성 검증도 통과했다. 사용자 `Project_J.log`는 덮어쓰지 않았다.

## 검증 결과

2026-10-02 직접 `UnrealBuildTool.exe`의 `Project_JEditor Win64 Development` 빌드 성공. 최종 변경 빌드에 컴파일 경고가 없다. 아래 자동화 12개가 모두 Success, 각 프로세스 종료 코드 0이다.

- 접촉/솔버/몸체: `HandContactConversion`, `HandContactRig`, `GuidedArmContact`, `GuidedBendStability`, `GuidedIKTraceIsolation`, `VisualMeshOwnership`, `PrimaryGripAttachment`.
- 장비·기존 Grip·판정: `WeaponPresentationIdentity`, `WeaponPresentationTeardown`, `StableGripTargetsAndAuthoredAlpha`, `CanonicalMeleeTrace`, `CanonicalBladeTrajectory`.

로그는 `Saved/Logs/PrimaryGripAttachment_Automation.log`, `PrimaryGripAttachment_Lifecycle.log`다. 새 부착 테스트는 무기 메시 상대 이동/회전/균일 스케일, 몸체 스케일, 기존 기본값, 순환 억제, 공격·비항등 복귀, 공격 중 보정 변경, 기존 Actor 유지, 몸체 메시 교체, handoff 없는 exit blend, IK 없는 직업, 누락 Palm, 비균일·Absolute 변환 거부와 납도를 검사한다. 테스트용 소켓/메시는 메모리에서만 만들고 프로젝트 에셋을 저장하지 않는다.

저장된 `ABP_Greatsword_Woman_RunTIme` 한 개의 범위 컴파일도 오류 0·ABP 경고 0·로드 실패 0·종료 코드 0이다. 로그는 `Saved/Logs/PrimaryGripAttachment_Blueprint_Scoped.log`. 프로세스 요약의 경고 1개는 설치된 MCP 플러그인 시작 안내이며 MCP 도구 호출은 없다.

이후 사용자가 에디터에서 Palm 부착 설정을 적용하고 Idle 파지가 정상 동작함을 확인했다. 기존 Visual 소켓을 제거하는 구성도 정상 동작한다고 보고했다. 이는 사용자 시각 확인이며 새 PIE 로그의 수치 오차를 자동 재측정한 결과는 아니다. 공격 중 양손 파지 연결과 추가 검수 범위는 [보조 손 접촉](../../../../../Animation/Authoring/Weapon_Hand_Contact_System.md#secondary)을 따른다.
