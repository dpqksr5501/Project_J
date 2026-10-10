# 런타임 소유권·객체 수명 감사 — 2026-10-08

후속 상태(2026-10-10): CR-01의 누락된 Run 6개 EarlyTransition을 원본 조건·메타데이터와 비교해
native로 복원했다. [복구 및 적용 한계](../../Animation/Diagnostics/EarlyTransition_Recovery_2026-10-10.md)를 참고한다.
아래 본문은 10월 8일 감사 당시의 기록이다.

## 범위와 결론

사운드 외의 전체 Source를 대상으로 위험 패턴을 검색하고, 객체 수명·GAS·애니메이션·장비·탈것·비동기 작업·서버 권한의 주요 경계를 상세 검토했다. 실제 엔진에서 재현한 결함 5개를 수정했다. 단순히 포인터가 null인지 확인하는 것만으로는 **살아 있는 객체가 현재 실행의 소유자인지**, **Destroy 이후 사용할 수 있는지**, **GC가 네이티브 상태를 추적하는지**를 보장할 수 없었다.

- 기준 HEAD: `3a94ecc5a80cc5208708354905470d5053ca31e6`의 기존 미커밋 작업을 포함한 작업 트리. 기존 Foley와 콘텐츠 변경은 보존했다.
- 시작 시 C++ 헤더·구현 443개, 8개 모듈. 감사 중 테스트 파일 2개 추가 후 445개.
- 전체 파일의 위험 패턴 검색과 아래 책임 경계의 상세 검토를 구분한다. 모든 파일·에셋·실행 경로를 전수 실행하거나 결함 부재를 증명한 감사는 아니다.
- 이번 감사는 소스와 설정으로 재현 가능한 문제를 실제 `UnrealEditor-Cmd`에서 검증했다. Blueprint·레벨 일괄 수정이나 저장은 수행하지 않았다.

## 확인한 결함과 수정

| ID | 우선순위 | 실제 증상 | 원인과 적용 |
| --- | --- | --- | --- |
| RO-01 | P1 | 이전 Pawn의 입력 종료 또는 EndPlay가 교체된 Pawn의 Sprint를 취소 | PlayerState의 영속 ASC를 이전 Pawn도 참조한다. 취소 시 현재 ASC Avatar가 호출 Pawn인지 확인하고, UnPossessed에서 소유권을 넘기기 전에 Sprint와 입력을 정리한다. |
| RO-02 | P2 | 소환한 탈것을 Destroy한 직후 같은 아이템으로 재소환하지 못함 | GC 전 `SummonedMount`가 non-null이었다. getter에 유효성·파괴 상태 검사, OnDestroyed에서 해당 참조 정리와 복제 갱신, 탑승 자격 검사에서 파괴된 탈것·탑승자 거부를 추가했다. |
| RO-03 | P1 | 지연된 애니메이션 프록시의 PSD가 생산자 참조 교체 후 GC로 수거될 수 있음 | 비리플렉션 네이티브 프록시 안의 `TObjectPtr`는 자체적으로 GC root가 아니다. 프록시 `AddReferencedObjects`에서 대기·적용 스냅샷, 데이터베이스·동반 데이터, 네이티브 노드 참조를 수집한다. |
| RO-04 | P2 | 이전 무기 지면 접촉 NotifyEnd가 새 동작의 접촉 구간을 닫음 | 공유 깊이 카운터가 동작 초기화로 재설정된 뒤 이전 End가 새 Begin을 차감했다. 메시·notify 실행별 토큰을 발급하고, End는 자신이 받은 토큰만 반납한다. 기존 Blueprint Begin/End 깊이는 별도로 유지한다. |
| RO-05 | P1 | 화면 밖 공격 취소·장비 해제의 몽타주 종료가 멈춤 | `Montage_IsPlaying(nullptr)`는 블렌드아웃·종료 처리가 남은 인스턴스를 제외한다. ABA 재등록과 공격 종료 시 가시성 복원이 모두 종료 갱신을 막을 수 있었다. 몽타주에 독립 갱신 수요를 부여하고 모든 instance가 정리된 뒤에만 ABA로 복귀한다. |

