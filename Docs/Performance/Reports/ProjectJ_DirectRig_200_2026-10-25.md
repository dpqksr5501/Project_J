# UE5 호환 직접 ABP · 200명 비용 비교 — 2026-10-25

현재 GreatSword의 직접 재생 경로와 보존된 실시간 리타깃 측정을 비교한다. 골격·에셋·LOD·물리가 함께 바뀌었으므로 과거 대비 전체 프레임 차이를 리타깃 제거만의 효과로 계산하지 않는다.

**결과:** 현재200명 직접 ABP에서 엔진 리타깃 실행은0회다. 하지만 전체 frame은1.8–2.1초 수준으로 이전과 비슷했고, 공격 GPU는6.642→10.110ms로 증가했다. 이 fixture에서는 망토 Cloth·중첩 병렬 작업/대기가 우선 분석 대상이며, 새 리깅만으로 군중 성능이 해결된 결과는 아니다.

## 비교 대상과 지표

| 자료 | 부하·조건 | 읽을 수 있는 값 |
| --- | --- | --- |
| 2026-10-13 리타깃 CPU | 200명, 실제 BP/장비/follower, NullRHI, 60Hz, ABA 2ms, 조건별 준비120+측정120틱 | 비계측 World tick, 별도 trace의 worker 누적 CPU |
| 2026-10-24 Basic 대검 표시 군중 | 같은 군중 fixture, D3D12 1280×720 offscreen, 200명, 실제 이동/반전·점프/착지·R 공격/복귀, Nanite on·Basic 대검 표시2회 | 이전 리타깃 에셋의 전체 frame/GT/GPU. [원자료](Data/Weapon200_20261024/Summary.json) |
| 2026-10-25 직접 리깅 | 공용 ABP + 몸체 PostProcess, 호환 파츠 Leader Pose, Basic 대검, 현재 제작 물리·Cloth 유지 | 현재 전체 frame/GT/GPU 및 리타깃 호출 유무 |

CPU worker의 누적 inclusive 시간은 동시 실행을 합한 값이다. 전체 프레임/GT 지연과 같지 않으며 중첩된 부모·자식 타이머를 더하지 않는다. GPU/GT/RT/RHI도 서로 겹치므로 합계로 프레임을 만들지 않는다. 실제 원격 연결은0개이며 200명 동시 접속 성능을 측정한 결과가 아니다.

## 과거 리타깃의 실제 연산 비용

2026-10-13 최종 좁은 프로필 scope trace의 ABA on 각120틱을 재집계했다. 단위는 **world tick당 누적 CPU ms**다.

| 조건 | IK Retarget | 그 안의 Processor Run | 프로필 설정 PreUpdate | 리타깃 호출/틱 |
| --- | ---: | ---: | ---: | ---: |
| Near | 8.383 | 6.741 | 0.074 | 100 |
| Far | 5.100 | 4.107 | 0.043 | 50 |
| Hidden | 0 | 0 | 0 | 0 |
| AttackBurst | 19.966 | 16.237 | 0.131 | 200 |

`IK Retarget` 안에 `Processor Run`이 포함된다. 이전 문서의0.074/0.131ms는 **프로필 설정 처리**만 측정한 값으로, 리타깃 전체 비용이 아니다. Near/Far의 호출 수는 당시 평가 예산이 적용된 결과이며 200명을 매틱 전원 리타깃한 조건과 다르다.

