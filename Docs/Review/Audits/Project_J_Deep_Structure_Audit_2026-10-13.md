# Project_J 전체 구조 심화 감사 — 구현·검증 결과 및 남은 범위

후속 [2026-10-14 전수 범위 재점검](Project_J_Reaudit_2026-10-14.md)에서 추가 RA 변경이 진행됐다. 이 문서의 332개 회귀·Game/Cook·CPU 및 source/binary hash는 DS01–18 당시 근거이며 이후 소스의 최종 검증으로 사용하지 않는다.

이 문서는 2026-10-13 시작, 2026-10-14 검증 기준의 추가 조사·개선 결과다. 앞선 결과를 모든 함수·에셋의 무결성 증명으로 확대하지 않는다. 실제 변경, 검토, 미검증을 구분한다.

[시스템·소유권·실행 흐름 지도](Project_J_Deep_Structure_Map_2026-10-13.md)를 함께 읽는다. DS01–18 최종 선별 회귀는 59개(57 clean·2 warning), 전체 ProjectJ는 332개(312 clean·20 warning)로 실패/미실행 0개다. [원본 회귀 보고서](DeepEvidence_2026-10-13/Regression/DeepWholeDelivery/index.json)를 보존한다. Game/Cook/패키지·기록한 통합/원격/표시 검증과 CPU 반복·단계 trace를 수행했다. CPU 소폭 증가 및 traced renderer 종료 실패는 해결 완료로 판정하지 않는다.

## 기준점과 보존

- 브랜치 `codex/runtime-retarget-profiles`, 기준 HEAD `f1bbd48ac4a37eab6ed7a33d025f1de2c270427c`.
- 앞선 미커밋 변경을 포함한 새 기준점: `Saved/Validation/DeepAudit_20261013/Baseline` (1141개 텍스트 파일, Git 상태 및 패치). 빌드 DLL/모듈 및 게임 실행 파일은 `BaselineBinaries`에 별도 보존했다.
- 원래 인계 문서, 리타깃 Body ABP, 단발 곡선 기본값 0.35초·100cm·3도·85%를 보존한다. 커밋·push·병합·종료는 수행하지 않는다.
- 최종 C++/Config 기준은 `FinalSourceHashes.json`의 Source/Config/Scripts/Plugins/uproject 648개 해시와 `FinalSourceSnapshot`이다. 이후 변경은 검증 스크립트 `Measure-RenderedCrowd.ps1`의 process handle/exit 기록 한 건이며 별도 해시와 이유를 남겼다. Editor의 8개 DLL과 modules 파일도 `FinalEditorBinaries` 및 해시에 기록했다. Game/Cook 검증과 CPU 비교 이후 C++/Config 변경은 없다.
- UBT를 직접 실행하고 정상 종료를 기다린다. 엔진·빌드 프로세스가 실행 중이면 다른 빌드를 시작하지 않는다. 실패 로그는 삭제하거나 성공 로그로 덮어쓰지 않는다.

## 상태 소유권 지도

| 사실 또는 자원 | 소유자 | 소비자와 경계 | 정리 방향 |
|---|---|---|---|
| 실제 위치·속도·Falling | CharacterMovement | GT locomotion context 수집 | 애니메이션 완료와 합치지 않는다 |
| 지상 전환 의미 | LocomotionAnimStateComponent.GroundMotionMode | 표시용 BP 플래그, immutable anim snapshot | enum에서 단방향 게시한다 |
| 이동 GameplayTag 기여분 | 해당 상태 컴포넌트와 실제 부여한 ASC | ASC 동기 delegate, PlayerState 교체 | 부여한 ASC에서 정확히 한 기여분만 해제한다 |
| 태그 부여 의도 | Add/Remove 이동 태그 API | ASC 없는 구간의 재연결 | 물리/원격 표현 플래그로 태그 의도를 새로 만들지 않는다 |
| 단발 논리 hold·소비 revision | GT StateControllerRuntime | 선택 캐시, worker command snapshot | 실제 노드 재생 시간과 구분한다 |
| 실제 재생·BlendStack outgoing pose | worker Unified Locomotion node | 완료된 관찰을 GT 다음 경계에서 소비 | 독립 타이머나 강제 reset으로 대체하지 않는다 |
| MM 재선택 요청 | GT publication revision → proxy | 기존 PreUpdate/worker traversal 경계 | 숨김 프레임이나 nested layer가 재무장하지 않도록 유지한다 |
| 캐릭터 생존 중 비동기 자산 lease | 해당 컴포넌트 또는 UI 요청 | 약한 소유자, generation, 완료 callback | 취소와 콜백 중 재진입·GC를 별도로 검토한다 |
| NPC 점수 계산 | Core data-only task | GT snapshot → worker → epoch/revision 검사 | world 종료는 취소, 모듈 unload는 완료를 기다린다 |
| 서버 AI path 요청 | NPCPathSubsystem | engine async 완료 → GT bounded delivery | 취소된 in-flight 예약을 완료까지 유지한다 |

## 발견 및 실제 변경 단위

### DS01: 지상 전환의 단일 게시자 (구현, 선별 회귀 통과)

`GroundMotionMode` 외에 BP 표시용 `bUseStartDatabase`/`bUseStopDatabase`를 snapshot에서 OR로 읽어 별도 의도처럼 처리했다. enum과 표시용 alias가 어긋나면 Start/Stop을 다시 요청할 수 있다. snapshot은 enum만 읽고 BP 호환 필드는 기존 `RefreshGroundMotionFlags`에서 게시한다. 빈 `ResetGroundMotionTransitionRequests`, 이미 앞 분기에서 처리한 `bStopEdge`의 도달 불가능 분기, 곧 덮어쓰는 표시 플래그 쓰기를 제거했다.

이는 실제 재생 버그를 관찰한 결과가 아니라 소유권 계약 결함을 분리한 테스트다. 최초 테스트의 잘못된 AnimInstance Outer는 SkeletalMeshComponent로 보정했다. 보정한 수정 전 실행 `DeepBaselineRepro4`에서 기대한 6개 불일치가 경고 없이 재현됐으며, 수정 후 `DeepPhase1Focused4`의 7개 선별 테스트는 실패·경고 없이 통과했다.