### RO-01: 영속 ASC의 Avatar 소유권

수정: `Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp`의 `CancelAbilitiesByTag`, `UnPossessed`.

실제 PlayerState·ASC·Sprint Ability·두 Pawn을 만들고 동일 컨트롤러로 순서대로 소유했다. 새 Pawn의 Sprint가 실제로 활성화된 상태에서 이전 Pawn의 StopSprint와 Destroy를 각각 실행했다. 취소 보호만 제거한 대조 실행에서는 두 경로 모두 새 Sprint를 취소했다. 보호 적용 후에는 이전 콜백이 새 Sprint를 유지하고 현재 Pawn의 StopSprint는 정상 종료한다.

테스트의 AIController는 Standalone 월드에서 local predicted ability를 실제 활성화하기 위한 구성이다. LocalPlayer가 없는 PlayerController로 활성화 반환값만 확인하면 로컬 실행을 입증하지 못하므로, 활성 GameplayTag까지 검사한다. 이 테스트 자체가 원격 클라이언트 예측 검증을 대체하지는 않는다.

### RO-02: Destroy와 GC 사이의 탈것

수정: PlayerCharacter의 소환·getter·OnDestroyed 및 `Source/Project_JMount/Private/Mount/Project_JMountCharacter.cpp`의 탑승 자격 검사.

인벤토리에 실제 소유한 MountItem을 넣고 소환 → Destroy → GC 없이 즉시 재사용했다. 수정 전에는 파괴된 탈것이 자격 검사에서 MountUnavailable로 거부되지 않고 새 탈것도 생성되지 않았다. 수정 후에는 다른 유효한 탈것이 생성되며 컨트롤러가 파괴된 탈것으로 넘어가지 않는다. 권한 검사는 서버 요청 경로에 유지한다.

### RO-03: 네이티브 프록시와 GC

수정: `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstanceProxy.{h,cpp}`.

생산자 측의 별도 강한 참조 없이 프록시에 transient PSD를 큐에 넣고 `CollectGarbage`를 실행했다. 수정 전에는 수거되고, 수정 후에는 큐가 보유한다. 큐를 null로 교체한 뒤 다시 GC하면 수거되므로 무기한 보존하지 않는다. 실제 애니메이션 인스턴스의 proxy GC 수집 경로를 이용한다.

UE 5.8 로컬 엔진의 `UAnimInstance::AddReferencedObjects`, `FAnimInstanceProxy::AddReferencedObjects`를 함께 확인했다. 프록시의 네이티브 MotionMatching·PoseHistory 노드도 엔진 리플렉션 구조 참조 수집에 포함한다. worker에서 UObject 작업을 추가하지 않는다.

### RO-04: 지면 접촉 실행 토큰

수정: WeaponGroundContact NotifyState와 WeaponPresentationComponent.

실제 소스 메시·표현 메시·무기 구성에서 A Begin → 동작 종료/초기화 → B Begin → A End 순서를 실행했다. 수정 전에는 B 접촉이 닫혔다. 수정 후에는 A End가 B 토큰을 제거하지 않으며, 중복 Begin과 B End도 균형을 유지한다. RuntimeState는 weak component를 보유하고 만료된 메시·토큰을 제거한다. 상태를 맵에서 제거한 뒤 외부 End를 호출한다.

### RO-05: 최적화와 종료 콜백의 순환 대기

수정: `Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.{h,cpp}`와 `Source/Project_JCharacter/Private/System/Project_JCharacterAnimationBudgetSubsystem.cpp`.

100캐릭터 장비 반복 테스트의 최초 실패에서 장비·무기·공격 태그는 이미 정리됐지만 `AM_Greatsword_UnEquip`이 위치 0.933/1.167, inactive·valid·weight 1.0으로 남았다. 메시의 요청 Tick은 true인데 ABA 관리 중 실제 Tick은 꺼져 있었다. 동일 테스트는 프로젝트 ABA를 끄면 통과했다. 메시 초기화에서 Tick을 명시적으로 켜는 것만으로는 문제가 해결되지 않았다.

