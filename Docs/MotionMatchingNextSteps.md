# Motion Matching Next Steps

Project J의 locomotion motion matching은 현재 notify/modifier 의존을 제거하고, C++ context와 timed phase 정책 중심으로 정리된 상태다.
이 문서는 프로젝트 초반이라는 점을 기준으로 다음에 진행할 작업을 우선순위별로 정리한다.

## Current Baseline

- Locomotion phase 선택은 `UProject_JLocomotionAnimStateComponent`의 C++ 상태와 derived context를 기준으로 판단한다.
- `UProject_JMotionMatchingAssetSet`은 Run, Sprint, Start, RemoteStart, Stop, TurnRedirect, JumpStart, FallOffStart, JumpAir, Landing PSD를 역할별로 분리한다.
- Landing은 단순 sprint key intent가 아니라 착지 시점의 이동 상태를 기준으로 Stand, Run, Sprint PSD를 고른다.
- AnimInstance는 Game Thread에서 thread-safe snapshot을 만들고, proxy를 통해 animation evaluation 쪽으로 전달한다.
- Significance 기반 tick interval과 motion matching update interval 기반이 들어가 있다.
- 메인 캐릭터 mesh는 visibility/update-rate optimization 경로를 사용한다.

## Near Term

1. PSD 데이터 정리

   각 PSD가 한 역할만 갖도록 유지한다.
   `Cycle`에는 loop/locomotion 계열, `Start`에는 local start 계열, `RemoteStart`에는 remote proxy에서 자연스러운 reface/start 계열, `Stop`에는 stop 계열, `TurnRedirect`에는 이동 중 turn redirect 계열만 둔다.

2. Blueprint override 확인

   C++ 기본값을 바꿔도 캐릭터 Blueprint나 component instance에서 override된 값이 있으면 에디터 값이 우선된다.
   특히 아래 값은 플레이 테스트 전에 확인한다.

   - `StartMinDuration`
   - `StartMaxDuration`
   - `StopMinDuration`
   - `StopFallbackDuration`
   - `StandLandingRequestDuration`
   - `LandingRequestDuration`
   - `MidMotionMatchingUpdateInterval`
   - `FarMotionMatchingUpdateInterval`

3. Network PIE 회귀 테스트

   Listen Server와 Client 양쪽에서 remote start, stop 중 회전, jump, falloff, landing, chooser 선택을 반복 확인한다.
   Shift만 누른 stand landing이 sprint landing PSD로 가지 않는지도 같이 체크한다.

4. 깨진 notify/track 에셋 정리

   C++ locomotion notify와 animation modifier는 제거되었다.
   애니메이션 에셋에 남은 notify track이나 삭제된 notify class reference는 에디터에서 정리한다.

## Optimization Track

1. Significance tuning

   현재 significance tier는 거리 기준으로 Near, Mid, Far, Hidden 성격을 가진다.
   실제 맵 크기와 카메라 시야를 기준으로 distance와 tick interval을 조정한다.

   - Near: local quality 유지
   - Mid: motion matching re-evaluation 간격 완화
   - Far: far chooser row 또는 낮은 빈도 PSD 갱신
   - Hidden: animation-only data update throttle

2. Motion matching LOD 정책 세분화

   지금은 PSD family와 update interval 기반이 있다.
   이후 캐릭터 수가 늘면 tier별 정책을 더 명시적으로 나눈다.

   - Near: full motion matching
   - Mid: full PSD 유지, update interval만 완화
   - Far: far row 또는 cycle 중심 PSD 사용
   - Hidden: 최근 state 유지 또는 저빈도 갱신

3. Profiling 기반 결정

   직접 multithread/task system을 추가하기 전에 Unreal Insights로 병목을 확인한다.
   우선 확인할 항목은 AnimInstance update, PoseSearch, Motion Matching node, CharacterMovement, skeletal mesh evaluation이다.

## Later

1. LocomotionProfile 고도화

   현재 duration과 optimization 값 일부는 C++ component fallback에 있다.
   캐릭터 종류가 늘어나면 start/stop/landing duration도 `UProject_JLocomotionProfile`로 옮겨 직업, 체형, 무기별 튜닝을 쉽게 만든다.

2. Distance-based animation quality policy

   플레이어, 파티원, 적, 군중 NPC에 서로 다른 animation quality policy를 부여한다.
   같은 거리라도 전투 중요도나 화면 중심 여부에 따라 tier를 보정할 수 있다.

3. NPC용 저비용 locomotion path

   대규모 NPC가 필요해지면 player-grade motion matching을 그대로 쓰지 않는다.
   원거리 NPC는 sequence, blendspace, cached pose, reduced update rate 같은 별도 path를 고려한다.

4. Mass/Crowd 시스템 검토

   수십 명 수준에서는 현재 구조와 significance tuning으로 충분할 가능성이 높다.
   수백 명 단위 crowd나 MMO field simulation이 필요해질 때 MassEntity, crowd LOD, server-side simulation 분리를 검토한다.

## Do Not Do Yet

- UObject, CharacterMovement, AnimInstance 접근을 임의 worker thread로 옮기지 않는다.
- Motion matching PSD 선택을 여러 시스템에 흩뿌리지 않는다.
- 데이터가 안정되기 전에 복잡한 server handoff, Mass crowd, custom animation worker를 먼저 만들지 않는다.
- 현재 단계에서는 노티파이 기반 locomotion 완료 구조로 되돌아가지 않는다.
