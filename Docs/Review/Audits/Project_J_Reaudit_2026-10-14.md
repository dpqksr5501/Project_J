# Project_J 전수 범위 재점검 — 변경·검증과 미완료 범위

최신 권위 기록은 [현재 검증 결과](Project_J_Reaudit_Verification_2026-10-14.md)와 [상세 결과/남은 문제](Project_J_Reaudit_Remaining_2026-10-14.md)다. 아래 일부 진행 문구는 단계별 과거 기록이며, 최신 결과로 승격하지 않는다. 현재 RA27 native352개·index3회·원격3회와 production RA25 package3 통합을 완료했다. 전체 전수 정밀 완료·CPU 해결 완료는 아니다.

사용자가 모든 소스·로직·아키텍처의 파악과 개선 여부를 다시 확인하도록 요청했다. 이전 605개 파일 목록과 75개 파일의 명명된 계약 검토는 전수 정밀 검토 완료가 아니다. 이번에는 이전 장부의 inventory-only 중 구현 본문 141개부터 다시 읽고 관련 헤더·실제 호출·소유권·제작·종료 경계로 범위를 확장한다. 단순 파일 읽기, 계약 대조, 실제 수정, 실행 검증을 구분한다. 완료되지 않은 범위를 완료로 승격하지 않는다.

현재 RA01–19 변경본: LifecycleFinalBuild1 직접 Editor UBT 278.12초 정상 성공, LifecycleFocused1 19 clean/경고·실패·미실행0, exit0이다. 같은 소스의 WholeLifecycle1은 **351개(331 clean·20 warning), 실패·미실행·inProcess0, 665.24초, 정상 exit0**이며 새 RA19개가 모두 clean이다. 아래 RA15/16/17/19의 '빌드/회귀 진행'은 이전 단계 기록이다. Warning test 이름/개수20은 RA13 WholeTransition1과 같지만 모든 메시지 동일성·무해함의 증명은 아니다. RA19AutomationWarningComparison.json에 비교했다. 현재 소스650·Editor9개 산출물을 RA19SourceSnapshot/RA19EditorBinaries로 별도 고정했고, 51개 텍스트 변경(생산45·테스트/fixture3·검증script3)을 기능별17개 소스 단위에 매핑했다. 에셋 RA18은 별도 기록이다. RA19 GameBuild3·Cook2·Packaged2와 후속 실제 피해/전투/UI/표시200/원격 반복 통합은 정상 완료했다. RA20–25의 추가 변경·검증은 다음 절의 별도 버전 기록으로 읽는다. RA13 성능/패키지 자료를 추가 변경본의 실행 결과로 계산하지 않는다.

[앞선 심화 감사](Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](Project_J_Deep_Structure_Map_2026-10-13.md)는 DS01–18 시점의 근거다. 그 시점의 332개 회귀·Game/Cook/원격/CPU 결과를 이번 추가 변경의 최종 검증으로 사용하지 않는다.

## 중단 재개 이후 — RA20–25

[상세 결과와 남은 문제](Project_J_Reaudit_Remaining_2026-10-14.md)에 추가 GC 계약, 미사용 MM 접근자·조기 전이 depth·GAS 활성화 loop 제거, game/server 검증 host의 EditorOnly Toolset 범위를 기록했다. 현재 RefinementFinalBuild2 직접 UBT 263.64초 정상 성공 후 RA25SourceSnapshot/RA25EditorBinaries를 새로 고정했다. 텍스트650·Editor9개, 기준선 대비 변경63개(생산57·fixture/test3·검증script3), 기능 단위23개에 수정파일61개를 모두 매핑했고 추가 회귀파일2개도 별도 연결했다. WholeRefinement1 및 원격 반복·Game/Cook/패키지·통합 순차 검증 진행 중이다. RA19 패키지2 통합은 완료했으며 새 RA25의 결과로 계산하지 않는다.

## 기준선 보존

`Saved/Validation/Reaudit_20261014/Baseline`에 현재 미커밋 작업을 포함한 648개 텍스트를 해시와 함께 보존했다. 8개 Editor DLL+modules 파일 9개, 이전 장부·보존/에셋 해시, Git 상태와 tracked text patch를 별도 보존했다. 기존 인계 문서·Body ABP·RTG·공격 DA 교정과 지속 곡선 0.35초·100cm·3도·85%를 보존한다. commit·push·merge·컴퓨터 종료는 하지 않는다.

## 확인 및 변경 단위

