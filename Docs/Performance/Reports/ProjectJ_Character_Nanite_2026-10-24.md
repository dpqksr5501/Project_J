# 대검 스켈레탈 Nanite 적용과 비용 비교 — 2026-10-24

불투명 의상 7개(`hel/lower/sho/upper/hand/inner/foot`)에 Nanite를 적용·저장했다. PIE의 실제 `FSkeletalMeshObject::IsNaniteMesh()`로 7개 모두 Nanite 렌더 객체임을 확인했다. 최초 1인 장면에서는 GPU 비용이 소폭 증가했지만, 후속 50/100/200명 군중 조건에서는 이동 GPU 비용이 약14/23/28% 감소했다. 군중 전체 프레임은 CPU 측 비용/대기에 지배되어 개선을 확인하지 못했다. 적용 상태는 유지하며, 두 실험의 해상도·카메라·실행 방식이 다르므로 인원수 하나만으로 결과가 역전됐다고 단정하지 않는다.

## 적용 범위

- 몸체 `GreatSword_Woman`도 시험했으나 `MT_Eyedeco`의 Translucent 재질 때문에 기존 렌더링으로 fallback했다. 재질이나 눈 장식 구조를 바꾸지 않고 몸체 에셋을 SHA-256이 일치하는 원본으로 복원했다. 몸체 LOD0–2는 그대로다.
- 망토 Chaos Cloth 3개와 숨은 Manny는 Nanite를 켜지 않았다. 의상 8개의 LeaderPose, 독립 AnimInstance 없음, 망토 Tick/solver를 유지한다.
- 의상 7개는 원래 각각 단일 LOD다. 단계 수와 fallback 삼각형 수를 유지했으며 재빌드에서 일부 render vertex packing 수는 달라졌다. 의상 LOD0 삼각형 합계는37,773개다. 기존 본·물리·재질 파라미터를 교체하거나 LOD를 삭제하지 않았다.
- 엔진이 요구한 의상 재질12개의 Nanite usage 설정을 저장했다. 몸체 재질의 시험용 미저장 usage 변경은 공식 reload로 되돌렸다. 기존 `DA_MeshLOD_Default.ManualReviewAssets`에 의상7개의 native 제작 경로를 명시했다. 일반 축소 경로를 다시 적용하거나 검증을 전역 비활성화하지 않는다.
- `InspectSkinnedMeshRendering`은 editor 전용 읽기 진단이다. enable 여부뿐 아니라 유효 Nanite 데이터·실제 렌더 객체·현재 LOD·재질/삼각형을 반환하며 pose·physics를 변경하지 않는다.

## 1인 PIE 조건과 방법

기준 HEAD `ada9ca26c5b870f2dff7d71bda5364f69e76a13c`, 시작 작업 트리는 깨끗했다. **UE5.8.4 / CL58902052**, Ryzen7 9800X3D, Radeon RX9070XT, DX12다. 같은 Editor 바이너리에서 기존 → Nanite → runtime override 대조군 순서로 측정했다.

같은 `Lvl_ThirdPerson` 위치(-1800,-1000,180)의 local GreatSword 한 명, 같은 카메라(FOV45, 캐릭터 기준 offset -270/330/15), viewport **2870×1222**, ScreenPercentage100, VSync off, FPS cap0, 백그라운드 throttle off 조건이다. 각 조건은 6초 준비 후 16초 CSV 캡처를3회 수행했다. 전반8초는 정지, 후반8초는2초마다 방향을 바꾸는 실제 이동이다. 전환부를 제외해 CSV 누적 프레임 시간 기준1–7초/9–15초를 집계했다. 이동 표본 평균 속도는 기존496.77/Nanite496.92cm/s였다. 캡처 중 MCP 화면 조작·스크린샷·셰이더 변환은 수행하지 않았다. 동일 Python 카메라/입력 callback과 CSV profiler 비용은 양쪽에 포함된다.

대조군은 Nanite 에셋을 유지하고 의상7개에만 `SetForceDisableNanite(true)`를 적용한 새 PIE다. 환경 Nanite나 전체 `r.Nanite`는 끄지 않았다. 대조군의 실제 Nanite 객체는0개였고 측정 후 새 PIE에서7개로 돌아왔다. 대조군 fallback은 재빌드된 render vertex packing을 사용하므로 전환 전 원본과 바이트 단위로 동일한 입력은 아니다.

## 1인 PIE 평균 비용

아래는 **에디터/장면 전체 프레임·스레드 ms**이며 캐릭터의 순수 증분 비용이 아니다. CPU/GPU는 병렬 실행하므로 더하지 않는다.

