# 직접 ABP 200명 · Cloth 병목 진단과 품질 예산

직접 재생 전환으로 실시간 IK Retarget 실행은 제거됐지만, 기존과 현재 모두 활성화된 망토 표면 Cloth의 대기가 전체 프레임을 지배했다. 리타깃의 worker CPU 합계와 프레임 경과시간은 다른 지표다. [직접 재생 기준 측정](ProjectJ_DirectRig_200_2026-10-25.md)을 대체하지 않고 후속 진단과 변경 검증을 구분한다.

진단은 10월 25일, 변경 검증과 아래 교대 비교는 **10월 26일 KST**에 수행했다. 동일 바이너리 ON/OFF/OFF/ON에서 이동 평균은 **1,827.665→63.092ms (96.548% 감소)**였다. 망토는 양쪽 모두 200개 활성 상태이며 로컬 품질 축소는 0개다. 60FPS 달성이나 실제 200명 네트워크 접속 성능을 뜻하지 않는다.

## 같은 바이너리에서 원인 분리

200명·로컬 1명·실제 이동/반전·점프/착지·동시 R 공격, Basic 대검, 표시 파츠 2,000개, Nanite 1,400개, D3D12 1280×720 Epic 조건이다. 실제 원격 접속은 0개다. GPU/GT/RT를 더해 프레임을 만들지 않는다.

| 진단 모드 | 반복 | 정지 Frame ms | 이동 ms | 공격 ms | 판정 |
| --- | ---: | ---: | ---: | ---: | --- |
| 원래 설정 재확인 | 1 | 1,835.304 | 1,840.157 | 2,142.560 | 기준의 초 단위 지연 재현 |
| Cloth 전체 OFF | 2 | 16.588 | 20.671 | 41.821 | Cloth 경로가 지연의 주원인. 품질이 달라 생산 최적화로 채택하지 않음 |
| 내부 Chaos ParallelFor 직렬화 | 2 | 1,909.658 | 1,903.958 | 2,194.309 | 단순 직렬화로 개선되지 않음. 전역 설정 미채택 |
| ChaosCloth 충돌 비활성 debug flag | 2 | 1,895.076 | 1,865.418 | 2,156.291 | 설정 요청은 확인했으나 활성 충돌 커널 제거까지 입증하지 못함. 충돌 비용의 반증으로 사용하지 않음 |

기존 도구의 구간 2–7게임초, 캡처 요청과 다음 2프레임 제외, 공격 전원 montage 조건을 동일하게 사용했다. 반복별 동일 가중치 평균이며 서로 다른 표본 수를 합쳐 가중하지 않았다. [반복별 평균·median·p95](Data/ClothBudget200_20261025/Diagnostics.json), 각 실행 폴더의 원본 CSV·manifest·종료/검증 결과를 보존한다. 진단은 모두 변경 전 같은 바이너리·에셋으로 수행했으며 인자는 실행별로만 적용했다.

## 게임 스레드는 무엇을 기다렸나

원래 대표 trace 전체의 `FEngineLoop::Tick` 구간 합집합은 236.324초, 게임 스레드 `WaitForTasks` 합집합은 219.893초로 **93.047%**였다. 대기의 **99.907%**가 실제 `FParallelClothTask` 구간과 겹쳤다. 자동으로 함께 export된 가상 frame 이벤트는 제거하고 실제 Cloth scope 23,800개만 사용했다. [구간 합집합·교집합 계산](Data/ClothBudget200_20261025/CriticalPath.json)

겹침 자체는 task dependency ID를 증명하지 않는다. Cloth OFF의 반복 비교와 함께 Cloth 경로가 지배적이라는 근거로 읽는다. 병렬 worker의 누적 inclusive 시간은 wall time·순수 CPU 사용량이 아니며 중첩 타이머를 합산하지 않는다. `PhysicsParallelFor` 이름의 약 1,894만 scope 관측도 동일 횟수의 상위 함수 호출이라고 단정하지 않는다. 충돌 관련 `ChaosPBDCollisionRule` 계열이 큰 비중을 차지하지만 정확한 커널별 순수 연산량은 별도 계측이 필요하다.

