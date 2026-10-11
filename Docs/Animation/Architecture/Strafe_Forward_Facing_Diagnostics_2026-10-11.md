# Strafe 전진 Arc 몸통 방향 진단

## 목적과 범위

W를 누른 채 마우스로 천천히 회전할 때 Arc의 몸통이 진행 방향보다 앞을 바라보는 느낌을 조사한다. 이번 변경은 진단이며 선택 조건, yaw smoothing, Steering, Aim Offset, PSS/PSD와 그래프를 수정하지 않는다. 에셋·레벨 저장도 하지 않는다.

기존 ContinuityTrace에 선택·예측·블렌드 정보는 있지만 최종 골반·몸통 방향은 없었다. 새 `Facing` 행으로 이를 같은 평가 프레임에 연결한다. 기본값은 계속 0이며 Shipping에는 진단 본문이 포함되지 않는다.

## PIE 재현

```text
p.ProjectJ.LocomotionContinuityTrace 3
```

Strafe Run에서 W를 계속 누르고 천천히 좌우 회전한다. 직진 2초 → 느린 오른쪽 원 이동 5초 → 직진 2초 → 느린 왼쪽 원 이동 5초 순서로 비교한다. 카메라 높이는 유지한다. Arc가 어색했던 순간을 포함하면 된다. 테스트 후 끈다.

```text
p.ProjectJ.LocomotionContinuityTrace 0
```

사용자 로그는 `Saved/Logs/Project_J.log`에 기록된다. 자동 검증은 별도 `-abslog` 경로를 사용해 이 로그를 덮어쓰지 않는다. 모드 2는 기존 이벤트·10Hz 상세와 Facing을 기록한다. 모드 3은 전진 방향 30도 이내의 접지 Strafe Cycle/Turn, 입력 유지, 속도 100 초과, 외부 단발 override 없음인 구간에서 Facing만 매 평가 프레임 추가한다. 로그를 켠 실행의 성능은 프레임 품질 판정 근거로 삼지 않는다.

## 기록과 해석

- `Stage=Facing`: 실제 Actor/카메라/요청 이동/속도 방향, 물리 축으로 환산한 Steering 목표, raw/clamped 카메라 회전율, Aim yaw/pitch/alpha, 최종 root/pelvis/spine_01/chest 방향. 선택 대표 clip/PSD/time 및 snapshot/result frame도 함께 기록한다.
- `Stage=FacingPlayer`: 기여 중인 MM 내부 플레이어 최대 4개의 실제 시간·상대 블렌드 가중치·mirror/table·loop, 0.1초 원본 이동·회전과 Steering AnimatedTime 구간 원본 루트 회전. 기존 상세 주기로만 추출하며 mirror를 반영한다. BlendInWeight는 각 플레이어의 상대 블렌드 값이지 정규화한 최종 기여도 합계가 아니다.
- 기존 `Selection`, `NodeSettings`, `MMPlayer`, `Prediction`, `Branch` 행으로 클립 교체·블렌드 중첩·미래 목표와 함께 판단한다.

골반·척추의 로컬 기준 축은 다르므로 단순 bone Rotator.Yaw를 비교하지 않는다. 각 본의 컴포넌트 기준 포즈 회전을 제거하고 캐릭터의 중립 전방 축을 최종 포즈로 회전해 XY 평면 heading을 계산한다. root/pelvis/spine/chest 각각 Valid를 기록하며 무효일 때 yaw 0은 측정값으로 사용하지 않는다. chest는 spine_05를 우선하고 없으면 spine_03을 사용한다. `ChestToRoot/Camera/Velocity`는 각 기준 yaw에서 chest까지의 부호 있는 차이다. 일반적인 자연스러움을 위해 이 차이가 항상 0이어야 한다고 가정하지 않는다.

