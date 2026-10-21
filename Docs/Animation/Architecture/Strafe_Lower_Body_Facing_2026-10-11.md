# Strafe 전방 Run 골반 방향과 발 제약

> **이전 이동 구조의 기록 — 2026-10-21:** 본문은 당시 CMC/MM·마스터·후보 정책의 구현·제안·측정 이력이다. 현재 생산 제작은 [플레이어 로코모션](Player_Locomotion_Architecture.md)을 따른다. 과거 구조를 복원하라는 지시나 현재 구조의 성능 결과로 해석하지 않는다.

## 진단과 적용 범위

상체 보정 후 W + 느린 마우스 회전에서 하체가 이전 방향을 바라보는 느낌을 조사했다. 기존 실제 CMC 재현의 60/90도·초 Arc 표본에서 루트–속도 방향 차이 중앙값은 4.14/4.03도였고, 골반–속도 차이는 20.92/24.79도였다. 선택된 `M_Neutral_Run_Arc_Tight_R`의 압축 포즈 33표본을 실제 재생 시간에 비교했을 때 원본 골반–루트 회전 중앙값은 -23.16도였다. 안정된 표본에서는 이 원본 회전이 최종 포즈에 남아 있었다. 블렌드 중 표본이 모두 원본과 일치한다는 의미는 아니다.

UE 5.8 `AnimNode_Steering.cpp`는 루트 모션 속성의 회전량을 조절한다. 원본 골반의 상대 yaw를 별도로 보정하는 노드는 아니다. 이 결과는 Steering 고장이나 OTM 전체의 이상을 입증하지 않는다. OTM의 곡선 이동에 맞는 원본 골반 자세가 전방 Strafe의 시선/이동 관계에서는 과하게 남는 경우를 제한적으로 다룬다.

## 평가 구조

`기존 Aim/OffsetRoot/FootPlacement/LegIK → Pose History → Project J Strafe Lower Body Facing → 기존 Upper Body Facing → OnFoot`.

골반 보정을 Foot Placement 앞에 넣는 방식은 적용된 골반이 기존 Pose History와 검색에 들어가는 영향을 만든다. 이번 노드는 History 뒤의 표시 포즈만 처리한다. 새 전신 Local/Component 변환 쌍·별도 검색·Tick·네트워크 메시지를 추가하지 않는다. 기존 Steering·OffsetRoot·PSD/PSS·Chooser·검색 비용·단발 복귀 정책을 수정하지 않는다.

현재 평가 포즈에서 기준 회전을 제거한 골반의 실제 전방을 구한다. 전방 Strafe Run에서만 실제 속도 방향과의 과도한 yaw 차이를 줄인다. 10도는 원본 보행 자세로 허용하며 기본 최대 보정은 25도, 반감기는 0.08초, 속도 한도는 180도·초다. 시선을 고정된 골반 목표로 사용하지 않는다. 큰 루트 추종 오차에서는 기존 감쇠 정책으로 보정량을 낮춘다.

골반 회전 후 양 다리를 기존 최종 양발 컴포넌트 변환에 맞춰 analytic Two Bone IK로 다시 푼다. 발의 위치·회전·스케일 및 그 아래 발가락 포즈는 유지한다. 원본 무릎 굽힘 방향을 회전한 위치를 관절 목표로 사용하고 다리 길이를 늘리지 않는다. 발에 도달할 수 없으면 골반 보정을 먼저 줄인다. 보정 전체를 만들 수 없으면 원본 포즈를 통과시킨다. 양 다리 계산을 모두 성공한 뒤에만 포즈를 수정한다.

척추 시작 로컬 회전을 반대로 보정해 상체 방향을 유지한다. 척추 이후의 기존 Upper Body Facing이 시선 보정을 담당한다. 척추 시작 위치는 골반을 자연스럽게 따라가므로 손·무기의 세계 위치까지 동일하다고 주장하지 않는다. 움직이는 발의 경로와 발끝 방향까지 새 방향으로 재작성하는 기능은 아니다.

## 소유권과 확장

- 로컬 primary 메시의 Combat·접지·OnFoot·이동 입력·Run Cycle·전방 Strafe가 대상이다.
- 제자리 회전, 이동 Turn 소유권 및 Turn/TurnInPlace Phase에서는 잔여 보정도 적용하지 않는다.
- 몽타주가 하나라도 재생 중이거나 공격·회피·피격·Root Motion·MM Interaction·Start/Land 등 단발이면 원본 포즈를 통과한다.
- OTM·Walk·Sprint·정지·공중·다른 이동 모드·원격·NPC·follower는 제외한다. Walk/Sprint 확대는 각 데이터와 접지의 별도 검증 이후에 한다.
- A/S/D의 Run Cycle은 새 보정을 요구하지 않고 기존 보정만 풀어 준다. 다른 소유권으로 넘어가면 즉시 통과한다.
- 전방 여부는 실제 held local input의 X/Y 각도도 검사한다. 빠른 카메라 회전으로 AimYaw가 제한될 때 Actor+AimYaw만으로 A/S/D를 전방으로 오인하지 않는다. 입력이 없거나 유효하지 않으면 잔여 보정도 적용하지 않는다.
- Foot Placement 또는 Leg IK의 알파가 0.99 미만이면 적용하지 않는다. 전투 full-body locomotion이 별도로 하체를 소유하는 정책을 존중한다.

