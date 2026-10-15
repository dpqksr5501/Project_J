# Project_J 전체 감사 계속 작업 — 2026-10-14

**전체 정밀 감사는 진행 중이다.** 이 문서는 실제 추가 수정·명시한 범위 검토·남은 검토·환경 미검증을 구분한다. [인계](../../Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md)의 RA01–27와 DS/SI 결과를 계승하며 새 개선으로 중복 집계하지 않는다. [현재 함수·호출·소유권 장부](ContinuationEvidence_2026-10-14/CoverageLedger.json)는 이전 소스607·패키지2738·추가설정44와 새 구현/회귀를 유지한다. 현재 소스624개·명시 범위22개는 전수 상세 검토 완료 수가 아니다. 관련 소스/호출/소유권을 시스템으로 묶고 호환 수정·재현을 함께 빌드하며, 집중 회귀와 통합 기준점의 전체/Game/Cook/package를 구분한다. 비율 대신 시스템별 명시 검토·실제 수정·남은 검증을 기록한다.

## 기준점과 보존

실제 Git HEAD는 `855a1c8e111c94a5dffdd08ad05c68bdea2accec`, 브랜치는 `codex/runtime-retarget-profiles`다. 커밋 제목은 `전체 프로젝트 감사 개선·검증 기록과 후속 마무리 인계 정리`다. 시작 당시 사용자의 미커밋 변경은 인계 문서 끝 요청문 삭제였으며 수정하지 않았다. 최초 기록에 함께 표시된 `Scripts/Audit`는 이 계속 작업에서 만든 파일이다. 커밋·push·병합·컴퓨터 종료를 실행하지 않는다.

[시작 SHA 대조](ContinuationEvidence_2026-10-14/InitialHashChecks.json)는 RA27 소스650·Editor9·보존13 모두 일치했다. Body ABP·RTG·공격 DA10·이전 인계의 해시와 OTM/Strafe·Run/Sprint·단발/MM 복귀·Land·측면 후보·전투·장비·리타깃을 계승한다. 지속 곡선 **0.35초·100cm·3도·85%**를 변경하지 않는다. 원래 Evidence/DeepEvidence/ReauditEvidence와 Saved RA27 자료는 덮어쓰지 않는다.

## 실제 추가 수정과 검증 상태

| ID | 발견·원인과 변경 이유 | 실제 변경 | 검증 상태 |
| --- | --- | --- | --- |
| CA01 | fallback이 정책 bool 변화만 확인해 같은 정책의 새 follower/새 AnimInstance 수명에 적용을 생략. coordinator 설정 활성과 실제 외부 tick 소유도 다름 | 현재 follower에 GT tick 대여를 적용하고 대상 변경·초기화·종료 때 원래 tick 반환. coordinator는 fallback 반환 후 authored 값을 캡처. 실제 외부 allocator 소유 동안 쓰기 중단. 같은 대상/값을 매 frame 덮어쓰지 않음 | FollowerRepro1 원본 실패 보존. 실제 coordinator 인수/반환·opt-out 포함 집중 회귀 및 전체 native 통과. Game/Cook/전체 품질은 별도 대기 |
| CA02 | 키 순서 검사만으로 단일 NaN 키·비정규 quaternion·비정상 duration/blend/IK와 invalid refresh의 부분 게시를 차단하지 못함. 편집 drag 합계 overflow도 입력이 finite일 수 있음 | 공통 WeaponMotion numeric admission을 Begin/Refresh/notify validator/Persona preview에 연결. invalid position은 마지막 finite 시간 유지. transform publication을 확인하고 drag는 finite 후보가 성립한 뒤 전체 대입. 부모 sequence/montage에 실제 notify를 검사하는 Editor validator 연결 | 집중 회귀·전체 native 통과. DataValidation의 실제91 montage 중 새 validator 대상8개 검사, 실패 보고 없음. 이것은 손 접촉/공격 품질 전수 검증이 아님 |
| CA03 | OneShot의 현재 yaw는 검사하지만 진입 yaw와 취소 각도는 누락됨 | 진입 sample과 tuning의 finite/nonnegative 계약을 `InvalidInputSample`로 명시. 실제 caller의 vector/control·legacy, Profile과 캐릭터 설정 validator에 관련3각도 연결 | malformed-input·기존 곡선/단발 회귀와 전체 native 통과. 실제 LocomotionProfile1개 DataValidation 정상. 다른 모든 Profile 숫자 검토는 미완료 |

CA01의 새 상태는 gameplay/worker 상태가 아니다. 약한 적용 대상, 대여 전 tick, 마지막 실제 적용값은 서로 다른 수명·반환 책임을 가진다. coordinator의 visibility·external rate·cadence와 source의 GT policy를 합치지 않는다. CA02는 montage의 실제 시간을 계속 사용한다. 새 timer·클립 예외·강제 reset을 추가하지 않았다. 유효한 socket/IK/entry/exit 기본값을 변경하지 않았다.

