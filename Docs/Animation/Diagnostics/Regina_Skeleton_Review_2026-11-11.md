# Regina 골격·모션매칭 사전 점검

## 판정과 범위

`C:\Users\I\Desktop\Regina`의 FBX/SMD를 읽기 전용으로 분석했다. **UE5에서 리타깃하여 사용할 후보이며, 현재 Manny 공용 Skeleton/ABP에 직접 호환되는 몸체는 아니다.** 이번 작업은 사전 점검이다. 골격 수정, Content 임포트, ABP 연결, 빌드·PIE·시각 품질 승인은 수행하지 않았다.

Blender MCP의 `get_objects_summary`는 `127.0.0.1:9876` 연결 실패였다. 실행 중 Blender도 없었고 확인한 설치 경로에서 실행 파일을 확보하지 못했다. FBX binary 7100의 Model/Connection/Deformer/Geometry와 SMD nodes/skeleton/triangles를 직접 읽어 교차 확인했다. `.max`는 열지 않았으며 Blender/UE 임포터 결과와 실제 변형 화면은 미검증이다.

## 확인한 데이터

| 항목 | 결과 |
| --- | --- |
| FBX 골격 | 123개 LimbNode/Root, 최상위 `ValveBiped.Bip01` 1개 |
| SMD 메시 골격 | 122본. FBX는 이 이름들을 모두 포함하고 `jiggle_neckless`가 추가된다 |
| FBX 메시 | `regina` 18,852정점/30,347 polygon, `diadem` 2,108정점/3,295 polygon |
| 스킨 | 두 메시 모두 미가중 정점 0, 최대 영향 본 3, 음수/비유한 가중치 0 |
| 가중치 합 | 약 0.999999918~1.000000089; 부동소수점 오차 수준 |
| 본 local scale | 1에서 0.001 이상 벗어나는 본 0 |
| FBX 애니메이션 | AnimationStack `Take 001` 1개. 제공 SMD 70개 전체가 FBX에 들어 있다고 해석하지 않는다 |
| SMD 애니메이션 | 70파일 모두 96본. 공통 96본의 이름·ID·부모는 메시 SMD와 일치 |
| 단위·축 | UnitScaleFactor/OriginalUnitScaleFactor=2.54(inch), UpAxis=Y, OriginalUpAxis=Z. 메시 Model에 X -90도 PreRotation |

polygon 수는 삼각형 환산 수가 아니다. 스킨 수치는 bind matrix의 시각적 정확성, 접힘 품질, 관절 축 또는 좌우 대칭 품질까지 보장하지 않는다. FBX raw geometry bounds를 그대로 UE cm 크기로 읽으면 안 된다. 단위·PreRotation·import transform을 함께 적용해 실제 신장과 날개 폭을 확인해야 한다.

## 날개 본

```text
ValveBiped.Bip01_Spine2
└─ a_01
   ├─ Bone013
   ├─ R_01 → R_02 → R_03 → R_04
   └─ L_01 → L_02 → L_03 → L_04
```

좌우 주 체인 8본에 모두 스킨이 있다. 공통 부모 `a_01`과 `Bone013`은 직접 스킨 가중치가 없으나 계층/제어 역할이 있으므로 미사용으로 판단하지 않는다.

추가로 좌우 `jiggle_wingR_01..05`, `jiggle_wingL_01..05` 10본이 있으며 모두 실제 스킨이 있다. 각 본의 weighted vertex entry 수는 R_01=176/R_02=168/R_03=116/R_04=103/R_05=71, L_01=173/L_02=154/L_03=108/L_04=60/L_05=50이다. 중복 영향 정점을 포함하므로 합계를 고유 날개 정점 수로 사용하지 않는다.

보조 본 parent는 좌우 공통으로 02→주 체인04, 03/01→주 체인03, 04→주 체인02, 05→주 체인01이다. SMD에는 날개 주 체인 키가 있으며 `regina_sky_begin`, `regina_sky_attack`에서 큰 local transform 변화가 확인된다. 원본 비행 동작 제작의 재료가 있다. `jiggle`이라는 이름이 UE 물리 동작을 자동 생성하지는 않는다.

## 직접 호환을 막는 차이