CombatAnimProfile의 `StrafeLowerBodyFacing`이 강도와 Master 구동 스켈레톤의 root/pelvis/spine 및 좌우 thigh/calf/foot 역할을 설정한다. follower 외형에 노드를 복제하지 않는다. 현재 구현은 pelvis의 자식 thigh와 spine, thigh→calf→foot의 직접적인 두 마디 체인 및 양의 균일 스케일을 요구한다. 다른 체인·누락 본·비균일 스케일·LOD 2 이상에서는 안전하게 통과한다. 역할 프로필이 임의 스켈레톤 자동 호환을 뜻하지 않는다.

GT는 프로필과 몽타주/행동 소유권을 값 스냅샷으로 발행한다. worker는 값과 평가 중인 포즈만 소비한다. 본 경로와 역할은 캐시하고 작은 배열은 inline 저장한다. 일반 평가에서는 양 다리 analytic solve 2회, 접지 도달 범위가 벗어날 때만 최대 8회 이분 탐색으로 yaw를 줄인다. 발 제약이 우선이므로 도달 가능한 보정량이 줄어드는 프레임에서는 시간 필터보다 안전한 접지 범위를 우선한다.

## 검증과 저장

`InstallLowerBodyInMasterForReview`는 Master 한 개의 History→Upper Body Facing 연결에만 임시 노드를 삽입한다. 중복 삽입을 막고 컴파일 실패 시 원래 연결을 복구한다. 패키지는 저장하지 않는다.

검증 증거는 `Saved/Validation/StrafeLowerBody_20261011/`에 기록한다. `p.ProjectJ.StrafeLowerBodyFacing 0/1`은 하체 OFF/ON 비교이며 상체 CVar와 독립적이다. `p.ProjectJ.LocomotionContinuityTrace 3`의 `Stage=LowerBodyFacing`에는 입력 골반 방향, 목표/적용 yaw 및 도달 가능 비율을 기록한다. 검사 후 Trace를 0으로 끈다.

검증 결과 확인 후 사용자의 “Master 하체 연결·저장 승인”에 따라 Master 한 개에 하체 노드를 연결해 저장했다. 저장 범위와 새 프로세스 재로드 결과는 아래에 기록한다.

### 저장 전 완료한 검증

| 검사 | 결과 |
| --- | --- |
| 최종 직접 UBT Editor / Game Development 빌드 | 둘 다 성공: EditorBuildFinal.txt / GameBuildFinal.txt |
| 소유권·발 제약 및 기존 상체 정책 | 성공. 제자리/이동 Turn, 단발, 몽타주/RootMotion 소유권, 행동, OTM, Sprint, 비대상 메시·IK 정책 차단 포함 |
| 실제 CMC A/S/D·W·OTM·큰 회전 복귀 | RegressionOn 23장면 완료, 기존 Turn/Cycle 조건 통과 |
| 회귀 묶음 | RegressionOn 8/8 성공·테스트 경고 0. 기존 단발 복귀 연결·품질 예산·숨겨진 리더/보이는 follower·복구·모듈 메시 포함 |
| W 우회전 60/90도·초 Arc 골반–속도 방향 차이 중앙값 | 20.92/24.79도 → 11.02/10.49도 |
| W 좌회전 60/90도·초 Arc 골반–속도 방향 차이 중앙값 | 32.72/29.73도 → 15.78/12.47도 |
| 좌우 W의 보존 변환 | 각 90표본의 root·좌우 FK foot·좌우 IK foot 위치/회전/스케일이 OFF/ON에서 소수점 6자리까지 동일. 가슴 방향 차이도 기록 정밀도에서 0 |
| 좌우 W의 선택/이동 | 선택 clip/time·루트·Steering 목표·CMC/예측 보고서 동일. 30도·초에서는 Arc가 선택되지 않아 Arc 개선 수치를 제시하지 않음 |
| 실제 제자리 회전·전투 몽타주·입력 해제 경계 | BoundaryOff/On 각 3/3 성공. 제자리 장면에서 TIP 169프레임, 이동 장면에서 실제 장비 전투 몽타주 159프레임 재생. 제외 87표본의 모든 로컬 본 값 해시가 동일 |
| 우회전 연속 보정 변화 | Enabled가 유지되는 연속 541쌍에서 적용 yaw 변화 최대 1.16도. 해당 120Hz 검사에서 180도·초 제한인 1.5도를 넘는 표본 없음 |
| 40Hz 카메라 입력 묶음 | PacketOn W 3장면 및 정책 3/3 성공·테스트 경고 0 |
| 전방 입력 오인 보강 후 최종 OFF/ON | 두 실행 모두 23장면·8/8 성공. A/S/D 270표본의 모든 로컬 본 값 해시 동일. 이동·선택 보고서 동일 |
| OTM 초기 발 수치의 출처 | 최종 제외 472표본 중 471개는 모든 로컬 본 값 해시 동일. 나머지는 Case17 Frame12의 왼발/왼발 IK 약 0.0461도 차이. 서로 다른 선행 월드/Trace 조건의 OFF 기준 실행에서도 동일한 변동이 있었고, 추가 같은-평가 입력 비교 120표본에서 노드 이전 발과 최종 발이 모두 동일하게 통과 |
| 최종 입력 전 발 검증을 켠 W 재현 | RightOn 3장면·정책 3/3 성공. 90표본에서 노드 입력 양발과 최종 양발을 1e-6 이내 보존했고 개선/선택 수치는 이전 ON과 일치 |