### DS02: 이동 GameplayTag의 ASC lease (구현, 선별 회귀 통과)

기존 bool 보유 표시는 실제 부여한 ASC를 저장하지 않았다. 새 ASC가 캐시에 들어온 후 해제하면 새 ASC의 다른 기여분을 줄이고 이전 ASC에 자신의 기여분을 남겼다. Add가 동기 delegate를 호출한 뒤 bool을 설정하는 순서도 재진입 시 잘못된 소유권을 만들었다.

수정 전 native 실제 ASC 두 개와 동기 tag delegate를 사용한 테스트에서 이전 ASC 2→기대 1, 새 ASC 1→기대 2, 해제 후 새 ASC 0→기대 1을 기록했다. null cache gap과 BeginPlay 이전 DestroyComponent의 누락도 함께 재현했다. 이 테스트는 실제 BP 플레이 재현과 구분한다.

실제 ASC를 약하게 저장하고 Add 전에 lease를 게시하며 Remove 전에 lease를 비운다. ASC 변경 hook에서 기존 기여분을 반환하고 현재 요청 의도만 새 ASC에 재부여한다. EndPlay와 OnComponentDestroyed가 같은 소유권 해제를 사용한다. 원격 착지 표현만으로 로컬 태그를 발명하지 않는다.

보정한 수정 전 재현과 수정 후 7개 선별 테스트에서 계약을 확인했다. 이후 최종 전체 회귀 332개와 기록한 2-client 패키지 원격 3회도 통과했다. 이 결과를 모든 ASC 교체와 원격 시각 조합의 검증으로 확대하지 않는다.

### DS03: 단발 복귀의 공통 게시 계약 (구현, 선별 회귀 통과)

full-body action, 입력에 의한 단발 종료, Start 도중 gait 변경의 세 경로가 같은 loop 상태와 phase를 여러 필드에 별도로 게시했다. `PublishStateControllerGroundLoop`가 표현 상태, 단발 요청, locomotion phase, MM phase, 논리 hold를 함께 게시한다. 재선택을 요청할 시점은 기존 각 호출자가 결정한다. 네 곳의 같은 입력 기준 해제는 `ClearStateControllerInputReferences`로 모았다. 읽는 곳이 없던 비반영 private `bHasStateControllerFallOffForChooser`만 제거했다. 새 타이머·clip 예외·강제 reset은 추가하지 않았다.

### DS04: 장비 부여의 commit과 binding 소유권 (구현, 선별 회귀 통과)

수정 전 실제 ASC attribute callback에서 BeginPlay 이전 runtime을 파괴했을 때 공격력 100→110, 방어력 50→70이 남았다. 장비 runtime map에 게시하기 전에 부여했으므로 종료 정리가 그 자원을 볼 수 없었다. 로컬 부여 transaction은 동기 callback 이후에도 binding revision과 생존 조건이 유지될 때만 게시한다. 종료·재연결로 무효화되면 해당 transaction의 실제 ASC/handles/스탯 기여분을 반환한다. 기존 slot map 정리는 binding을 먼저 분리하고 재진입으로 새 binding이 만들어지면 이전 정리를 중단한다.

회귀에는 파괴, null binding, 이미 장비가 있는 다른 manager로 교체하는 세 경우를 추가했다. `DeepPhase2Focused`에서 이전 작업이 새 binding의 장비 identity나 스탯을 덮어쓰지 않는 것을 확인했다.

### DS05: UI 완료 callback 중 GC 수명 (구현, 선별 회귀 통과)

기존 아이콘 요청은 완료 전에 WeakOwner를 검사하고 texture를 강하게 유지했지만 완료 callback 동안 위젯을 강하게 유지하지 않았다. 실제 streamable 완료 callback에서 마지막 외부 위젯 참조를 놓고 GC를 실행하면 callback 종료 전에 수거됐다. 수정 전 `DeepBaselineRepro4`에서 재현했다. 완료 callback의 범위 안에서만 소유자를 강하게 유지하며 대기 중과 완료 후의 약한 소유권은 유지한다. 테스트는 지연 취소, A→empty→A, 대기 중 수거, callback 중 수거, 완료 후 잔류 참조를 구분한다.

### DS06: 파괴된 전투 상태 컴포넌트의 접근 종료 (구현, 선별 회귀 통과)

BeginPlay 이전 DestroyComponent에는 EndPlay가 호출되지 않았다. 살아 있는 ASC 바인딩을 계속 읽거나 late Bind로 다시 연결할 수 있었다. 수정 전 실제 컴포넌트/ASC 테스트에서 두 경우를 재현했다. OnComponentDestroyed와 EndPlay가 같은 바인딩 해제를 사용하고 종료된 컴포넌트의 ASC 읽기·명령·재바인딩을 거절한다. 태그 자체를 지우지 않는다. 등록과 해제의 별도 다섯 태그 배열은 실제 획득한 tag/handle map으로 정리했다.

### DS07: Sprint의 무조건 부모 위임 제거 (구현, 선별 회귀 통과)

`CanActivateAbility` override는 부모가 false이면 false, 그 외 true만 반환했다. 부모 구현을 그대로 상속하도록 삭제했다. activation tag, prediction, effect 부여와 해제, 입력 release는 유지했다. Blueprint용 빈 callback이나 에셋 참조 여부를 확인하지 않은 반영 API는 삭제하지 않았다.

### DS08: 성장 XP 규칙의 공통 계산 (구현, 선별 회귀 통과)

snapshot 복원 검증과 현재 레벨의 다음 XP 조회가 최대 레벨·배열 clamp·누락 fallback을 따로 계산했다. `GetRequiredExperienceForLevel`로 동일 규칙을 사용한다. 경험치·레벨·전직의 소유자, 보상 권위, 기본값, snapshot schema는 유지한다.

