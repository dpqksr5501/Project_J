# 잔여 소스 아키텍처 고도화 적용 기록

기준: 2026-10-03, UE 5.8, 시작 HEAD `33b574a1d6775306a26357f67bc3d79311e7189b`.

[잔여 전체 소스 감사](Project_J_Remaining_Source_Maturity_Audit_2026-10-03.md)의 R01–R14를 구현했다. 기존 모듈 의존성, GAS, NPC 의사결정·이동·행동의 역할 분리, Mass 표현 전환, 판정 포즈와 시각 표현의 분리를 유지했다. 이번 변경은 실패 복구, 실행 소유권, 수신 적용 결과, 콘텐츠 제작 검증을 보강한다. 앞선 캐릭터 감사 A01–A10 전체의 구현 완료를 뜻하지 않는다.

Editor Development와 선택적 실험 플러그인 빌드가 성공했다. **최종 회귀 테스트 89개: 일반 성공 86개, 경고 동반 성공 3개, 실패·미실행·실행 중 0개.** 새 `ProjectJ.Maturity.*` 테스트 11개와 기존 장비 수명 테스트는 모두 오류·경고 없이 성공했다. 소스·검증 스크립트를 변경했으며 Config, uproject, 실제 콘텐츠 에셋은 변경하지 않았다. Unreal MCP와 실제 에셋 저장 migration은 실행하지 않았다.

## 적용 내용

