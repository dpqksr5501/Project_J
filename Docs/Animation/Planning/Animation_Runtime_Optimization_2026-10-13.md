# 200명 애니메이션 런타임 최적화 — 2026-10-13

> 2026-10-20: 이전 CPU/CMC 측정·이관 이력이다. 현재 Source에 없는 `ProjectJ.Animation.AuthoredCPU` 실행기는 퇴역했으며 현재 기능 회귀는 [GASP 기준](../Architecture/Player_Locomotion_Migration_2026-10-19.md#현재-회귀-실행)을 따른다. 아래 명령·수치는 당시 조건의 기록이다.

> 후속 리타깃 작업: 이 문서의 MM 캐시 변경은 `8485562c`에 커밋됐다. 그 위에 작성한 [골격·몸체 프로필과 검증](../Architecture/Runtime_Retarget_Profiles_2026-10-13.md)은 별도 변경이다. 후속 CPU 표본의 증가와 이번 MM 타이머 감소를 합쳐 전체 성능 개선으로 해석하지 않는다.

## 결과와 범위

현재 main `c05467868c524ef24ed9382a50db2960b6ca8724`에서 시작해 기존 MM 인덱스 캐시를 데이터베이스 적용 경로에도 재사용했다. 별도 200명 trace에서 `ProjectJ_AnimProxy_ApplyDatabase`의 동시 공격 ABA on 누적 CPU는 틱당 **0.267075→0.085192ms**로 약 **68.1% 감소**했다. 이는 해당 경로의 worker CPU 감소이며 전체 프레임 개선율이 아니다.

동일한 v2 fixture의 비계측 5회 비교에서 동시 공격 ABA on World tick 평균은 20.417→20.087ms였다. 차이 0.330ms는 변경 전 반복 평균의 표준편차 0.980ms보다 작다. 근거리 ABA off 최대값은 29.617→31.073ms로 증가했다. **전체 CPU 개선이나 p95/최대 회귀 부재는 입증하지 않았다.** 이전 인계의 +8.3% 회귀 원인도 이번 변경으로 확정하거나 해결 완료로 처리하지 않는다.

조사 기준은 [인계](Animation_Optimization_Handoff_2026-10-13.md), [파이프라인](../Architecture/Animation_Pipeline_Refactor_2026-10-13.md), [시각 예산](Crowd_Visual_Presentation_Budget_2026-10-12.md), [시각 메시 소유권](../Architecture/Visual_Presentation_Mesh_Ownership.md), [착지 복귀](Strafe_Land_Return_2026-10-13.md), [원격 단발](../Locomotion/RemoteOneShotReplication.md), 성능 기반 및 실행·스레딩 감사 문서와 현재 소스다. UE 5.8.3, CL 58210709의 로컬 엔진 소스도 확인했다.

## 실행 구조와 실제 제거한 작업

`FProject_JCharacterAnimInstanceProxy`에는 이미 generated MM 노드 인덱스를 클래스별로 보관하는 `GetGeneratedMotionMatchingNodeIndices()`가 있었다. 검색 정책·재선택·완료 관측은 이 캐시를 사용하지만 `ApplySelectedDatabaseToNativeNode()`의 적용/해제 두 경로는 모든 AnimNode property를 매번 타입 검사했다.

두 루프를 기존 인덱스 캐시 순회로 바꿨다. 같은 후보 배열, primary/companion 비교, interrupt mode, 적용 기록과 해제 동작을 유지한다. 클래스 초기화·변경 시 인덱스, 적용 기록, throttle, 재선택 acknowledgement를 무효화하는 기존 소유자가 계속 책임진다. 원시 노드 포인터 캐시나 새 상태 소유자는 추가하지 않았다.

이번 fixture에서 실제 Master 인스턴스의 AnimNode property는 87개, MM 노드는 1개였다. 안정된 Master의 데이터베이스 적용 루프는 **87개 타입 조회→1개 조회**가 된다. MM이 없는 linked graph는 캐시를 만든 후 빈 목록을 순회한다. 클래스별 최초 탐색은 남는다. 87은 컴파일된 런타임 property 수로, 과거 문서의 에디터 그래프 노드 수 131과 다른 지표다. 포즈 그래프 노드를 삭제한 것이 아니다.

| 실행 경계 | 유지한 소유자와 계약 |
| --- | --- |
| GT 수집 | Actor/CMC/GAS/복제 이벤트를 기존 스냅샷으로 게시 |
| 의미 상태·단발 수명 | native State Controller와 기존 presentation runtime |
| 검색 경계 | 최신 스냅샷 소비, 요청 병합, 실제 검색 완료 acknowledgement |
| 재생·평가 | Unified Locomotion 및 엔진 update/evaluation/task 경로 |
| 원본 예산 | 기존 ABA subsystem/mesh와 gameplay pose 요구 |
| 시각 예산 | 기존 Coordinator의 평가 간격·보간·wake·설정 복구 |
| GC | queued/consumed snapshot, database/companion과 기존 참조 추적 |

검색 간격·continuing bias·후보·몽타주·단발 종료 수치 0.35초/100cm/3도/85%를 유지했다. 새 UObject/tick/작업 큐/생산 CVar, Actor/CMC/GAS 거리 throttle, 네트워크 payload 변경은 없다. Content/Config 파일을 수정하거나 에셋·레벨을 저장하지 않았다. 기존 사용자 문서 변경도 보존했다.

## 측정 도구 보완

`Project_JAuthoredAnimationPerformance.cpp`의 결과에 `fixture_version=2`를 기록한다. World tick 타이머의 범위와 기본 120 준비+120 측정 tick은 유지하고 다음 항목을 추가했다.

- `world_tick_samples_ms`: 정렬 전 tick 순서의 원자료. p50/p95/최대는 별도 정렬 복사본으로 계산한다.
- `observer_samples_ms`, `mean_observer_ms`: World tick 뒤의 기존 graph/pose 관측 시간. Proxy 접근으로 실제 대기가 생기는지도 이 범위에서 조사할 수 있다.
- `attack_dispatch_and_validation_ms`: 첫 측정 tick 전에 실행하는 공격 요청 루프, fixture 확인과 AbilityTask 열거의 시간. 순수 GAS 활성화만의 시간으로 해석하지 않는다.
- `attack_montage_actor_counts_after_tick`: tick 종료 뒤 몽타주가 재생 중인 캐릭터 수. 타격 판정 구간이나 공격만의 몽타주 종류를 인증하는 값은 아니다.
- 별도 `ProjectJ.AuthoredAttackDispatch_*` region과 observation timer. CPU region은 마지막 관측 뒤 종료해 보고서 직렬화/저장과 구분한다.
- 실제 메시의 graph property/MM 노드 수 및 기존 장비·RigidBody 구성.

공격은 첫 측정 tick에서 한 번 동시 요청한다. 200명 기본 구간에서는 전후 모두 **120tick 중 79tick**에 200명 모두 몽타주를 재생했다. 측정 길이를 늘려도 공격은 반복되지 않는다. 긴 구간 평균을 지속 전투 평균으로 해석하지 않는다.

스크립트 변경:

| 도구 | 변경 |
| --- | --- |
| `Measure-AuthoredAnimation.ps1` | `-WarmupTicks`, `-MeasuredTicks` 각각 1~3600, 기본 120. C++ case deadline도 길이에 맞춰 확장 |
| 같은 도구 | v2·leaf Success/errors·준비/측정 길이·각 raw sample 배열 길이 검사 |
| 같은 도구 | 프로젝트/Editor DLL/보호 에셋 해시, 길이·Hz·trace 여부를 manifest에 기록 |
| 같은 도구 | 선택적 `-NoDDCCleanup`, manifest에 활성 여부 기록. 기본 off |
| `Export-DTrace.ps1` | 공격 시작 region 통계, 선택적 `-AnimationEvents`와 실제 thread 이벤트, CPU region별 파일 수 검증 |
| Proxy | consume/database/search policy/completion/observation 세부 CPU scope |

기존 v1 결과와 v2 결과는 분리한다. 관측에 몽타주 수 확인이 추가되고 trace region 끝도 명시됐으므로, runtime 변경 효과의 비교는 같은 v2끼리 수행했다. trace 수치를 비계측 수치에 합치지 않는다.

각 프로세스 로그에 DDC maintenance가 함께 실행된 기록이 있다. 그것이 편차의 원인이라는 인과 관계는 확인하지 않았다. 로컬 엔진 `FileSystemCacheStore.cpp`의 `NoDDCCleanup` 지원을 확인해 선택 옵션을 제공했다. 이번 전후 200명 비교는 모두 기존 기본 조건으로 실행했다. 향후 이 옵션을 사용하면 양쪽에 동일하게 적용하고 새 baseline을 만든다. DDC 파일을 삭제하지 않았다.

## 200명 결과

장비 follower 1,800개, Near/Far/Hidden/AttackBurst × ABA off/on, ABA 예산 2ms, 조건별 120 준비+120 측정 tick, 5회 반복이다. 실제 authored BP·장비·ABP를 사용한 NullRHI World tick wall time이다. CMC tick은 꺼진 controlled kinematics이며 전체 게임 프레임·GPU·200명 렌더 FPS를 측정하지 않는다.

### 같은 v2 fixture의 비계측 전후

p95 열은 반복별 p95의 평균이다. 최대는 5회 중 최대다. 별도의 pooled p95·반복별 평균·표준편차·counter 원자료는 `CPUComparison.json`에 남긴다.

| 조건 | ABA | 평균 이전→이후 ms | p95 이전→이후 ms | 최대 이전→이후 ms |
| --- | --- | --- | --- | --- |
| Near | off | 19.097→18.536 | 21.418→19.593 | 29.617→31.073 |
| Near | on | 12.374→12.148 | 13.171→12.941 | 16.747→16.498 |
| Far | off | 15.698→14.907 | 17.227→15.864 | 22.132→18.092 |
| Far | on | 11.149→10.871 | 12.464→11.761 | 16.299→13.776 |
| Hidden | off | 11.115→11.100 | 12.115→12.226 | 16.280→13.586 |
| Hidden | on | 6.267→6.221 | 6.614→6.468 | 7.746→7.259 |
| AttackBurst | off | 20.457→19.863 | 22.899→21.955 | 28.567→24.411 |
| AttackBurst | on | 20.417→20.087 | 23.506→23.162 | 30.175→29.852 |

평균 차이를 모두 이번 변경에 귀속하지 않는다. 특히 AttackBurst ABA on의 전후 반복 평균 표준편차는 0.980/0.225ms로, 독립 프로세스 사이의 환경 변동을 제거하지 못했다.

Near off의 visual graph update 관측 23,800, AttackBurst off/on의 23,800, combat protected actor-tick 15,800, 전원 montage 재생 79tick은 유지됐다. ABA 적응 제어로 일부 bone finalization과 Near on 관측 수에는 차이가 있으므로 모든 조건의 실제 평가 횟수가 완전히 같았다고 표현하지 않는다.

### 별도 trace로 확인한 국소 비용

같은 v2 fixture의 전후 1회씩 trace다. 아래는 전체 실행 thread의 누적 CPU/120tick이며 wall time과 구분한다.

| `ProjectJ_AnimProxy_ApplyDatabase` | 이전 CPU ms/tick | 이후 CPU ms/tick | 호출 수 이전→이후 |
| --- | --- | --- | --- |
| Near ABA off | 0.370 | 0.099 | 24,000→24,000 |
| Near ABA on | 0.109 | 0.031 | 5,635→5,635 |
| AttackBurst ABA off | 0.307 | 0.093 | 37,600→37,600 |
| AttackBurst ABA on | 0.267075 | 0.085192 | 34,844→34,792 |

동시 공격 ABA on의 호출 수 차이는 약 0.15%이며 호출당 CPU도 약 0.920→0.294μs로 줄었다. thread export에서 worker 실행을 확인했다. 리타깃 평가 24,000회, RigidBody 평가 72,000회는 같아 해당 품질/작업량을 줄여 얻은 국소 개선으로 해석하지 않는다.

같은 trace의 AttackBurst ABA on World tick은 20.667→21.971ms로 비계측 비교와 방향이 다르다. 계측 오버헤드와 환경 변동이 있는 단일 결과를 전체 성능의 증명으로 사용하지 않는다. GT observer는 약 0.223→0.247ms로 이번 실행에서 큰 관측 대기를 보여 주지 않았다. getter 변경이나 zero-copy는 추가하지 않았다.

## 검증과 증거

| 검증 | 결과 |
| --- | --- |
| 직접 UBT Editor | diagnostic 41.58초, 최종 14.55초 성공 |
| 직접 UBT Game Development | 최종 30.28초 성공 |
| Regression | 33 leaf 성공, errors 0, notRun/inProcess 0. 기존 전투 fixture의 MissingPalm 경고는 5 leaf 각 4개, 합계 20개 보존 |
| generated MM lifecycle | 실제 Master 클래스의 primary/companion 설정, 안정 재적용, 비활성화 해제, 재활성화, 클래스 교체 시 cache/ack 정리와 재초기화 확인 |
| D3D12 RecordedStrafeLand | 성공, errors/warnings 0. jump 11, Land return 11, coherent reselection 6 |
| D3D12 VisualBudget lifecycle | 성공, errors/warnings 0. 120tick당 Near update 120, Far update 30, 보간으로 bone이 진행한 skip frame 90 |
| 착지 기록 동등성 | 기존 FinalRendered의 Frame/Unified/Chest/Torso/CurveReturn/CurveSummary 3,816행과 순서까지 동일 |
| 렌더 자료 | PNG 149개. Frame 825/4410 확인. 정지 이미지는 전체 영상·world 발 접촉·사용자 체감의 대체가 아님 |
| 실제 소켓 | server+client 2+native NPC 200, 50ms/3% 손실, parallel net 설정. 성공, exit 0/0/0, timeout 0 |
| 측정 길이 smoke | 1명, 60 준비+180 측정 tick, 8조건 완료. v2/배열 길이/leaf Success 검사와 `-NoDDCCleanup` 전달 성공 |
| 보호 에셋 | Master·전방 PSS/PSD·플레이어 프로필 SHA256 모두 시작과 동일 |

실제 소켓은 200개 접속이나 200명 authored player 시험이 아니다. 기존 Toolsets/ToolsetRegistry의 `unreal.AgentSkill`/`PythonTestRunner` 시작 오류도 verification에 보존하며 로그 전체를 오류 없음이라고 표현하지 않는다.

로컬 증거는 `Saved/Validation/AnimationOptimization_20261013/`다. 주요 파일은 `CPUComparison.json`, `TraceComparison.json`, `PlaybackEquivalence.json`, `Regression/Automation/index.json`, `RenderedLand/Automation/index.json`, 직접 UBT 로그다. 기존 DLL도 `OriginalBinary`와 `DiagnosticBinary`에 보존했다.

보완 측정 예시(기본 비교와 별도로 새 이름 사용):

```powershell
./Scripts/Validation/Measure-AuthoredAnimation.ps1 -RunName <고유영문이름> -Counts @(200) -Repeats 5 -WarmupTicks 120 -MeasuredTicks 120 -NoDDCCleanup
./Scripts/Validation/Export-DTrace.ps1 -RunRoot <별도_trace_Count200_경로> -Animation -AnimationEvents
```

trace는 앞 실행 완료 후 별도 `-Trace -Repeats 1` 실행에서 확보한다. 빌드와 시험을 겹치지 않고, 직접 UBT가 실행 중이면 정상 완료까지 기다린다.

CPU 원본은 `Saved/Validation/AuthoredAnimation_20261005/` 아래 실행들이다. v2 manifest에는 binary/asset hash와 변경 소스 snapshot이 있다. 기존 v1의 추가 해시는 baseline의 `ArtifactHashes.json`에 별도 보존했다.

- `OptimizationBaseline_20261013_02`: 기존 v1, 200명 5회, 40행. fixture leaf 성공·errors/warnings 0.
- `OptimizationTraceBefore_20261013_01`: 기존 v1 trace.
- `OptimizationDiagnosticTrace_20261013_01`: v2·runtime 변경 전 세부 trace, thread export.
- `OptimizationComparableBefore_20261013_01`: v2·runtime 변경 전 5회.
- `OptimizationComparableAfter_20261013_01`: v2·runtime 변경 후 5회.
- `OptimizationTraceAfter_20261013_01`: v2·runtime 변경 후 trace, thread export.
- `OptimizationWindowSmoke_20261013_01`: 비기본 window/선택 DDC flag의 1명 확인. 200명 성능 비교에는 사용하지 않음.

네트워크 원본은 `Saved/Validation/GroupD_20260910/AnimationOptimization_20261013_Lag50Loss3/verification.json`이다. 최초 제한 환경의 baseline 시도 `OptimizationBaseline_20261013_01`은 CIM 권한으로 중단됐으며 Unreal 프로세스 시작 전이었다. 실패 자료/디렉터리도 보존했다. Saved는 Git ignore이므로 다른 clone에 있다고 가정하지 않는다.

## 다음 측정과 설계 판단

국소적인 전체 graph 타입 탐색을 제거한 단계이며 200명 전체 최적화 완료 인증은 아니다. 다음은 DDC·백그라운드 부하를 맞춘 독립 프로세스의 교차 순서 비교, Near 최대값 발생 tick/critical path, 공격 시작·공격 중·복귀의 구분이다. 단발 fixture를 길게 늘리는 것만으로 지속 전투가 되지 않으므로 별도 workload에서 공격 밀도와 재생 구간을 기록한다.

후단에서는 원본 gameplay pose/무기 socket/접촉 의존성을 유지하고 실제 리타깃/물리 critical path를 조사한다. 기존 cache가 충분한 곳에 새 소유자를 늘리지 않고, 구성 정보는 변경 시 무효화하며 동적 보호/visibility는 현재 값에서 판단한다. 캐릭터 도착·wake 집중·장비 교체·GC·montage cancel도 품질 게이트로 유지한다.

200명 전체 실제 RHI CPU/GPU/메모리, Cook/packaging, 최소 사양/목표 FPS, 최종 사용자 체감은 이번에 새로 인증하지 않았다. 기존 MM 복귀나 몸체/의상 접촉 보호를 줄이는 변경은 별도 의존성 확인과 품질 시험이 필요하다.