### DS09: 퀘스트의 변경·자원 종료 계약 (구현, Editor 선별·전체 회귀 통과)

실제 `DA_ProjectJFirstSteps`의 여행 objective로 BeginPlay 전에 Accept를 실행하고 컴포넌트를 파괴했다. 종료 후에도 여행 타이머가 남고, Claim이 성공하여 성장 레벨이 1→2가 됐으며 새 Accept·objective 변경도 허용됐다. `DeepPhase3Baseline`에서 5개 위반을 실제 native 컴포넌트/PlayerState로 재현했다.

EndPlay와 OnComponentDestroyed의 공통 Retire, 변경/게시 생존 조건, 여행 측정의 공통 `ReconcileTravelSampling`을 구현했다. Accept와 Restore가 각각 타이머를 시작하던 로직을 제거했으며 완료·보상 후에는 active travel objective가 없으면 기존 측정을 종료한다. 보상 중 동기 callback이 컴포넌트를 종료해도 종료된 publisher를 다시 사용하지 않는다. 새로운 타이머나 보상 공식은 추가하지 않았다.

### DS10: GAS grant의 세대 lease와 단일 handle 소유자 (구현, Editor 선별·전체 회귀 통과)

실제 persistent GE 적용 delegate 안에서 기존 grant를 반환하고 같은 SourceId로 새 provider를 만들었다. 기존 호출의 완료가 새 provider의 handle 배열과 새 registry record에 이전 효과를 넣어 persistent 효과 하나가 남았다. `DeepPhase3Baseline`에서 두 위반을 재현했다.

SourceId에 단조 증가 generation을 결합했다. keyed 제공자는 `GrantSourceGenerations`만 보유하고 실제 spec/effect handle은 ASC registry가 소유한다. unkeyed 제공자만 개별 handle을 보유한다. 부여 완료는 해당 generation을 다시 확인하며 이미 반환된 작업의 실제 handle을 회수한다. 반환은 container를 먼저 분리하므로 제거 callback의 새 부여를 이전 반환이 지우지 않는다. 장비의 별도 AbilitySet 정리도 공통 Take 계약으로 모았다. BaseCharacter의 실제 기본 grant 호출도 새 계약을 사용한다. 기본 grant의 reserve API는 실제 호출이 있어 삭제하지 않았다.

### DS11: 공격 피해 에셋 연결 및 제작 계약 (소스·에셋 수정, Editor 선별·전체 회귀 통과)

MCP 필드와 실제 ASC 타격을 대조했다. LMB1은 `GE_TestDamege`로 체력을 100→90 줄인다. 나머지 10개는 피해 modifier/execution이 없는 infinite `BP_GE_CombatMode`를 부여하고 체력을 줄이지 않으며 대상에게 `State.CombatMode`를 남긴다. 이전 문서의 전투 smoke·R 연계 통과는 실제 피해의 무결성 증거가 아니었다.

원래 재현 테스트는 BaseDamage 1.5를 예상해 정상 LMB1도 실패로 세었다. 이 기대는 잘못됐으므로 LMB1의 기존 GE magnitude를 읽도록 보정했다. 원실패 보고서는 그대로 보존한다. 10개 잘못된 효과 연결과 상태 태그 부여에 대한 관찰은 별도로 유효하다. 정상 피해량/GE와 공유 CombatMode 효과는 보존하면서 잘못 연결된 10개 정의만 MCP로 교정하고 저장했다. `AttackAssetCorrectionRecords.json`의 각 수정 전후 값과 `AttackAssetSaveResults.json`의 10개 save success를 기록했다. 실제 `GE_TestDamege`는 Instant·Health AddBase -10이며 execution/component가 없다.

DamageEffect에 modifier·execution·on-application additional effect가 전혀 없는 제작 결함을 검출하도록 native DataValidation을 보완했다. 현재 서버가 소비하지 않는 BaseDamage는 기본값과 직렬화를 유지하고 legacy 제작 필드와 tooltip으로 명시했다. DeprecatedProperty 플래그는 저장 직렬화에 영향을 주므로 사용하지 않았다. AttackPower/Defense 공식이나 임의 피해 배율은 새로 도입하지 않았다.

### DS12: 입력 수명의 공통 종료 (구현, Editor 선별·전체 회귀 통과)

BeginPlay 전 input executor 파괴는 실제 GAS spec의 InputPressed를 true로 남겼으며 Initialize로 다시 연결해 입력을 보낼 수 있었다. router도 종료 후 quick tap을 전달했다. `DeepPhase4Baseline`의 세 실패는 경고 없이 재현됐다. EndPlay와 OnComponentDestroyed가 동일 RetireInput으로 자신이 전달한 입력·기존 pending chord timer·명령 history를 반환하고 종료 후 재초기화·명령을 차단한다. 일반 재초기화도 revision을 확인해 release callback의 새 연결을 이전 초기화가 덮어쓰지 않는다. 기존 chord grace 값과 quick tap 전달은 유지한다.

EnhancedInput binding owner도 OnComponentDestroyed에서 소유한 device handle만 해제한다. Router 대상의 binding은 binding component 자신에 대한 weak callback만으로 정리되지 않으므로 별도 해제가 필요하다. 회귀에는 실제 EnhancedInput binding 수, 독립적인 살아 있는 executor를 사용하는 router, pending timer, late initialization을 추가했고 Editor 선별·전체 회귀에서 통과했다. 일반 재바인딩의 동기 callback 경계는 아래 추가 조사로 구분한다.

### DS13: 성장 제공자의 grant 수명 (구현, Editor 선별·전체 회귀 통과)

실제 class spec을 부여한 성장 컴포넌트가 BeginPlay 전 파괴돼도 lease가 남았고, XP 지급·레벨 변경도 계속됐다. 다른 provider가 동일 source를 공유한 경우 그 provider를 반환해도 이전 성장 lease가 남았다. `DeepPhase4Baseline`에서 7개 위반을 경고 없이 재현했다.