| ID | 실제 근거와 원인 | 변경 | 검증 상태 |
| --- | --- | --- | --- |
| RA01 | native MVVM FieldNotify가 C→B로 재진입한 뒤, 이전 B delegate가 새 A 모델 체력 10을 75로 덮어씀. 초기 값을 게시한 뒤 handle을 등록해 nested binding의 handle이 유실됨 | complete delegate lease를 FieldNotify 전에 게시. 실제 ASC/handle을 먼저 분리해 반환. revision payload로 이전 방송 거절, callback 범위의 component/model/ASC pin, 4개 attribute 게시를 공통 guard로 정리 | 수정 전 ReproContracts1 실패, 수정 후 ContractsFocused1 포함 통과. release/GC 및 callback 후 pin 반환도 확인 |
| RA02 | BeginPlay 이전 파괴한 CombatPresentation이 공격 tag를 남기고 late Begin을 다시 수락 | EndPlay/Destroy 공통 retirement, notify/RPC admission 종료. Niagara stop 전에 이전 cue map/lease를 분리하고 callback 범위 GC pin. 새 reentry가 기존 정리에 의해 지워지지 않도록 기존 attack instance로 확인 | 수정 전 ReproContracts1 두 mismatch, 수정 후 같은 native 계약 및 기존 PresentationRecovery 통과. RHICosmetics1 실제 pooling 2 clean 및 WholeTransition1 통과 |
| RA03 | actual TakeWidget/NativeConstruct가 다른 world의 map definition을 거절했지만 이전 MapTexture는 남음 | 재구성/NativeDestruct/BeginDestroy의 공통 release, texture/brush/request 정리, timer를 실제 SampleWorld에서 반환. 별도 streamable 구현을 기존 UIIconRequest의 취소/세대/GC 계약으로 대체 | DataReproContracts1 실패 확인. 구현 후 실제 재구성 회귀와 WholeTransition1 통과 |
| RA04 | NaN WalkSpeed를 native IsDataValid와 runtime configuration 진단이 모두 정상처럼 수락. 비교 `<=`/`<`만으로 nonfinite가 검출되지 않음 | authored movement의 finite/nonnegative 계약을 공통 함수로 대조. 선택한 CMC 5개 출력은 모두 검사한 뒤 함께 게시해 잘못된 값 또는 곱 overflow의 부분 적용을 차단 | DataReproContracts1 실패 확인. 실제 CMC publication 회귀는 수정 후 추가했으며 수정 전 런타임 재현으로 주장하지 않음. WholeTransition1 통과 |
| RA05 | 공개 RequestMount와 실제 GAS AnyCountChange callback에서 첫 mount 종료 중 두 번째 mount를 요청. mounted count가 2가 되고 최종 해제 후 1이 남음. 첫 mount의 늦은 이동 복원도 새 세션을 덮어씀 | component가 전이 revision과 실제 ASC lease를 소유. 콜백 후 이전 전이는 게시를 멈추며 EndPlay/Destroy는 공통 retirement. actor의 possession/이동 복원은 현재 rider 관계를 다시 확인 | OwnershipReproContracts1 세 불일치 → OwnershipFocused1 통과. destroyed component의 late request도 확인. RA10 play 재사용 회귀까지 WholeTransition1 통과 |
| RA06 | 동일 모델을 재바인딩한 뒤 이전 행의 비교/tooltip이 새 장비 값을 노출. 명령은 SourceRevision을 검사하지만 표시 경로는 검사하지 않음 | 기존 IsCurrentEntry를 표시 경계에도 공통 사용. 새 세대와 이전 행의 metadata를 혼합하지 않음 | 수정 전 비교 허용/새 장비 sentinel 노출 실패 → 동일 회귀 통과 |
| RA07 | native save-state callback에서 Deinitialize 후 늦은 Write/Read/Flush가 settings를 재활성화하고 저장 횟수를 1→3으로 늘림 | public admission을 종료한 뒤 private final flush 한 번. EnsureLoaded는 콜백 후 availability를 반환하고 public 호출 중 self를 pin. 종료 후 읽기/쓰기/재시도 차단 | fake storage만으로 수정 전 두 불일치 확인 → 수정 후 통과. backup recovery callback 종료+GC 회귀는 수정 후 추가했으며 수정 전 crash 재현으로 설명하지 않음 |
| RA08 | 살아 있지만 source에서 분리된 이전 follower가 캐시에 남음. 새 follower가 생기거나 파괴돼도 이전 객체를 반환 | 기준 클래스의 세 중복 탐색/이름 heuristic/캐시 제거. 현재 source의 attached role을 기존 공통 resolver로 판별. bone 소비자는 mesh asset을 요구하고 quality 조회는 로드 전 role도 판별 | 수정 전 교체/없음 두 불일치 → 동일 회귀 통과. 실제 MCP source/body 연결 및 공개 subobject template 읽기 대조. 전체 체형/클립 품질의 근거는 아님 |
| RA09 | ModularMesh BeginPlay override는 Super 호출만 하고, 사용하지 않는 가상 메서드 주석도 남음 | 중복 override와 선언 주석 제거, 상속 lifecycle 사용 | 직접 UBT 성공. 구조 정리이며 별도 버그 수정이나 성능 향상으로 계산하지 않음 |
| RA10 | RA02/05에서 추가한 종료 admission gate가 정상 EndPlay 후 같은 actor의 다음 BeginPlay에서도 닫혀 있음. 이번 변경의 새 실패 경로다 | 새 play lifecycle의 BeginPlay에서 gate만 다시 열고 이전 전이/tag/cue retirement는 유지 | 첫 fixture는 월드 초기화 누락으로 EndPlay 자체가 실행되지 않아 제품 결함 근거에서 제외. 초기화/HasBegunPlay 확인 후 ReproContracts2에서 새 presentation/mount 요청 두 mismatch. 보완 후 WholeTransition1 통과 |
| RA11 | enabled 무기 지면 접촉의 NaN TraceLength를 native IsDataValid가 정상 수락. 런타임은 그 값을 trace endpoint에 직접 사용 | GroundContact 공통 finite/nonnegative/positive-length/channel 계약을 제작 validator와 runtime trace admission에서 사용. 변환한 endpoint도 finite 검사. 기본값 유지 | ReproContracts2 native validator mismatch. 런타임 trace crash를 수정 전 재현했다고 주장하지 않음. 보완 후 정상 defaults/잘못된 값 회귀와 WholeTransition1 통과 |
| RA12 | UI·원격·AuthoredCPU 검증 wrapper의 child handle/종료 코드 보유·기록이 불완전 | WaitForExit 전에 모든 handle을 보유하고 null 종료를 실패/미확인으로 취급. idle guard와 결과 기록 보완 | parser 0 error. 수정 wrapper의 실제 실행 결과는 후속 검증에 따로 기록 |
| RA13 | State Controller와 presentation helper가 같은 다섯 전이 상태를 별도로 판정. 실제 GT 호출 3곳 확인 | 기존 StateControllerRuntime::IsTransitionState 공통 사용, helper lambda 삭제 | TransitionEditorBuild1 정상 성공·WholeTransition1 통과. 별도 증상 수정·CPU 향상으로 계산하지 않음 |
| RA14 | Mass active step에서 실제 월드 종료가 재진입하면 Stop의 지연과 Deinitialize의 즉시 Entities/Processor 해제가 충돌 | admission 즉시 종료는 유지하고, entity/processor/archetype 반환을 step 완료 뒤 공통 Stop으로 이동. 사용하지 않는 scoring include/forward와 중복 시간 쓰기 제거 | 정상 engine-owned fixture의 Tick·Unregister 두 경로에서 4 mismatch 재현 → MassFinalBuild1 311.22초 정상 성공·MassFocused1 13 clean/0 warning/0 fail, exit0. 첫 잘못된 이중 종료 fixture는 제외. Game/Cook/전체 검증은 추가 변경본 기준으로 후속 진행 |
| RA15 | NPC scoring/action/activation의 이전 EndPlay admission이 새 play에서도 닫혀 있음 | 각 소유자의 BeginPlay에서 admission만 재개. 종료한 등록/능력 grant/action token/intent revision은 복원하지 않음 | 유효 새 AIController와 collaborator를 사용한 native 공개 API 세 불일치 재현. 보완 빌드/동일 회귀 진행 |
| RA16 | 일반 EnhancedInput 바인딩·GAS 입력 executor·modifier/router가 동일 actor의 새 play에서 동작하지 않음 | 각 component BeginPlay에서 admission만 재개. old device handles/held inputs/chord/history/monotonic transport revision retirement 유지 | device handle0→기대6, GAS InputPressed/modifier admission 등 4 mismatch 재현. 보완 빌드/동일 회귀 진행 |
| RA17 | 실제 combat draw montage와 rider를 가졌던 mount actor가 새 play에서 요청을 거절 | Intro component와 Mount actor BeginPlay admission 재개. montage revision·pending combat/tag·rider 종료 계약 유지 | 실제 같은 montage/공개 RequestMount 각1 mismatch 재현. 보완 빌드/동일 회귀 진행 |
| RA19 | Controller UI와 Quest가 새 play에서 projection/새 quest 수락을 재개하지 않음 | 기존 BeginPlay에서 admission을 Super 전에 재개. 이전 UI 자원은 재생성하며 기존 quest progress는 보존 | 실제 initialized LocalPlayer·engine viewport·native projection과 서로 다른 두 valid quest로 4 mismatch 재현. 보완 빌드/동일 회귀 진행 |