## 에셋 부하와 역사적 차이

| 항목 | 확인 내용 | 의미 |
| --- | --- | --- |
| 현재·10월 24일 망토 | 표면 Cloth 3개, physics vertex 총 2,534개/캐릭터 | 200명은 506,800개. 설정은 iteration 8 / max 16 / substep 3, CCD ON, self collision OFF |
| 현재·10월 24일 부차 RigidBody | 헤어/치마 2개 | 기존 보고서의 10월 24일 RigidBody 3개 표기를 정정. 10월 13일의 다른 구성과 혼동하지 않음 |
| 10월 22일 빠른 기존 측정 | 망토 bone RigidBody 구성. 10월 24일 surface Cloth로 변경됨 | 약 20ms였던 과거 자료를 현재와 같은 Cloth 부하로 해석하지 않음 |
| 현재 몸체+머리 | LOD0만, vertex 4,324 + 25,742 = 30,066 | 숨겨진 Complete 생산자는 표시 중복이 아니며, 표시 소비자의 geometry LOD가 별도로 필요 |
| 기존 통합 몸체 | LOD0/1/2 vertex 30,070 / 15,788 / 8,303 | 로컬 1개 LOD0+원격199개 LOD2는 1,682,367개. 새 몸체·머리 전원 LOD0는 6,013,200개로 약 3.574배 |

정점 합계는 몸체·머리의 비교이며 전체 화면 GPU 작업량이나 성능 비율이 아니다. GPU 증가의 pass별 인과 비율은 측정하지 않았다. [읽기 전용 에셋 메타데이터](Data/ClothBudget200_20261025/Metadata.json)를 보존한다.

## 변경 검증 상태

최종 direct UBT **Editor 355.33초·Development Game 262.34초 성공**, 실제 RHI 집중 회귀 **73개 통과·실패/경고/미실행 0개**다. 직접 ABP·명시적 리타깃 fallback·서버 source·장비 교체·Cloth 소유권·로컬 복구·기존 접촉 계약을 포함한다. NullRHI에서는 배분 정책 1개가 통과했고 실물 검사는 `NonNullRHI`로 제외됐다. 중복 정책 검사를 더해 74개 독립 검증으로 세지 않는다. [회귀 목록과 빌드 지문](Data/ClothBudget200_20261025/Validation.json), [피부 body LOD 생성/원본 보호 결과](Data/ClothBudget200_20261025/BodyLODGeneration.json)를 보존한다. Game 빌드는 Cook/Shipping 실행 검증을 뜻하지 않는다.

마감 중 기존 진단 코드의 editor-only preview mesh helper와 Nanite 제작 설정 조회를 타깃별 데이터 경계로 바로잡았다. Editor의 후보 집계는 유지하고 Game에서는 실제 cooked render data를 조회한다. 측정 이후 변경은 [진단/검사 범위의 파일 목록](Data/ClothBudget200_20261025/PostMeasurementChanges.json)으로 구분하며 생산 Character 런타임·Config는 교대 측정 때와 같다. 화면 검수와 최종 스모크는 아래 별도 근거다.

### 같은 바이너리의 원격 Cloth 예산 OFF/ON

| 동작 | OFF Frame ms | ON Frame ms | 감소 | GPU OFF→ON ms |
| --- | ---: | ---: | ---: | ---: |
| 정지 | 1,851.444 | 63.116 | 96.591% | 6.487→9.349 |
| 이동/반전 | 1,827.665 | 63.092 | 96.548% | 6.398→9.970 |
| 점프/착지 | 1,815.788 | 59.785 | 96.707% | 6.560→9.513 |
| 200명 동시 공격 | 2,123.513 | 79.754 | 96.244% | 9.609→10.160 |
| 복귀 | 1,851.382 | 54.847 | 97.038% | 6.293→7.807 |