실제 부여한 ASC를 저장하고 컴포넌트 종료 시 자기 class/advancement lease만 공통 Take로 반환한다. grant loop는 매 부여 경계에서 종료와 ASC identity를 확인한다. 상태값·snapshot·XP 규칙은 보존하며 스트리밍 재진입 BeginPlay에서는 종료 때 반환한 lease만 기존 상태로 재구성한다. 실제 GE callback 안의 파괴와 공유 provider 보존을 회귀에 포함했다.

### DS14: 상태효과 UI의 binding 게시·GC 계약 (구현, Editor 선별·전체 회귀 통과)

Unbind가 OnChanged를 알린 뒤 바깥 Bind가 계속 실행해 callback의 더 최신 Bind를 덮어썼다. 새 source의 delegate는 남고 기존 작업의 handle로 덮여 정확히 해제하지 못했다. `DeepPhase4Baseline`에서 실제 ASC delegate와 세 source로 3개 위반을 경고 없이 재현했다.

기존 source/handle/timer를 먼저 분리하고 binding revision을 확인한다. BeginDestroy에는 구독을 정리하며 종료된 모델을 다시 연결하지 않는다. Bind·Unbind·Refresh의 callback 범위에만 모델을 강하게 유지해 동기 GC에서도 실행 중 게시자가 수거되지 않도록 한다. 기존 약한 대기 소유권과 .25초 UI 효과 관찰 주기는 유지하며 callback 후 수거를 새 회귀로 검증한다.

### DS15: 일반 device 재바인딩의 세대·반환 계약 (구현, 최종 Editor 선별·전체 회귀 통과)

`PlayerInputBindingComponent::UnbindInput`는 direct 입력 해제를 방송한 뒤 기존 member의 pawn/router/이동 정리를 계속했다. 실제 EnhancedInput direct action과 native Sprint 종료 태그 callback의 새 Bind로 `DeepPhase5Baseline`에서 세 위반을 경고 없이 재현했다. 바깥의 이전 Bind가 성공하며 최신 연결을 덮었고, 정리할 device가 바뀌어 최신 device의 이벤트 binding 3개를 남겼다. 종료 경로 테스트의 통과와 이 일반 재바인딩 계약은 별개였다.

BindingRevision으로 이전 Bind의 완료를 검사하고, Unbind는 이전 device·handle·입력·pawn을 먼저 분리한다. callback 범위의 이전 pawn을 보유하며 이동 정리는 명시적인 이전 pawn을 받는 공통 함수를 사용한다. callback이 같은 pawn의 새 device를 연결했다면 이전 호출이 최신 router/이동 상태까지 정리하지 않는다. 회귀에는 최신 device의 실제 입력 전달과 이후 자기 GAS 입력·device handle 반환도 추가했다. 값/타이머·키/클립 예외는 추가하지 않았다.

### DS16: 인벤토리의 변경 권한·게시 수명 (구현, 최종 Editor 선별·전체 회귀 통과)

BeginPlay 전 파괴한 native 인벤토리에서도 소비가 성공하며 실제 소유자 ASC의 체력이 50→60이 됐다. stack 변경·소비·추가·제거도 계속됐고 원래 stack 4가 보존되지 않았다. `DeepPhase6Baseline`의 실패 원자료에 남아 있다. 서버 권위만 확인하던 여러 mutation을 `HasMutationAuthority`의 공통 생존 계약으로 모았다. EndPlay/OnComponentDestroyed의 Retire는 변경과 게시를 닫고 자신이 소유한 FastArray callback 연결·delegate를 반환한다. 읽기 전용 item/cooldown/receipt 상태는 삭제하거나 reset하지 않는다. 정상 BeginPlay 재진입은 살아 있는 소유자에서 기존 데이터로 시작한다. RPC admission·완료/receipt 게시에도 종료 경계를 적용한다.

### DS17: 장비 aggregate와 소비자 수명 (구현, 최종 Editor 선별·전체 회귀 통과)

같은 재현에서 파괴된 장비 aggregate가 기존 장비를 해제·교체하고 inventory ID 요청도 성공했다. 공통 변경 권한과 Retire로 이 경로를 닫았다. 장비 데이터와 runtime GAS/visual lease는 다른 소유자이므로 데이터 배열을 비우거나 가짜 Unequip을 방송하지 않는다. 제공자의 native OnRetired를 분리해 소비자가 자기 자원을 반환하도록 연결했다. Runtime은 정확한 이전 manager의 종료만 소비하고, callback의 새 연결은 기존 binding revision으로 보존한다. incoming inventory lock callback 후에도 실제 source와 변경 권한을 다시 확인한다. 제공자 종료 후 실제 equipment spec 반환·독립 provider spec 보존·종료 source 재연결 거부를 추가 회귀에 넣었다. 이 추가 소비자 테스트는 앞선 세 baseline 실패와 별도의 검증 경로다.

### DS18: 원격 점프 확인의 단일 순서 소유자 (구현, 최종 Editor 선별·전체 회귀 통과)

현재 sequence 5를 받아 상승 속도 400을 게시한 뒤 landing 중에 더 오래된 4를 전달하면 속도가 800으로 덮이고 landing이 취소됐다. 기존 두 장소가 각각 equality만 비교했다. Locomotion 상태가 실제 적용된 확인의 순서를 단독 소유하고 새 확인을 수락했는지를 반환한다. ReplicatedJumpStateComponent의 별도 LastAppliedRemoteJumpSequence를 제거하고 수락된 확인에만 기존 urgent update를 요청한다. duplicate/older/종료 source·비유한 값은 거절한다. owning client의 예측, CharacterMovement 물리, unreliable RPC와 replicated recovery property, int32 wire schema·0.35초 재생 기본값은 보존한다. 서버 sequence의 signed overflow도 방지했다. 이 native admission 재현을 모든 실제 packet 재정렬·jump/landing cross-family 타임라인의 검증으로 확대하지 않는다.

## 빌드 및 재현 원자료