`RightOnComparison.json`, `LeftOnComparison.json`, `BoundaryOnComparison.json`과 자동화 `index.json`을 함께 확인한다. 모든 시작 로그의 오류 문구가 이번 테스트 실패를 뜻하지는 않는다. 엔진 초기화 단계의 `UnifiedErrorTest` 및 `Condition failed` 15줄은 이전 기준 로그에도 있으며, 위 테스트별 보고서의 errors/warnings는 0이다.

최초 전체 포즈 비교에서 빠른 A/S/D 21표본에 보정이 들어가는 전방 오인 조건을 발견해 실제 로컬 입력 각도 검사를 추가했다. 최초 로그/보고서는 `*.PreInputGuard.*`로 보존했다. 수정 후 270표본은 모두 동일하다. 이 보강은 하체 보정의 적용 경계이며 기존 상체 보정의 동작을 재작성하지 않았다.

OTM 첫 포즈의 비트 해시 차이를 골반 보정의 변화로 취급하지 않았다. 차이가 난 본 전체 값을 확인해 골반·루트·상체·다리의 수치는 1e-9 이내로 같고 왼발 회전만 0.0461도 차이임을 확인했다. OFF 기준 실행들의 초기화 조건에도 같은 차이가 있었으며 `ColdControlProof.json`에 조건 차이를 명시했다. 마지막으로 검증 플래그가 있을 때만 노드 입력의 양발을 기록해 같은 평가의 최종 발과 비교했다. OTM 4장면·120표본 모두 위치/회전/스케일 1e-6 이내 일치했다. 따라서 관측된 초기 발 변동은 새 보정 전에 입력 포즈에 이미 존재한다. 기존 발 보정의 초기화 변동 자체를 수정하거나 원인 전체를 해결했다는 뜻은 아니다. 검증용 입력 기록은 Shipping에 없고 일반 실행에서는 작동하지 않는다.

### 사용자 승인 후 저장 및 재로드

`SaveLowerMaster.py`는 기존 Master SHA256이 승인 전 파일과 일치하는지 확인하고 `ABP_Humanoid_Master.BeforeLowerFacing.uasset`으로 백업했다. 하체 설치·중복 설치·기존 상체 연결 검증을 성공한 뒤 `ABP_Humanoid_Master` 한 개만 저장했다. Content 변경 목록도 Master 한 개이며 PSD/PSS·프로필·다른 에셋·레벨은 저장하지 않았다. 설치 함수 자체는 패키지를 저장하지 않는다.

이후 새 프로세스에서 **상체/하체 Trial 설치 플래그 없이** 디스크의 Master를 평가했다. `Persisted` 회귀 검사 8/8 성공·경고 0이다. W 30/60/90도·초의 이동·선택 보고서는 미저장 `RightOn` 검증과 동일했고, 최종 루트·골반·척추·가슴 방향 및 clip/time 544표본도 일치했다. 하체 노드 진단 544표본에서 실제 보정 최대 23.32도를 확인했다. 재로드 검사 이후 Master 파일 해시도 저장 직후와 같다. 저장 증거는 `MasterSaved.json/SaveMaster.log`, 비교 증거는 `PersistedVerified.json/VerifyPersisted.py`다.

에디터에서는 Combat Strafe W + 느린 좌우 마우스 회전을 우선 비교한다. `p.ProjectJ.StrafeLowerBodyFacing 0`과 `1`로 하체만 OFF/ON 전환하며 기존 상체 설정은 유지한다. A/S/D 이동·제자리 회전·큰 Turn·전투 몽타주·OTM도 확인한다. 필요할 때 `p.ProjectJ.LocomotionContinuityTrace 3`으로 로그를 남기고 검사 후 `0`으로 끈다.

NullRHI 검증은 실제 CMC·MM·본 포즈에 대한 결과다. 보이는 follower 리타깃 후 무릎·의상·무기 및 체감은 PIE 확인이 필요하다. 발을 보존했기 때문에 원본 발 자체의 진행 방향까지 재작성했다고 해석하지 않는다. 향후 더 큰 보정을 원할 경우에는 데이터의 발 디딤 경로와 발 회전까지 함께 검증해야 한다.