UE 5.8의 `IsAnyMontagePlaying()`은 이름과 달리 `MontageInstances.Num() > 0`을 반환한다. 이 수명 경계가 필요한 조건이다. ABA 재등록만 막은 중간 수정에서는 장비 soak는 통과했지만 hidden 공격 취소·unequip 3개 경로와 신규 blend-out 테스트가 실패했다. 공격 수요 해제 후 `OnlyTickPoseWhenRendered`로 돌아가면 ABA 밖에서도 blend-out 갱신이 멈출 수 있었다.

최종 변경은 MontageStarted에서 메시 자신을 requester로 한 GameplayPose 수요를 추가한다. 기존 world budget policy pass에서 모든 인스턴스가 제거된 것을 확인한 뒤 그 수요만 반납한다. 다른 공격·urgent 요청의 수요는 유지한다. 블렌드아웃 동안 가시성·notify·URO 보호가 유지되고 종료 후 원래 정책을 복원한다. 새 component Tick을 만들지 않으며 모든 원격 캐릭터를 상시 고빈도로 갱신하는 방식은 아니다. 명시적으로 비활성화된 메시의 요청 Tick을 임의로 켜지 않는다.

추가 테스트는 실제 authored montage를 재생·중단하고 남아 있는 blend-out instance 동안 ABA 미등록, Tick 유지, 월드 갱신 후 인스턴스 제거 및 ABA 복귀를 검사한다. 기존 100캐릭터 테스트의 종료 불변식은 완화하지 않았다.

기존 `NPCGameplay.GroundRootMotion` 테스트도 실제 종료 갱신을 추가했다. 공격·판정·montage 재생은 즉시 중단돼야 하고, 가시성·URO는 남은 인스턴스가 1초 이내 정리된 뒤 원래 값으로 돌아와야 한다. 이 이동 정책 테스트는 ABA를 끈 조건에서 검사하고, ABA 복귀는 별도 신규 테스트와 기존 CombatContinuity에서 검사한다. 즉시 복원 단언을 없애기만 하거나 timeout을 늘려 실패를 숨기지 않았다.

## 검토한 주요 경계

| 영역 | 확인한 계약·구조 | 검증 범위 |
| --- | --- | --- |
| PlayerState·GAS·입력 | 영속 ASC/Avatar 교체, 태그·Ability grant 회수, 입력 해제·컨텍스트 lease | RO-01 실제 재현, 기존 GAS·입력 자동화 |
| 장비·전투·무기 표현 | 장비 revision, 이전 grant의 ASC, 취소·unequip·destroy, 메시 소유권·notify 실행 | RO-04/05, 기존 전투 연속성·장비 soak |
| 애니메이션·리타깃·ABA | GT 스냅샷/worker 데이터, GC 참조, urgent/gameplay pose 수요 합성, 종료 후 복귀 | RO-03/05, 기존 컴포넌트·애니메이션 자동화 |
| 탈것·비행 | 파괴 전후 탑승, possession, 비동기 클래스 로드 재검사, 착지 bounded fallback | RO-02, 기존 mount 자동화 |
| NPC·Mass·비동기 경로 | weak owner/revision/token, 값 스냅샷 worker, 취소된 nav 예약 유지, 완료 재진입 | 상세 소스 검토, 기존 NPC·Mass 자동화 |
| Core 메시지·비주얼 로드 | 재진입 전 map/delegate 상태 분리, weak 소유자, scoped asset lease·용량 제한 | 상세 소스 검토, 기존 Core 자동화 |
| 네트워크·SSR | 서버 이력·예측키·장비 revision·LOS, stale hit 거부, owner-only FastArray, Iris 설정 | 기존 자동화 및 별도 실제 소켓 fixture |
| Gateway·Handover·Social·Progression | exactly-once completion, cancel 전에 tracker 분리, CAS/idempotency, restore 사전 검증 | 상세 소스 검토와 기존 foundation 자동화; 실서비스 backend 검증 제외 |
| Editor·AnimNodes | ticker 종료, 에디터 전용 경계, 네이티브 노드 참조 | 위험 패턴·대표 소유권 경로 검토; 모든 UI 실행 제외 |