- 최초 `ReproBaselineBuild` 성공. 첫 테스트는 Outer 구성 오류를 포함하므로 계약 재현의 최종 증거로 사용하지 않는다.
- 최초 `Phase1Build`는 복원된 헤더보다 오래된 UHT 줄 매크로로 C4430 정상 실패. 원본 헤더의 복원 시각을 갱신하고 정식 UHT 강제 재생성 옵션으로 보정했다. generated 파일을 직접 수정하지 않았다.
- `ReproBaselineBuild2/3`은 여러 obj의 C1083 Permission denied로 정상 실패했다. 프로세스 겹침 없음, 폴더 쓰기 probe 성공, 일치하는 Application/Defender 오류 이벤트 0개를 기록했다. 접근 권한·보안 설정·캐시는 변경하지 않았다. 실패 원인은 확정하지 않는다.
- 동일 transport 로그 경로와 컴파일 병렬도 4로 실행한 `ReproBaselineBuild4_Attempt1` 성공(278.15초), `Phase1Build4_Attempt1` 성공(256.01초). 각 정상 종료 후 transport 로그를 별도 불변 기록으로 복사했다. UE 5.8의 NoUBA는 detour를 끄며 local UBA executor 표기는 그대로 남는다는 점을 설치된 UBT 소스에서 확인했다.
- `DeepBaselineRepro4`: 다섯 테스트 모두 의도한 계약 위반으로 실패, 테스트별 경고 0개. `DeepPhase1Focused4`: 7개 성공, 경고/실패/미실행 0개. 원자료는 `Saved/Validation/StructuralAudit_20261013` 아래 해당 이름에 있다(이전 검증의 경로와 결과를 덮어쓰지 않음).
- DS03–08은 11개 파일에 변경 전후 해시를 남기고 적용했다. `Phase2ApplyManifest.json` 및 `Phase2Source`를 보존했다. `Phase2Build_Attempt1` 성공(256.77초), `DeepPhase2Focused`는 35개 clean success·1개 success with warning·실패/미실행 0개다. 탑승 native fixture의 미설정 애니메이션 profile 경고를 clean success로 합치지 않는다.
- `Phase3ReproBuild_Attempt1` 성공(261.57초). `DeepPhase3Baseline`에서 퀘스트 5개·grant 재진입 2개 위반, 공격 정의 10개의 피해 누락/CombatMode 부여를 관찰했다. 공격 테스트의 LMB1 기대값 오류를 별도 기록하고 보정했다.
- `Phase3Build_Attempt1`은 새 읽기 전용 감사 도구의 private PoseSearch 필드 접근 C2248로 정상 실패했다. public GetChannels/GetRoledSkeletons로 보정하고 Deprecated GameplayEffect 접근도 최신 component API로 변경했다. `Phase3BuildRepair1`은 244.36초에 성공했다. 실패 로그는 보존했다.
- `Phase4ReproBuild` 성공 후 실제 native 입력·성장·상태 UI 수명 재현 3개가 각각 실패했다. `Phase4FinalBuild`의 typed UObject pointer 반복문 컴파일 오류를 고친 `Phase4FinalBuildRepair1`은 246.69초에 성공했다.
- `DeepPhase4Focused`: 47개 중 45 clean·2 warning·실패/미실행 0개. warning은 MouseChordLifecycle 및 Mount native fixture의 미설정 profile 경고다. `DeepWholeFinal`: 327개 중 307 clean·20 warning·실패/미실행 0개, 667.84초. 두 원보고서는 `Saved/Validation/StructuralAudit_20261013`의 같은 RunName 폴더에 있다.
- 이후 피해 계약 테스트를 Cooked Game에서도 실행하려고 context를 추가했다. `GameBuild`는 잘못된 `EAutomationTestFlags::ApplicationContextMask` 이름 때문에 192.15초에 정상 실패했다. 현재 UE 헤더의 context API를 읽고 EditorContext/ClientContext로 보정했다. Cook/Stage는 이 실패 뒤 실행하지 않았다. 이전 327개 통과를 새 Game 빌드 성공으로 해석하지 않는다.
- `Phase5ReproBuild`는 테스트에서 캐릭터 private component를 직접 참조해 294.60초에 정상 실패했다. public FindComponentByClass로 고친 `Phase5ReproBuildRepair1`은 295.26초에 성공했다. 이어진 `DeepPhase5Baseline`은 1개 테스트 실패·경고 0개이며 위의 세 실제 위반이 원보고서에 남아 있다.
- 피해 계약 테스트는 각 정의마다 대상 체력을 100으로 복원한다. 앞선 공격들이 체력을 0으로 만들면 마지막 정의의 피해 누락을 검사할 수 없던 검증 빈틈을 보완한 것이다. 실제 게임 피해량·GE·공유 효과는 변경하지 않는다.
- `Phase5FinalBuild` 성공(294.15초), `DeepPhase5Focused` 37개=35 clean·2 warning·실패/미실행 0개. 그 뒤 추가 조사한 `Phase6ReproBuild` 성공(285.84초), `DeepPhase6Baseline` 3개 실패·경고 0개다. 이전 성공 보고서를 세 추가 계약의 통과로 합치지 않는다.
- `Phase6FinalBuild`는 새 provider 회귀 테스트의 잘못된 AbilitySet 필드 이름으로 263.27초에 정상 실패했다. 실제 헤더의 GrantedAbilityEntries를 읽어 보정한 `Phase6FinalBuildRepair1`은 265.19초에 성공했고 선별 59개가 통과했다. 실패 로그는 보존했다.
- 마지막 inventory source callback 검사 공통화를 포함한 `FinalEditorBuild`는 252.58초에 성공했다. `DeepDeliveryFocused`는 59개(57 clean·2 warning) 실패/미실행 0개, `DeepWholeDelivery`는 332개(312 clean·20 warning) 실패/미실행 0개, 664.65초다. DS01–18의 최종 Editor 검증이며 Cook/원격/품질/CPU 결과와 별개다.

## 최종 통합 검증

