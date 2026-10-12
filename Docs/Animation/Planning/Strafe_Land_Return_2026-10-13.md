# Strafe Land 복귀 연속성 보완 — 2026-10-13

## 재현과 확인한 범위

사용자는 Strafe Run 착지 이후 약한 마우스 회전에서 순간 튐을 보고했다. 새 기록은 01:12에 종료된 로그이며 `Saved/Validation/StrafeLandPop_20261013/NewUser/User.log`에 보존했다. SHA256은 `9C6DAD5F6C9CBB53F1FE11C5A349F7777C8414253D253DD0839FE2930B3678A0`이다. 이전 00:26 기록과 구분한다.

지정한 초반 구간은 Frame 1530~1581이다. Land가 보이는 첫 발행부터 복귀까지 약 0.425초이며, 복귀 이유는 `ContinuousCurveToCycle`이다. 기록된 곡선 증거는 이동 175cm·방향 변화 17.3도·일관성 100%였다. 작은 입력 한 번이 보호 조건을 우회하는 즉시 취소로 판단하지 않는다. 첫 MM 선택은 `PSD_Combat_Run_Cycle / M_Neutral_Run_Arc_Tight_R @ 0.433`이며 이후 같은 클립의 시간이 진행했다.

이 첫 구간의 루트 방향 변화는 프레임당 약 0.75도이고, 복귀 직후 가슴의 수평 방향 변화는 최대 약 5.79도였다. 뒤쪽 왼발 착지 구간에는 약 15.93도의 수평 방향 변화도 있었다. 수평 투영 방향은 숙인 자세에서 민감할 수 있으므로 실제 3D 회전량과 동일하게 해석하지 않는다. 사용자 로그에는 복귀 직전 Land의 모든 가슴 쿼터니언이 들어 있지 않으므로 한 프레임의 전체 포즈 점프 크기를 그 로그만으로 확정하지 않는다.

## 수정 구조

### 재생 소유권과 후보 선택을 같은 시점에 복귀

기존 AnimInstance는 소비된 Land의 스냅샷을 Cycle로 바꾸지만, 컴포넌트의 후보 자격은 물리 착지 이벤트가 남아 있는 `DerivedLocomotionContext.PhaseFamily=Landing`을 계속 사용했다. 따라서 MM이 이미 재생을 맡아도 전방 Loop+곡선 풀의 `ForwardCycle`은 닫혀 있었다.

Primary AnimInstance가 실제 Land 재생을 해제한 경우 그 착지 revision을 GT 컴포넌트에 알린다. 컴포넌트는 현재 착지와 같은 revision만 받아 검색에 사용할 presentation phase를 현재 지상 의도의 Cycle/Idle로 해석한다. 기존 방향·속도·정렬·급회전·행동·optional database 조건으로 전방 풀을 다시 판단한다. 첫 복귀 검색에도 결과를 전달하며, 이후 컴포넌트 tick에서도 같은 판단을 유지한다.

이 알림은 실제 착지, impact revision, 최소 보호 시간, 착지 종료 타이머, 이동/회전/네트워크 값을 변경하지 않는다. 원격 입력에는 이 로컬 알림을 적용하지 않는다. 새 착지와 착지 종료 때 소비 표시를 초기화해 이전 착지의 해제가 새 착지로 누출되지 않게 한다. 알림 처리에서는 저장된 곡선 증거를 재사용하여 같은 프레임에 물리 관측이나 settle 시간을 두 번 진행하지 않는다.

### 회복 동작 중 이탈은 기존 관성 보간 경로 사용

회복 자세가 남은 Land를 MM 포즈와 바로 교차 혼합하면 두 자세의 차이가 복귀 초반의 포즈 속도를 키울 수 있다. 프로필의 `MotionMatchingSearchPolicy.bUseInertialLandingInterruptReturn`을 기본 활성화했다. Land가 아직 살아 있고 기존 `bTransitionAnimationAlmostComplete`가 거짓인 복귀에서는 공통 노드가 이미 가진 관성 보간 경로를 선택한다. 평가한 이전 포즈와 속도를 이어 주는 기존 downstream Inertialization 노드를 사용한다.

완료에 가까운 Land tail 및 Start/Stop의 정상 복귀는 기존 outgoing 샘플 재생을 유지한다. 새 블렌드 노드를 추가하거나 특정 Arc/Loop/Box 클립을 강제하지 않는다. 복귀 시간은 기존 에셋/프로필의 값을 사용한다. 이 정책을 끄면 기존 Land 교차 혼합 방식으로 비교할 수 있으며, 후보 소유권 보완은 별도다.

최종 곡선 종료 조건은 **0.35초·100cm·3도·일관성 85%**로 유지한다. PSS/PSD, Master ABP, CMC 및 착지 에셋을 저장하거나 수정하지 않는다. 새 프로필 필드는 C++ 기본값을 사용하며, 기존 프로필 파일의 해시는 유지한다. State Controller와 Chooser도 계속 사용한다.

## 검증과 한계