이번 수정은 새 per-frame 서비스·RPC·복제 필드를 추가하지 않았다. 기존 소유권 경계를 강화하고 필요한 순간에만 애니메이션 갱신을 보존한다. GC 참조 수집은 보존이 필요한 기존 상태를 추적한다.

## 실행 증거

로컬 원본 경로는 `Saved/Validation/RuntimeAudit_20261008`이며 Saved는 커밋 대상이 아니다. 서로 다른 실행의 통과 수는 합산하지 않는다.

| 실행 | 조건 | 결과 |
| --- | --- | --- |
| `Baseline/index.json` | 수정 전 전체 ProjectJ, 기본 CVar | 240개: 221 성공·13 경고 포함 성공·6 실패. CombatContinuity 5개는 요구된 `a.Budget.BudgetMs 0.1` 없이 실행된 조건 오류이며 해당 조건으로 별도 재실행 시 통과. EquipmentSoak 1개는 실제 종료 문제. |
| `EquipmentWithoutBudget/index.json` | 프로젝트 ABA off, 실제 100캐릭터 장비 반복 | 1개 성공. ABA가 원인 경계에 포함됨을 분리 확인. |
| `EquipmentBudgetDiagnostic/index.json` | ABA on, Tick 초기화·추가 진단, 종료 보호 전 | 1개 실패. `managed=1 requested=1 visibility=0 tickHidden=1`인데 실제 Tick이 꺼진 상태에서 납도 instance 잔류. |
| `FinalVerified/index.json` | 최종 전체 ProjectJ, `a.Budget.BudgetMs 0.1`, NullRHI | **244개: 225 성공·19 경고 포함 성공·0 실패·0 미실행.** 새 수명 회귀 4개, 기존 notify 소유권·CombatContinuity·NPC 취소 테스트 포함. |
| 최종 EquipmentSoak | 위 `FinalVerified`와 동일 실행 | 100캐릭터 × 3회 = 300 actor-cycle 완료, failures=0. 100개 네트워크 클라이언트 부하가 아니다. |

최종 전체 자동화 명령:

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'C:/Users/I/Documents/GitHub/Project_J/Project_J.uproject' `
  -unattended -NullRHI -nosound -NoLiveCoding -nop4 -nosplash `
  '-ExecCmds=a.Budget.BudgetMs 0.1,Automation RunTests ProjectJ' `
  '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/RuntimeAudit_20261008/FinalVerified' `
  '-ABSLOG=C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/RuntimeAudit_20261008/FinalVerified.log'