Game 직접 UBT 빌드는 187.06초에 성공했다. 새 Windows Cook은 1,361개 package·platform skip 7개, 오류 0개·경고 1개, 17.96초다. 별도 Stage/package는 36.78초, exit 0으로 완료했다. Cook 범위는 default game map·Entry·설정된 AlwaysCook/AssetManager 의존성 closure이며 미사용 sample/plugin 전체 CookAll은 아니다.

Cook된 11개 공격 정의를 각각 체력 100인 실제 native ASC 대상으로 확인한 AuthoredDamageContract는 1개 success with warning·실패 0개다. 경고는 GameplayCueNotifyPaths 미설정으로 /Game 전체를 검색하는 기존 설정 문제다. 전투 3주기, 1280×720/1920×1080 UI 1,000행·24버프·4window·10slot·5Hz 50단계 갱신은 통과했다. UI 검사에서 오류 로그는 0개다. 이 결과를 모든 hover/drag·접근성·클립·사운드 품질로 확대하지 않는다.

200개 실제 authored BP/retarget 몸체의 렌더 검증은 trace를 끈 실행에서 실제 exit 0·1,426 frame·idle/동시 R montage·캡처 조건을 통과했다. 1280×720 offscreen·VSync 0·uncapped·단순 조명/카메라에서 측정했다. capture 요청 frame을 제외한 idle 평균 frame 15.27ms(약 65.48FPS), attack 19.65ms(약 50.88FPS), GPU counter 평균은 각각 9.96/10.15ms다. GPU counter의 지연과 capture가 다음 frame에 남기는 영향은 별도 한계다. 실제 socket 접속은 이 렌더 fixture에서 0개다.

trace를 켠 두 실행은 캡처/fixture success를 남겼지만 종료 검증을 통과하지 못했다. 첫 실행의 종료코드는 보존되지 않았다. native process handle/exit 기록을 보완한 R2는 실제 `777003`이었다. UE GenericPlatformCrashContext/WindowsPlatformCrashContext 소스에서 CrashReportingThread의 SEH handler가 이 code로 TerminateProcess하는 것을 확인했다. 관련 Application 오류·새 crash context/dump는 찾지 못했다. 같은 조건에서 trace만 끈 실행은 0으로 종료됐지만 trace 실패의 원인을 해결했다고 판정하지 않는다. 실패 로그·trace·캡처와 [스크립트 보완 이유](DeepEvidence_2026-10-13/ValidationScriptCorrection.json)를 보존한다. 이 변경은 검증 스크립트만의 보완이며 이미 검증한 C++/Config는 변하지 않았다.

동일 패키지 해시·fixture에서 채널을 분리했다. `cpu,frame,bookmark,region,counters`와 `cpu,frame,bookmark`는 각각 777003으로 실패했고, `frame,bookmark`는 exit 0·cooked fixture·두 capture 검증을 통과했다. 관찰한 실행에서는 CPU 채널을 포함하는 경우에 재현되며 GPU/task/region/counters는 재현의 필수 조건이 아니었다. 이는 정확한 최초 exception이나 Project_J/엔진/드라이버 원인을 확정한 결과가 아니다. 정상 종료처럼 보이는 로그와 실제 OS exit의 차이를 그대로 보존한다. [채널 분리 원자료](DeepEvidence_2026-10-13/RenderedTraceIsolationSummary.json)와 각 raw trace 경로/해시를 기록했다. 검증을 통과시키기 위해 trace를 끄거나 exit code를 무시하지 않았다.

실제 독립 서버·socket client 2개·native NPC 200개, 병렬 Iris·authored animation·CombatInput, 50ms lag/20% loss의 3회는 모두 세 process exit 0·timeout 0으로 통과했다. 각 실행의 Toolset/MCP 초기화 Python 오류 45줄은 원로그와 verification에 별도로 기록했다. native Server target 빌드나 200개 실접속, live backend/DB, 모든 SSR·late join을 뜻하지 않는다.

## CPU200 반복·교대 및 단계 trace

A는 SI01–08 이후·DS01–18 이전의 보존된 전체 8개 Editor 모듈, B는 DS01–18 최종 전체 8개 모듈이다. 둘 다 같은 non-unity/toolchain·현재 에셋을 사용한다. A1/B1/B2/A2마다 정상 종료 후 DLL 8개와 modules 파일을 함께 교체하고 최종 B 9개 artifact 해시를 일치 복원했다. 기준선 실행 때 복사된 working-tree source는 A의 compiled source가 아니며 각 `comparison-provenance.json`에 구분했다. 원래 8485562c 보관본은 DLL 3개뿐이므로 새로운 전체 8개 비교에 섞어 쓰거나 이 결과를 원래 848 대비 회복 증명으로 처리하지 않는다.

같은 200개 authored BP actor·ABA 2ms·60Hz simulation·조건당 2회로 두 ABBA 세션을 실행했다. 첫 세션은 warmup120/measured120, 긴 세션은 warmup240/measured600이다. 각 64행, 총 128행과 모든 tick sample·실행 인자·provenance를 보존했다. trace overhead는 이 두 비교에 포함하지 않았다.

| 조건 | 첫 창 A→B 평균 변화 | 긴 창 A→B 평균 변화 |
| --- | ---: | ---: |
| Near ABA off | -0.59% | -0.65% |
| Near ABA on | -1.14% | -0.43% |
| Far ABA off | -1.18% | +0.15% |
| Far ABA on | +0.95% | +0.25% |
| Hidden ABA off | +3.37% | +0.59% |
| Hidden ABA on | +0.13% | +0.23% |
| AttackBurst ABA off (전체 창) | -0.19% | -2.88% |
| AttackBurst ABA on (전체 창) | +0.73% | -1.38% |

첫 B1 Hidden/off 반복 13.14ms를 제외하지 않았다. 긴 창에서 Hidden/off는 11.50→11.57ms로 소폭 증가가 남았다. 두 세션의 양/음 변화와 반복 범위가 겹치므로 통계적 동등성·전체 성능 개선·이전 증가 해결을 주장하지 않는다. 원자료는 [첫 64행](DeepEvidence_2026-10-13/CPUComparisonRows.csv), [긴 64행](DeepEvidence_2026-10-13/CPUComparisonRows_Deep_Long_ABBA.csv)에 있다.