NativePostEvaluate 시점의 **평가 완료·버퍼 교체 전 컴포넌트 포즈**를 읽는다. 소켓의 이전 프레임 read buffer를 혼용하거나 강제 pose 평가·동기화를 하지 않는다. 물리 처리 이후 화면 포즈나 Aim 전후 각 단계별 차이까지 직접 측정한 것은 아니다. AimAlpha와 최종 몸통 오차의 상관관계만으로 Aim이 원인이라고 확정하지 않는다.

## 자동 재현

기존 `ProjectJ.Animation.Naturalness.StrafeCameraCMC`에 개발 전용 `-ProjectJStrafeSlowForwardCamera`를 추가했다. 실제 CMC·충돌 바닥·설치된 MM/ABP로 W 30/60/90도/초의 세 장면을 재생한다. 각 장면은 직진 1초, 회전 2초이며 120Hz world tick이다. 기존 회귀 실행의 장면·속도는 이 플래그가 없으면 유지된다. `-ProjectJStrafePacketCamera`를 함께 쓰면 40Hz 카메라 입력 묶음을 비교할 수 있다. 데이터 비교용 임시 비용·스키마 변경 플래그는 사용하지 않는다.

NullRHI 자동 재현은 선택·최종 평가 본 방향 및 진단의 관찰 전용 여부를 확인한다. 사용자 화면의 체감, 렌더링·물리 이후 포즈, 원격 마우스의 정확한 패킷 간격은 사용자 PIE 로그와 함께 확인한다.

## 이번 검증 결과

증거는 `Saved/Validation/StrafeFacing_20261011/`에 둔다. 설치된 Strafe Cycle 비용 -0.03을 그대로 사용했다. `ContinuousOff`, `ContinuousOn`, `PacketOn` 세 실행은 각각 자동화 1/1 성공이며 각 실행에서 W 30/60/90도/초 장면의 240개 측정 프레임이 모두 접지·이동 조건을 만족했다. 로그 off/on의 전체 Playback.txt 내용은 동일했다. 연속 입력의 Arc 선택 프레임은 각각 0/189/203이었다.

Facing은 연속 입력 544행, 패킷 입력 541행이며 모든 행에서 네 본이 유효하고 현재 frame/snapshot/result가 일치했다. 기존 평가 root offset으로 환산한 몸 방향과 새 root heading의 최대 차이는 출력 반올림 범위인 0.09/0.10도였다. FacingPlayer 61/274행의 원본 루트 추출도 모두 유효했다. 이번 오른쪽 회전에는 mirror 플레이어 표본이 없어 mirror 경로의 실제 재생을 검증했다고 주장하지 않는다. `InspectFacing.py`와 `FacingSummary.json`에 비교를 보관했다.

연속 60/90도/초에서 T>=1.5인 Arc 표본은 각각 181개였고, 가슴과 속도 방향 차이의 절댓값 중앙값은 25.39/26.51도, 95백분위는 44.43/42.88도였다. 같은 표본의 AimYaw는 0, AimAlpha는 1이며 Steering 목표는 카메라보다 29.50/34.25도 앞섰다. 패킷 입력에서는 목표가 카메라 부근과 약 35도 앞 사이로 달라지는 표본도 나왔다. 이 수치는 원본 Arc의 몸통 회전·최종 보정·입력 예측을 구분해 볼 단서이며, 어느 단계가 부자연스러움의 원인이라는 판정은 아니다. 최종 포즈만 읽으므로 Aim 전후/Steering 전후 기여는 아직 분리 측정하지 않았다.

Editor/Game Win64 Development 직접 UBT 빌드는 모두 통과했다 (`EditorBuildVerified.txt`, `GameBuild.txt`). 최초 링크에서 UE 5.8의 FBlendStackAnimPlayer::GetMirrorDataTable이 export되지 않아 실패한 기록은 `EditorBuild.txt`에 유지했다. 엔진을 수정하지 않고 reflected MirrorNode의 공개 const getter로 읽는 경로를 사용해 해결했다. 자동 검증의 별도 abslog 사용 후 사용자 `Saved/Logs/Project_J.log`의 크기 374,431바이트·수정 시각 14:35:27은 유지됐다.