0을 허용한 기존 metadata/물리 값과 정상 이동 수치는 유지한다. 수명/ownership 문제에 clip·키 예외, 새 타이머, 강제 locomotion reset을 추가하지 않는다. 미니맵의 기존 0.2초 sampling과 기존 streamable 서비스를 사용한다.

## 현재 실행 근거

현재 후속 검증: RA01–11의 WholeFinal1은 344개(324 clean·20 warning), 실패/미실행/inProcess 0개, 667.32초, 정상 exit 0이다. 이후 RA13의 같은 전이 판정 복사본을 제거했고, TransitionEditorBuild1은 256.73초 정상 성공했다. 해당 최종 DLL의 WholeTransition1도 344개(324 clean·20 warning), 실패/미실행/inProcess 0개, 664.74초, 정상 exit 0이다. 새 RA 회귀 12개는 모두 clean이며 lifecycle fixture의 EndPlay 정리 경고도 사라졌다. 20개 warning 테스트의 이름과 개수는 이전 WholeRA04와 동일하다. 같은 개수는 모든 경고 메시지의 동일성이나 무해함을 증명하지 않는다. AutomationWarningComparison.json에 예시와 비교를 남겼다. 아래 과거 순서의 '진행 중' 표현은 해당 단계의 기록으로 읽는다.

RA12는 검증 도구의 공통 수명 보완이다. UI·원격·AuthoredCPU wrapper가 모든 child process의 native handle을 WaitForExit 전에 보유하고, null exit를 성공으로 취급하지 않으며 종료 결과를 기록한다. 이는 제품 플레이 버그의 재현으로 설명하지 않는다. 새 wrapper의 실제 패키지/UI/원격/CPU 실행은 별도 후속 근거다.