| 동작 | 지표 | 기존 | Nanite | Nanite만 끈 대조군 |
| --- | --- | ---: | ---: | ---: |
| 정지 | Frame | 13.636 | 14.450 | 14.694 |
| 정지 | Game Thread | 6.593 | 6.766 | 6.911 |
| 정지 | Render Thread | 3.996 | 4.510 | 4.517 |
| 정지 | GPU | 5.371 | 5.598 | 5.494 |
| 이동 | Frame | 14.043 | 15.241 | 14.905 |
| 이동 | Game Thread | 6.850 | 7.266 | 7.094 |
| 이동 | Render Thread | 4.085 | 4.704 | 4.607 |
| 이동 | GPU | 5.657 | 5.903 | 5.786 |

기존 대비 Nanite GPU는 정지+0.228ms(+4.24%), 이동+0.246ms(+4.35%)다. 후속 대조군 대비 차이는+0.104/+0.117ms(약1.9%/2.0%)로 작다. 대조군도 처음보다 느려졌고 정지 전체 Frame은 Nanite보다 대조군이 더 느리다. 따라서 전체 Frame 차이6–9%를 Nanite의 순수 손해로 단정하지 않는다. 정지 GPU의3회 평균은 기존5.317/5.373/5.420, Nanite5.576/5.596/5.622, 대조군5.449/5.489/5.538ms였다. 시간 순서·열/클럭·에디터 작업의 편차가 남으며 통계적 유의성이나 모든 장면의 우열을 주장하지 않는다.

현재 의상의 비교적 작은 geometry와 파츠별 처리 비용에서 이득이 작을 수 있다는 추정은 가능하지만 원인을 분리 측정한 결론은 아니다. 다수 캐릭터·원거리 LOD·전투/VFX·다른 카메라/해상도/GPU·패키지 성능으로 일반화하지 않는다. 몸체 Nanite는 정상 적용된 비교가 아니다.

## 최초 적용 검증과 근거

- 읽기 진단 direct UBT Editor 빌드 성공. 최종 플레이에서 Nanite7·Cloth3·LeaderPose8을 확인했다. 전체 LOD 단계 수도 원본과 일치한다.
- 관련 메시·재질·LOD 정책 및 보존 몸체/망토22개를 등록된 전체 validator로 검사해 valid22 / warning0 / invalid0이었다. 미저장 content/map은0이며 측정용 throttle·FPS cap·VSync·ScreenPercentage를 복원했다.
- 몸체 투명 재질 및 native 경로 미분류 경고는 중간/실패 근거로 보존한다. 최종 설정은 원본 복원과 명시적인 native 제작 정책으로 해결했다. MCP EULA 안내는 유지한다.
- 엔진 업데이트로 기존5.8.3 전용 Core 포맷 테스트의 영어 source-language 격리가5.8.4에서 빠져 한국어 시작 테스트 오류가 재발했다. 같은 영어 literal/FText 계약이 남아 있음을 확인하고 기존 가드를5.8.4에도 한정 확장했다. assert 생략·로그 숨김·사용자 언어 설정 저장은 하지 않는다. 이 후속 수정은 benchmark 이후이며 렌더/물리 로직은 바꾸지 않는다.
- 후속 direct UBT Editor 빌드 성공 후 재시작에서 시작 시 `Condition failed`가 사라졌다. Core UnifiedError14개·StructuredLogFormat1개·LOD6개·Cloth 수명1개·PlayerMasterAssembly1개 등23개 자동화가 경고/실패 없이 통과했다.

로컬 근거: `Saved/Validation/CharacterNanite_20261024`의 `Summary.json`(프레임 수·p95·run별 값·CSV SHA-256), `Before/Nanite/Control.json`(입력/렌더 객체), `FinalRuntime.json`, `FinalValidation.json`, `Applied.json`, `BodyRestored.json`, `MeasuredBinary.json`과 빌드/에디터 로그. CSV는 `Saved/Profiling/CSV/CharacterNanite_*.csv`다. 선택 프레임은 총7,459개이며 원자료·스크립트·원본 백업을 로컬에 유지한다. 이 로컬 파일이 공유되었다거나 새 PC 재현의 필수 입력이라고 주장하지 않는다. Cook·Shipping·원격/군중 성능은 이번 범위가 아니다.

## 후속 군중 비교