실제 BP·CMC·Master ABP를 사용하는 재생에 카메라 시점과 3D 가슴 변위를 추가했다. 컨트롤러의 Pitch만 설정하면 headless fixture의 `GetPlayerViewPoint`는 pawn 회전을 반환해 Aim Pitch가 0에 남았으므로, 명시적인 카메라 재현 옵션에서 엔진의 공개 카메라 캐시 API도 갱신한다. 카메라 각도·곡선 회전율·점프 시점을 지정하는 옵션은 Editor/dev 재현에만 사용한다.

`ReturnLive1`과 `PolicyChecked`의 착지 진입부터 복귀 직전까지 가슴/공통 재생 스택의 기록 74개 행은 모두 같았다. 두 재생의 Land 진입은 Frame 303, 복귀는 Frame 345였다. 120Hz·85도/초의 해당 장면에서 복귀 첫 프레임의 컴포넌트 공간 가슴 3D 변위는 **4.5453→3.1815도**다. 이 값은 초기 포즈 변위 비교이며 전체 구간의 최고 변위, 발 미끄러짐, 200명 성능 또는 사용자 체감 승인으로 사용하지 않는다. 같은 장면의 후보 소유권 수정만으로는 첫 변위가 줄지 않았으며 그 사실도 기록한다.

`PolicyChecked`는 5개 leaf 성공·오류/경고 0개다. 첫 복귀에 ForwardCycle을 사용하고, 실제 엔진 Inertialization이 0.2초로 활성화된 것을 debug traversal에서 확인했다. 현재 착지의 revision 수락·오래된 알림 거부·물리 착지 보존·보호 시간 보존·연속 착지 초기화도 검사한다. 회전 방향·점프 전 이력을 바꾼 진단에서는 착지 foot/진입 시점이 다른 경우가 있어 이를 동일 초기 조건의 비교로 합산하지 않는다.

임시 착지 CVar 전환용 코드는 제거했다. 처음의 `SetCameraCacheTime`, 외부 `GetProxyOnGameThread` 접근은 보호 멤버 컴파일 오류가 있었으며 공개 API로 수정했다. 실패한 빌드 후 이전/부분 갱신 바이너리로 실행된 `PolicyFinal`은 최종 검증에서 제외한다. 빌드와 실패 원자료는 보존한다.

이 변경은 확인한 후보 상태 불일치와 착지 회복 중 복귀 방식을 보완한다. 사용자가 지정한 실제 화면의 튐이 완전히 사라졌는지는 같은 초반 조건의 에디터 체감 확인이 필요하다. 앞 커밋에서 남은 `RecordedStrafeCurve`의 Box/Diamond 1% 기준 실패는 별도 제한이며 이 수정으로 해결 완료라고 표시하지 않는다.

곡선 기록의 후보 구성 후속은 [전방 이동 의도별 PSD 구성](Strafe_Forward_Intent_2026-10-13.md)에 기록한다. 아래 실패 수치는 착지 수정 단독 검증 시점의 결과다.

## 최종 확인

`EditorFinalBuild.log` 및 `GameFinalBuild.log`의 직접 UBT Win64 Development 빌드는 성공했다. 관련 회귀는 70개 leaf 중 68개 성공·2개 실패였다. 미세 입력의 실패는 보고서 저장뿐이며, 새 경로의 `MicroChecked` 재실행에서는 저장과 모든 동작 검사가 통과했다(Loop 732/733, Box/Diamond 0). 두 실행에서 성공을 확인한 고유 검사는 **69개**, 남은 실패는 기존 곡선 기록의 Box/Diamond 1% 기준 하나다. 최초 실패 보고서는 보존하며 전체 검사가 한 번에 통과했다고 표현하지 않는다.

OTM/Strafe Run/Sprint의 단발 입력 16장면, 일반 복귀 8장면, 방향별 복귀 16장면, 느린 곡선/미세 입력 12장면 및 상태 수명·모드·포즈 이력·검색·숨김 복귀·병렬 snapshot/군중 정책을 검사했다. 기존 C++ 전투 fixture의 MissingPalm 호환 소켓 경고는 5개 leaf에 발생했다. 실제 생산 BP의 오류로 합산하지 않는다.

`WeakRendered`의 120Hz Strafe Run 착지 후 5도/초 회전은 D3D12 렌더 경로에서도 통과했다. 첫 곡선 복귀가 ForwardCycle의 Loop를 선택했고 관성 보간이 실제 활성화됐다. 15개 캡처를 저장하고 복귀 첫 프레임 및 0.05초 뒤의 이미지도 확인했다. 이 단일 장면을 사용자 화면의 전체 튐 해결이나 전체 렌더 품질 승인으로 표현하지 않는다. 근거는 `NewUser/Regression`, `MicroChecked`, `WeakRendered` 및 빌드 로그에 남겼다.

Master ABP와 기본 프로필의 해시는 각각 `701E189BE5AAFF7D5D9C4D451CBC3F64BDFA2F8266E936B40936D5D9694CA91A`, `AA4E2E350A679A328C6E3D291577D07406F8E9EA3F03B4D4F1A03E1B559A59BF`로 유지한다. 이번 실행에서 200명 성능이나 Cook/패키징을 새로 검증한 것으로 표현하지 않는다.