| 후속 ID | 실제 발견/최종 변경 이유 | 검증·한계 |
| --- | --- | --- |
| CA04 | EffectLevel NaN/Inf/0.5가 validator를 통과해 실제 GAS apply에 게시됨. header ClampMin1과 공통 `HasValidLevel` finite·>=1로 제작/runtime을 맞추고 원값1.5를 유지 | EquipmentStatRepro1 실제 실패2개 중 한 시험. 수정 directUBT·집중13개 통과. 전체/실제 DA·Cook·원격은 해당 새 버전에서 별도 판정 |
| CA05 | base100에 -150 적용 후 clamp0→해제150, bonus10 장착 중 DA99로 바꾼 뒤 해제11, 별도 base성장120에서 bonus 소실/해제110을 두 policy에서 재현. 고정 보너스를 stable native Infinite GE/SetByCaller spec와 기존 active handle 배열로 소유하며 역가산/중복 bool 제거 | EquipmentStatFixFocused1의 actual manager/ASC·합계·중첩·기존 originalASC·callback 폐기/reentry·UI 포함13개 모두 clean. 제작/UI/runtime tuple 일치. 합산 뒤 clamp하므로 입력 순서가 결과를 바꾸지 않음. native GE 원격/Cook·callback GC는 후속 |
| CA06 | 실제 mapping Add callback에서 release/deinit 뒤 Acquire true, GC 때 owner/payload 소실. operation/dispatch 동안 참가자를 strong 보유하고 저장은 weak 유지. callback 뒤 현재 lease 재조회/prune·퇴역 Actor/component 판정 | CallbackContractRepro1 실패를 보존. concrete fixture 교정 후 callback/GC 및 기존 lease/message4개 통과. dispatcher membership snapshot은 payload deep immutable snapshot이 아니다. 모든 BP/device/외부 mapping 변경 조합은 미완료 |
| CA07 | 같은 basename·다른 정의/ItemId의 겹친 avatar provider가 동일 grant source로 충돌해 current ability를 받지 못함. Equipment source key를 definition full path로 변경 | 실제 native 충돌 실패 및 수정 후 distinct 정의 분리·same 정의 공유·각 provider release 모두 통과. generation/refcount 공유 계약은 유지 |

CA05 해제는 live ItemDef/avatar를 다시 읽지 않고 적용 당시 ASC 핸들을 반환한다. 영구 base를 snapshot으로 되돌리지 않으며 성장과 다른 장비/GE를 보존한다. 유효한 음수 장비와 authored GE 우선 정책은 유지하고 최대치 증가 시 무료 heal을 하지 않는다.

### CA08–10 후속 수정과 실제 연결

장부는 후속 소스624개·명시 범위22개까지 확장됐다. 추가 진단 fixture도 대조했으며 이 수치를 전수 정밀 완료 수로 계산하지 않는다.

| ID | 실제 원인·최종 변경 이유 | 검증·남은 경계 |
| --- | --- | --- |
| CA08 | CancelMouseInput이 callback 뒤 새 BoundPlayerCharacter에 나머지 old release를 보냄. active Move도 GAS sprint cancel 재연결 뒤 new pawn에 old start 게시 | old cancel target을 scoped 보유하고 같은 pawn의 새 press는 유지. Move는 원래 pawn/revision을 확보하고 외부 호출 뒤 현재 연결 확인. finite 입력/회전 admission. 실제 실패와 수정 후 관련23 clean. queued old device callback·모든 ASC 이동·BP/device/GC 조합은 미완료 |
| CA09 | Lower installer가 Upper부터 commit한 뒤 duplicateLower에 false를 반환해 graph45→46 및 원래 edge 소실 | 전체 topology/schema preflight→한 node/edge 계획→한 compile. 실패 때 이번 node/edge 반환, compile 뒤 semantic pin 재조회. 실제 transient Master의 duplicate preflight·두 번째 연결 실패 rollback·성공 compile·idempotence 통과. arbitrary compiler/plugin side effects·모든 compile-failure 조합은 미검증 |
| CA10 | Serialize 중 source/manager 종료 뒤 envelope 성공·부분 output 게시, Apply 중 retired actor 성공 ack, caller payload 변경 후 validated3→관찰0, InfiniteDelta의 false timeout/retry | atomic envelope 후보·원래 authority actor/manager lifetime 재확인·scoped 참가자 보유·bounded immutable apply packet·session별 late reply gate·finite elapsed time. 실제 native 실패와 수정 후 관련23 clean. 추가 강제 GC fixture는 빌드/회귀 진행. 모든 config 숫자·분산 commit/중단 복구는 미완료 |

`ActiveInputInstallerReproBuild1`의 C2248 private 시험 접근과 C3535 TObjectPtr range-loop 타입은 fixture 오류다. 중단 없이 UBT 정상 실패 exit6·299.13초까지 기다렸다. 교정 `ActiveInputInstallerReproBuild2`는 exit0·300.72초. `ActiveInputInstaller_Repro1` source685/Editor9/보존13 일치 후 actual Router cancel/Lower preflight 두 시험이 실패했다. 원자료를 유지한다.

`RouterInstallerFixHandoverReproBuild1`은 exit0·296.86초, frozen source688/Editor9/보존13 일치. 집중6개에서 Router/Installer2 clean, 원본 Move/Handover4 실패했다. Move native fixture에는 실제 BP CDO의 authored SprintAbilityTag를 명시했다. `ActiveInputHandoverFixBuild1`은 exit0·299.05초. `CA01_10_Final1` source690/Editor9/보존13 일치, `ActiveInputHandoverFixFocused1` 관련23 clean·fail/warning/notRun/inProcess0. 기존 전체362/CA07 Game/Cook을 CA08–10의 배포/전체 통과로 확대하지 않는다.

`InputDefaults1`은 public Python CDO read exit0·save0지만 montage15개의 Notifies direct property는 보호되어15개 오류를 남겼다. 그때의 state_notifies 빈 배열은 실제 notify0의 근거가 아니고 sprint struct repr도 실제 tag의 근거가 아니다. `InputDefaults2`는 설치 UE5.8의 공식 BlueprintPure `UAnimationBlueprintLibrary::GetAnimationNotifyEvents`와 `UGameplayTagLibrary::GetTagName`을 사용해 exit0·error0·save0으로 읽었다. Config global GameMode→BP_ThirdPersonPlayerController→BP_GreatSword, IMC_Default/mobile-excluded IMC_MouseLook·네 방향 IA·Sprint `State.Movement.Sprinting`을 확인했다. parent montage15 중 Motion8 montage/9 state이며 이 subset의 GroundContact state는0이다. nested sequence/다른 assets·레벨 override·runtime instance는 미완료이며 Ground 전체 미사용을 주장하지 않는다.

앞선 CPU/notify trace를 **R workload라고 표현한 것은 부정확했다.** 실제 AuthoredCPU `FMeasurement::Attack`은 LMB intent를 보낸다. 실제 선택 montage 이름은 이전 measurement receipt가 수집하지 않아 추가 확인해야 한다. 이것은 CookedCombat/원격 R trial과 다른 실행이다. 공식 API로 읽은 AM_Greatsword_R에 Motion이 존재하는 것도 그 CPU workload가 해당 montage를 실행했다는 증거가 아니다. Source caller와 trace의 미관찰을 따로 유지한다.