사용자 요청으로 같은 날짜에 기존 native `Project_JRenderedCrowdFixture`를 재사용해 50/100/200명의 실제 `BP_GreatSword`를 비교했다. 몸체·Manny·의상8개·장착 대검·Cloth를 포함하며, 간이 메시로 교체하지 않았다. 로컬1명과 나머지 AI 조종 권위 아바타를 같은 프로세스에서 실행하는 합성 부하다. 실제 원격 플레이어의 SimulatedProxy 역할·네트워크·서버 수용량이나 생산 NPC/Mass 군중을 측정한 것은 아니다.

### 동일 조건과 판정

- UE5.8.4 / CL58902052, 같은 CPU/GPU, Development Editor 실행기의 `-game -RenderOffscreen` 실RHI, DX12, **1280×720**이다. Entry 맵에 동일한 바닥·DirectionalLight·고정 카메라(FOV65)를 생성한다. ScreenPercentage100, DynamicRes off, VSync off, FPS cap0, Scalability 항목 전부3(Epic)이다. 최초1인 PIE의2870×1222/근접 카메라와 다른 조건이다.
- 같은 저장된 Nanite 의상 에셋에서 `r.SkinnedMesh.RenderNanite=0/1`만 바꾼다. 환경 Nanite를 전역으로 끄거나 에셋을 매회 재저장하지 않았다. 따라서 전환 전 원본 파일과 비교한 메모리/다운로드 절감 시험이 아니라, 현재 에셋의 classic fallback/Nanite 렌더 경로 비교다.
- 50명은 Off→On, 100명은 On→Off, 200명은 Off→On→On→Off로 총8회다. 이8회는 source/config/scripts/binary provenance와 보호한 캐릭터/의상 에셋 지문이 각각 동일했다. 종료 후에도 Content29파일이 측정 시작 지문과 일치했다.
- 준비8게임초 후 정지·왕복 이동/전환·점프/착지·R 공격·복귀를 각8게임초 진행한다. 평균은 phase의2–7게임초만 사용하고 스크린샷 요청 프레임과 뒤2프레임을 제외한다. 공격은 모든 아바타의 몽타주가 실제 재생 중인 표본만 사용한다. 동일 인원·동작·카메라·품질은 유지하지만 프레임 단위로 결정적인 재생을 보장하지 않는다.
- 실행별 동작 검증은 모든 아바타의 이동100cm 초과·방향 전환·착지, 동시 R 몽타주 관찰·복귀 후 종료, 모든 몸체의 최근 렌더 관찰을 요구한다. 로컬은 LOD0·Local 예산·60Hz prediction·필수 IK/retarget 품질을 유지한다. 선택 표본의 나머지 몸체는 모두 LOD2이며 켬/끔에서 동일하다.
- 같은 CSV에서 Nanite 후보7N개, 실제 Nanite 렌더 객체0/7N개, modular 의상8N개, Cloth 파츠N개와 활성 Tick/시뮬레이션 비중단 상태N개를 확인한다. 200명에서는 실제0/1,400개, 의상1,600개, 망토200개였다. 망토별 embedded Cloth3개 설정은 바꾸지 않았다.

### GPU 평균 결과

실행별 선택 표본 평균을 구한 뒤 반복 실행에 같은 가중치를 줬다. 숫자는ms이며 감소율은GPU 비용 감소다.

| 인원 | 동작 | Nanite 끔 | Nanite 켬 | 감소 |
| --- | --- | ---: | ---: | ---: |
| 50 | 정지 | 4.305 | 3.832 | 11.0% |
| 50 | 이동/전환 | 4.746 | 4.067 | 14.3% |
| 100 | 정지 | 5.823 | 4.789 | 17.8% |
| 100 | 이동/전환 | 6.023 | 4.664 | 22.6% |
| 200 | 정지 | 8.757 | 6.246 | 28.7% |
| 200 | 이동/전환 | 8.717 | 6.298 | 27.8% |

200명 이동 GPU는 Off8.695/8.738ms, On6.260/6.336ms로 역순 반복에서도 이득이 유지됐다. 동시 공격 GPU는50명4.611→3.964ms,100명5.991→4.920ms,200명8.859→6.566ms였다. 점프/복귀도 검증을 통과했고 상세값은 JSON에 보존한다.

### 전체 프레임은 CPU 측 병목

| 인원 | 이동 Frame 끔→켬(ms) | 이동 Game Thread 끔→켬(ms) |
| --- | ---: | ---: |
| 50 | 469.439→460.991 | 411.108→405.775 |
| 100 | 918.240→921.059 | 877.077→843.193 |
| 200 | 1845.946→1852.093 | 1800.238→1813.233 |