검증 분석에서 AttackBurst의 범위를 추가 구분했다. 이 fixture는 한 번 R을 시작한다. 모든 A/B에서 post-tick 200개 montage가 관찰되는 tick은 79개이며, 첫 창의 나머지 41개·긴 창의 521개는 montage 종료 후다. 긴 창 평균을 지속적인 200명 동시 공격 비용으로 부르지 않는다. 긴 창의 실제 200 montage 구간은 ABA off -1.16%, ABA on +0.06%였다. 종료 경계 tick의 작업은 post-tick 관찰보다 앞서 발생하므로 이 분할도 완전한 critical-path 구분은 아니다. [공격/종료 후 분할 원자료](DeepEvidence_2026-10-13/CPUAttackWindowSummary_Deep_Long_ABBA.json)를 남겼다.

별도 A/B NullRHI cpu/frame/region trace는 양쪽 normal exit·UnrealInsights export를 통과했다. 16개 조건/반복 region과 GT/worker thread 목록, timer의 inclusive/exclusive·호출 수를 보존했다. 전체 trace의 누적 exclusive CPU는 GT proxy PreUpdate 0.698→0.688초, worker proxy/anim update 6.359→6.405초, snapshot consume 0.312→0.319초, MM/playback 0.644→0.685초, post selection 0.415→0.434초였다. 여러 worker의 누적 시간이나 겹치는 inclusive 시간을 world-tick wall 시간과 합치지 않는다.

RigidBody의 전체 trace 누적 exclusive는 8.868→8.803초지만 Hidden/off region에서는 RigidBody·retarget capture가 0이었다. 따라서 Hidden 증가를 cloth/rigid-body 탓으로 돌리지 않는다. Hidden/off region의 world-tick scope는 R0 1.425→1.412초, R1 1.452→1.463초였으며 worker update도 서로 다른 방향의 소폭 변화를 보였다. 현재 자료는 추가 branch 하나를 확정 원인으로 지목하지 못한다. 품질 기준 없는 물리/pose 빈도 축소, 곡선 기본값 변경, reset/timer 추가로 성능 숫자를 낮추지 않았다. [단계와 region 원자료](DeepEvidence_2026-10-13/CPUStageTraceSummary.json), 개별 export CSV, local raw trace의 SHA/경로를 보존했다.

## 전수 목록과 정밀 검토의 구분

Source 및 Plugins의 generated Intermediate/Binaries를 제외한 605개 소스·빌드 파일에 대해 모듈, include/토큰, 2478개 lexical 함수 정의, worker·snapshot·소유 자원·종료·권위·reflection 표식을 수집했다. 이는 컴파일러 기반 call graph나 모든 함수의 수동 정밀 검토 결과가 아니다. `SourceReferenceScan.json`, `NativeSymbolCrossReferences.json`의 발생 횟수만으로 반영 API·virtual override·BP callback을 미사용으로 삭제하지 않는다.

추가로 실제 경로를 읽은 영역은 locomotion 상태/태그·전환, GT presentation와 worker playback/proxy, trajectory/curve/turn 정책, 장비 manager/runtime/AbilitySet, UI 아이콘·입력 lease·화면/모델, Core 메시지·visual lease·점수 task/spatial snapshot, NPC path/decision/activation/action, Mass snapshot/join/representation, 서버 권위/원격 anim event, 탑승, 성장/퀘스트/소셜, gateway/handover/repository, 제작 validation·그래프 진단이다. `SourceReviewLedger.json`은 75개 파일의 명명된 계약 범위와 530개 inventory-only 파일을 구분한다. 긴 파일의 일부 경로만 읽은 경우까지 파일 전체 수동 검토로 계산하지 않는다.

## MCP 대조 범위

51개 DA/UI/IMC/캐릭터·게임모드·라이더 ABP 루트의 직접 의존성과 실제 reflected 필드를 기록했다. 추가 입력·Chooser·탑승·리타깃·Master ABP 44개(기존 IMC 2개 중복), PoseSearch database 53개와 schema 6개의 reflected 필드를 MCP로 읽었다. 미러 DT의 실제 87개 행도 읽었다. Master ABP의 45개 AnimGraph 노드/pin 연결을 조회했다. DSL 도구의 빈 문자열은 지원 한계로 기록하며 빈 그래프로 해석하지 않는다.

Chooser 조건/컨텍스트는 실제 필드로 읽혔지만 결과 행은 MCP와 공식 Python property 조회 모두 미지원이었다. native public API 기반 읽기 전용 감사 도구로 20개 Chooser의 327 result 행, 53개 PSD의 486 authored entry, 6개 schema를 보완했다. 활성 entry의 skeleton/mirror/누락 animation 실패는 0개이며 compiled search index와 모든 클립 품질은 검사하지 않았다. Python 보완 기록에는 각 미지원 필드를 그대로 남겼다. 에셋 교정 전 10개 공격 정의의 원본 파일과 SHA를 별도 백업했다. 레벨은 변경/저장하지 않았다.

개별 MCP 응답·필드·graph pin과 native export를 [이동 가능한 원자료](DeepEvidence_2026-10-13/README.md)에 보존한다. 해당 목록의 asset identity·지원 여부·연결 깊이만 검토 완료 범위다. 모든 legacy/plugin 에셋 내부 이벤트와 품질은 별도 미검증이다.

## 확정 결함·잠재 위험·제안

다음 표는 수정 완료와 남은 문제를 구분한다. 영향과 근거가 다른 항목을 모두 버그나 모두 해결 완료로 묶지 않는다.