## 15:14:53 사용자 PIE 로그

새 로그를 `User_151453.log`로 보존했다. Facing 1,663행 모두 네 본이 유효하고 frame/snapshot/result가 일치했다. FacingPlayer 395행의 원본 루트 추출도 모두 유효했다. 직진·빠른 회전·회전 감속과 방향 변경이 함께 들어 있으므로 전체 로그를 일정한 느린 원 이동이라고 해석하지 않는다.

연속 Facing 표본의 0.15~0.30초 카메라 각도 변화로 평균 회전율을 계산해 절댓값 10~90도/초인 구간을 분류했다. 이 안의 Arc 표본 116개에서 루트–속도 방향 차이 절댓값 중앙값은 1.87도(95백분위 8.28도), 가슴–속도는 22.49도(47.80도), 가슴–루트는 22.58도(41.06도)였다. AimYaw 절댓값은 중앙값 1.58도·최대 3도였다. 순간 입력은 더 빠르게 들어온 경우가 있으므로 이 분류에는 입력 묶음과 감속의 영향이 남는다. 요약·도구는 `UserFacingSummary.json`, `InspectUserFacing.py`에 보관한다.

Steering 목표의 카메라 대비 차이는 같은 Arc 표본에서 중앙값 32.73도였고 빠른 목표 변화도 관측했다. 현재 TrajectoryComponent의 raw/clamped rate와 AnimInstance가 소비한 trajectory는 같은 시각의 데이터라고 가정하지 않는다. 후자는 약 0.008초 된 trajectory를 사용한다. 현재 rate가 같다는 이유만으로 목표 변화가 잘못됐다고 판정하지 않는다. 예측과 단기 입력 변화는 별도 후보지만 최종 포즈의 루트 오차만으로 큰 가슴 오차를 설명하기 어렵다.

원본 자세 비교를 추가했다. 로컬 엔진의 AnimPoseExtensions로 실제 로그의 Player 0 상대 가중치가 0.999 이상이고 최종 선택 clip과 같은 무미러 Arc 68표본을 실제 플레이어 시간에 읽었다. BP CDO의 SKM_Quinn_Simple·mesh yaw -90을 평가 옵션에 사용해 compressed pose와 기준 포즈를 같은 방식으로 환산했다. 대상은 Arc Small/Tight L/R 네 에셋뿐이며 에셋 수정·저장은 없다. 원본 가슴–루트 각도 절댓값 중앙값 24.04도·최대 46.96도, 원본과 최종의 가슴–루트 각도 차이 절댓값 중앙값 0.40도·95백분위 1.44도였다. 최대 7.18도인 표본에서는 AimYaw 26.62도·AimPitch -7.52도도 함께 관측됐다.

따라서 조사한 표본에서는 원본 Arc의 상체 회전이 최종 포즈에도 대부분 유지되는 것이 주된 단서다. Steering은 루트 회전 속성을 보정하고 현 AimYaw는 Actor–카메라 차이이므로, 원본 Arc의 몸통 상대 회전을 자동으로 지워 주는 구조가 아니다. Strafe의 원하는 상체 방향과 원본 Arc 자세의 관계를 먼저 정리하는 것이 적절하다. 모든 몸통 오차가 부자연스럽다는 판정, 다른 에셋·mirror·블렌드 도중 표본이나 보이는 follower의 리타깃 이후 자세까지 동일하다는 판정은 하지 않는다. 현 Strafe/OTM 동작은 수정하지 않았다.

원본 읽기 결과는 `AuthoredArcFacing.json`, `AuthoredArcFacingVerified.log`, `ReadAuthoredArcFacing.py`에 보관했다. 첫 실행의 Python 클래스명 오류는 `AuthoredArcFacing.log`에 남아 있으며 실제 비교 성공은 Verified 로그의 68표본 기록으로 확인했다. 원본 확인 후에도 사용자 로그의 크기 4,021,994바이트·수정 시각 15:14:53은 유지됐다.