- `root/pelvis/spine_*/thigh_*/ik_*`의 Manny 계층이 아닌 ValveBiped 계열이다. 손·발·손가락·twist 가지는 존재하지만 이름 변경만으로 Manny 호환이 되지 않는다.
- 좌우 주 다리는 `ValveBiped.Bip01_Pelvis`가 아닌 `ValveBiped.Bip01_Spine` 아래다. 대상 IK Rig의 다리 체인과 pelvis/root 동작을 명시적으로 검증해야 한다. 지금 자료만으로 임의 reparent가 필요한 결함이라고 확정하지 않는다.
- 최상위 본은 지면 원점 root가 아니라 골반 높이에 있다. 메시 SMD의 초기 위치는 약 (-0.104, 0.448, 79.532) 원본 단위다. `idle`의 root Z는 약 95.078~106.219, `regina_sky_begin`은 약 61.309~280.722로 바뀐다. 원본 root 이동에는 몸체 상하 동작이 포함되므로 UE root motion을 그대로 켜는 방식은 별도 검증이 필요하다.
- Manny IK helper 이름은 없으므로 현행 Manny 발 Control Rig를 Regina 골격에 직접 적용하지 않는다. 리타깃 목표/체인과 실제 발바닥 접촉을 대상에서 확인한다.
- 70개 애니메이션에서 공통으로 빠진 26본은 머리카락 5, 날개 jiggle 10, 다리 jiggle 4, `jiggle_k` 4, `jiggle_f` 3이다. FBX에만 있는 목걸이 본까지 원본 SMD 애니메이션으로 제어된다고 가정하면 안 된다. 명시적 animation/secondary motion이 없으면 별도 흔들림은 없고 부모 변환만 따를 수 있다.
- SMD 자체에는 신뢰할 프레임 속도 메타데이터가 없으므로 time 번호만으로 재생 초·타이밍을 확정하지 않는다.

## Project J에 맞는 연결 방향

프로젝트의 canonical 검색 Skeleton은 `SK_Mannequin`, 공통 실행기는 `ABP_Player_Master`, 검색 Schema는 `PSS_ProjectJ_Manny_*`다. 플레이어 시험에는 **기존 Manny source의 MM을 유지하고 Regina visual follower로 리타깃하는 경로**가 변경 범위가 작다.

1. 원본을 보존하고 별도 Regina Skeleton으로 임포트한다. unit/axis와 실제 신장, bind pose, 손발 축을 확인한다.
2. Manny→Regina IK Rig/Retargeter를 제작한다. 기존 Bip01 대검과 이름이 비슷해도 skeleton asset 쌍이 다르므로 기존 Family를 그대로 재사용하지 않는다. Regina용 Family는 기존 `UProject_JRetargetFamilyProfile`의 SourceSkeleton/TargetSkeleton/Retargeter 계약을 따른다.
3. 기존 Body/HandGrip 프로필 계약으로 대상 체형과 무기 소켓·손 접촉을 맞춘다. 공통 master나 gameplay 상태 writer를 복제하지 않는다.
4. 날개는 Manny에 대응 체인이 없으므로 보존한 Regina 본에 별도 local animation/레이어를 작성한다. 원본 SMD의 주 날개 키를 재사용할 수 있고, 보조 jiggle은 필요할 때 해당 몸체의 후처리/물리로 제작한다. 몸체 전체 원본 포즈가 MM 결과를 덮지 않게 날개 계층에 한정한다.
5. Idle/Walk/Run, OTM/Strafe 전환, 급회전, 점프/착지, 전신 montage·MM 복귀, 손발 접촉과 날개 간섭을 실제 화면으로 확인한다. follower 교체/숨김/재등록 및 LOD에서 추가 본이 유지되는지도 확인한다.

이 방법은 **기존 프로젝트 구조를 활용하는 통합 방안**이며 이번에 구현·검증한 연결이 아니다. 원본 골격을 Manny로 재리깅하는 방안은 더 큰 제작 작업으로, 이번 점검에 포함하지 않는다. NPC로 쓸 경우 현행 프로젝트 정책상 NPC는 MM을 사용하지 않으므로 용도를 구분한다.

Epic의 [IK Retargeting 공식 문서](https://dev.epicgames.com/documentation/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine?lang=en-US)는 서로 다른 이름·본 개수·방향의 골격 사이 전송을 지원한다. 이는 이 특정 Regina의 변형 품질이 검증됐다는 뜻은 아니다.

## 재현 근거와 미검증

원자료와 입력 72개(FBX 1, 메시 SMD 1, animation SMD 70)의 크기·SHA-256은 중앙 자료 `Saved/StorageCleanup_20261104/ArchiveStore/02_Measurements/Regina_Skeleton_Audit_20261111.json`에 있다. 분석기는 같은 ArchiveStore의 `06_OtherArchives/Regina_Skeleton_Audit.py`다. 압축·이동·삭제 없이 중앙 위치에 새로 생성했다. 원본 복원 작업은 없고 원본 파일은 기존 Desktop 경로에 그대로 있다.

UE import, Retargeter 제작, PSD 재인덱싱, ABP/follower 실제 실행, root motion, 물리, LOD·성능, 모든 원본 클립의 시각 품질은 미검증이다. 원본 문제 수정 또는 런타임 연결 완료로 보고하지 않는다.
