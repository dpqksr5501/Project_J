# Master Strafe 상체 방향 보정

> **이전 이동 구조의 기록 — 2026-10-21:** 본문은 당시 CMC/MM·마스터·후보 정책의 구현·제안·측정 이력이다. 현재 생산 제작은 [플레이어 로코모션](Player_Locomotion_Architecture.md)을 따른다. 과거 구조를 복원하라는 지시나 현재 구조의 성능 결과로 해석하지 않는다.

## 문제와 책임

전방 Strafe에서 느린 카메라 회전으로 선택된 Arc의 원본 몸통 회전이 최종 포즈에 남았다. 기존 AimYaw는 Actor–시선 차이이고 Steering은 루트 회전 보정이므로, 둘만으로 원본 가슴–루트 회전을 처리하지 못한다. 조사 근거는 [진단 문서](Strafe_Forward_Facing_Diagnostics_2026-10-11.md)에 있다.

새 노드는 표시 포즈의 과도한 가슴 yaw만 척추 체인에 분산한다. 작은 원본 회전은 허용하고 회전 속도를 제한한다. 선택 비용·PSD·PSS·트래젝터리·루트 모션·이동·네트워크 권한은 이 노드의 책임이 아니다.

## 그래프와 캐릭터 확장

상체 노드 최초 저장은 `Pose History → Project J Strafe Upper Body Facing → OnFoot BlendListByEnum`이었다. 이후 승인된 [하체 보정](Strafe_Lower_Body_Facing_2026-10-11.md)을 추가해 현재 연결은 `Pose History → Project J Strafe Lower Body Facing → Project J Strafe Upper Body Facing → OnFoot BlendListByEnum`이다. 기존 Aim·OffsetRoot·FootPlacement·LegIK 이후이며, 두 보정 전 포즈가 Pose History에 저장된다. 보정 결과를 다음 검색으로 다시 먹이는 피드백을 만들지 않는다. 로컬 포즈 입출력이라 새 전신 Local/Component 변환 쌍은 필요 없다.

Master를 평가하는 주 메시에서 한 번 처리하고 기존 follower 리타깃이 그 포즈를 소비한다. 외형별 ABP에 같은 노드를 복제하지 않는다. 서로 다른 기준 스켈레톤을 직접 구동할 경우 CombatAnimProfile의 `RootBone/SpineRootBone/ChestBone` 역할을 해당 기준 리그에 맞춘다. 이는 follower 외형의 뼈 이름이 아니다. 대상 외형의 기존 리타깃 체인·기준 포즈는 올바르게 설정돼 있어야 하며 임의의 스켈레톤에 자동 호환된다는 의미는 아니다.

프로필은 활성화·허용 각도·최대 보정·반감기·회전 속도·전방 범위·최저 속도·루트 추종 시 감쇠를 제공한다. 기본값은 작은 원본 yaw 8도 허용, 최대 보정 40도, 반감기 0.04초, 최대 360도/초다. 에셋 이름에 따른 분기나 캐릭터별 코드 수정은 없다.

