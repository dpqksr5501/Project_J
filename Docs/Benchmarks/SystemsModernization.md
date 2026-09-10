# A–E 성능 측정과 검증 근거

[저장소 소개](../../README.md) · [CSV/JSON](Data) · [재계산 스크립트](recompute.py)

README의 수치는 2026-09-10에 수집한 서로 다른 실험의 결과다. 이 문서는 측정 구간, 비교 조건, 표본 수와 적용 한계를 함께 기록한다. 데이터는 보존된 실행 결과에서 복사했으며 문서 작성 과정에서 새 Unreal 실행이나 성능 측정을 하지 않았다.

## 환경과 코드 기준

| 항목 | 기준 |
|---|---|
| Engine / OS | Unreal Engine 5.8 / Windows 11 |
| CPU | AMD Ryzen 7 9800X3D, 8 cores / 16 threads |
| GPU 실험 | AMD Radeon RX 9070 XT / D3D12 / Adrenalin 26.8.1 |
| 빌드 | Win64 Development Editor 또는 Development Game, 실험별 구분 |
| 통계 | p95 = 정렬된 표본의 `ceil(N × 0.95)`번째 값(nearest rank) |
| 통합 코드 | `f2c31fb266740ab337c025a91ecfd884bb8eb49d` |
| main 회귀 에셋 | `89f94b4`, 이후 BP 이벤트 그래프 삭제 전 상태 |

과거 각 run은 별도 manifest/source snapshot으로 식별된다. 모든 실험이 동일 최종 binary로 수행된 것은 아니다. PSO A/B는 아래 명시한 동일 executable hash를 사용했다. 전체 `.utrace`, cooked 패키지, crash dump와 로그는 큰 로컬 검증 자료이며 이 데이터 묶음에 포함하지 않는다. 공개 데이터만으로 가능한 것은 **표본 재집계와 보고된 결과 점검**이며, 독립 재실행은 아래 스크립트와 해당 코드·에셋·환경이 필요하다.

[provenance.json](Data/provenance.json)은 공개 파일의 SHA256와 원래 run 경로를 기록한다. 공개 checksum은 UTF-8 BOM 제거·줄바꿈 LF 정규화 후 계산하므로 Windows/Linux checkout 모두에서 검증할 수 있다. 원본 파일 byte hash도 별도로 보존한다. 원래 `Saved/...` 경로는 로컬 아카이브의 식별자이며 GitHub 다운로드 링크가 아니다.

## 1. Mass: 이동과 간격 계산

`CrowdE_20260910/RegressionFinal02`의 `mass-crowd.csv`. manager, 값 snapshot, grid, 간격 계산, join을 포함한 CPU 구간이다. 조건별 warm-up10회 후120표본이며 warm-up은 CSV에 포함하지 않는다. 동일 입력의 최종 위치는 1e-8 허용치로 비교했다.

| Moving agents | 직렬 p50 / p95 (ms) | 병렬 p50 / p95 (ms) |
|---:|---:|---:|
| 100 | 0.011899 / 0.016898 | 0.012100 / 0.013500 |
| 512 | 0.111703 / 0.118598 | 0.071898 / 0.111297 |
| 1,024 | 0.261199 / 0.272799 | 0.103600 / 0.146002 |
| 2,048 | 0.603899 / 0.634197 | 0.190999 / 0.240900 |

2,048개에서 p95 감소율은 `(0.634197 − 0.240900) / 0.634197 = 62.0%`. 총960표본이며 반복 측정은 같은 실행 환경 안에서 이루어졌다. 독립 실행 간 신뢰구간을 제공하는 실험은 아니다.

**판단:** 실제 이동1,024개 이상 + 간격 계산 활성 시 자동 병렬화한다. 이전 512개 표본에서 병렬 p95가 더 높았던 관측과, 간격 계산 없는 작은 단순 이동에서는 직렬이 우세했던 결과를 함께 반영했다. 현재 표의 모든 행에서 병렬이 우세하다고 임계값을 무조건 낮추지 않았다. 이는 Character 전체 tick, 서버 tick 또는 동시 접속자2,048명의 성능이 아니다. Mass는 Character/ASC/장비를 유지한다.

근거: [raw samples](Data/mass-crowd.csv) · [Mass tests](../../Source/Project_JCharacter/Private/Tests/Project_JMassTests.cpp)

## 2. Niagara: 생성 비용과 풀 수명

`CrowdE_20260910/RenderedFinal01`, 실제 RHI에서 같은 Trail 시스템100개를 동시에 생성·종료하는12 cycles. 첫 cycle을 제외한 각11표본으로 생성 CPU를 비교한다.

| 항목 | Unpooled | Pooled |
|---|---:|---:|
| 생성 CPU 중앙값 | 3.529999 ms | 2.573200 ms |
| 생성 CPU p95 | 4.573900 ms | 3.345102 ms |
| 12 cycles 누적 고유 component 수 | 1,200 | 848 |