| 구분·상태 | 항목과 근거 | 영향·다음 판단 기준 |
| --- | --- | --- |
| 확정 결함 · 수정/검증 | DS02/04/05/06/09/10/12–18의 actual ASC·delegate·GC·input·aggregate·sequence 계약 위반을 수정 전 native 실행에서 재현 | 실제 소유 자원과 generation/수명을 기준으로 수정. 최종 선별 59개·전체 332개 회귀 통과; 모든 BP callback 조합의 증거는 아님 |
| 확정 에셋 결함 · 수정/검증 | 정상 LMB1 외 공격 정의 10개가 무피해 CombatMode GE를 대상에 부여 | 10개 참조만 정상 기존 피해 GE로 교정·저장. 11개 실제 정의를 매번 HP100에서 native/cooked 검사 |
| 구조 결함/중복 · 개선/검증 | DS01/03/07/08의 이중 게시·동일 정리 분기·부모 위임·XP 계산 중복 | 단일 의미 소유자와 공통 게시/해제 함수로 정리. 서로 다른 물리·hold·playback 상태는 유지 |
| 확정 검증 실패 · 미해결 | trace를 켠 packaged RHI 실행의 실제 종료코드 777003 | fixture success와 정상 종료 로그만으로 통과하지 않음. 최초 exception/context 확보 및 같은 채널 정상 종료 재현 필요 |
| 측정된 증가 · 원인 미확정 | 긴 교대 CPU 비교의 Far/Hidden +0.15~0.59%, 일부 active attack +0.06% | 범위가 겹치는 변동을 유의한 회귀나 동등성으로 단정하지 않음. 원래 848 기준의 증가 해결 증거도 아님 |
| 제작/운영 위험 · 확인 | GameplayCueNotifyPaths 미설정 → /Game 검색 경고 | 실제 Cue 경로·의존성이 정해진 뒤 제한하고 Cue/Cook 검증 필요. 경로를 추측해 바꾸지 않음 |
| 계약/확장 위험 · 검토만 | BaseDamage·AttackPower/Defense·knockback/launch 제작 값과 현재 서버 피해 GE 계약의 차이 | 현재 공식에 실제 소비되지 않는 값을 기능 완료로 설명하지 않음. 균형/단위/서버 권위 계약을 먼저 정해야 하므로 임의 공식은 추가하지 않음 |
| 잠재 위험 · 미검증 | inventory 소비/lock, manager 교체의 임의 외부 BP callback·GC·ASC 교체 복합 조합 | 개별 종료·provider 반환·incoming identity 회귀와 구분. 중간 callback의 transaction 원자성은 추가 fixture/실제 이벤트 대조 대상 |
| 확장/품질 제안 | production map/nav, native Server target·live backend/DB, 다수 실제 socket, clip/체형/음향·UI 접근성 | 현재 로컬 mock/2-client/200몸체/일부 capture를 운영 품질로 확대하지 않음. 현재 coverage ledger의 남은 범위를 기준으로 후속 검증 |

최종 변경 이유는 호출자 수나 파일 길이 자체가 아니라 상태의 게시자, 실제 획득한 자원의 반환자, 동기 callback 뒤 commit 자격을 명확히 하기 위함이다. 기존 task/snapshot/epoch·engine animation worker·서버 권위 경계를 재사용했다. 구조 지도의 각 시스템에 검토 후 유지한 계약과 실제 변경 단위를 함께 기록했다.

## 검토와 미검증

- Core 메시지 라우터·visual asset lease·target scoring task, NPC path/decision scheduler의 기록한 종료·취소·callback 경계를 검토했다. 기존 snapshot/epoch/예약 제한을 재사용했고 새 병렬 시스템으로 중복 구현하지 않았다.
- MM/로코모션의 hold와 playback, 물리 착지와 표현 완료, local/remote 입력의 목적이 다른 상태를 구분했다. 유사 이름만으로 삭제하지 않는다.
- `SourceCoverageInventory.json`은 최초 파일별 조사 목록이다. 새 `SourceReviewLedger.json`은 명시적으로 읽은 계약 범위와 변경 여부를 구분한다. `inventory_only` 항목과 계약 일부만 읽은 파일을 전수 정밀 검토 완료로 계산하지 않는다.
- 최종 Editor·Game·Cook/패키지·명시한 통합/원격/렌더 범위와 CPU 반복 비교·별도 단계 trace는 수행했다. traced renderer 종료 실패와 CPU 일부 소폭 증가가 남아 있으며 성공으로 합치지 않는다. 이전 단계 결과는 당시 소스 기준으로 보존한다.
- 이전 200명 NullRHI CPU 비교의 일부 증가와 변동성은 미해결이다. 200개 표시 캐릭터, NullRHI CPU, GPU/FPS, 실제 원격 접속 수는 별도 지표다.
- 전용 Server target, 실제 외부 backend와 대규모 실접속, 모든 클립의 시각·음향 품질과 모든 BP 이벤트 경로는 무결성을 보증하지 않는다.
- 현재 driver에 등록되지 않은 Iris Party/Guild filter·prioritizer adapter, 소비되지 않는 ObjectPool 정책 저장소·초기화 scaffold는 구현 완료로 승격하지 않았다. 실제 호출이 있는 제작/진단/반영 확장 API를 토큰 횟수만으로 삭제하지 않았다.

## 최종 재대조

인계 문서·Body ABP·기존 미커밋 RTG 3개는 시작 보존 SHA와 일치한다. 최종 Editor DLL 8개+modules 1개 및 수정한 공격 정의 10개도 검증 기준 SHA와 일치한다. Source/Config는 최종 빌드 이후 변경하지 않았으며 648개 동결 해시 중 차이는 기록한 검증 스크립트 한 건뿐이다. 지속 곡선 0.35초·100cm·3도·85%의 선언과 기준선 파일도 일치한다. [최종 보존 대조](DeepEvidence_2026-10-13/FinalDeliveryPreservation.json)를 남겼다.

Git의 Content 변경은 원래 사용자의 RTG 한 개와 교정한 공격 정의 10개뿐이다. 코드/문서 변경은 미커밋 상태다. 커밋·push·병합·컴퓨터 종료는 수행하지 않았다. 이 문서·시스템 지도·각 분야 README·제작/수명 기준 문서·문서 카탈로그를 갱신했다.