소스·바이너리·82개 에셋이 같고 `ProjectJ.ClothBudget.Enabled`만 다르다. **양쪽 모두 새 body LOD0–2**를 사용하므로 이 표는 LOD 생성 자체의 절감률이 아니다. ON A→OFF A→OFF B→ON B 순서이며 반복별 평균을 동일 가중치로 평균했다. 기존 2–7게임초/캡처 제외 규칙과 전원 montage 조건을 적용했다. 공격 표본은 OFF 각각 6개, ON 32/31개이며 frame 속도가 달라 표본 수가 다르다. [반복별 mean·median·p95, GT/RT/RHI/GPU, 메모리, 검증 값](Data/ClothBudget200_20261025/Comparison.json)과 각 폴더의 CSV/manifest를 따른다. 반복 2회씩의 관측이며 신뢰구간이나 모든 환경의 보장으로 확대하지 않는다.

ON의 유효 품질 검사 820행에서 관리 Cloth 200개·실제 시뮬레이션 200개·Cloth tick 200개, 축소 원격 199개·로컬 축소 0개였다. 원격의 실행 iteration 최대는 OFF 16, ON 4로 확인했다. 로컬은 override 없이 authored 설정을 유지했고 표시 body LOD0도 유지했다. source producer의 `body_lod*`와 별도 `render_lod*`를 혼동하지 않는다. 후자는 하위 LOD가 있는 표시 파츠만 세며 이번에는 피부 body다. 자동 선택/스트리밍에 따라 원격 LOD2가 최대 199개까지 관찰됐고 준비 직후 일부 LOD0도 남았다. 강제 LOD로 숫자를 맞추지 않았다.

위 교대 측정은 Editor Build4 기준이다. 이후 1명 근접 검수 카메라가 캐릭터를 따라가도록 하고 호출 조건을 보완했으며, 생산 런타임 소스·에셋이 같다는 것을 파일 지문으로 확인했다. 최종 Editor Build5는 370.72초에 성공했다. [최종 200명 스모크 A](Data/ClothBudget200_20261025/FinalSmoke.json)는 이동 61.780ms·공격 96.509ms, [스모크 B](Data/ClothBudget200_20261025/FinalSmokeB.json)는 이동 62.507ms·공격 80.105ms였다. 두 번 모두 정상 종료했다. 공격의 실행 간 편차 원인은 분리하지 못했으며, 위 A/B 평균에 합치거나 79.754ms를 고정 성능으로 주장하지 않는다.

### 로컬 보존과 근접 표본

[보호 파일 지문](Data/ClothBudget200_20261025/ProtectedFiles.json)에서 공통 Master 캐릭터 BP·Master ABP·foot rig·제작 blend·head가 그대로임을 확인했다. body 패키지는 LOD 추가로 바뀌었지만 기존 제작 도구의 LOD0 geometry/skin/reference-pose 보호 검사를 통과했다. 저장 후 vertex 수는 body **4,324 / 2,275 / 1,221**, head **25,742 / 단일 LOD**다. [재질·LOD 재조회](Data/ClothBudget200_20261025/MeshMetadataAfter.json)

추적 근접 카메라에서 로컬 full 품질과 강제로 quota 0을 준 원격 reduced 품질 각각 이동·반전·점프·착지·R 공격/복귀가 정상 종료했다. 로컬은 표시 LOD0·축소 0, 원격은 축소 1·활성 Cloth 1을 확인했다. [검증 값](Data/ClothBudget200_20261025/Quality.json) · [로컬 정지](Data/ClothBudget200_20261025/Local_Idle.png) · [로컬 공격](Data/ClothBudget200_20261025/Local_Attack.png) · [원격 reduced 공격](Data/ClothBudget200_20261025/RemoteReduced_Attack.png)