당시 프로필 연결 후 비계측 World tick 평균은 Near ABA on12.686ms, Far11.480ms, AttackBurst22.736ms였다(2회 평균). 이 숫자와 위 trace CPU 합계는 다른 실행이다. 현재 실RHI frame과 직접 빼서 개선율을 계산하지 않는다. [당시 조건·증가·한계](../../Animation/Architecture/Runtime_Retarget_Profiles_2026-10-13.md#최종-검증과-200명-cpu-비교)

## 현재 측정

| 동작 | 이전 Frame (ms) | 현재 Frame (ms) | 이전 GT (ms) | 현재 GT (ms) | 이전 GPU (ms) | 현재 GPU (ms) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 정지 | 1,866.735 | 1,861.003 | 1,829.426 | 1,813.195 | 6.337 | 6.600 |
| 이동/방향 전환 | 1,873.124 | 1,834.390 | 1,838.343 | 1,790.063 | 6.294 | 6.631 |
| 점프/착지 | 1,857.432 | 1,813.662 | 1,814.063 | 1,772.883 | 6.587 | 6.733 |
| 200명 동시 공격 | 2,130.990 | 2,122.059 | 2,014.275 | 2,008.402 | 6.642 | 10.110 |
| 복귀 | 1,848.653 | 1,858.260 | 1,814.818 | 1,815.430 | 6.510 | 6.267 |

각2회 실행 평균을 같은 가중치로 평균했다. 정지/이동/점프/복귀는 실행당12표본, 동시 공격은6표본으로 최종108표본이다. p95는 nearest rank이며 실행별 평균·median·p95와 메모리 지표는 [수치 JSON](Data/DirectRig200_20261025/Summary.json)에 보존한다. 구간의2–7게임초를 사용하고 캡처 요청+다음2프레임을 제외했으며 공격은 전원 montage가 재생 중인 행만 쓴다.

전체 프레임은 이전과 비슷한1.8–2.1초 수준이며 CPU 측 처리/대기에 지배된다. 이동의 관찰 차이는 약−2.1%지만 정지/복귀 방향은 일관되지 않아 전체 개선으로 판정하지 않는다. 공격 GPU의 관찰 증가는6.642→10.110ms(약+52.2%)로 두 번 모두 남았다. 새 몸체/머리 분리·LOD0·318본·포즈/물리 변화가 포함된 기록 비교이므로 GPU 증가나 frame 차이를 리타깃 제거의 인과로 돌리지 않는다. GPU 세부 pass의 원인 분리와 새 geometry/bone LOD 승인도 이번에 수행하지 않았다.

## 실제 측정 조건·품질

UE5.8.4(CL58902052), Ryzen7 9800X3D/Radeon RX9070XT, D3D12, 1280×720 offscreen, Epic 각품질3·ScreenPercentage100·DynamicRes/VSync/FPS cap off. 20×10 고정 배치·FOV65·간이 바닥/DirectionalLight는 이전과 같다. 준비8게임초 뒤5개 동작을 각8게임초 실행했다. 게임 시간은 동작 진행에, wall time은 비용과600초 한도에 사용한다.

로컬 소유1명+AI 권위 Avatar199명이며 원격 연결0개다. 실제 client SimulatedProxy200명의 부하와 다르다. 모든200명의 이동100cm 초과·반전·착지, 동시 R 몽타주·복귀 종료, 로컬 LOD0·손발 IK·예측60Hz, 정확한 Basic 대검200개·자동LOD·표시를 확인했다. 직접 body200/리타깃 main0, 모듈러2,000개, Nanite1,400개, 활성 망토200개가 유지됐다. 단일 producer가 공용 ABP+몸체PP를 평가하고10파츠가 Leader Pose를 사용한다.

| 구성 | 이전 리타깃 | 현재 직접 재생 |
| --- | --- | --- |
| 포즈 | Manny source + 별도 리타깃 몸체 | 318본 Complete의 공용 ABP + 몸체PP |
| 표시 파츠 | 몸체1 + 의상8 | 몸체·머리·의상10 |
| 몸체 LOD | 로컬0 / 원격2 | 전체0 (새 제작LOD 미승인) |
| 부차 물리 | 기존 RigidBody3 + 망토 Cloth | 헤어/치마 RigidBody2 + 망토 Cloth |
| 무기·의상 렌더 | Basic200 / Nanite7·활성망토1/명 | 동일 |

전체 프로세스 peak 메모리는 시작·로딩·모든Avatar를 포함한500ms 간격 표본이며 인원당/증분 메모리나 VRAM이 아니다. 저표본·GPU 카운터 지연·클럭/열·스케줄링 편차가 있어 통계적 유의성이나 다른 하드웨어 성능을 주장하지 않는다.

[현재 정지 화면](Data/DirectRig200_20261025/Direct_Idle.png) · [동시 공격 화면](Data/DirectRig200_20261025/Direct_Attack.png). 이 원경 캡처는 전체200명 배치 확인용이며 모든 프레임의 접촉/의상 품질 승인을 뜻하지 않는다.

## 별도 trace: 제거된 연산과 남은 병목

동일 source/binary 지문의 별도 trace1회도200명·이동/공격/복귀·정상 종료를 통과했다. 현재 엔진이 동일하게 선언하는 `IK Retarget`과 `IK Retarget Processor Run` 실행 timer는 **둘 다0회/0초**다. 로딩 중 나타난 `AnimGraphNode_RetargetPoseFromMesh` 클래스 메타데이터3회는 노드의 실행 비용이 아니다. `RetargetAnimInstance`라는 기존 접촉 controller 이름이나 프로필 설정 scope가 남아 있어도 실제 리타깃 실행과 구분한다.

| CPU scope | 호출 수 | 누적 inclusive (초) |
| --- | ---: | ---: |
| `FEngineLoop::Tick` | 121 | 236.324230 |
| `IK Retarget` | 0 | 0.000000 |
| `IK Retarget Processor Run` | 0 | 0.000000 |
| `ProjectJ_RetargetProfile_PreUpdate` | 3,243 | 0.001429 |
| `ControlRig_Evaluate` | 547 | 0.187851 |
| `RigidBody_Eval` | 6,674 | 0.368096 |
| `FParallelClothTask` | 23,800 | 3,188.989852 |
| `PhysicsParallelFor` | 18,936,102 | 3,206.384103 |

이 표는 준비·초기화·전체 동작을 포함한 **trace 전체**이며 비계측 steady frame 수치와 분리한다. Cloth 작업과 PhysicsParallelFor는 서로 중첩된다. worker의 경과시간에는 대기·스케줄링도 포함되므로 합산하거나3189초를 wall time·순수 CPU 연산량으로 읽지 않는다. 그럼에도23,800개 Cloth task와 약1,894만 PhysicsParallelFor 호출이 있는 경로가 남아 있고, 공용/몸체 Control Rig나 RigidBody의 범위보다 크게 관찰된다. 다음 최적화에서는 실제 remote proxy 부하를 분리하고 망토의 substep/iteration·작업 분할·거리별 독립 Cloth 예산을 집중 검증할 근거다. 이 측정에서는 품질을 낮추거나 Cloth를 끄지 않았다.

[정확한 scope/횟수·해시·로그 상태](Data/DirectRig200_20261025/TraceScopes.json)를 보존한다. 최종1명 스모크와200명3회는 종료 코드0·오류/ensure/fatal0이었다. 경고는 스모크24줄,200명 각241줄로 전체 로그가 무경고인 것은 아니다. 측정 전 초기화의 EditorDataStorage·deferred scalability·PoseSearch 비동기 인덱스 준비 경고를 원문과 함께 유지한다.

## 측정기의 두 경로 지원

기존 측정기는 main AnimInstance가 리타깃 클래스인 몸체만 찾고 파츠8개를 고정으로 요구했다. 현재는 main 또는 PostProcess의 기존 접촉 소유자를 찾아 포즈 producer를 판별하고, 실제 표시된 producer 또는 Leader Pose 파츠로 렌더 여부를 확인한다. `pose_routes.csv`에는 실제 mesh/main/PP/leader/bone 수/모듈러 여부를 기록하며, 초기 생성 파츠 수와 매 프레임 값을 대조한다.

이전 구성은 리타깃 몸체1개+의상8개였고, 직접 구성은 숨겨진 Complete producer1개+몸체/머리/의상10개다. 기존8개 조건에서 새10개를 실패로 판정한 첫 스모크는 제외하고 실패 로그도 보존한다. 파츠를 줄이거나 물리/IK·모션 품질을 낮춰 검사를 통과시키지 않는다. 리타깃 main/직접 main+PP 계수는 경로 식별이며, 실제 리타깃 연산 부재는 별도 engine trace로 확인한다.

## 재현

```powershell
# Editor·빌드 프로세스 종료 후, 동일 품질 명령과 새 RunName으로 반복
./Scripts/Validation/Measure-RenderedCrowd.ps1 -RunName <unique_name> -Count 200 `
  -Movement -LocalPlayer -NaniteMode 1 -WeaponVisibility 1 `
  -ExpectedWeaponMesh /Game/Weapons/Greatsword/Basic/SM_Greatsword_Basic.SM_Greatsword_Basic `
  -ConsoleCommands '<manifest의 Epic 품질·ScreenPercentage·DynamicRes 명령>'
# 원인 진단은 별도 실행에 -Trace를 추가하고 비계측 평균과 분리
./Scripts/Validation/Export-CrowdE.ps1 -RunRoot <trace_run> -TraceFileName Render.utrace
python Scripts/Validation/Summarize-RigCrowd.py <run_A> <run_B> --output <summary.json>
```

실행은 Development Editor의 `-game -RenderOffscreen`이며 Cook/Shipping·네트워크·VRAM·서버 수용량 측정은 별도다. 모든 실행은 정상 종료를 기다리며 진단 목적의 게임 설정만 인자로 적용한다. 측정용 코드 외의 생산 그래프·물리·리깅 에셋은 이번 비용 측정에서 변경하지 않는다.

## 원자료 정리와 미검증 범위

[안전 정리·보존·복원](../../Maintenance/Storage_Cleanup_2026-10-25.md)에 로컬 압축본 위치·SHA와 논리/실제 디스크 관측을 기록했다. 전체 trace907,223,845bytes와 실패/최종 로그는 그 압축본으로 복원할 수 있고, [타이머 원표](Data/DirectRig200_20261025/TraceAllTimers.csv)는 저장소에서 바로 읽을 수 있다. 숫자·조건·대표 화면을 유지한 후 임시 스크립트·캡처·중간 백업의 중복 원본만 정리했다. 최종 직접 UBT Editor 빌드339.91초·1명 스모크·200명 비계측2회/trace1회가 통과했으며 Cook/Shipping·네트워크·군중 성능 최적화 구현은 이번 범위에 포함하지 않는다.