이 합성 부하는50명부터 CPU 측 비용/대기에 매우 크게 지배되며, GPU 몇ms를 줄여도 전체 프레임 개선은 확인되지 않는다. GPU28% 감소를FPS28% 향상으로 표현하지 않는다. 위 시간은 standalone 권위 아바타의 서버/클라이언트 작업과 모든 망토 시뮬레이션을 함께 실행한 fixture의 실제 전체 시간이다. 일반 플레이의50/100/200명 클라이언트 FPS라고 단정할 수 없으며, CPU 시간의 세부 원인을 Cloth/모션매칭 등으로 분리 프로파일링하지 않았다.

정지/이동/점프/복귀는 실행당 각12표본, 동시 공격은6표본이다. 최종 선택 표본은 총432개이며200명은 모드별2회다. 낮은 프레임률 때문에GPU 카운터의 지연·클럭·열/스케줄링 편차가 영향을 줄 수 있다. 신뢰구간·통계적 유의성·다른 GPU/해상도의 우열은 주장하지 않는다. 현재 의상은 각각 단일LOD라 거리별 garment LOD가 있는 대안과도 비교하지 않았다.

현재 저장된 의상7개는 **이 군중 조건에서 GPU 여유를 확보하는 이점이 있어 유지할 근거가 있다.** 전체 군중 프레임 목표는 별도의 CPU 병목 분석이 우선이다. 몸체의 투명 재질과 망토 Cloth를 Nanite로 바꾸어 해결됐다는 의미는 아니다.

### 측정기 보완·실패 보존·마감

처음에는 기존 fixture가 wall time으로 동작 구간을 바꿨다.100명 부하에서는 엔진의 game delta clamp로 게임 시간과 벌어져, 공격/복귀가 충분히 진행되기 전에 wrapper가 몽타주 종료를 실패로 판정했다. 최초 `NaniteCrowd_*` 시도는 최종 수치에서 제외하고 보존했다. 동작·구간·복귀 검사를 모두 실제 Mover/몽타주와 같은 world/game time으로 수정했고, wall time은 실제 프레임 비용과 bounded timeout에만 사용한다. 프로파일링 실행의 한도는600실초이며 기존 일반 실행은180실초다. 타임스케일·물리·애니메이션 품질을 낮추거나 강제 reset으로 검사를 통과시키지 않았다. 보완 후 `NaniteCrowdClock_*`8회가 모두 종료·움직임·실제 렌더 경로 검증에 성공했다.

8회 모두 frame0에서 동일한 에디터 전용 Python toolset 초기화 오류58줄이 있었고 측정 구간의 Python 오류·ensure는 없었다. `-game`에는 `ToolsetDefinition/PythonTestRunner` 같은 editor 심볼이 없어 생긴 문제이며, 원문을 지우지 않았다. 이 fixture는 native 입력/검증을 쓰므로 이후 headless Nanite 프로파일링에만 기존 `-DisablePython` 실행 옵션을 자동 적용했다. 일반 Editor·MCP·프로젝트 플러그인 설정은 바꾸지 않는다. 측정 후에는 잘못된 Nanite 인자의 실패 결과도 지정된 폴더에 기록되도록 초기화 순서를 정리했다. 이 두 마감 변경은8회 캡처 이후이며, 캡처 당시 source/binary 지문을 별도로 보존한다. 첫 실행의 deferred scalability 명령·PoseSearch 인덱스 준비 경고는 준비 구간에 있었으며 최종 데이터에서 제외했다.

마감의 direct UBT Editor 빌드는 성공했다. 최종1인 native 스모크 실행에서 Nanite7개·Cloth1개·local 품질·전체 이동/공격/복귀 검사를 통과했고 Python 초기화 오류·실행 오류·ensure는0이었다. 이 스모크는 실행 구성 검증이며 군중 수치에 합산하지 않는다.

재사용 runner는 `Scripts/Validation/Measure-RenderedCrowd.ps1`이며 `-NaniteMode 0/1 -Count <N> -Movement -LocalPlayer`로 실제 경로 검사를 활성화한다. 공유 집계는 [군중 수치 JSON](ProjectJ_Character_NaniteCrowd_2026-10-24.json)에 둔다. 원본/보완 빌드 로그·로컬 집계는 `Saved/Validation/CharacterNaniteCrowd_20261024`에,8개 실행별 manifest/provenance·검증·frames/workload CSV·화면은 `Saved/Validation/WholeProjectAudit_20261013/NaniteCrowdClock_*_20261024`에 둔다. 초기 실패 경로는 그 옆의 `NaniteCrowd_*`, 마감 스모크는 `NaniteCrowdSmoke_Final_20261024`다. 에셋 재저장·프로젝트 설정 변경·Cook/Shipping·실네트워크 시험은 이번 군중 작업에서 수행하지 않았다.
