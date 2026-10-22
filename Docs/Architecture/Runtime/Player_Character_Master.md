# 플레이어 캐릭터와 애니메이션 마스터

## 현재 제작 구조

```text
AProject_JMoverPawn → AProject_JBaseCharacter → AProject_JPlayerCharacter
  └─ /Game/Character_BPs/BP_Player_Master
       └─ /Game/Character_BPs/GreatSword/BP_GreatSword

UProject_JPlayerAnimInstance
  └─ /Game/Animation_Logic/ABPs/ABP_Player_Master
       ├─ 기존 전투 Linked Anim Layer
       └─ 기존 탑승 Linked Anim Layer
```

캐릭터와 ABP의 상속 트리를 일대일로 복제하지 않는다. 플레이어의 공통 컴포넌트와 기본값은 `BP_Player_Master`, 공통 포즈 흐름은 `ABP_Player_Master`가 제공한다. 직업·장비·몸체 차이는 기존 프로필과 레이어로 공급한다. 사람형 NPC는 필요한 애니메이션 계약을 공유할 수 있지만 플레이어 입력·카메라·PlayerState 소유권까지 상속하지 않는다. 동물·탈것을 이 플레이어 부모 아래로 이동하지 않는다.

## 책임과 데이터

| 위치 | 책임 |
| --- | --- |
| native Player/기존 컴포넌트 | Mover 입력, GAS 연결·권위, 장비·표현 수명, 입력 바인딩, 시각 예산 |
| `BP_Player_Master` | 공통 Manny source/마스터 ABP, 입력 자원, 공통 캐릭터 애니메이션 프로필과 컴포넌트 기본값 |
| `BP_GreatSword` | 직업 정의, 대검 시작 장비, 직업 입력 매핑, 비표준 몸체와 의상 구성 |
| CharacterAnimProfile | 이동·전투·Foley·몸체 보정 프로필의 제작 진입점 |
| native AnimInstance/Proxy | GT 수집과 worker 데이터, 선택·전환·핀 계산 |
| 마스터 ABP/Linked Layer | 포즈 연결·노드별 콜백 시점·슬롯·직업/탑승 표현 |

테스트용 `BP_Player`를 `BP_Player_Master`로 승격했다. 테스트에 남아 있던 Avatar 로컬 AttributeSet, 기본 능력·속성 데이터는 native 기본값으로 정리했다. 플레이어의 지속 상태와 능력 부여는 기존 PlayerState/직업 계약이 담당한다. `CharacterAnimProfile`과 동일한 직접 `LocomotionProfile` 값도 제거하여 프로필을 통해 해석한다. native fallback 자체는 다른 제작·테스트 소비자가 있으므로 유지한다.

대검은 기존 BP 경로와 에셋 정체성을 유지한 채 부모를 변경했다. GameMode·레벨·기존 제작 데이터가 새 복사본을 잘못 가리키는 문제를 피한다. Content 안에 레거시 캐릭터 복사본을 추가하지 않았고 변경 전 네 에셋은 `Saved/Validation/PlayerMaster_20261021/Backup`에 보관했다. 원본은 Git 이력에서도 복구할 수 있다. `AProject_JGreatswordCharacter`는 동작 없는 native fixture 타입으로 기존 C++ 테스트와 파생 검증 타입이 소비하므로 보존하며, 생산 BP의 부모는 아니다.

## 의상 포즈 연결

기존 `UProject_JModularMeshComponent`의 `bFollowAttachParentPose`를 켠 authored 파츠는 등록 시 skeletal attach parent를 LeaderPose로 사용한다. 대검 의상 8개는 실제 Bip01 표시 몸체에 붙이며 숨은 Manny source에 직접 연결하지 않는다. 등록 해제·재등록·부모 교체·분리에서도 기존 source를 해제하고 현재 부모를 사용한다. BP Construction Script의 연결 호출 8개를 포함한 노드 29개를 제거했다. 공유 파츠의 bone pose 평가는 끄며, 일반 파츠의 component Tick도 끈다. **독립 Chaos Cloth가 연결된 파츠는 예외적으로 component Tick이 필요하다.** 활성 cloth 메타데이터/리소스로 판단하고 전용 파츠 이름이나 두 번째 예산 시스템을 만들지 않는다. Dedicated Server에서는 cosmetic cloth Tick을 켜지 않는다.

공통 본만 따라가는 파츠에는 LeaderPose를 사용한다. 파츠 자체의 추가 뼈를 독립적으로 평가해야 하면 자동 LeaderPose를 끄고 기존 `AttachAndCopyPose`와 해당 Post Process를 사용한다. `AttachAndSetLeader`로 생성한 장비도 동일한 수명 정책을 사용한다. 같은 source에 다시 연결할 때 강제 갱신하지 않아 물리 상태 재생성을 피한다. 장비의 mesh/pose source를 설치한 **뒤에** 등록하여 cloth/Post Process가 빈 source로 시작하지 않게 한다. 슬롯 교체는 기존 visual revision·lease 취소/해제 계약을 유지하고 표시 몸체의 AnimInstance나 전체 물리를 초기화하지 않는다. LeaderPose만으로 draw call이나 머티리얼 비용까지 제거되지는 않는다.