새 `SharedNotifyLifecycle` 진단은 actual BP body/장비의50/100/200명을 사용하지만 **월드/전체 montage CPU와 별도의 direct native notify 단계**다. Motion은 실제 authored R notify의 public copy API, GroundContact는 명시한 native synthetic registry fixture다. 동일 event-reference time0·동일 engine frame에서 tick bookkeeping을 측정하므로 전체 pose evaluation/시간 진행·손 접촉·GPU·socket 검증으로 확대하지 않는다. 아래 CA11의 국소 sweep 개선과 원래 전체 CPU 증가 미해결을 구분한다.

### CA11 공유 notify와 최신 집중 기준점

`NotifyLifecycleFixtureBuild1`은 trace macro의 statement/ternary와 shared-ref JSON 호출이라는 fixture 오류로 정상 실패했고 원자료를 유지했다. 교정 Build2는 directUBT exit0·291.139초이며 `CA01_10_NotifyBaseline1` source692/Editor9/보존13 일치다. 강제 GC를 추가한 입력/인계 `ActiveInputHandoverGCFocused1`은23 clean이다.

`CA_NotifyLifecycleBefore1`의12 measured region(1warmup/4반복×50/100/200)에서 Motion/Ground 각각 weak visits는 **1,225 / 4,950 / 19,900**으로 매 반복 같았다. Begin마다 모든 live weak mesh를 순회하는 quadratic 비용을 실제로 확인했다. CA11은 dead mesh 정리를 공식 native weak PostGC delegate로 이동하고 BeginDestroy에서 handle을 반환한다. matching End/current token과 inner stale-instance 검사는 유지한다. Motion End는 component cleanup 전에 registry entry를 반환해 callback 뒤 map pointer를 사용하지 않는다. timer/frame cache/특정 clip 예외를 추가하지 않았다.

`SharedNotifyGCLifetimeBuild1` directUBT exit0·294.25초, `CA01_11_Final1` source693/Editor9/보존13 일치. `SharedNotifyGCFocused1`은25 clean·warning/fail/notRun/inProcess0이다. 실제 authored 두 참가자 중 하나를 Destroy+GC해 registry2→1, live motion 보존, matching End의 마지막 entry 반환, native delegate가 notify를 root하지 않는 것을 확인했다. 입력/인계 GC와 기존 PrimaryGrip 교체 계약도 함께 통과했다.

`CA_NotifyLifecycleAfter1`도12 region/exit0이며 Begin 구간에 full prune scope가 없다. 두 직렬 run의 combined Begin 평균(ms)은50 **0.406→0.331**,100 **1.017→0.682**,200 **1.856→1.441**이다. 이 평균은 교대 ABBA가 아니므로 최종 비용 개선률로 확정하지 않는다. 전체 world/OS active CPU/FPS/GPU/손접촉·cloth/remote와 별도다. 공식 Insights counter-list export는 UE5.8이 인수를 tokenize하지 않아 이전 quoted filename이 실패했으나 개별 counter/region CSV는 성공했다. 원본 실패 로그를 보존하고 다음 export에서 공식 parser에 맞춰 수정했다. before/after `NotifyStageSummary.json`의 원자료 SHA·방문량과 한계를 따른다.

Quest/Progression의 전체 함수·헤더·inline과 PS/HUD의 관련 caller, existing reward/snapshot/retirement를 묶어 읽었다. Claim 실패에서 원래 tracking=false를 true로 바꾸는 경로와 transport/config 비정상 시간이 신규 후보다. `RewardTransportReproBuild1`은 세 native fixture 묶음의 원본 동작을 검증하기 위한 빌드이며 아직 수정·통과를 주장하지 않는다. 모든 callback GC·class/MaximumLevel 정책·actual DA/map marker/remote·저장 adapter는 미완료다. ObjectPoolRegistry의 정책만 보관하는 전체3파일/inline와 native caller를 명시 범위로 읽었고 현재 계약을 유지했다.

## 범위 명시 검토 후 유지

CR01은 MMO의 `Coordination`·`Repository`·`FeatureCatalog` 구현/헤더/inline과 `ServicePort`, 실제 foundation fixture와 GI composition root를 대조했다. Gate의 Close는 실제 완료 전 reservation을 유지하고, RequestTracker의 Close는 늦은 delivery를 막는다. 다른 권위이므로 통합하지 않았다. 저장 receipt는 replay 때 원래 결과를 반환하며 현재 record revision으로 덮지 않는다. MemoryRepository는 Core 값 전용 개발 adapter이고 durable 서비스가 아니다. `ServicePort`는 설치된 구현이 없으며 enum/schema/service name 검증 확장은 그 port의 추가 검토다. catalog inl205행의 각 제작 의미와 문서 정합성을 완료했다고 표시하지 않는다.

CR02는 입력 lease의 Acquire/Prune/Release/Deinitialize, Controller의 기본 입력 setup/종료와 Message의 모든 함수 및 fixture를 읽었다. 설치 엔진 EnhancedInput의 Add/Remove가 실제 동기 dynamic callback을 호출하는 것도 확인했다. 기존 map 재진입 수정은 계승하고 새 GC/성공 반환 문제는 CA06로 실제 재현·수정했다. 이것은 모든 BP/device/퇴역·mapping 변경 조합의 완료가 아니다. [기존 IMC 대응](ContinuationEvidence_2026-10-14/InputEvidenceReconciliation.json)의 실제 default27행을 계승했다. UE5.8 GetMappings는 DefaultKeyMappings를 사용하므로 deprecated Mappings 빈 배열을 미연결로 해석하지 않는다. Controller/Character CDO와 instance override·기기/focus 추가 대조는 대기한다.