| 항목 | 반영한 계약과 동작 | 검증 또는 남은 경계 |
| --- | --- | --- |
| R01 자원 불변식 | Health/Mana를 0부터 현재 최대값까지 제한하고 최대값·공격력·방어력의 음수와 비유한 입력을 제한한다. base/current 변경과 GE 실행 경로를 검사하고 서버에서 최대값 감소 시 현재값을 맞춘다. 최대값 증가로 자동 회복시키지 않는다. | `GAS.ResourceInvariants`: 실제 지속형 최대값 GE의 적용·제거, 직접 base 변경, 음수·NaN, 최대값 증가 |
| R02 Mass·행동 복구 | 행동 중단 ticket에 context, revision, 논리적 소유권 token을 보존한다. demote 취소 시 자신이 중단한 행동만 복구한다. promote의 일시적 재개 실패는 소유한 timer로 제한 재시도하며 외부 Start/Stop이 새 소유자가 되면 이전 복구를 폐기한다. 실제 활성 컴포넌트를 찾아 비활성 peer를 잘못 고르지 않는다. | `NPC.ActionSuspension`, `GroupD`, `NPCAction`. 실제 BeginPlay fixture, timer 복구, 소유자 교체, 비활성 peer와 Mass handoff |
| R03 착륙 실패 | 진행 정체·총 제한 시간·명시적 취소를 구분하고 실패한 시도 ID와 사유를 제공한다. 실패 후 Flying 이동·입력 상태로 복귀하고 착륙 cue를 정리한다. 권한 없는 취소는 거절한다. | `Mount.LandingRecovery`, 기존 Mount suite. 실제 레벨의 지형·경사·애니메이션 연동은 별도 플레이 확인 |
| R04 Notify 소유권 | WeaponMotion과 MeleeHit 상태를 mesh와 Notify 실행 ID에 귀속한다. 모션 lease와 공격 세대에 속한 hit-window token으로 Begin/Tick/End를 묶는다. 이전 End와 거절된 Begin의 End가 다음 실행을 닫지 않는다. | `Combat.HitWindowOwnership`, 기존 PrimaryGripAttachment에 실제 Notify Begin/End 중첩·중복·거절 검사 추가 |
| R05 Handover 적용 | 수신 구현의 checked apply 성공 뒤에만 영수증을 기록한다. 내부 버전·정확한 크기·유한 좌표·권한·대상을 검사한다. 같은 전송·payload·대상·소스의 replay는 이미 적용된 성공이며 다른 내용으로 같은 ID를 재사용하면 거절한다. 재진입 적용도 차단한다. | `Handover.ApplyAndAdmission`. 현재 Player snapshot은 레벨·위치·회전만 포함하는 프로토타입이며 메모리 영수증은 서버 재시작 후 보존되지 않는다. |
| R06 요청 의미·수용량 | 조회와 변경 명령을 구분한다. 변경에는 idempotency key가 필요하며 응답 유실·timeout·서버 오류는 조정이 필요한 UnknownOutcome으로 표시한다. context를 버리는 legacy mutation fallback은 거절한다. Handover에 활성/보관 건수·총 payload byte·영수증 한도를 둔다. | `Backend.MutationOutcome`, Handover 및 MMO 계약 테스트. 실제 endpoint의 업무 성공 판정·DB 영속 중복 방지는 서비스 adapter의 책임 |
| R07 그룹 복원 | 그룹 ID·원래 leader·전체 members·revision·삭제 상태를 하나의 snapshot으로 복원한다. 모든 인덱스를 먼저 갱신한 뒤 PlayerState에 알린다. 낮은 revision, 같은 revision의 다른 내용, 다른 그룹과의 멤버 충돌을 거절한다. 삭제 tombstone으로 오래된 복원에 의한 부활을 막는다. | `Social.GroupSnapshot`, 기존 소셜 계약. persistent account ID와 백엔드 revision CAS는 별도 연결 필요 |
| R08 LocalPlayer 자원 | Input Mapping Context를 LocalPlayer subsystem의 owner lease로 관리한다. 마지막 프로젝트 owner가 떠날 때 자신이 추가한 mapping만 제거한다. Controller 종료·LocalPlayer 교체 시 lease와 자신의 모바일 widget을 정리한다. 입력 callback 재진입 전에 lease 상태를 게시한다. | `Input.SharedMappingLease`: 공동 소유·반복 acquire/release·기존 외부 mapping 보존. travel·분할 화면의 실제 UI 확인은 별도 |
| R09 무기 모션 편집 | 실제 attachment socket의 월드 좌표계로 이동·회전을 변환한다. 회전된 socket, 기존 key 회전, 양의 균일 scale을 반영하며 비균일·반전 scale은 거절한다. 선택한 미리보기만 편집하고 클릭한 Slate 창의 후보가 하나일 때만 연결한다. 편집 종료·선택 상실·Escape에서 transaction을 닫거나 취소한다. | `Editor.WeaponSocketFrame`. 다중 창, 탭 이동, 드래그·Escape·Undo의 실제 Slate 조작은 수동 확인 필요. 한 창 안의 후보가 모호하면 편집 선택을 거절한다. |
| R10 DA·IK 제작 검증 | ClassId/AdvancementId를 AssetRegistry searchable tag로 노출한다. 태그가 있는 미로드 DA는 전체 로드 없이 검사하고 legacy·로드된 미저장 DA는 기존 실체 검사로 보완한다. IK 컴파일 시 runtime chain resolver를 재사용해 정적 bone/scale/자기 의존 설정을 검증한다. | `Editor.GuidedIKCompileValidation`, Authoring.Bundle. 동적 pin·profile 값은 정적 기본값으로 오판하지 않으며 기존 DA는 재저장 전까지 fallback을 사용한다. |
| R11 선택적 NPC 활성화 | 검증한 정의·Scoring·Action·TeamId를 받아 scoring 등록, 자신이 부여한 공격 GA, 행동 lease를 한 번의 활성화로 소유한다. 실패는 자신의 자원만 역순 정리한다. 재활성화·중복 행동 소유를 거절하고 Mass 중단 중 해제도 이후 자동 재개를 막는다. | `NPC.ActionSuspension`: grant 실패 복구·기존 타 소유 grant 보존·중복·중단 중 해제. 기존 NPC를 자동 변환하지 않는 opt-in 컴포넌트 |
| R12 비용 가시성 | NPC Decision의 전체 GT tick·shared snapshot·ready 결과 나이, Mass의 전체 step·eligibility·worker join·전환 대기 시간을 추가한다. 기존 budget 일부만 전체 프레임 비용으로 해석하지 않도록 측정 범위를 드러낸다. | **측정 기반까지 적용.** 실제 authored workload의 P95/P99·전투 보호로 제외된 비율·지연 측정 없이 scheduler 변경이나 FPS 개선을 주장하지 않는다. |
| R13 장비 수명 fixture | positive lifecycle fixture에 호환 pose source를 준비한다. 준비되지 않은 pose source는 계속 거절하는 assertion을 먼저 검사한다. runtime 검사를 완화하거나 기존 수명 assertion을 제거하지 않았다. | `Modernization.EquipmentLoadLifecycle` 성공 |
| R14 migration 복구 | DryRun/Apply를 명시적으로 구분한다. 원본 hash·backup·staged 파일을 기록하고 모든 저장을 준비한 뒤 검증한 파일을 게시한다. 단계별 receipt와 복구 경로를 제공하고 이미 변환된 상태의 재실행을 허용한다. 복구 전 모든 대상·backup·현재 hash를 검사하여 이후 사용자 편집을 덮어쓰지 않는다. | `Tools.MigrationReceipt`는 Saved의 가짜 파일로 중간 중단·수동 편집·재복구·손상 backup을 검사했다. 실제 그래프 preflight는 transient 복제본만 컴파일했다. Apply와 실제 에셋 복구는 실행하지 않았다. |

