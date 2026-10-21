# 데이터 기반 이동 콘텐츠 확장

이 문서는 현재 `ABP_Player_Master`/Mover 경로의 제작 기준이다. 이전 MM/CMC 가이드 원문은 [갱신 전 보존본](../../Archive/Navigation/2026-10-21/Before_Current_Documentation.zip)에 있다. 과거 R19/R20 분기·퇴역 `ABP_Player`를 새 마스터에 다시 추가하지 않는다.

## 책임부터 선택한다

| 바꾸려는 것 | 수정할 곳 |
| --- | --- |
| 이동 속도·가속·제동·facing·공중 입력 승인 | Mover의 native 이동 설정과 입력/정책 코드 |
| 상태·전환·선택 조건·노드별 Steering 등 핀 계산 | `UProject_JPlayerAnimInstance`와 native 계약 |
| 클립 후보·검색 기준·선택 출력·전환 튜닝 | 현재 Chooser·PSD·Schema·BlendSpace·프로필 |
| 포즈 합성·콜백 위치·전신 슬롯·직업 표현 | 공통 ABP와 Linked Anim Layer |
| 골격·체형·손 접촉·추가 망토 뼈 | Family/Body/HandGrip 프로필과 몸체/장비 Post Process |

[현재 구조](../Architecture/Player_Locomotion_Architecture.md)의 실제 경로를 먼저 확인한다. 공통 입력·GAS·상태 계산·이동 그래프를 직업마다 복사하지 않는다. 계산을 native로 바꿀 때에도 노드별 NodeRef·재생 시각·커브와 StateEntry/OnUpdate/BecomeRelevant 시점을 유지한다.

## 새 이동 모션 연결

1. **공통 Skeleton 확인:** 새 UE5/Manny 모션은 직접 사용한다. 다른 골격의 모션은 명시적인 IK Rig/Retargeter로 Manny 공용 라이브러리에 변환한다. 원본의 Skeleton만 재할당하지 않는다.
2. **메타데이터 유지:** 길이·프레임 속도·루트 모션·가산 base/프레임·커브·sync marker·notify payload와 시점을 확인한다. 가산 base와 BlendSpace 샘플도 Manny 자산을 참조해야 한다.
3. **생산 선택 경로 연결:** 지상은 `CHT_ProjectJ_Ground`, 공중은 `CHT_ProjectJ_Air`를 기준으로 한다. result/fallback·중첩 Chooser·내장 PSD·Branch In 자동 등록까지 실제 소비 경로를 확인한다.
4. **검색과 현재 포즈 기준 통일:** `PSS_ProjectJ_Manny_*`와 Manny source/trajectory를 사용한다. 보존 원본이나 개발용 비교 클립을 생산 Idle Branch 후보에 섞지 않는다.
5. **출력 소비 확인:** Chooser의 `Player_FastFeet*` 같은 이름 출력은 실제 Manny BlendProfile과 연결해야 한다. 같은 이름처럼 보이는 기존 프로필을 덮거나 런타임 문자열 fallback을 추가하지 않는다.
6. **실제 평가 확인:** 후보 존재·선택 성공·최종 포즈를 따로 확인한다. 미러 메타데이터만 연결했다고 mirrored 검색/재생을 켜지 않는다.

새 모션의 수입 전에는 [라이브러리·중복 검사](Player_Animation_Library.md)로 기존 결과의 재사용·revision·recipe·동명 충돌을 판별한다. 원본 뱅크는 별도 보관되어 있으므로 재제작에 필요하면 명시적으로 복원한다. 원본 UEFN 모션을 재제작할 때에는 `RTG_Player_UEFN_to_Manny`와 명시된 source/target mesh를 사용한다. 사용 중인 변환본을 batch overwrite로 강제 삭제하지 않는다. 임시 변환본에서 `UpdateRetargetedAnimationData`로 기존 타임라인과 트랙 계약을 검증하고 뼈 트랙만 갱신한다. 자세한 절차는 [스켈레톤 제작](HumanoidSkeletonRetargetingStandards.md)을 따른다.

## 유지해야 하는 동작

- OTM/Strafe를 모두 유지한다. Strafe Sprint는 전방 세 방향이며 측후방에서는 승인된 Run 정책과 후보가 일치해야 한다.
- 점프는 공중 입력·facing·gait 변경에 재선택하되 foot·물리 점프 정체성·playhead 연속성을 지킨다. 일반 추락/루프/착지 경계를 특정 점프 클립 예외로 합치지 않는다.
- 전신 스킬 뒤의 발 IK가 authored 하체 포즈를 덮지 않아야 한다. 스킬 종료·취소·blend-out에서도 갱신된 이동 포즈로 복귀한다.
- 원격 이벤트는 표현 경계이며 이동/피해 권위를 갖지 않는다. owner·simulated proxy·재초기화·월드 종료에서 오래된 상태를 되살리지 않는다.

## 새 직업·몸체·탈것

직업은 얇은 native class + 직업 BP + 전투 레이어/프로필을 사용한다. 공통 source/master는 유지하며, 다른 골격의 외형은 Family RTG와 Body/HandGrip 프로필로 연결한다. 같은 UE5 Skeleton의 몸체는 직접 재생할 수 있지만 체형별 접지·소켓·도달 범위는 별도로 검수한다. 장비의 LeaderPose/CopyPose와 추가 뼈 물리는 독립 계약이다.

`EProject_JAnimationLocomotionMode`는 큰 전신 문맥을 구분한다. 종·모델·코스메틱 이름을 enum으로 늘리지 않는다. 탑승은 `UProject_JMountedAnimationLayerComponent`, Rider Profile과 공통 mounted layer를 사용한다. enum에 수영·차량·변신 값이 있다는 사실은 해당 모드의 이동·입력·게임플레이 구현 완료를 뜻하지 않는다.

새 전신 문맥은 서버 전이 조건, possession·입력·카메라·이동·충돌 소유자, 취소/사망/탈것 파괴/맵 이동과 해제 경로를 먼저 정한다. 실제 계약이 다른 경우에만 새 문맥을 추가하고, 같은 문맥 안의 포즈/외형 차이는 데이터와 레이어로 표현한다.

## 검증과 정리

```powershell
./Scripts/Validation/Run-PlayerLocomotionRegression.ps1 -RunName <새로운_실행명>
```

C++ 변경은 direct UBT로 빌드하고, 관련 프로세스가 실행 중이면 겹치지 않는다. 변환 모션은 `Validate-MannyRetargetedKinematics.py`의 authored-frame helper 추종과 대표 주기 검사를 수행한다. 실제 입력 검사는 OTM/Strafe·Start/Stop·Sprint 급회전·공중/착지, 전신 몽타주를 확인한다. 새 몸체는 IK Goal 실제 도달·관절 방향·손 접촉·추가 뼈/LOD·원격 역할을 추가한다. 모든 콘텐츠 변경에 전체 Cook/성능 실험을 요구하는 규칙은 아니며 변경 위험에 맞춘다.

교체 후에는 옛 호출·이중 writer·soft reference·제작/테스트 소비·취소/해제까지 확인한다. 미참조라는 이유만으로 원본·비활성 제작 자산을 제거하지 않는다. 임시 변환본은 Branch In 등록과 참조 해제를 확인한 뒤 정리하고, 로그·원본 백업·최종 결과와 재생성 캐시를 구분한다. [변경 마감 기준](../../Maintenance/Change_Completion_Policy.md)을 따른다.