표면 검수에만 `r.MotionBlurQuality 0`을 사용했다. 200명 비교와 생산 렌더링 설정은 바꾸지 않았다. 이전 고정 카메라의 화면 이탈과 바닥 없는 static close trial은 품질 승인 자료에서 제외했다. 최종 표본에서 Cloth 발산은 보이지 않았지만 이미지와 계약 회귀만으로 모든 프레임의 미세 손 접촉·신발 관통·극단 경사/장비 조합까지 승인하지 않는다. 리깅 가이드의 기존 도달 잔차와 원본 검수 기준을 유지한다.

### 남은 비용과 해석 한계

- GPU는 개선되지 않았다. 이동은 6.398→9.970ms, 공격은 9.609→10.160ms로 관측됐다. Cloth 결과 형상·pose sampling·프레임 간격/렌더 파이프라인 상태가 달라졌으며 GPU pass별 인과를 분리하지 않았다. GPU 개선이나 geometry 절감률로 포장하지 않는다.
- ON 이동 63.092ms는 약 15.85FPS, 전원 공격 79.754ms는 약 12.54FPS다. 이 200명 fixture에서 60FPS 목표를 달성한 결과가 아니다. 이미 Cloth OFF 진단도 이동 약 20.671ms·공격 약 41.821ms여서, Cloth만으로 전체 비용을 제거할 수 없다.
- 준비·초기화 비용은 정상 구간과 별도다. ON A 첫 기록은 5,920.185ms였고 초기 grant/전환 구간에도 약 2,095.890ms 행이 있었다. 200명 일괄 생성·리소스/포즈 준비와 full→reduced 전환이 포함되며, 정상 구간 개선이 spawn/streaming hitch까지 해결한 뜻은 아니다.
- head의 헤어 카드 전용 LOD, 저해상도 Cloth simulation mesh/LOD와 충돌체 제작, 실제 원격 proxy/다중 관측자·복잡한 맵·Cook/Shipping·네트워크 회귀는 별도다. 이들의 제작/검수 없이 임의 뼈 제거·전역 Cloth OFF·게임플레이 필수 pose/notify 생략으로 수치를 맞추지 않는다.

## 적용한 소유권과 품질 경계

| 담당 | 정책 | 보존·복구 |
| --- | --- | --- |
| 기존 월드 animation-budget subsystem | 같은 거리 관측/등록 pass에서 근거리 원격 Cloth 파츠 최대 4개에 full 품질 허용. 기존 grant에 거리 10% hysteresis. 로컬은 quota 밖 | ABA OFF에서도 관측 유지. 카메라·거리 설정이 불확실하면 authored 품질 |
| 기존 animation-update coordinator | 검증 가능한 native modular Leader Pose Cloth에만 인스턴스 interactor 명령. remote는 iteration `min(2, authored)` / max `min(4, authored)` / substep 1 | 로컬·rollback은 authored 설정. 하향 전환 0.2게임초, 상향 즉시. 별도 timer·manager 없음 |
| 의상별 선택 | `bAllowClothQualityBudget`, `bAllowDiscreteRemoteClothCollision` | 외부 interactor writer는 opt-out. 원격 CCD만 discrete로 바꿀 수 있으며 두께·마찰·기존 self collision 설정과 몸체 충돌은 유지 |
| 표시 LOD 소비자 | 숨겨진 producer와 분리해 원격 표시 파츠의 parent/Leader Pose LOD 동기화를 해제. native screen-size/streaming/강제 LOD 권위는 유지 | 로컬·해제·장비 교체·종료 시 이 코드가 바꾼 동기화만 복구. 별도 AnimGraph/tick을 추가하지 않음 |
| 제작 데이터 | 피부 body에만 기존 공통 도구로 LOD1/2 생성. LOD0 protected fingerprint 검증 | head의 피부·헤어 카드·속눈썹 혼합 구조는 자동 축소 제외. 텍스처·제작 blend/FBX·공통 Master BP/ABP 보존 |