```

자동화 프로세스 exit 0만으로 통과를 판단하지 않고 `index.json`의 failed/notRun과 테스트별 error를 확인했다. 경고 포함 성공 19개를 로그 전체가 깨끗한 것으로 표현하지 않는다. 블렌드아웃 보호 전후 결과와 수정 실패 증거도 로컬 원본에 보존했다.

### 빌드와 실제 네트워크 연결

- 직접 `UnrealBuildTool.exe`로 `Project_JEditor Win64 Development`, `Project_J Win64 Development` 모두 성공했다. 최종 에디터 로그는 `Build_FinalFixtureGround.log`, 게임 로그는 `Build_FinalGame.log`다.
- 다른 Unreal/UBT/dotnet/Live Coding/MSBuild/ShaderCompileWorker 프로세스가 실행 중이지 않을 때만 빌드했고 모두 정상 완료를 기다렸다.
- `Scripts/Validation/Measure-Network.ps1 -RunName RuntimeAudit_20261008_Final_P80_L2 -ClientCount 2 -NPCCount 100 -Animation -PacketLag 80 -PacketLoss 2 -Port 17879`를 실행했다.
- 실제 dedicated server 1개와 별도 소켓 client 2개, NPC 100개, Iris AOI·owner-only inventory/equipment FastArray·애니메이션 관측·graceful disconnect 조건을 검증했다. 서버·두 클라이언트 모두 success, exit code `[0,0,0]`, timeout 0, verification passed=true.
- 네트워크 원본은 `Saved/Validation/GroupD_20260910/RuntimeAudit_20261008_Final_P80_L2`의 manifest·SourceSnapshot·verification·Server/client JSON·network phases·animation observations·utrace다. `PktLag=80`, `PktLoss=2`는 엔진 emulation 설정값이며 측정 RTT나 개선율로 표현하지 않는다.
- 저장소에 남긴 축약 증거는 [검증 요약 JSON](Project_J_Runtime_Ownership_Validation_2026-10-08.json)이다. 상세 로그는 로컬 Saved 원본과 구분한다.

### 확인한 잔여 콘텐츠·환경 항목

1. **CR-01 — 기존 EarlyTransition 에셋 의존성 누락.** 네트워크 서버에서 Run Box LR/RL F Lfoot/Rfoot 및 Pivot B F Lfoot/Rfoot 6개 시퀀스가 `/Game/Blueprints/AnimNotifies/BP_NotifyState_EarlyTransition`을 불러오지 못했다. 실제 파일도 없다. [기존 Foley 이관 기록](../../Animation/Diagnostics/Foley_Notify_Migration_2026-10-08.md)에도 남아 있는 항목이며 이번에 새로 발생한 것으로 분류하지 않는다. 현재 native early-transition 구현은 존재하지만 미저작 상태다. 이 누락의 현재 locomotion 기능 영향이나 모든 시퀀스에서의 대응을 검증했다고 주장하지 않는다. native notify를 일괄 넣으면 전환 시점이 바뀔 수 있으므로 에셋별 상태 정책을 비교하는 별도 콘텐츠 점검 항목으로 남겼다.
2. **ENV-01 — 엔진 Experimental Toolset Python startup 오류.** `-game` 실행에서 EditorToolset/ToolsetRegistry가 editor 전용 `AgentSkill`/`PythonTestRunner` API를 찾지 못했다. verification에도 원문이 기록된다. 프로젝트 C++ 컴파일·서버 연결 assertion은 통과했고, 이번 감사에서 엔진 설치 파일이나 플러그인 설정을 임의로 바꾸지 않았다. 기능 검증 통과를 전체 시작 로그 무오류와 혼동하지 않는다.

## 적용 한계와 다음 점검

자동화는 NullRHI와 실제 게임 월드를 사용한다. 시각 품질·실제 오디오 출력·모든 애니메이션 에셋의 notify·패키징/cook·운영 MMO 접속량을 입증하지 않는다. 실제 소켓 fixture도 로컬 dedicated server와 소수 클라이언트의 기능 검증이며 MMO 대규모 처리량 측정과 구분한다.

현재 montage 수요는 gameplay 종료 안전성을 우선하며 인스턴스가 남아 있는 동안 pose 갱신을 보존한다. 추후 순수 표현용 무한 반복 montage를 도입하면 수요가 지속되는 비용을 측정하고, gameplay notify/root motion이 없는 콘텐츠의 저비용 표현 정책을 별도 계약으로 확장해야 한다.

추가 기능을 확장할 때에는 Pawn 교체와 늦은 callback, cancel 뒤 완료, GC 전 Destroy, 화면 밖 montage 종료, 같은 notify의 겹침을 해당 기능의 회귀 시나리오에 포함한다. 현재 살아 있는 포인터를 확인한 뒤에도 revision/avatar/실행 토큰까지 맞는지 검사한다. Backend foundation의 테스트 통과를 실제 서비스 연동 완료로 표기하지 않는다.