CR03은 Gateway의 모든 구현·헤더/inline, Handover manager/loopback transport의 모든 함수와 checked actor interface를 읽었다. HTTP GT completion/at-most-once tracker, mutation UnknownOutcome, attempt/state 재확인·replay digest·terminal payload 반환을 유지한다. 외부 serialize/TryApply/state callback에서 참가자가 GC되거나 월드가 종료되는 경우와 malformed timeout/capacity는 새 조사 범위다. actor의 실제 apply 구현과 관련 사회/PS/저장 투영을 끝까지 대조하기 전 완료 처리하지 않는다. `Ghosting/SwitchingAuthority`라는 상태 이름은 분산 권위 commit의 실행 증거가 아니다.

## 실행 경계와 원자료

새 run root는 `Saved/Validation/Continuation_20261014`다. 모든 이름은 새 이름이며 이전 시험·실패·원응답을 보존한다.

- `FollowerReproBuild1`: 직접 UBT가 ProgramData UBA session 쓰기 거절을 반복했다. 사용자 최신 답변 **“이번 멈춘 빌드만 종료 허용”** 후 해당 exec 세션만 종료했다. 다른 실행 종료 권한으로 확대하지 않는다. 큰 raw 로그는 Saved에 유지한다.
- `FollowerReproBuild2`: 공식 `-UBARootDir`로 작업 폴더를 지정했다. sandbox의 `FindFirstVolume (Access is denied)`로 즉시 실패543221. source compiler 오류가 아니다.
- host `Get-CimInstance`/`Get-Process`를 표준 승인 경로로 실행해 잔류 빌드 없음 확인, 관련 Application Event Log1000/1001/1026 조회. sandbox Get-CimInstance는 거절됐고 Get-Process의 이름 없는 출력은 host idle의 충분한 증거로 쓰지 않는다.
- `FollowerReproBuild3`: 같은 직접 UBT를 표준 승인 경로에서 정상 완료, exit0·267.87초. engine/ProgramData 권한 또는 엔진 파일 변경 없음.
- `FollowerRepro1`: native 실제 인스턴스 시험 실패. 뒤 assertion 일부는 앞 실패의 결과이므로 assertion 수를 독립 결함 수로 세지 않는다.
- `ContractsFixBuild1`: 처음 오류C2661은 새 시험의 DataValidationContext 생성자 사용이었다. UBT 정상 실패 종료273.12초까지 기다렸다. `ContractsFixBuild2`는 default 생성자를 사용하는 시험 수정만 추가한다.
- `ContractsFocused1`: 새 fixture의 `Keys.Add(Keys[0])` 자기 배열 참조가 UE Array assertion을 일으켰다. fixture에서 값을 먼저 복사해 수정했다. 제품 결함으로 집계하지 않는다. 종료 전 기존 authored curve/playback 및 새 follower/OneShot 시험은 통과했으나 미완성 report를 전체 통과로 계산하지 않는다.
- `WholeContinuation1`: 356개 중 기존 CombatContinuity5개 실패. 실행 wrapper가 앞선 기준의 `a.Budget.BudgetMs 0.1`을 누락한 원인이었다. wrapper 조건을 복원했으며 production tuning으로 증상을 가리지 않았다.
- `FinalContractsBuild1`: directUBT 정상 exit0, 268.06초. `CA01_03_Final2` source/editor/preservation SHA로 해당 버전을 고정했다. 보존13개 일치.
- `FinalFocused1`: 기존 CombatContinuity5개와 새 contract/편집 경계5개, 총10개 통과(5 clean·5 warning), 21.21초.
- `WholeContinuation2`: CA01–03 Final2, 총356개(336 clean·20 warning), 실패/미실행/진행 중0, exit0, 665.81초. 이전 RA27의352개와 새4개를 구분한다. 기존 fixture socket 경고 등을 억제하지 않았다. queue 밖 startup Python self-test `LogAutomationTest: Condition failed`는 RA27에서도 관찰된 별도 로그이며 모든 raw error가0이라고 주장하지 않는다.
- `CA_AssetValidation1`: 읽기 전용 native DataValidation. LocomotionProfile1·Montage91의 실제 로드/검사와 부모 WeaponMotion validator8 대상 실행, exit0. 총 validator별 대상 수가 상세 기능·품질 완료 수가 아님.
- `EquipmentStatReproBuild1`: 기존 production+재현 fixture directUBT 정상 성공269.53초. `EquipmentStatRepro1` actual EffectLevel/EquipmentStatLease 두 시험 실패, 원자료 보존.
- `EquipmentStatFixBuild1`: CA04–05 수정 directUBT 정상 성공275.16초. `CA01_05_Final1` source675·Editor9·보존13 일치. source675에는 script/config도 포함되므로 소스 수동 검토614와 의미가 다르다.
- `EquipmentStatFixFocused1`: 13 clean, warning/fail/notRun/inProcess0.
- `WholeContinuation3`: CA01–05 Final1, 전체359개(339 clean·20 warning), fail/notRun/inProcess0, 정상 exit0·665.93초. 기존 warning을 유지한다. 임의 전체 소스/에셋 정밀 완료로 확대하지 않는다.
- `CA01_05_GameBuild1`: directUBT Game 정상 성공 exit0·204.52초. 실제 exe/pdb/target SHA와 frozen Source/Config/descriptor626개 일치.
- `CA01_05_Cook1`: 별도 fresh Windows Cook 정상 exit0. cooked1361·platform 제외7·incremental skip0, error0·기존 GameplayCue warning1. 기존 Saved/Cooked·Package 보존. Zen은 공유 mutable cache이며 이전 cache 전체의 불변/인증된 cold를 주장하지 않는다.
- `CA01_05_Package1`: UAT가 CookOutputDir에 Windows 하위 경로를 요구한 것을 wrapper가 놓쳐 Stage 정상 실패1. 제품 결함으로 집계하지 않는다. 원래 Cook을 새 Windows layout43파일·432912976bytes로 복사했고 모든 SHA가 같았다.
- `CA01_05_Package2`: 그 사본으로 Stage/Package 정상 exit0·38초. exe SHA가 directUBT Game과 일치. 기존 DefaultMass.ini 명시 allowed/denied 경고를 유지.
- `CA01_05_CookedCombat1`: 해당 실제 cooked exe에서 input/GAS/montage/notify/unequip3회 정상, cooked1·exit0. native standalone authored visual fixture이며 모든 retarget/장비 품질이나 새 stat GE의 원격 증거가 아니다.
- `CA01_05_CookedUI1`: 같은 exe,1280×720/1920×1080, 각각 fresh UserDir·1000행·24buff·4window·50step·5Hz·50filter·5windowtoggle·10replacement, 모두 success/cooked1·error0·exit0. CSV600행과 앞선 recipe의120행 이후480행을 함께 보존. 개별 inclusive UI timer를 합산해 GT 전체 시간으로 표시하지 않는다. UI screenshot/접근성 전수 품질과 이전 UI3 A/B 비교는 아님.
- `CA01_05_Remote1`: 최신 Editor host의 독립 server+socket client2·NPC200·AllRelevant·Iris parallel 설정·lag40/loss2·실제 animation/전투3trial. AOI/FastArray5단계·authority/owner prediction/remote R→target→end·graceful disconnect 통과, exit0×3·timeout0·startup Python error0. 실제200socket/WAN/독립 native Server/새 stat GE 복제 보증은 아님.
- `CallbackContractReproBuild1`: fixture directUBT exit0·310.60초. CallbackContractRepro1 기존2개 성공·새2개 실패. abstract UObject 생성 ensure는 fixture 오류이며 concrete type으로 교정했다. 단순 ensure 수를 제품 결함 수로 집계하지 않는다.
- `CallbackFixIdentityReproBuild1`: directUBT exit0·295.35초. CallbackFixIdentityRepro1 GC/기존4개 통과, 새로운 EquipmentGrantIdentity 실제 실패1.
- `IdentityTraceFinalBuild1`: CA06/07·역할/notify trace 계측 directUBT exit0·301.55초. `CA01_07_Trace_Final1` source682·Editor9·보존13 일치. source682는 config/script를 포함한다.
- `CallbackIdentityFinalFocused1`:18개(17 clean·1기존 warning), fail/notRun/inProcess0.
- `WholeContinuation4`: CA01–07/trace Final1의 전체362개(342 clean·20기존 warning), fail/notRun/inProcess0·정상 exit0·665.67초. 전체 소스/에셋 감사 완료 수가 아니다. Game/Cook/원격은 아래 CA01–05 실행과 구분하며 새 CA06/07 production 버전은 별도 검증 대기다.
- `CA01_07_AssetValidation2`: 실제 AbilitySet2·EquipmentDefinition1, 각 contains valid data·exit0·error/warning0. 작성 형식/숫자 계약의 범위이며 전체 전투/원격/품질 증거가 아니다.
- `CA01_07_GameBuild2`: directUBT Game 정상 exit0·250.64초, actual source629개와 CA01_07_Trace_Final1 대응 및3산출물 SHA 기록.
- `CA01_07_Cook2`: fresh Windows default map/Entry/configured AlwaysCook·AssetManager closure, cooked1361·incremental skip0·platform 제외7, error0·기존 Cue warning1·exit0. 미사용 에셋 CookAll 또는 전수 품질의 증거는 아님.
- `CA01_07_Package3`: 최신 별도 Cook의 정상 Windows layout으로 Stage/Package exit0. actual exe SHA `18EE81687C463DA630A484FB922AC7690C6B4D4B050B213A4A86A3FE250EC418`가 직접 UBT Game과 일치. 후속 production 수정이 생기면 이 실행은 CA01–07 버전의 증거로 유지한다.