### 대검의 실제 2차 물리 소유권

`ABP_Greatsword_Woman_RunTIme`의 retarget/손 보정 뒤에 있는 세 RigidBody 노드가 망토 24개, 머리카락 4개, 치마 2개의 dynamic bone을 계산한다. `cloak_Physics`, `GreatSword_Woman_Physics`, `sho_Physics`를 각각 사용한다. dynamic bone writer 중복은 없으며, 몸통 등의 공통 kinematic collision body는 서로 다른 solver의 지지대여서 단순 중복으로 제거하지 않는다. 의상은 최종 본 포즈를 공유한다. 현재 몸체/의상에 Post Process ABP가 있다는 가정으로 설명하지 않는다.

망토 메시에는 이전 관통 해결 실험의 embedded Chaos Cloth 데이터 3개가 남아 있었고, 별도 vertex solver도 실행되고 있었다. 사용자 요청에 따라 mesh 원본을 `Saved/Validation/SecondaryPhysics_20261021/Backup/cloak.uasset`에 SHA-256 검증 후 보관하고, 엔진 Cloth Editor와 같은 unbind/LOD section 갱신/embedded object 해제 경로로 제거했다. 골격·skin weights·머티리얼·LOD 수·`cloak_Physics`와 세 RigidBody 노드는 보존했다. 메시 파일은 3,087,169 → 773,925바이트로 줄었다. 이 백업은 로컬 복구용이며 테스트/패키징 의존성이 아니다. Git의 원본 이력도 유지한다.

현재 망토에는 독립 vertex cloth가 없으므로 의상 8개의 Tick을 꺼도 뼈 물리는 표시 몸체에서 계속 계산된다. 앞으로 Chaos Cloth 장비를 추가하면 위 Tick 예외가 자동 적용된다. 물리 구동 확인은 모든 동작의 관통 방지를 보증하지 않는다. 관통은 해당 PA의 몸통 collider·constraint·충돌 pair·skin weight·체인 길이를 실제 포즈와 함께 튜닝한다.

## 애니메이션 확장과 비용

현재 마스터 ABP는 BP 자체 상태 변수 없이 native 계산을 소비한다. 직업마다 지상·공중 그래프를 복제하거나 모션 목록을 부모 BP의 분기문에 넣지 않는다. 후보·튜닝은 Chooser/PSD/프로필, 다른 포즈 동작은 기존 Animation Layer Interface와 레이어를 사용한다. 동일한 노드 함수라도 재생 시각·커브·NodeRef·레이어별 aim 한계가 다르면 결과를 전역 캐시로 합치지 않는다.

기존 `AnimationUpdateCoordinator`, source mesh budget, 장비 표시 큐와 프로필의 중요도 설정을 유지한다. 새 전역 manager·두 번째 Tick/예산 시스템·모든 레이어의 하드 참조 목록은 추가하지 않는다. 이번 부모 정리는 대규모 인원 성능 측정을 대신하지 않는다. 서버 판정과 시각 갱신 빈도는 별도 계약이다.

엔진 소스 패치 없이 프로젝트의 native 컴포넌트·프로필·에디터 도구를 사용한다. Mover·애니메이션 실험 기능의 변경은 해당 adapter와 회귀에서 확인하며, 미래 엔진 호환성을 보장하는 별도 프레임워크는 만들지 않는다.

## 제작 시 주의와 검증

새 플레이어 BP는 `BP_Player_Master`의 자식으로 만들고 기존 프로필을 지정한다. 값만 달라지는 직업마다 빈 native 클래스를 추가하지 않는다. 추가 런타임 행동이 실제로 필요한 경우에만 native 확장을 검토한다. NPC의 상태 소유권과 플레이어 부모를 혼동하지 않는다.

Mover의 class-owned inline mode는 자식 BP의 archetype이므로 `RF_Public | RF_ArchetypeObject` 계약을 지켜야 한다. 이전 제작 함수에서 public 플래그 누락을 수정했고, 기존 템플릿은 editor 전용 `FinalizePlayerAvatarTemplates`로 보정한다. 이동 모드 설정은 바꾸지 않으며 저장 검사를 끄지 않는다.

마스터 이관 당시 76개 회귀가 경고·실패·미실행 없이 통과했다. 새 검사는 실제 부모·자식 상속/기본값, native 컴포넌트 단일성, 8개 의상 실제 연결, 등록·활성화·교체·해제 수명과 상속 템플릿 저장 계약을 확인한다. 기존 handover·실제 대검의 PlayerState/GAS 소유권·표시 몸체 검사를 실행기에 포함했고, 마지막 검사의 예전 CMC `ACharacter` 타입 가정도 현재 플레이어 Mover 타입으로 갱신했다. 대검의 제작 기본값·몸체·입력 매핑은 변경 전후 비교했다.