현재 읽기 전용 DataValidation commandlet은 정확한 두 native 타입과 하위 타입의 on-disk project 에셋만 검사했다. 실제 DA_Player_Profile 1개와 DA_Greatsword_Presentation 1개가 각각 0 error·0 warning, exit 0이다. 다른 에셋 전체의 IsDataValid나 모든 compiled search index를 검사한 결과는 아니다. 실제 final freeze는 650개 텍스트, 9개 Editor artifact이며 RA 기준점 이후 32개 텍스트 변경(생산 코드 27개, 새 테스트 2개, 검증 script 3개)의 해시·patch를 보관했다. 이 수치도 수동 review 비율이 아니다.

- 직접 UBT ReproBuild1 251.97초 성공 → ReproContracts1 2개 실패·경고 0개. 기대한 3개 실제 불일치를 원본 보고서에 남겼다.
- ContractsFinalBuild1 255.86초 성공 → ContractsFocused1 5개 clean·실패/미실행 0개. RA01/02, 기존 UI demand/dedicated 및 PresentationRecovery 범위다. 잘못 요청한 EffectsPool prefix는 일치 테스트가 없어 포함되지 않았으며, pooling 통과로 설명하지 않는다.
- DataReproBuild1 254.67초 성공 → DataReproContracts1 2개 실패·경고 0개. 미니맵 texture 잔류 및 제작 validator/진단의 NaN 누락이다.
- DataFinalBuild1은 251.59초 후 정상 실패했다. 첫 오류는 새 회귀의 double `RotationRate.Yaw`와 float 기대값 사이 C2666 overload 불일치였다. 기대값만 double로 교정했고, DataFinalBuildRepair1은 257.08초 성공했다. 엔진 경로·기존 코드·설치를 바꾸거나 실행 중인 빌드를 중단하지 않았다.
- DataFocused1은 이번 RA 회귀 5개 clean·실패/미실행 0개였다. 다른 prefix를 더한 요청은 보고서에 포함되지 않았으므로 다른 테스트의 근거로 계산하지 않는다.
- WholeRA04는 컴파일된 RA01–04 기준 337개(317 clean·20 warning)·실패/미실행 0개, 666.95초, 프로세스 exit 0이다. 실제 `ProjectJ.Integrated.PresentationRecovery`도 포함됐다. `NonNullRHI` flag의 EffectsPoolLifetime 등 렌더 필수 회귀는 NullRHI 전 범위 결과에도 포함되지 않는다. 별도 실제 RHI 실행이 필요하다.
- 다음 소유권 재현 4개를 추가해 OwnershipReproBuild1부터 원본을 보존했다. WholeRA04가 실행한 DLL에는 이 새 회귀들이 아직 포함되지 않는다. 빌드 중 소스 수정·다른 엔진/빌드 실행은 하지 않는다.
- OwnershipReproBuild1은 새 회귀에서 `GetPawn()`의 TObjectPtr/raw pointer TestEqual 비교 C2665로 255.21초 정상 실패했다. 비교식만 교정한 OwnershipReproBuildRepair1은 252.73초 성공했다. OwnershipReproContracts1은 5 clean·4 fail·경고/미실행 0개, 0.2355초였다. 새 네 경로의 총 9개 mismatch를 원본 index.json에 남겼다.
- OwnershipFinalBuild1 257.12초 성공 → OwnershipFocused1은 9 clean·실패/경고/미실행 0개, 0.1594초, exit 0. 이는 RA01–08의 명명된 native 계약이며 전체 시스템 무결성의 수치가 아니다.
- 새 retirement gate가 실제 레벨 재진입의 EndPlay(RemovedFromWorld)→DispatchBeginPlay에서도 재사용 가능한지 native lifecycle 회귀를 추가했고 PlayReuseReproBuild1을 진행 중이다. 현재 source 수정 전 상태를 재현하는 단계다.
- PlayReuseReproBuild1 248.92초 성공 후 첫 lifecycle fixture는 월드/actor play 초기화가 부족했다. 이 결과를 새 gate 결함의 증거로 계산하지 않았다. 실제 world play 초기화와 두 component HasBegunPlay를 확인하도록 교정했다.
- PlayReuseReproBuild2 254.02초 성공 → PlayReuseReproContracts2는 10 clean·2 fail·경고/미실행 0개, 0.2889초. 실제 BP construction의 AuthoredFollowerRole와 기존 9개 RA 계약은 통과했다. 실패는 play 재사용의 admission 두 값과 ground-contact NaN validator 한 값이다.
- RA10/11 보완과 RA06 표시 비교의 불필요한 재검사/중복 설명 분기를 정리한 ReuseGroundFinalBuild1을 진행 중이다. 최종 전체/실제 RHI/패키지 검증 전 상태다.
- ReuseGroundFinalBuild1 254.07초 성공 → ReauditFocused2는 12개(11 clean·1 warning), 실패/미실행 0개, 0.1922초, exit 0. 한 경고는 새 lifecycle fixture의 마지막 World.EndPlay 누락이며 제품 assertion 실패와 구분한다. EndPlay(Quit)→DestroyWorld 순서로 테스트 정리만 교정한 FinalEditorBuild1을 진행 중이다.
- 실제 RHI/1280×720 offscreen의 RHICosmetics1은 DistortionPrecacheLayout·EffectsPoolLifetime 2 clean, 실패/경고/미실행 0개, 0.2932초, exit 0이다. 이는 현재 제품 RA 소스의 실제 engine Niagara pooling 회귀이며 렌더 FPS/GPU·200 캐릭터 부하 수치와 구분한다.

## 이번 MCP 대조와 본문 장부