[원자료 인덱스](ContinuationEvidence_2026-10-14/EvidenceIndex.json)는 bounded receipt/report/manifest/CSV 사본과 큰 raw log·trace·이미지의 Saved 경로/SHA/크기를 보존한다. 실행별 source manifest와 실제9산출물을 결합해 이전 자동화 결과를 새 수정본에 잘못 귀속하지 않는다.

컴퓨터 사용은 제공된 스킬의 공개 API로 관련 창 목록을 조회했다. 작업 관리자 실행은 app approval timeout으로 실패했고 대체 비공개 UI 경로를 사용하지 않았다. 사용할 수 있는 창 목록만으로 모든 예외 대화상자의 부재를 증명하지 않는다.

## CPU·렌더 조사

기존 RA13 CPU 일부 증가와 Render2→3 FPS 차이는 아직 해결되지 않았다. `Scripts/Audit/Run-ContinuationComparison.ps1`는 명시적 A binary·compiled source manifest와 실행 시작 당시 B의 완전한9산출물을 고정한다. 교대 ABBA 후 **시작 당시 B**를 복원하고 해시 receipt를 남긴다. 프로세스가 남으면 binary 교체를 거절한다. 기존 `Run-BalancedCPU.ps1`를 최신본에 실행하지 않았다.

새 CA 수정의 RA27 대비 성능과 앞선 DS→RA13 변화는 다른 비교다. NullRHI world tick 시간, D3D12 실제 표시200의 FPS/GPU, localhost 실제 socket 수는 각각 별도 축이다. thread별 exclusive elapsed timer는 OS의 실제 CPU active time이 아니며 특히 WaitForTasks는 대기 시간이다. worker 누적을 world critical-path처럼 더하지 않는다.