앞선 마스터 이관의 Editor/Game Development direct UBT 빌드가 통과했다. MCP로 시작한 새 네트워크 PIE에서 지상 2,134표본의 OTM/Strafe·Start/Stop, 공중/착지 8조건 4,187표본과 공중 재선택·재생 되감기 0건, 전신 몽타주 6개 280표본을 확인했다. 몽타주 하체의 클립 대비 최대 회전 차이는 약 0.0541도였다. 두 월드 1,278표본의 의상 검사는 공통 본 연결만 증명했으며 당시 남아 있던 vertex cloth 실행까지 증명한 검사는 아니었다. 후속 점검에서 이 구분을 바로잡고 cloth Tick 예외와 실험 데이터 정리를 적용했다.

로컬 원자료는 `Saved/Validation/PlayerMaster_20261021`, 최종 회귀는 `Saved/Validation/PlayerLocomotion/PlayerMasterComplete_20261021/Automation/index.json`이다. 첫 저장 실패의 private archetype 진단과 예전 CMC 타입 테스트 실패도 보존했다. 초기화/Iris 대상 경고는 마지막 GUI에서 발생하지 않았으며 MCP 라이선스 안내는 별개다. 콘텐츠·맵 미저장 항목 0개를 확인하고 테스트용 편집기 설정을 복원한 뒤 에디터를 정상 종료했다. 전체 448개 모션 외형·대규모 성능·Cook/Shipping 검증을 의미하지 않는다.

### 의상 물리 후속 검증

실험 데이터 정리 후 Editor/Game Development direct UBT 빌드가 통과했고, 최종 **80개 회귀가 경고·실패·미실행 0건**으로 통과했다. `EquipmentLoadLifecycle`은 최초 등록의 포즈 연결, 같은 완료 콜백의 중복 방지, 슬롯 교체 시 이전 mesh 파괴, 이전 item 완료의 재생성 거부와 lease 해제를 확인한다. 기존 `EquipmentRetry`, 일반 cloth의 예산 제외/등록·활성화·mesh 교체 정책도 실행기에 포함했다. Cloth 정책 fixture는 생산 망토나 로컬 백업에 의존하지 않는 transient render-section 메타데이터이며, 미래 Chaos solver의 품질 테스트를 대신하지 않는다. 첫 회귀의 이 두 fixture 실패는 editor가 cooked cache bit가 아닌 실제 section 메타데이터로 활성 cloth를 계산하는 차이를 반영해 수정했다.

MCP로 시작한 PIE에서 지상 OTM/Strafe·Start/Stop·Walk/Run/Sprint 2,115표본, 공중/착지 8조건 4,115표본(공중 재선택 증가 2, 무효 후보·되감기 0), 전신 몽타주 6개 261표본을 확인했다. 몽타주 하체 최대 차이는 약 0.05384도였다. 병행한 두 월드의 완료된 물리 포즈 1,266표본에서 망토/헤어/치마의 30개 dynamic bone이 모두 변화했고, finite transform·세 solver 생성·dynamic writer 중복 0·의상 AnimInstance/Tick 0·독립 cloth instance 0을 확인했다. 원본/복제 PA의 900프레임 물리 동등성 검사도 통과했다(91 actor/30 dynamic, 최대 회전 차이 약 0.00000342도). 실제 화면에서 파츠 누락·분리는 보이지 않았다.

후속 원자료는 `Saved/Validation/SecondaryPhysics_20261021`, 최종 자동 보고서는 `Saved/Validation/PlayerLocomotion/SecondaryPhysicsFinal_20261021/Automation/index.json`이다. 진단 중 잘못 호출한 Python getter 오류 및 첫 fixture 실패는 수정 전 근거로 보존한다. GUI 로그에는 단일 LOD 제작 검토 안내와 무거운 물리 관측 중 `Independent Interpolation fell behind` 1건이 있어 GUI 전체 경고 0건으로 보고하지 않는다. 별도 새 에디터에서 상세 물리 관측 없이 같은 공중/착지 8조건을 다시 실행한 4,213표본은 통과했고, 해당 보간 경고는 재현되지 않았다(`LightReplayEditor.log`, `AirLanding_20261021_214520_495596.json`). 관측 부하의 영향 가능성은 있으나 원인으로 확정하지 않으며, 경고를 감추는 설정 변경도 하지 않았다. 이 점검으로 모든 네트워크 부하의 안정성이나 모든 포즈의 망토 관통 방지까지 보장하지 않는다. 종료 시 미저장 콘텐츠/맵 0개와 background throttle 복원을 확인했고 GUI/자동화 에디터를 정상 종료했다.

- [로코모션 아키텍처](../../Animation/Architecture/Player_Locomotion_Architecture.md)
- [이동 콘텐츠 제작](../../Animation/Authoring/DataDrivenLocomotionExtensionGuide.md)
- [모듈러 의상 제작](../../Gameplay/Authoring/ModularCharacterEquipmentGuide.md)