8개의 성공한 최소 MCP 조회에서 BP_GreatSword의 숨겨진 CharacterMesh0가 SKM_Quinn_Simple·ABP_Humanoid_Master, CharacterAnimProfile이 DA_GreatSword_AnimProfile에 연결됨을 확인했다. Body ABP는 기존 DA_RetargetBody_Greatsword를 사용하며 follower retarget 활성, LOD -1/IK LOD 0이었다. CDO의 greatsword 값 None을 follower 부재로 해석하지 않았다. 공개 SubobjectDataSubsystem 보충에서 Greatsword_GEN_VARIABLE의 GreatSword_Woman·ABP_Greatsword_Woman_RunTIme 연결을 읽었다. component template의 parent null은 인스턴스의 부모 연결 증거가 아니다. 에셋 저장/수정 없이 두 Editor 프로세스가 exit 0으로 종료했다.

이전 inventory-only 중 production/tooling cpp는 **141개**다. 초기 143 표기는 합계 오류여서 정정했다. 이 141개 구현 본문은 잘린 구간을 재조회해 읽었고 SourceReviewLedger.json에 해시와 명명된 추가 계약을 구분했다. 현재 장부는 607개 소스 파일(458 production/tooling·149 test/fixture)을 포함한다. 본문 읽기 141개, 이전 명명된 75개 계약, 나머지 목록/헤더/inline/test coverage를 합쳐 '607개 전수 정밀 검토 완료'라고 계산하지 않는다. 이 기록도 모든 실행 분기·BP 이벤트·배포 환경을 이해했다는 보증이 아니다.

## 검토 후 유지와 추가 후보

Core의 data-only bounded scoring/ParallelFor 취소 및 모듈 shutdown drain, GAS의 실제 속성 clamp/replication, FeatureCatalog의 dependency cycle와 등록 중복 검사, Strafe 상·하체 worker의 proxy snapshot/발 제약·중복 평가 시간 소유권, GuidedHandIK의 GT profile capture/worker math/bone-space 독립성, 제작 graph wrapper/임시 mount/검증/저장 경계를 읽었다. 검토한 경로와 유지한 계약을 후속 장부에 파일별로 기록한다. 이를 모든 함수·에셋 내부 이벤트의 무결성으로 확대하지 않는다.

Mount·settings·stale inventory·follower 후보는 native 재현 뒤 RA05–08로 수정했다. Ground contact 수치 계약도 RA11로 재현·보완했다. Coordinator의 RefreshSourceRequirement는 실제로 urgent와 wake를 합성하고 양쪽 반환을 보호하므로 최초 후보를 기각하고 유지했다.

남은 잠재 위험은 별도로 유지한다. Editor의 InstallLowerBodyInMasterForReview는 먼저 Upper 설치를 호출하므로 이후 Lower 검증/설치 실패의 원자성이 충분하지 않을 수 있다. 실제 호출은 opt-in Strafe trial과 공개 Editor Blueprint API이며, 현재 저장된 Master의 오류로 재현한 것은 아니다. Mass active step 중 Deinitialize의 storage 해제, search-cost 진단의 async index readiness/cardinality 전제도 실행 근거가 더 필요하다. OneShot 입력의 entry/cancel-angle 수치와 semantic reselect cooldown의 일부 raw 프로필 입력도 추가 제작 계약 후보다. 현재 두 제작 에셋의 검증 성공을 모든 raw 수치의 validator 완전성으로 해석하지 않는다.

## 아직 마무리되지 않은 범위

141개 추가 구현 본문과 명명된 header/inline 계약을 읽었지만 모든 607개 파일의 모든 함수·헤더·테스트·BP callback을 수동으로 정밀 검토한 것은 아니다. 현재 소스의 native 전체·Game/Cook·아래 패키지/원격 검증을 수행한 범위와 이 한계를 구분한다. 이전 CPU 소폭 증가와 packaged CPU trace 777003의 정확한 예외/원인은 미해결이다. live backend/DB·native Server target·200개 실접속, 모든 clip·체형·음향/UI 접근성 품질도 미검증이다. 다음 검증의 성공을 이런 범위의 완료로 해석하지 않는다.

## 현재 패키지·통합·실제 접속 검증

현재 RA01–13 기준 GameBuild2는 직접 UBT로 177.53초 정상 성공했다. Cook1은 Windows 기본 맵·Entry와 설정된 AssetManager/AlwaysCook 의존성의 1,361개 패키지를 Cook, 7개 플랫폼 제외, 오류 0·기존 GameplayCueNotifyPaths 경고 1개, exit 0이다. unused sample/plugin 전체 CookAll은 아니다. 새 Packaged1의 stage/package도 36.55초, exit 0이다. 이전 DS 패키지를 재사용한 결과가 아니다.