| 실행 | 실제 조건·결과 | 판정 한계 |
| --- | --- | --- |
| CA_CPU_ABBA1 | RA27 A→Final2 B→B→A, NullRHI body200·socket0, sim60Hz, warm240/측정600·조건8·repeat2·64행. AttackBurst ABA off/on 평균 -3.25/-2.73%, Near -0.71/-3.17%, Far -0.39/+0.20%, Hidden -0.32/-0.87%. 종료 후 최신9 복원 SHA 일치 | ABA 실제 실행량/스케줄링도 함께 기록. 단일 비율로 전 workload 또는 기존 증가 해결을 선언하지 않음 |
| CA_CPU_Trace1 | 별도 A/B trace, warm120/측정120. 실제 PoseSearch PCA/KNN111126→110984, BuildQuery 같은 수, UpdateAssetPlayer478979→478640. measured16+attack dispatch4 region. 전체 trace WorldTick3840개의 inclusive60.483→61.753초(+2.10%) | whole trace는 setup/warmup 포함·짧은 pair·trace overhead. ABBA와 수치가 다르므로 이 pair를 장기 회귀로 단정하지 않음. `MMResult` 관찰과 실제 database 검색을 구분. 개별 notify Begin/Tick/End·weak-table 방문 수는 미계측 |
| CA_Render_ABBA1 | 같은 RA27/Final2 ABBA, D3D12 Editor/game-mode offscreen1280×720·uncapped/VSync0·actual rendered body200·socket0. 공격200 montage 구간 평균33.602→32.926ms(-2.01%), 복귀 후15.865→14.973ms(-5.62%). 최신9 복원 일치 | single R burst·복귀, 지속200공격 아님. capture/stall 포함, GPU counters 지연. 이전 packaged Render2/3과 host가 달라 직접 FPS 회귀로 합산하지 않음 |
| CA_CPU_Historical_ABBA1 | 해시가 일치한 DS01–18 A·RA01–13 B의 전체9+compiled source를 현재 asset/스크립트의 동일 ABBA 조건으로 비교64행. AttackBurst off/on +0.19/-2.58%, Far +0.31/-1.60%, Hidden +0.86/+2.74%, Near -0.99/-2.61%. 종료 후 실행 당시 CA01–05 최신9 복원 | 이전 동일 코드 세션의 off/on 모든 증가와 일부 방향이 다르다. Hidden+ABA의 bone/MM 관찰량2400·visual graph0·managed mesh tick120000은 A/B 각4repeat 모두 같았지만 +2.74%가 남음. 실검색·스케줄링·열·미계측 조회의 인과는 미확정. 과거 asset/열 조건 동일을 주장하지 않음 |

위 CPU/렌더 비교는 CA01–03 Final2 버전의 증거다. 후속 production 수정 뒤 해당 기능 회귀와 필요한 성능 조건을 다시 판정해야 한다. 기존 DS→RA13 CPU 증가와 Render2→3 차이의 원인은 여전히 미해결이며 이 결과로 닫지 않는다.

## 전체 범위에서 새로 확장한 조사

CR04는 GAS AttributeSet의 모든 clamp/replication/execute·ASC의 grant/입력/권위·AbilitySet의 give/take/validator·장비 runtime/manager/definition·UI 고정 보너스 실제 caller를 읽었다. 기존 current resource clamp, 최대치 증가 시 무료 heal 금지, source generation/reentry 반환은 유지한다. 모든 ability/task와 모든 예측/GC를 완료한 범위가 아니다.

CA04–11은 위 표/버전별 receipt를 따른다. router release loop/active HandleMove와 Handover serialize/TryApply의 실제 실패는 CA08/10으로 재현·수정했고 추가 forced GC를 통과했다. semantic press signed 순번 overflow, 모든 ASC prediction epoch/registry 참가자 GC, queued input origin 및 Blueprint callback 전체는 미완료다.

CR06의 `CA_CPU_NotifyTrace1` CA05 A/CA07 B pair에서 B 전체 trace의 demand4,453,941회·exclusive2.407729초, follower1,392,902회·0.127767초, policy1,386,502회·0.144947초를 관찰했다. TwoHandIK Begin/End는 각각800회지만 **LMB intent** workload의 WeaponMotion/GroundContact timer/counter는 미관찰이었다. 실제 실행 montage명은 미수집이다. 이 조건을 R라고 한 옛 요약은 현재 문서와 함께 정정하여 읽고 이미 저장한 원자료는 덮어쓰지 않는다. CA11의 별도 실제 R Motion/synthetic Ground direct stages에서 quadratic sweep을 확인·수정했다. 이것은 원래 CPU 증가의 인과가 아니다. Super의 Blueprint notify callback 참가자 수명과 모든 clip의 실제 품질은 여전히 미완료다.

## 23시 작업 경계

사용자 최신 요청에 따라 **2026-10-14 23:00 KST까지 가능한 작업을 진행하고 나머지는 이후에 계속한다.** 전체 검토·검증 범위 자체를 축소하지 않는다. 늦게 시작한 빌드/검증을 강제 중단하지 않도록 종료 전 충분한 시간이 남는 실행만 시작한다. 종료 시 실제 수정/범위 명시 검토/미완료 검토/환경 미검증과 마지막 성공 산출물·Git·프로세스·다음 순서를 갱신한다. 커밋·push·병합·컴퓨터 종료는 별도 최신 요청이 필요하다.

AssetRegistry 읽기 결과는2735 package이고 이전 disk2738 중3개 `(1)` 이름 파일은 등록되지 않았다. 세 파일은 원본과 SHA가 달라 내용 동일 복제라고 단정할 수 없다. 관련 Source/Config/Scripts literal 참조 증거가 없지만 그것이 삭제 권한·무용성 증명은 아니다. 파일을 보존했고 [registry 대응](ContinuationEvidence_2026-10-14/AssetRegistryReconciliation.json)에 기록했다. Cue notify provider0은 공개 registry class/native-parent tag의 한정 결과이며 모든 soft/native/config closure 완료가 아니다.

## 전체 범위에서 남은 일

장부의 미분류/미검토 함수·헤더·inline·조건부 컴파일·실제 native/reflection/BP entry 및 실패 분기를 계속 닫아야 한다. CA 수정의 회귀와 production Game/Cook/package·전투/UI/원격·시각 품질·부하 검증도 최신 수정본에 대해 추가해야 한다. 이전 RA27 통과를 새 production 수정본의 통과로 계산하지 않는다.