생성 p95 **26.9% 감소**, 고유 component 수 **29.3% 감소**. 원본 pool 상한32 때문에 매회100개가 모두 재사용되지는 않는다. 종료 비용은 약0.6ms로 큰 변화가 없었다. 실행 순서가 고정된 한 프로세스 비교이고 p95가11표본의 최댓값이므로 꼬리 지연의 확정적인 일반화에는 추가 반복이 필요하다. cold load, GPU 렌더 시간, 전체 FPS의 개선율로 사용하지 않는다.

oneshot은 엔진 `AutoRelease`, 프로젝트가 참조하는 loop는 `ManualRelease`로 수명을 관리한다. StopCue/refresh/EndAttack/graceful stop/world teardown을 검사했다.

근거: [cycle samples](Data/effects-pool.csv) · [VFX 수명 시험](../../Source/Project_JCharacter/Private/Tests/Project_JCrowdEffectsTests.cpp)

## 3. Network: 관련성 정책과 전송률

`GroupD_20260910/NetworkAllRelevant02`, `NetworkSpatial02`, `NetworkParallel02`. 로컬 dedicated server + 별도 socket clients2, native NPC100, 30FPS cap. 두 군집 사이에서 관찰자 교환/재진입, NPC 삭제, 장비·인벤토리 변경, 접속 종료의5단계를 실행했다. 각 모드의 최종 실행은1회다.

| 모드 | 5단계 전송 bytes | 관측 초 | Aggregate B/s |
|---|---:|---:|---:|
| AllRelevant 대조군 | 283,839 | 15.109 | 18,786.09 |
| Spatial / 직렬 Net Tick | 199,180 | 15.063 | 13,223.13 |
| Spatial / 병렬 Net Tick | 203,591 | 15.062 | 13,516.86 |

각 단계의 rate를 단순 평균하지 않고 **총 bytes / 총 seconds**로 계산했다. Spatial의 감소율은29.6%다. fixture RPC, 초기 복제 및 재입장 비용도 포함한다. 운영 이전/이후 전체 트래픽 비교나 실제 사용자100명의 입력 부하는 아니다.

병렬 Net Tick은 worker connection tick454회와 겹치는 실행 pair451개로 실제 병렬 실행을 확인했다. 그러나 단계별 join p95는 직렬0.1409–0.2265ms, 병렬0.1140–0.2401ms로 개선이 일관되지 않았다. 기본 병렬 Net Tick은 켜지 않았다.

별도 `Crowd4Clients01`은 **서버1 + 실제 clients4 / NPC512**의 AOI 교환·재입장, owner-only inventory, 공개 장비, 삭제·접속 종료 기능 검증이다. NPC 배치가 달라 위2client 성능 비교와 합치지 않는다. 엔진 Python 초기화 로그 오류와 fixture 성공/exit0를 구분했다.

근거: [단계별 집계](Data/network.csv) · [네트워크 fixture](../../Source/Project_J/Testing/Project_JNetworkLoadFixture.cpp)

## 4. PSO: 같은 binary에서 누락 해결

`GroupE_20260910/PSOBaseline01`, `PSOAfter01`. 동일 packaged executable SHA256:

`6FDB0989C6C1A60C805B644B8C0D032F61224BAD9BF2E09F8914BE9C0A95AB36`

| 조건 | Full Precached | Full Missed | Full TooLate |
|---|---:|---:|---:|
| 보완 OFF | 6,620 | 1 | 0 |
| 보완 ON | 6,621 | 0 | 0 |

UE5.8 rough-refraction의 기존2 render target PSO에 빠진 variance/coverage target을 보완했다. 기존 shader/state를 재사용하고 엔진 설치 파일·material·화질은 바꾸지 않았다. 이후 validation-only lookup과 modular registration 제약을 보완한 최종 `PSOFinal01`도 miss0을 확인했다.

최종 Game executable은 `FE345F4F535ED063B530A0B1DE6D3BFDECAE581708471FEB86BDA6BC1DB084B2`. 이 binary의 스킬 통합 시험은 조명을 포함하므로 **별도 DefaultLightFunctionMaterial / TranslucentLightingMaterialPSOCollector miss1이 남는다**. Trail 보완을 프로젝트 전체 PSO miss0으로 표현하지 않는다. modular Editor에는 보완 collector를 등록하지 않는다.

각 A/B는 새 application user directory이며 OS/driver cache는 보존했다. 60fps cap / 각1회로 driver cold나 hitch 제거를 증명한 실험이 아니다. 최초 cooked 스킬3회 반복은 신규/재사용 application cache 모두 통과했지만, visual/equipment 동기 로딩37.4466/39.5124ms 및 setup 최대 delta44.2607/48.4986ms가 있어 전체 첫 실행이16.7ms 안에 끝났다고 할 수 없다.

근거: [A/B 집계](Data/pso-comparison.json) · [보완 코드](../../Source/Project_J/Rendering/Project_JDistortionPSOPrecache.cpp)

## 5. 컬링과 갱신 분산: 비용을 줄이는 위치