| 검증 | 현재 결과와 실제 범위 | 한계/원자료 |
| --- | --- | --- |
| CookedDamage1 | 실제 cooked 대검 공격 정의 11개·피해 GE 로드, 각 타격 전 HP100, 권위 hit·동일 swing 중복 피해 차단 통과. 1 warning test, fail/notRun 0, exit0 | 경고는 위 GameplayCueNotifyPaths와 같다. montage timing/원격 SSR 모든 조합과 구분. Reaudit_20261014/CookedDamage1 |
| Reaudit_Combat1 | 실제 입력/GAS/montage/notify/unequip 3회, cooked=1·success=1·exit0 | authored visuals를 쓰는 native fixture. 모든 장비/클립의 품질 검사 아님. EF_20260912/Reaudit_Combat1 |
| Reaudit_UI1 | 1280×720·1920×1080 각각 success/exit0·로그 error0. 1,000행·24버프·4창·10슬롯, 5Hz 동적 50단계·8변경/단계·10교체·50필터 변경·5창 토글 | 각 600 CSV 프레임 중 초기120 제외. UI GT 평균 0.5830/0.6051ms, p95 0.8137/0.8235ms. 동일 exe, FPS60 제한, 사전 변경본 비교 아님. 모든 hover/drag/focus/접근성 수동 품질은 미검증 |
| Reaudit_Render1 | 실제 RHI/D3D12·offscreen1280×720·FPS/VSync 제한0, 실제 BP/retarget 몸체 200, 모든 idle/attack 표본에서 rendered200. actual BP pose 캡처 두 장 검토, 정상 exit0 | 간단한 조명/카메라 장면·소켓 접속0, 근접 관절/의상/전체 클립 품질 보증 아님. WholeProjectAudit_20261013/Reaudit_Render1 |
| Reaudit_Network1–3 | 각 실제 localhost 소켓 클라이언트2+native NPC200, dedicated server mode/NullRHI, lag50ms/loss20%. 세 프로세스 모두exit0·timeout0. 각 3회 R→RMB 권위/소유자 예측/원격 R·목표·종료 관찰 통과 | 각 시작 ToolsetRegistry Python 오류45개를 별도 보존. fixture pass는 로그 전체 clean이 아니다. 200개의 실접속·독립 Server target·광역 네트워크/모든 SSR은 아님. GroupD_20260910/Reaudit_Network1–3 |

렌더 idle 표본은 평균15.2784ms(평균 frame 역수65.45FPS), GPU9.9444ms다. 공격/복귀 phase 전체는19.9216ms(50.20FPS), GPU10.1146ms지만, **실제 200개 montage가 동시에 재생된 172프레임만** 보면26.2178ms(38.14FPS), GPU10.3130ms다. 종료 후 230프레임은15.2132ms(65.73FPS)다. 한 번의 R burst/복귀이며 지속 공격 부하나 교대 렌더 개선 비교가 아니다. PNG 캡처·stall 포함, GPU 카운터 지연 가능성을 함께 남겼다. 통합 phase 평균으로 피크 공격 비용을 가리지 않는다.

RA12의 바뀐 UI/원격 wrapper도 이 실제 실행에서 native handle 보유·nonnull 종료코드 기록을 확인했다. AuthoredCPU wrapper는 교대 비교 실행에서 별도 확인한다. full source 650·Editor9개 artifact freeze와 이전 기준선 manifest가 서로 구분돼 있다.

## 200명 RA 교대 CPU 비교 — 2026-10-14

`Reaudit_ABBA_Long_A1/B1/B2/A2`는 네 실행 모두 정상 exit 0이며 child handle을 대기 전에 보유했다. A는 DS01–18 이후·RA 이전, B는 고정한 RA01–13이다. 전체 8개 Editor 모듈과 modules 파일을 함께 교체했고, 마지막에 B 전체를 복원했다. 실제 authored BP 200개, 조건별 240틱 준비/600틱 측정, 실행마다 2회 반복, 총 64행이다. 같은 현재 에셋과 호출 script를 사용했다. NullRHI World tick에 관측 비용을 포함하며 렌더 FPS/GPU나 200개 소켓 접속 수를 뜻하지 않는다.

| 조건 | ABA | A 평균 ms | B 평균 ms | B 변화 |
|---|---|---:|---:|---:|
| Near | off | 18.2474 | 19.3525 | +6.06% |
| Near | on | 12.0333 | 12.4856 | +3.76% |
| Far | off | 15.0008 | 15.5409 | +3.60% |
| Far | on | 11.0095 | 11.3033 | +2.67% |
| Hidden | off | 11.1694 | 12.1292 | +8.59% |
| Hidden | on | 6.2531 | 6.5342 | +4.50% |
| AttackBurst | off | 20.2404 | 20.7256 | +2.40% |
| AttackBurst | on | 11.5211 | 12.3479 | +7.18% |

증가가 남아 있으므로 성능 완료로 판정하지 않는다. 실행/반복별 분산도 보존했다. 예를 들어 Hidden/off의 B 반복은 10.8455–14.1812ms, A는 10.9003–11.4254ms이며 A1보다 A2가 높다. 이 결과만으로 증가 전부를 특정 코드에 귀속하거나 통계적 동등성을 선언할 수 없다. 별도 단계 trace로 원인을 좁힌다. 과거 pre-DS 비교를 이 RA 비교로 대체하지 않는다.

AttackBurst는 한 번의 R 공격 후 복귀다. 실제 montage 200개가 동시에 재생된 표본은 A/B·ABA 각각 316개이며 off는 21.0447→21.7504ms(+3.35%), on은 21.2348→23.2443ms(+9.46%)다. 이후 montage 0개인 2,084개 표본은 off 20.1185→20.5702ms, on 10.0482→10.6956ms다. 지속적인 200명 공격 비용과 혼동하지 않는다. 각 경계 틱의 실행 비용은 틱 뒤 montage 수로 분류한 한계가 있다.