CA09의 whole chain preflight·링크 실패 rollback·성공 compile/idempotence, CA11의 동시 시작 방문량·native GC 수명은 **명시한 시험 범위**에서 확인했다. 임의 compiler/plugin side effect, 모든 Blueprint notify callback·누락 End·pose progression/품질, 전체 source/asset reachability·BP/CDO/instance override·DT/Chooser/PSD/IA/IMC 제작 연결, GameplayCue closure, 모든 몸체·장비·clip/cloth/audio/접근성 조합은 미완료다. live HTTPS backend·지속 저장·분산handover·WAN·독립 Server binary·실제200socket은 환경 미검증이다. 설치 엔진의 Server 미지원 제한을 우회하지 않는다.

**전체 검토가 남아 있으므로 완료 보고를 하지 않는다.** 상세 상태는 장부의 각 명시 범위, 기능별 run 및 후속 receipt를 우선한다.

## CA01–13 통합 기준점과 시스템별 상태

`RewardTransportReproBuild1`은 directUBT 정상 성공·289.98초이고 frozen `RewardTransport_Repro1`은 source694/Editor9/보존13 일치다. 실제 세 시험 모두 실패했다. Quest는 XP1+MAX_int64 reward overflow 거절 뒤 tracking=false→true, loopback은 NaN의 이후 valid clock 독성과 Inf의 즉시 ack/invalid delay 정지, manager의 timing4개×NaN/Inf/negative12종은 validate/dispatch 승인으로 재현했다.

CA12는 claim-before-reward의 재진입 지급 방지 계약을 유지하고 실패 시 원래 `bWasTracked`를 반환한다. CA13은 공통 finite/range timing admission을 Build/Validate/Start/Tick에 연결하고 `InvalidConfiguration`을 기존 enum 끝에 추가했다. invalid config는 새 record/transport를 게시하지 않는다. loopback invalid elapsed는 소비하지 않고 invalid tuning은 마지막 finite delay를 유지한다. **finite age0의 unlimited 정책·finite negative loopback delay의 immediate 정책은 유지**한다. invalid live config에서 실제 elapsed를 reset하지 않고 valid config 복원 후 원래 finite 값에서 재개한다. capacity integer/outcome enum·arbitrary transport/GC·config callback 중 변경·live 서비스는 미완료다.

`RewardTransportFixBuild1` directUBT 정상 성공·281.33초. `CA01_13_Integrated1` source694/Editor9/보존13 일치이며 `IntegratedFocused1`38 clean이다. `WholeContinuation5`는 **373=353 clean+20 warnings, fail/notRun/inProcess0**, native test duration667.413초다. 앞선 WholeContinuation4의362개가 전부 포함되고 새11개가 추가됐다. 경고20개의 시험 이름과 실제 Warning message가 모두 정확히 동일함을 `InheritedWarningComparison.json`으로 대조했다. 자동화 통과가 모든 source/asset/운영 실패 검토 완료는 아니다.

`CA01_13_GameBuild3`는 directUBT 정상 성공·210.50초이고 실제 Source/Config/descriptor636개가 통합 frozen manifest와 일치한다. `CA01_13_Cook3`는 Windows configured default map+Entry+AssetManager/AlwaysCook closure의 **cooked1361 / incremental0 / platform skip7 / error0 / 기존 Cue warning1**, exit0이다. `CA01_13_Package4` Stage/package도 exit0이며 실제 native packaged exe SHA **26098d789b055b9e3e54d7832311d391e8d08e24216aa6960e652bcfe37e8cea**가 direct UBT Game과 일치한다. CookAll/unused content·전체 시각/원격 완료로 확대하지 않는다.

| 시스템 | 명시 검토·실제 수정 | 최신 확인·남은 검증 |
| --- | --- | --- |
| Presentation/무기/단발 | CA01–03/11 tick 대여·공통 숫자·입력 sample·native shared notify GC 정리 | 통합373·Game/Cook/package. native notify50/100/200 국소 trace. 모든 Super/attachment callback GC·clip/몸체/cloth·pose 진행 품질 미완료 |
| 장비/GAS | CA04–07 finite effect level·native active GE 보너스·definition source identity | 실제 재현·기존 source lease/회귀 계승+통합373. native GE의 별도 원격 효과 복제/예측·모든 ability/task/GC 미완료 |
| 입력/Message | CA06/08 operation GC 보유·현재 lease 재조회·원래 cancel/Move pawn과 revision | 통합38/373. actual CDO/IMC/IA/tag 읽기. queued old-device origin·모든 ASC/기기/focus/BP callback 미완료 |
| Handover | CA10/13 atomic serialize·immutable apply packet·manager epoch/actor 수명·finite timing | 강제GC/실제 원본 실패와 통합38/373. 실제 Player v1=pose/rotation/level. inventory/cooldown 전체 이동·durable/distributed/WAN 미검증 |
| Editor facing 설치 | CA09 chain preflight·owned mutation rollback·one compile | actual Master transient clone failure/success/idempotence 및373. save0. 임의 compiler/plugin side effect/모든 제작 graph 미완료 |
| Quest/Progression | CA12 actual overflow rejection의 tracking rollback. 두 component 함수/헤더/inline와 관련 PS/HUD caller | XP/replay/restore/retirement 포함38/373. callback GC·class/MaximumLevel 정책·DA map target/BP/저장·원격 미완료 |
| NPC decision/path/action/activation/attack | CR11 관련 함수/헤더/inline·실제 호출 대조, 기존 소유/완료 계약 유지 | 기존 경로/공격/suspension/native load 회귀373. CancelAbility 뒤 weak ASC 재사용·BP IsDead/target multicast GC는 새 미재현 후보. 실제 BT/BB/BP/DA/nav 제작·운영 부하 미완료 |
| Foundation/ObjectPool policy | CR01/09 명시 범위 검토 후 유지. cancellation delivery/실제 완료·원래 replay receipt·값 policy 역할 분리 | native 회귀 계승+373. catalog205행 제작 의미·서비스 설치/영구저장·pool consumer/BP는 미완료/미검증 |