## 연결 시 지켜야 할 조건

### 소유권과 복구

NPC Action의 revision은 새 Start/Stop에 의한 상태 교체를 판별하고 ownership token은 활성화 주체의 논리적 lease를 판별한다. Mass 중단·재개에서는 token을 유지한다. 재개 실패의 timer는 0.25초 간격으로 최대 8회 재시도한 뒤 종료하며 실패 통계를 제공한다. 영구적인 설정 오류를 무한 재시도로 숨기지 않는다. 기존 GE·root motion·combat 보호 조건은 유지했다.

NPC Activation은 `ActivateNPC(Definition, Scoring, Action, TeamId)`로 명시적으로 사용한다. 정의의 AttackAbility가 없으면 추적 행동만 구성할 수 있다. 공격 능력을 사용한다면 기존 Action이 지원하는 instancing·실행 정책을 충족해야 한다. `DeactivateNPC`는 자신의 grant·등록·행동만 정리한다. TeamId의 영속 faction 의미는 호출자의 권한 있는 adapter가 제공한다. StateTree·기존 spawner·BeginPlay 구성을 자동으로 교체하지 않는다.

새 Input lease는 참여한 owner 사이의 계약이다. 같은 mapping을 공유하는 다른 프로젝트 기능도 이 subsystem을 사용해야 한다. acquire 이전부터 있던 외부 mapping은 보존하지만, 프로젝트가 추가한 뒤 외부 시스템이 직접 같은 mapping을 추가하는 상황의 독립 소유권은 Enhanced Input의 단일 mapping 상태만으로 판별할 수 없다.

### 통신과 소셜

`IProject_JHandoverSerializable::TryApplyHandoverSnapshot`을 성공을 보고하는 수신 계약으로 추가했다. 기존 void deserialize API는 남겼고 Player에서는 checked 구현으로 연결했다. custom 수신 구현은 checked API를 구현해야 한다. 기본값은 성공을 추정하지 않고 false를 반환한다. 영수증의 digest는 동일 내용 판별용이며 인증 수단이 아니다.

Handover 기본 한도는 활성 전송 128개, 전체 보관 record 1,024개, pending payload 16 MiB, 적용 영수증 4,096개다. overload를 명시적으로 반환하고 미검증 대용량 payload를 진단용 record에 남기지 않는다. 현재 영수증의 시간창과 actor identity는 프로세스 내부 계약이며, 권한 이전·재시작·영구 캐릭터 식별까지 보장하지 않는다.

Gateway의 `bSucceeded`는 HTTP 전송 성공이다. 변경 명령에서 `bRequiresReconciliation`이면 같은 key의 결과 조회·재전송 정책이 필요하다. 이번 변경은 자동 재시도를 추가하지 않았다. 업무 성공, 서버 트랜잭션, durable idempotency receipt는 endpoint별 typed adapter와 서버 구현에서 검증해야 한다.

알 수 없는 그룹의 `RestoreMembership`으로 leader를 추정하지 않는다. 우선 `RestoreGroupSnapshot`으로 전체 그룹을 복원해야 한다. 같은 revision의 같은 내용은 반복 복원할 수 있다. revision은 int64 고갈도 거절하지만 현재 로컬 구현 자체가 분산 서버의 영속 revision 관리자는 아니다.

### 제작 도구와 측정

에디터는 지원하지 않는 socket scale 또는 모호한 preview 소유자를 거절한다. 정확한 좌표 변환은 자동화로 확인했지만 창·탭·Undo·Escape의 전체 사용자 조작까지 headless 테스트로 검증한 것은 아니다. 태그 기반 DA 검증은 기존 DA를 강제 재저장하지 않으며 미저장 변경도 최종 실체 검사에서 확인한다.

migration은 여러 에셋의 전역 원자적 transaction이 아니다. 게시 도중 중단되면 receipt에 따라 검증된 원본으로 복구할 수 있고, 이후 편집 또는 backup 손상이 있으면 복구를 거절한다. 손상된 receipt나 알려지지 않은 파일 상태는 수동 조정이 필요하다. 실제 Apply 없이 복구 helper와 transient graph preflight를 검사했다.

R12의 추가 지표는 다음 성능 결정을 위한 근거다. Decision 결과 나이는 제출 이후 worker 시간도 포함하고, Mass worker join 비용은 전체 GT step과 함께 해석해야 한다. 기존 Presentation queue 나이 지표도 유지했다. 실제 캐릭터·전투·전환 workload에서 profiler로 지연과 적용률을 관찰한 뒤 추가 알고리즘 변경 여부를 결정한다.