원자료는 `Saved/Validation/AuthoredAnimation_20261005/Reaudit_ABBA_Long_*`의 cpu.json/process.json/comparison-provenance.json이며, 계산은 `Saved/Validation/Reaudit_20261014/CPUComparisonRows.csv`, CPUComparisonSummary.json, CPUAttackWindowSummary.json과 Analyze-BalancedCPU.py에 남겼다. 상세 결과/남은 문제의 조사도 계속 진행한다. 다음 후보는 Mass Tick/Unregister의 종료 지연 계약과 Deinitialize의 조기 저장소 해제가 충돌하는 경계이며, 아직 native 재현 전에는 잠재 위험으로 구분한다.

### 별도 단계 trace와 귀속의 한계

`Reaudit_Trace_A_trace/B_trace`도 모두 정상 exit 0, 16개 측정 구간(120틱 준비/120틱 측정·2회 반복)을 내보냈다. GT preupdate/update, worker update, snapshot 소비, MM/재생 적용, 선택 후 처리, 리타깃 캡처/계산, 시각 예산, rigid body를 exclusive thread CPU로 구분했다. 중첩 inclusive 합계와 worker CPU 합계를 World tick 벽시계로 합산하지 않는다. CPUStageTraceSummary.json과 양쪽 TraceExport의 CSV/Export.rsp/Insights.log가 원자료다. B 내보내기가 끝나기 전 analyzer를 한 번 실행해 파일 부재로 실패했고, 정상 내보내기 후 다시 실행해 16개 구간을 확인했다. 그 첫 실패는 제품 실행 실패나 부분 결과의 통과로 처리하지 않았다.

이 trace 한 쌍에서는 B의 전체 주요 단계 CPU 합계가 A보다 낮았고 World tick도 16구간 모두 낮았다. 예를 들어 snapshot 소비는 0.397696→0.336256초, worker update는 7.470681→6.568108초다. 그러나 이는 no-trace ABBA의 증가와 방향이 다르므로 증가 원인 해결의 근거가 아니다. ABA/on의 MM 관련 호출 수 또한 구간마다 조금 달라 실제 평가량도 확인해야 한다. 같은 조건의 반복, 시스템 외 변동과 ABA의 실행 예산 변동을 분리할 추가 조사가 남았다. RA13의 공통 판정 정리를 CPU 개선으로 설명하지 않는다.

원격 실행의 Python 오류도 구체적으로 분리했다. `EditorToolset/init_unreal.py`의 unreal.AgentSkill 부재와 `ToolsetRegistry/init_unreal.py`의 unreal.PythonTestRunner 부재가 실제 stack의 첫 오류다. 두 플러그인은 Editor 모듈이며 네트워크 fixture는 Editor-Cmd의 game/server 모드였다. 현재 public MCP의 정상 Editor 조회 성공과 해당 모드의 startup 실패는 서로 다른 근거다. 엔진 플러그인을 변경하거나 오류를 숨겨 전체 clean으로 만들지 않았다.

### Mass 종료 경계 후속 재현

RA13 고정 측정 이후 Mass의 실제 Tick/Demote와 Unregister/Promote에서 virtual SetActorHiddenInGame의 종료 재진입을 조사한다. Stop은 bStepping 동안 소유권 반환을 미루지만 Deinitialize는 Entities/Processor를 즉시 해제한다. 아직 정상 fixture의 native 실행을 완료하기 전에는 잠재 위험으로 유지한다. 첫 MassReproBuild1은 339.14초 정상 성공했으나 MassReproContracts1은 fixture의 직접 Deinitialize 이후 월드 collection의 이중 Deinitialize로 엔진 bInitialized assertion, exit3·report 없음이었다. 제품 crash 재현으로 계산하지 않는다. 첫 엔진 오류와 Application Event Log를 확인했으며 새 엔진 경로나 파일 삭제로 대응하지 않았다. 실제 EndPlay→DestroyWorld의 engine-owned 종료를 쓰는 fixture 교정만 MassReproBuild2에서 빌드한다.

후속 결과: MassReproBuild2는 309.61초 정상 성공했다. MassReproContracts2는 engine-owned 월드 종료로 두 경로의 저장소/processor 조기 해제 총 4개 불일치를 재현했다(1 failed test·경고/미실행 0, 0.09980초, 정상 실패 exit255). baseline에만 test-only 저장소 복구를 써 후속 null 접근을 피했으므로 제품 crash 재현으로 설명하지 않는다. RA14는 지연 Stop이 반환까지 소유하게 했고, MassFinalBuild1 311.22초 정상 성공 후 MassFocused1은 새 Mass와 기존 RA 회귀 13개 모두 clean·경고/실패/미실행 0, 0.34118초, exit0이다. 수정 후 test-only 복구는 실행되지 않았다. active worker는 기존 동기 join 이후에만 결과를 사용하며 새 task/timer/전역 locomotion reset을 추가하지 않았다. RA13 성능/패키지 결과를 RA14 검증으로 승격하지 않는다.

추가 공통 후보도 실제 호출 기준으로 조사한다. EndPlay에서 닫힌 admission을 같은 actor의 새 BeginPlay에서 열지 않는 경로가 NPC activation/action/scoring, 일반 입력/skill executor/router, 전투 전이, controller UI/quest에서 보인다. Mount actor는 rider를 가진 종료 경로에 한정해 같은 후보를 확인했다. 지금은 해당 공개 API와 기존 생명주기 회귀의 범위 차이를 대조하고 native 재현을 준비하는 단계다. 비슷한 필드 이름만으로 다른 소유권을 합치거나 전부 수정 완료로 표시하지 않는다.