2026-10-13 느린 착지 복귀 후속에서는 가슴의 단일 전방 벡터를 수평 투영하던 측정을 기준 포즈로 보정한 회전의 수직축 twist로 교체했다. 깊이 숙인 가슴의 전방 벡터가 수직을 지나며 뒤집히는 것을 큰 yaw 오차로 처리하지 않는다. 보정 강도에는 현재 가슴의 upright 정도 `clamp(dot(DeltaRotation * WorldUp, WorldUp), 0, 1)`를 곱한다. 숙인 회복 포즈에는 적게 개입하고 몸이 서면 기존 보정으로 돌아온다. 기존 반감기·최대 회전율과 실행 경계를 유지하며, 특정 Land/Arc 이름·키·종료 타이머 분기는 추가하지 않는다. [재현·동일 장면 비교](../Planning/Strafe_Land_Return_2026-10-13.md#0926-느린-착지-회전-후속)

## 실행 경계

- 로컬 플레이어의 primary 메시, Combat·OnFoot·접지·전방 Strafe Cycle·유효 Aim에서 작동한다.
- A/S/D로 바뀌면 새 보정을 요구하지 않고 남은 보정을 0으로 풀어 준다.
- Turn 소유/Phase로 진입하면 새로운 목표 보정은 중단한다. 이미 적용된 보정만 제한된 속도로 0에 수렴시켜 보정 층 자체의 급격한 제거를 피한다.
- OTM·공중·Start/Land 등 단발·공격/회피/피격·몽타주·RootMotion·MM Interaction은 원본 포즈를 통과시킨다.
- 루트가 시선 방향을 따라잡는 중에는 25~45도 오차 범위에서 보정을 감쇠해 상체가 먼저 과도하게 비틀리지 않게 한다.
- 재관련·텔레포트·LOD/뼈 캐시 변경 때 누적 상태를 초기화한다. 필요한 뼈 또는 연속 체인이 없으면 안전하게 통과한다.

현재 대상은 로컬 플레이어다. 원격 플레이어·NPC·follower에는 이 추가 보정을 적용하지 않으며, 기존 애니메이션 경로와 예산 정책이 동작한다. 원격에서도 동일한 상체 방향이 필요하면 기존 복제 Aim/의도와 근거리 중요도 정책을 사용하는 별도 확장이 필요하다. 이번 작업만으로 네트워크 동기화나 대규모 MMORPG 부하 검증이 완료됐다고 주장하지 않는다.

## 비용과 스레드

Game Thread가 프로필·소유권·행동 상태를 값 스냅샷으로 발행한다. 애니메이션 작업 스레드는 해당 스냅샷과 평가 중인 포즈만 사용하며 캐릭터/프로필을 따라가지 않는다. Evaluation에서 UObject 조회·포즈 검색·RPC·별도 Tick·메시 재평가는 없다.

비대상은 Source 포즈를 그대로 전달하며 뼈 좌표 계산을 생략한다. 기본 LODThreshold는 1이고 그보다 낮은 품질 LOD는 보정을 생략한다. 뼈 경로는 필요할 때 캐시하고 실제 평가에서는 root→chest의 짧은 경로만 합성한다. 척추 로컬 회전만 수정하며 root/pelvis/다리와 모든 로컬 이동·스케일을 쓰지 않는다. 기울기 축을 카메라 yaw로 덮어쓰지 않으며 팔·무기는 기존 로컬 포즈로 상체를 따라간다.

## 설치·검증

`Project_JStrafeUpperBodyFacingEditorLibrary.InstallInMasterForReview`는 정확히 Master 한 개의 PoseHistory→OnFoot 연결만 대상으로 한다. 중복 설치를 방지하고 컴파일 실패 시 연결을 복구한다. 패키지를 저장하는 API는 호출하지 않는다.

`-ProjectJStrafeUpperBodyFacingTrial`은 CMC 자동 재현에 이 미저장 연결을 적용한다. `-ProjectJStrafeSlowForwardCamera`는 W 30/60/90도/초, `-ProjectJStrafeSlowForwardCameraLeft`는 같은 속도의 좌회전, `-ProjectJStrafePacketCamera`는 40Hz 카메라 입력 묶음이다. Trial 보고서는 척추 외 모든 로컬 본 및 양발 컴포넌트 포즈 해시를 기록해 ON/OFF 보존 여부를 검사한다.

콘솔 `p.ProjectJ.StrafeUpperBodyFacing 0/1`로 비교한다. `p.ProjectJ.LocomotionContinuityTrace 3`에는 최종 Facing과 `Stage=UpperBodyFacing`의 입력 가슴 방향·오차·목표·실제 보정·루트 감쇠를 기록한다. 검사 후 Trace를 0으로 끈다. 진단 활성 실행으로 실제 프레임 성능을 판정하지 않는다.

2026-10-13 이후 `UpperBodyFacing.UprightWeight`도 기록한다. Trace 3은 Strafe Land 소유 구간까지 포함하며, `TorsoRotation`의 평가된 가슴 컴포넌트 공간 쿼터니언과 실제 `Inertialization` 상태를 남긴다. Land 중 `Facing.Clip`은 마지막 MM 검색 결과일 수 있으므로 현재 소유한 클립은 `TorsoRotation.ExternalClip` 또는 기존 `ExternalPlayer`와 함께 읽는다. 복귀 전후를 3D 회전으로 비교할 수 있으며, 프레임마다 trace하는 추가 작업은 진단을 켠 경우에만 실행한다.

미저장 비교는 선택·이동 보고서가 동일했고 W 60/90도/초 Arc에서 가슴–카메라 차이 중앙값이 30.02/33.47도에서 9.52/9.68도로 감소했다. 루트·골반 방향과 선택 clip/time/Steering 목표도 같았다. W 30도/초 장면에서는 Arc가 선택되지 않아 Arc 개선 수치가 없다.

### 완료한 검증

증거는 `Saved/Validation/StrafeUpperBody_20261011/`에 보관한다.

| 검증 | 결과 |
| --- | --- |
| 최종 직접 UBT Editor / Game Development 빌드 | 둘 다 성공: EditorBuildFinal.txt / GameBuildFinal.txt |
| 정책·공유 그래프·기존 Handoff·품질 예산·follower 복구·모듈 메시 및 CMC | 최종 RegressionFinal 8/8 성공, 경고 0 |
| 최종 실제 CMC의 A/S/D·전방 회전·OTM·큰 회전 복귀 | 23장면 완료, 기존 Turn 선택 및 Cycle 복귀 검사 통과 |
| W 우회전 ON/OFF의 발·비척추 로컬 포즈·선택·이동 | PoseOff/PoseOn 보고서 SHA256 동일, 3장면의 모든 기록 동일 |
| W 좌회전 ON/OFF | LeftOff/LeftOn 보고서 동일; Arc 오차 중앙값 35.69/39.68도 → 9.38/10.41도 |
| 40Hz 카메라 입력 묶음 | PacketOn 3장면 통과 |
| 프레임 의존성·Turn 진입 해제 | 정책 테스트에서 30/60/120Hz 수렴·회전 속도 제한·40도 보정의 한 프레임 제거 방지 통과 |
| 승인 전 미저장 검증의 저장 범위 | Content 변경 없음, Master SHA256 유지. 기존 사용자 로그 4,021,994바이트·15:14:53 유지 |

포즈 보존 비교는 실제 마네킹의 척추 외 모든 로컬 본과 양발 컴포넌트 포즈를 해시로 비교했다. 팔의 컴포넌트 위치는 상체 보정에 따라 변하는 것이 의도된 동작이므로 보존 대상으로 주장하지 않는다. 기본 기준 리그와 기존 follower 정책은 확인했지만 모든 외형 에셋과 비표준 뼈 역할 조합을 실행한 것은 아니다.

첫 TrialOff 콘솔 실행에서는 CVar 뒤 명령 구분자 때문에 테스트가 시작되지 않았다. 같은 미저장 세션에서 MCP 자동화로 두 테스트를 완료했고 결과는 TrialOffMcpResult.json에 보존했다. 이후 실행은 올바른 ForceDPCVars와 자동 종료 경로를 사용했다. 이 첫 콘솔 실행을 정상 자동 종료나 테스트 성공의 근거로 사용하지 않는다. 검증 코드는 직접 패키지를 저장하지 않으며 모든 에셋 검사는 한정된 Master/기존 설치 데이터에서 수행했다.

과거 광범위한 빠른 회전 검사에서 확인했던 Case 41의 450도/초 W -180 Turn 선택 실패는 이번 23장면 범위에 포함하지 않았다. 이번 결과를 기존 전체 테스트의 실패까지 해결했다는 뜻으로 해석하지 않는다.

### 사용자 승인 후 저장 및 재로드

사용자의 “Master 연결·저장 승인”에 따라 `ABP_Humanoid_Master` 한 개만 저장했다. 기존 파일은 Saved 검증 폴더에 백업했다. `SaveMaster.py`는 연결·컴파일·중복 설치 검사 후 이 Master에만 SaveLoadedAsset을 호출했다. Content 변경 목록에서도 이 파일 한 개만 확인했으며 PSD/PSS·프로필·다른 에셋·레벨은 저장하지 않았다.

이후 새 프로세스를 열어 **Trial 설치 플래그 없이** 디스크의 Master를 평가했다. `Persisted` 테스트 8/8 성공·경고 0이다. W 30/60/90도/초의 이동·선택 보고서는 미저장 검증 보고서와 같았고, Facing 544표본의 루트·골반·가슴 최종 방향과 clip/time도 일치했다. 저장 성공은 `MasterSaved.json/SaveMaster.log`, 재로드 비교는 `PersistedVerified.json/VerifyPersisted.py`에 기록했다.

NullRHI 결과는 실제 CMC·MM·최종 본 포즈 검증이다. 보이는 follower 외형의 리타깃 이후 기울기·무기 접촉·체감 및 저주기 네트워크 환경은 PIE/실환경 확인이 남는다. 에디터에서 Combat Strafe W + 느린 좌우 카메라 회전, A/S/D 이동, 큰 Turn, OTM을 비교한다. `p.ProjectJ.StrafeUpperBodyFacing 0/1`로 OFF/ON을 전환하고 필요할 때만 `p.ProjectJ.LocomotionContinuityTrace 3`을 켠 뒤 검사 종료 시 0으로 끈다.