## 검증 증거

| 검증 | 결과 | 로컬 원본 |
| --- | --- | --- |
| 최종 Editor + 선택적 플러그인 빌드 | 성공, 변경한 Editor 모듈 compile/link 포함 | `Saved/Validation/Experiments/Build/MaturityImplementation_20261003_EditorSelection/Build.log` |
| Mass 소유권·실제 활성 peer 수정 빌드 | 성공, 관련 Character/Editor 모듈 재컴파일 | `Saved/Validation/Experiments/Build/MaturityImplementation_20261003_MassOwnership/Build.log` |
| 실제 BeginPlay NPC fixture 빌드 | 성공 | `Saved/Validation/Experiments/Build/MaturityImplementation_20261003_MassFixture/Build.log` |
| 최종 회귀 suite | **89개 통과: 86 일반 + 3 경고**, 실패·미실행 0, 프로세스 exit 0 | `Saved/Validation/Maturity/Implementation_20261003_Complete/Automation/index.json`, 같은 폴더의 `Run.log` |
| 추가 소유권 집중 검증 | 9개 일반 성공, 실패·경고 0; 최종 suite에도 포함 | `Saved/Automation/MaturityImplementation_20261003_MassOwnership/index.json` |
| 모듈·catalog validator | 성공, 205 catalog 항목 / 20 domains | `Scripts/Validation/Validate-MMOArchitecture.py` |
| 변경 diff 공백 검사 | 성공 | source/plugin/script 범위의 `git diff --check`, Windows CR 허용 |

경고 동반 성공 3개는 `Animation.TwoHandIKTransitionAndCurve`(1건), `Combat.WeaponPresentationIdentity`(5건), `Presentation.StableGripTargetsAndAuthoredAlpha`(1건)의 미설정 Draw/Sheathe source/visual socket이다. 경고를 삭제하거나 runtime socket 검증을 완화하지 않았다.

개발 중 첫 89개 실행에서는 새 테스트 fixture의 LocalPlayer 준비와 예상 IK compiler 오류 등록 문제로 2개가 실패했다. fixture를 수정하고 개별 확인 및 전체 재실행으로 해결했다. 최초 실패 report도 `Saved/Automation/MaturityImplementation_20261003_Final/index.json`에 보존했다. 감사 시점 R13의 실패와 현재 구현 검증 결과를 구분한다.

UE 5.8의 foreign `-Plugin=` 빌드는 좁은 target makefile을 재사용하여 일반 프로젝트 소스가 변경되어도 up-to-date로 표시되는 경우가 있었다. 소스/object 시각과 UBT 코드를 확인하고 해당 makefile 한 개를 `Saved/Validation/MaturityImplementation_20261003/PluginMakefile.bin`으로 보존한 뒤 전체 target을 재빌드했다. 빌드 스크립트는 direct UBT의 `-AdditionalPlugins=ProjectJExperiments`로 전체 프로젝트와 선택적 플러그인을 함께 검증하도록 수정했다. 빌드/에디터 프로세스를 중단하거나 중첩 실행하지 않았다.

최초 제한된 빌드의 AppData 접근 실패는 컴파일 전 환경 문제였다. Application/.NET 이벤트와 첫 오류를 확인했으며 dotnet 예외 창이 관찰되지 않았다. 필요한 로컬 경로 접근이 허용된 빌드로 실제 compile/link를 확인했다.

재실행할 때 Editor·UBT·dotnet·Live Coding·MSBuild·ShaderCompileWorker가 모두 종료된 뒤 새 RunName을 사용한다.

```powershell
./Scripts/Validation/Build-Experiments.ps1 -RunName Maturity_Verify_01
./Scripts/Validation/Measure-Maturity.ps1 -RunName Maturity_Verify_01
```

`Measure-Maturity.ps1`은 exit code와 결과 JSON의 실패·미실행·실행 중을 모두 검사하고, 새 Maturity 11개 및 장비 수명 테스트 1개가 실제로 성공했는지 확인한다. 실제 에셋 저장 commandlet은 호출하지 않는다.

검증 요약, 테스트별 진단, 변경 파일 SHA-256과 감사 범위표 hash는 [적용 검증 JSON](Project_J_Architecture_Maturity_Implementation_Validation_2026-10-03.json)에 저장했다. 기존 감사 보고서·범위표·검증 JSON은 감사 당시 기록으로 유지했다. cooked/package, 실서버 재시작·다중 노드 이전, 실제 에셋 배선, 에디터 수동 조작, 대규모 성능 수치는 이번 검증 범위에 포함되지 않는다.