`GroupE_20260910/CullBaseline02`, `CullAfter03`: Editor-game, offscreen 실제 RHI1280×720. near20/far80으로 같은100개를 배치하고 distance2500cm + component bounds±350cm를 임시 적용했다.

- 첫 spawn allocation은 양쪽100개. **다음 world tick부터 활성 simulation100→20**, 이후5프레임 CSV assertion 통과.
- 실제 근접20개 Trail 표시를 캡처로 확인. 에셋의 운영 거리/bounds/instance cap은 저장하지 않았다.
- 전체 trace의 `NiagaraComponent` GT inclusive 합계17.9274→12.8617ms.
- `NiagaraGpuComputeDispatch` GPU0-Graphics0 합계12.2496→0.2320ms.
- `Niagara GPU Ribbons` 동일 GPU queue 합계22.1801→22.3281ms로 렌더 비용은 개선되지 않았다.

초기화/단일효과 단계가 섞인 inclusive 누적 시간이다. 부모·자식 scope를 더해 프레임 비용으로 만들지 않는다. 같은 이름의 RHIThread scope도 GPU 시간에 포함하지 않는다. 단회·고정 순서·distance와 bounds 동시 변경이므로 각 요인의 독립 효과나 전체 FPS 향상으로 해석하지 않는다. Graphics queue의 compute 실행이며 독립 async-compute queue overlap 증거도 아니다.

별도 선택 갱신 분산 시험은 **512 owners의 최초 반응512회 유지**, 이후 최대86회/프레임으로 GT Chooser/database 선택을 분산했다. 최초 프레임을 제외한 이후120프레임은 총10,240회(최초 포함121프레임10,752회)로, 총 평가량 감소나 전체 animation worker 제어를 의미하지 않는다.

근거: [활성 개수](Data/culling-active.json) · [thread별 timer](Data/culling-timers.json) · [갱신 분포](Data/animation-phases.csv)

## 6. main 통합 회귀

| 실행 | 결과 |
|---|---|
| `MergeMain_20260910/Regression01` | 56개 성공(일반50 / 경고 포함6), errors0 / warnings10 |
| `MergeMain_20260910/Rendered01` | 24개 성공, errors0 / warnings0 |

집합 간 중복이 있으므로80개 고유 시험으로 합산하지 않는다. [시험별 결과](Data/main-regression.json)와 [렌더링 결과](Data/main-rendered.json)를 포함한다. 이 결과는 main의 코드·전투 에셋 병합 직후이며 **이후 캐릭터 BP 이벤트 그래프 삭제까지 검증한 결과는 아니다**.

실패를 통해 수정한 내용은 cooked native Motion Matching NodeData 초기화, PSO validation-only lookup와 Editor DLL 등록 제약, 경로 cooldown 중 stale InRange, 종료 전에 검사하던 fixture 대기 조건 등이다. 과거 cooked explicit filetrace의 비정상 종료는 no-effect control에서도 재현됐고 내부 원인은 미해결이다. 최종 cooked 검증은 explicit filetrace 없이 정상 종료했으며 Editor trace와 구분한다.

## 재현

공개 데이터의 통계와 checksum은 Python3 표준 라이브러리만으로 확인할 수 있다. 엔진을 실행하거나 에셋을 변경하지 않는다.

```powershell
python Docs/Benchmarks/recompute.py
```

새 엔진 실험은 먼저 [빌드 안내](../../README.md#빌드와-실행)를 따라 빌드하고 기존 엔진/컴파일 프로세스가 끝난 뒤 하나씩 실행한다. 아래 예시의 RunName은 매번 새 이름을 사용한다.

```powershell
# Mass 비교가 포함된 다인원 시험
./Scripts/Validation/Measure-CrowdE.ps1 -RunName ReviewCrowd01 -Filters 'ProjectJ.Crowd.+ProjectJ.GroupD.'

# 실제 RHI의 PSO 레이아웃·Niagara pool 수명
./Scripts/Validation/Measure-CrowdE.ps1 -RunName ReviewEffects01 -Filters 'ProjectJ.GroupE.' -Rendered

# 서로 다른 실행으로 비교. 각 명령의 정상 종료 후 다음 실행
./Scripts/Validation/Measure-Network.ps1 -RunName ReviewAllRelevant01 -AllRelevant
./Scripts/Validation/Measure-Network.ps1 -RunName ReviewSpatial01
```

`Measure-CrowdE`의 Mass 수치와 `Measure-Mass`의 간격 계산 없는 초기 단순 커널은 서로 다른 workload다. GroupB 시험은 스크립트가0.1ms의 실험용 animation budget을 설정하며 운영 기본값이 아니다. packaged 실험은 별도로 cook/stage된 실행 파일을 요구한다. `Measure-CookedEffects.ps1 -Executable ...`, `Measure-PresentationE.ps1 -Executable ...`를 사용하고 application cache 범위를 기록한다. `Export-CrowdE.ps1 -RunRoot ...`로 완료된 trace를 CSV로 내보낼 수 있다. 과거 수치를 그대로 재현한다고 보장하지 않으며 새 결과는 새 revision·환경·에셋과 함께 기록한다.