CPU·렌더의 원래 차이는 미해결이다. `CA_CPU_Integrated_ABBA1`은 RA27와 최신 Editor9를 **A/B/B/A, body200·socket0, repeat2·warmup240/measured600·sim60Hz·NullRHI·budget2ms**로 비교한다. 실행 시작 당시9파일을 복원한다. 새 `ProcessCpu.json`은 전체 child process의 setup/warm/측정/종료 OS user/kernel 누적 시간이며 native world/GT/worker 측정 구간과 별도다. 실행이 끝나기 전 결과/회복을 확정하지 않는다.

### 최신 통합본의 성능·원격·cooked 실행

위 CPU ABBA는 정상 종료·64행·최신9파일 복원 확인을 마쳤다. mean world tick 변화는 Near ABA off/on **+7.521/+3.375%**, AttackBurst **+5.174/+6.333%**, Far **+4.783/+2.325%**, Hidden **−0.049/−0.084%**다. Near/Attack ABA off의 bone/MM/visual/managed 관찰량은 같은120000/119800이고 Hidden도 같다. ABA on의 bone 수 등 일부 수행량은 달라 비용·작업량을 분리해야 한다. B2가 B1보다 느린 조건도 있어 환경 편차를 배제하지 않는다. 전체 process OS CPU seconds는 A1/B1/B2/A2 **1020.453/1036.719/1033.797/1033.406**, wall seconds **260.580/267.029/275.182/258.892**다. 이는 setup/warm/측정/종료를 포함해 native measurement window와 직접 합산하지 않는다. 원래 증가를 해결했다고 표시하지 않는다.

`CA_CPU_Integrated_Trace1` 별도 pair는120warm/120measure·20지역·A/B 정상 종료 및 최신9 복원을 마쳤다. actual PCA/KNN **111113→110910**, node UpdateAssetPlayer **478992→478376**로 검색 호출이 늘지 않았다. 전체 world tick3840의 inclusive **59.518182→62.279522초**. worker animation exclusive5.931526→6.383168, rigid body8.711766→9.143818, WaitForTasks110.224078→115.663637초다. worker 누적·wait는 OS active CPU/critical path와 다르며 합산하지 않는다. 새 B demand4,454,834회/2.439281초, follower1,393,610회/0.133671초, policy1,387,210회/excl0.154644초를 관찰했다. **A의 새 timer 부재는 비용0이 아니라 미계측**이다. 공통 VisualAnimationBudget의 inclusive1.046447→1.105935초와 새 nested timer로 변한 exclusive를 구분한다. LMB workload의 Motion/Ground는 이번 pair도 미관찰이고 TwoHand1600개 scope만 관찰했다. search 호출 증가·CA11 sweep으로 현재 CPU 증가를 설명할 근거는 없다. 실제 검색시간/물리·snapshot·worker/대기·역할 호출의 세부 인과와 동조건 추가 대조는 미완료다.

`CA_Render_Integrated_ABBA1`은 Editor-host D3D12 offscreen1280×720·실제 rendered200·socket0·one R burst/복귀, 동일 카메라/품질/cap0/vsync0이다. full200 mean frame A1/B1/B2/A2 **30.602/31.954/29.893/38.160ms**, 각 GPU **11.612/12.172/11.554/12.514ms**. run mean 집계34.381→30.924ms(−10.054%)지만 A1/A2 편차가 커 확정 개선으로 해석하지 않는다. 복귀 집계14.889→14.721ms(−1.130%). capture/stall/GPU delay 포함이며 이전 packaged Render2/3과 host가 달라 직접 합산하지 않는다. 분석기의 과거 hardcoded CA01–03 label은 `IntegratedPerformanceContext.json`의 actual manifest/SHA로 정정해 읽고 옛 summary를 덮어쓰지 않았다. B1 phase3 PNG는 검은 배경의 작은200 grid이므로 hand/cloth/몸체 품질 전수 증거가 아니다.

`CA01_13_Remote1`은 Editor host server+**실제 socket2·NPC200·lag40/loss2**, AOI/FastArray/animation/CombatInput fixture passed·process3 exit0·timeout0·Python startup error0. actual R input3회 dispatch와 두 client의 R root-motion 기록을 확인했다. 새 native stat GE의 모든 remote effect/prediction·독립 Server binary·200socket·WAN 검증으로 승격하지 않는다.

같은 packaged exe SHA의 단독 `CA01_13_CookedCombat2`는 actual input/GAS/montage/notify/unequip **3cycle**, success1/cooked1/exit0이다. `CookedCombat1`은 첫 Insights 분석과 겹쳤으므로 기능 assertion만 계승하고 성능/PSO 비교에서 제외한다. 첫 B export 시도는 프로세스 가드로 시작 전에 거절됐고 정상 종료 뒤 단독 재실행했다. 둘 다 중단하지 않았다. `ValidationOverlapReceipt.json`과 원본을 보존하고 future cooked combat 가드에 Insights를 포함했다.

`CA01_13_CookedUI1` 두 해상도1280×720/1920×1080은 same exe SHA·exit0/pass·logError0, 각600 numeric frames의1000행/24buff/4window/50-step dynamic workload다. `CA01_13_CookedRender1`도 actual rendered body200·R montage/복귀·cooked1/success1/exit0를 통과했다. 모든 clip/장비/접근성/cloth·실제 플레이 visual 품질 완료가 아니다.

CR12는 새로운 source 계약 후보를 기록했다. Class/advancement header·graph validator와 actual Get/Set/serialize/apply를 대조한 결과, StartingLevel은 최소1만 요구하고 Initialize/Set은 MaximumLevel 위로 올릴 수 있지만 Restore는 초과값을 거절한다. Player v1 packet은 Level>=1만 검사하고 teleport 뒤 void SetCharacterLevel을 호출한다. initialized Progression이 level을 거절해도 Player apply는 그 결과를 전달받지 못하고 alive 여부로 성공할 수 있다. **native 재현/수정은 아직 없다.** 다음 session에서 원래 pose/level/receipt 정책을 묶어 재현하고 원천 계약으로 결정해야 한다. 필요한 teleport trajectory/SSR 보상은 유지한다.