공유 Cloth config asset을 런타임에 쓰지 않는다. 지원하지 않는 config/시뮬레이션이나 한 solver에 복구 불가능한 서로 다른 shared config가 있으면 건드리지 않는다. binding은 메시·Clothing asset·시뮬레이션 identity를 함께 검사하므로 이전 장비의 설정을 새 시뮬레이션에 쓰지 않는다. 전용 서버에는 cosmetic 소유권을 만들지 않는다. 전투·montage의 필수 포즈 보호와 Cloth 품질 quota는 별도이므로 전원 공격이 원격 Cloth full 품질을 무제한 예약하지 않는다.

`ProjectJ.ClothBudget.Enabled 0`은 authored Cloth 설정으로 복구하는 비교/rollback이다. `ProjectJ.ClothBudget.MaxFullRemoteParts`는 근거리 원격 quota이며 로컬을 포함하지 않는다. 초기 진입·grant 교체의 하향 유예 중에는 순간적으로 full 품질 개수가 quota를 넘을 수 있다. 이를 엄격한 프레임 시간 상한이나 서버 수용량 제한으로 해석하지 않는다.

**통계의 한계:** 설치 UE 5.8.4의 legacy `ChaosClothingSimulation.cpp`는 `ResetStats`에서 `NumSubsteps`를 초기화하고 이후 simulate에서 `NumIterations`만 갱신한다. 이번 raw CSV의 `cloth_substeps*`·`local_cloth_substeps`는 그 public interactor 통계이며, 관찰된 1을 실제 실행 substep이나 로컬 품질 축소의 증거로 사용하지 않는다. asset의 authored 8/16/3과 로컬 override 0, 인스턴스 명령/복구, 정상 갱신되는 iteration 통계를 구분한다. 엔진 내부 메모리를 비동기 worker와 경합하며 읽거나 측정을 위해 강제 대기를 추가하지 않았다.

## 보존과 정리

대표 군중/근접 캡처, 원본 CSV·manifest·종료/검증 결과, interval CSV·정점/재질·보호 지문을 이 보고서의 Data 폴더에 보존한다. 원래 전체 trace는 기존 로컬 ZIP·원본 member·임시 복원본 세 SHA를 검증한 뒤 중복 복원본만 제거했다. 테스트 전용 UserDir·중복/미채택 캡처·일회성 드라이버까지 **276개 파일, 논리 1,105,016,978bytes (1,053.83MiB)**를 정리했다. 즉시 관측된 C 드라이브 여유 공간은 233,472bytes 감소했으므로 물리 확보량이라고 주장하지 않는다. 원본 ZIP·SourceArt·생산 데이터·빌드 캐시·현재 바이너리와 원본 로그/CSV는 유지했다. [삭제 경로·해시·논리/실제 관측](Data/ClothBudget200_20261025/Cleanup.json) · [이전 보관·복원 기준](../../Maintenance/Storage_Cleanup_2026-10-25.md)

## 제작과 런타임의 책임

리깅·bind pose·skin·신발 관통 같은 근본 결함은 DCC/원본에서 수정한다. 런타임의 물리 품질 예산은 이미 검수된 의상의 원격 표현 비용을 조절하는 수단이며 결함을 숨기는 보정이 아니다. 렌더 LOD와 Cloth simulation LOD도 별개다. 로컬의 텍스처/얼굴/헤어/치마/망토 품질을 낮춰 군중 수치를 만드는 방식은 채택하지 않는다.

표면 Cloth의 저해상도 simulation mesh·LOD와 충돌체 제작은 엔진의 [Clothing Tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine) 및 [속성 기준](https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine---properties-reference)을 따른다. 애니메이션 cadence를 담당하는 [Animation Budget Allocator](https://dev.epicgames.com/documentation/unreal-engine/animation-budget-allocator-in-unreal-engine?lang=en-US)가 Cloth solver 비용까지 자동으로 제한한다고 가정하지 않는다. 정확한 interactor API와 적용 시점은 설치된 UE 5.8.4 소스로 확인한다.