LifecycleReproBuild1은 301.09초 정상 성공했다. LifecycleReproContracts1은 기존13 clean·새3 fail, 정상 실패 exit255였다. 실제 draw montage 재시작1, 입력 바인딩 재획득/handle수/GAS InputPressed/modifier4의 공개 동작 불일치를 확인했다. NPC fixture는 pawn EndPlay가 종료한 이전 AIController를 다시 사용해 OnActorBump handled ensure가 섞였다. NPC 세 경로의 깨끗한 근거로 계산하지 않고 새 controller로 교정한다. native input fixture의 정확한 no-profile 경고2도 explicit expected로 기록해 제품 무관 경고를 숨김 없이 구분한다. LifecycleReproBuild2는 이 fixture 교정과 mount actor/quest/실제 LocalPlayer UI 재사용 시험을 추가하며 해당 제품 gate는 아직 수정 전이다. 기존 부모/캐릭터 BeginPlay의 Super 호출 이후 component 초기화와 RefreshAbilitySystemDependentComponents·입력 Setup의 실제 순서도 대조했다.

LifecycleReproBuild2는 304.18초 정상 성공했으나 새 UI fixture가 Within=Engine인 LocalPlayer를 GameInstance outer에 직접 만들어 ensure를 냈고 초기화되지 않은 LayoutSettings에 접근했다(exit3·report 없음). 이 실행은 UI 제품 결함의 근거에서 제외한다. 원인은 최초 UObjectGlobals 오류와 native stack으로 확인했고 Application matching event는 없었다. GI.InitializeStandalone→GameViewportClient.Init→GI.CreateLocalPlayer의 실제 초기화로 교정했다.

LifecycleReproBuild3 285.46초 정상 성공 이후 LifecycleReproContracts3은 기존13 clean·새6 fail, 미실행0, 0.37721초, 정상 실패 exit255였다. NPC3·입력4·Intro1·Mount actor1·UI2·Quest2 총13개 공개 동작 불일치를 확인했다. 모든 실패의 non-assertion error는0이었다. UI의 최소 fixture에서는 초기 Character registry empty·pre-controller LocalPlayer user settings 경고2가 남아 각각 정확히 expected로 기록했다. 현재 LifecycleFinalBuild1은 이 실제10개 owner의 새 play admission만 보완한 변경을 빌드한다. 단발·이동 history, input sequence, ownership token과 callback revision을 강제 초기화하지 않는다. 폐기된 객체에 late request를 허용하는 변경도 아니다. 기능 단위 RA15/16/17/19와 에셋 정리 RA18을 구분한다.

### RA18 — 중복 빈 장비 정의 두 개 정리

실제 프로젝트 DataAsset 계열 native 제작 검사를 추가했다. 첫 ProjectDataAssetValidation1은 132개 개별 검사 로그, 130 valid·2 invalid, exit1이었다. DA_Player_EquipWeapon과 DA_Equip_Player의 무기 슬롯에 CombatStyleDefinition이 없으며 WeaponPresentationProfile 누락 경고도 있었다. MCP의 실제 속성에서 두 정의 모두 메시·소켓·능력·스타일·표시 프로필·아이템 ID·이름이 비어 있음을 확인했다. 공개 AssetTools 정적 참조자는 각각 `[]`, 의존성은 /Script/Project_JCharacter뿐이고 dirty=false였다. Source/Config/Docs의 경로 사용도 없었다. 현재 사용 중인 DA_Greatsword_Equip에 연결된 무기 구성과 구분했다.

이 두 빈 placeholder는 원본 파일·SHA256을 EmptyEquipmentAssetBackup과 EmptyEquipmentCleanupBefore.json에 보존한 뒤 공개 MCP delete로 정리했다. 임의 무기 스타일을 붙이거나 validator를 완화하지 않았다. 파일·MCP exists 모두 제거를 확인했다. 사용자 Body ABP/RTG/인계 문서 3개와 이전 수정 공격 DA10개의 SHA는 모두 동일했다. MCP 에디터는 정상 exit0이며 inspection 이후 정확히 두 개 삭제로 범위를 전환한 McpEquipmentMutationScope.json을 별도로 남겼다. 다른 에셋 저장은 요청하지 않았다. 정적 참조 검색은 외부 서비스의 임의 경로 문자열까지 증명하는 것은 아니지만 이 정의들은 identity·기능 데이터 자체가 없는 중복 빈 데이터다.

ProjectDataAssetValidation2는 남은 130개 개별 검사 로그가 모두 valid, invalid 로그0·asset error/warning0, 정상 exit0이었다. DataAssetValidationSummary.json은 실제 per-asset 관찰을 보존한다. native commandlet은 편집기의 Files Checked/Skipped/Unable summary API를 호출하지 않아 해당 합계는 unavailable로 남겼다. generic package/reference validator의 valid 결과는 클래스별 gameplay 숫자·모든 PoseSearch index/시각 품질의 완전한 검증이 아니다. 이 검사는 모든 UObject/Blueprint/DT/Chooser를 대상으로 한 것도 아니다. 현재 prototype quest의 native IsDataValid 부재와 runtime FindDefinition의 숫자/ID/중복 admission은 따로 기록한다.
