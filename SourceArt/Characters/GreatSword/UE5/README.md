# GreatSword UE5 제작 원본과 직접 비교

Desktop 원본 패키지를 보존한 작업 사본이다. `GreatSword_UE5_Rig.blend`가 수정된 제작 리그이며, 텍스처 35개를 파일 안에 패킹했다. `CTRL_GreatSword`는 Blender 제작용이고 UE에 내보내는 리그는 `Armature` 318본이다. 기존 얼굴·헤어·망토·장비 가지 229본과 메시/웨이트를 보존했다.

## 수정한 리깅 계약

리그 1.2에서 `ik_foot_l/r`, `ik_hand_l/r` 네 목표는 실제 해결된 `RES_foot_l/r`, `RES_hand_l/r`를 따른다. `ik_hand_gun`의 기본값은 기존 보정 전 오른손 기준이며, 독립 무기 애니메이션을 만들 때는 GreatSword 패널의 `Snap independent`로 현재 포즈를 유지하며 `FK_ik_hand_gun` 제어로 전환한다. `Follow wrist`는 오른손 기준으로 복귀한다. 모드는 `Key controls and IK/FK`에 포함된다. UE 몸체가 authored gun을 보존하는 설정인지 함께 확인한다. 현재 대검의 UE 구성은 명시적인 wrist-reference 복사 5개를 유지한다.

표준 `ik_*` 7개는 스킨 가중치 0을 확인해 Deform을 껐고, 수출은 모든 318본을 유지한다. 새 모델에 이 legacy 318본 구조를 복제할 필요는 없다. 기존 피부/얼굴/장비의 소비를 보존하기 위한 이관 구조다.

이번 helper/control 수정은 skin 자체의 결함을 고치는 작업이 아니어서 geometry/weight 불변을 확인했다. 실제 bind·weight 결함을 수정하는 작업은 의도한 변경과 비대상 보존을 나눠 검수하며 아래 hash 불변을 필수 조건으로 복사하지 않는다.

수정 전후 38개 메시의 정점·웨이트 SHA-256은 `e5be3a20b6c13a6bc5c0cbb9b7d91df7c5690b1fb728953b3a15f3103e38bc09`로 같다. 기준 포즈의 evaluated geometry 변화는 0cm였다. FK/IK 회전 4조건과 실제 IK 목표 이동 4조건에서 다섯 보조본과 해결된 사지의 위치·회전 오차는 0이었다. Blender 내부 검증이며 게임 접촉 품질은 별도로 확인한다.

## 메시 익스포트

Blender Text Editor에서 옆의 `ExportMeshes.py`를 실행한다. 기본 출력은 현재 blend 옆 `FBX` 폴더다. Complete와 18개 파츠/변형 파일을 생성한다. `Armature`와 해당 스킨 메시만 선택하고 컨트롤·위젯은 제외한다. cm 단위, Forward -Y / Up Z, leaf bone 없음, 모든 318본, 애니메이션 없음, FACE smoothing 및 tangent를 사용한다. 애니메이션 제작은 blend 안의 기존 `Export_UE5_Animation.py`를 사용한다. 이 exporter도 `use_armature_deform_only=False`/모든 본 bake를 사용하므로 무가중 IK/무기 기준 본을 누락하지 않는다. FBX에 제어 리그를 넣는 대신 평가된 결과를 bake하며 새 Action은 root·손발·추가 가지의 왕복 검수가 필요하다.

`GreatSword_Rig_UI.py`는 embedded UI의 검토 가능한 원본이다. 수정할 때 해당 Text block도 동기화해 blend를 저장한다. 열 때 자동 스크립트 실행을 허용하도록 보안 설정을 바꾸지 않고, 필요한 경우 Text Editor에서 실행한다. 기존 FK/IK 도구와 무기 선택을 함께 등록한다.

Unreal 재임포트에서는 기존 Skeleton을 지정하고 재질·텍스처·애니메이션·새 PhysicsAsset 생성을 끈다. 이번 파일은 UE5.8의 legacy FBX 재임포트로 기존 메시 UObject를 갱신했다. 재질 슬롯 이름의 `.004`/`_004` 같은 Blender suffix를 정규화해 기존 재질과 파츠 override를 다시 연결했다. 19개 메시의 재질·Nanite·물리·Post Process와 망토 3개 clothing asset을 유지했다. 기존 Basic 대검의 외형은 교체하지 않았다.

## UE5 → FBX → Blender 직접 비교

`Comparison/UE5_GreatSword_Comparison.blend`에는 실제 Unreal `SkeletalMeshExporterFBX`에서 나온 다음 세 메시와 리그가 들어 있다.

- `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`
- `/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple`
- `/Game/GreatSword/Meshes/SK_GreatSword_Complete` 수정 후 왕복본

`01 UE5 FBX Overlay`는 같은 단위·원점의 3D 비교이며, Collection을 켜고 끄며 확인한다. 기본 화면은 표준 코어만 표시한다. 추가 229본은 `Additional bones - preserved` collection을 켜고, 메시 형상은 Outliner의 눈을 켜서 비교한다. 데이터는 제거하지 않았다. `02 Bind Pose Measurements`의 카메라는 정면·측면·발 비교 도표다. PNG와 JSON 측정값도 같은 폴더에 있다. 크기 맞춤이나 체형 변형으로 차이를 숨기지 않았다. FBX를 Blender로 가져올 때 UE의 `root`는 Armature Object로 표현되어 본 목록은 88/317개다. UE에서 root를 포함한 89/318본을 잃은 것이 아니다.

| 왼쪽 기준 포즈 측정 | GreatSword | Manny Simple | Quinn Simple |
|---|---:|---:|---:|
| 골반 높이 cm | 110.17 | 95.90 | 98.69 |
| 발목 높이 cm | 15.95 | 8.24 | 8.13 |
| 발볼 관절 높이 cm | 2.28 | 0.75 | 1.17 |
| 발목→발볼 기울기 도 | 61.95 | 26.50 | 27.43 |
| 양 상완 시작점 폭 cm | 27.00 | 38.02 | 32.11 |
| 상완 길이 cm | 25.99 | 27.77 | 27.09 |
| 전완 길이 cm | 24.65 | 27.25 | 26.10 |

공유 88개 FBX 본의 부모 관계는 일치한다. 표준 코어의 누락은 없으며, Simple 메시가 쓰지 않는 Manny Skeleton의 추가 교정본 72개를 필수 누락으로 판정하지 않는다. GreatSword의 사지 기준 축은 Quinn과 대체로 일치하고, 발 형상·관절 길이·몸통 비율이 다르다. `center_of_mass`는 GreatSword에서 골반 높이에 있고 두 Simple 참조에서는 원점에 있다. 웨이트 없는 제작용 helper 차이이며 모션매칭 데이터는 기존 Manny 계약을 유지하고 실제 호환 몸체에서 공통 ABP가 평가한다.

왕복본과 Blender 제작 리그의 공통 본 위치 최대 차이는 약 0.00054cm, 표준 코어 회전 차이는 약 0.0792도였다. UE FBX를 Blender로 읽은 legacy 말단 `Bip01_R_Foot`의 표시 축은 달라지므로, 그 축의 검증은 UE의 실제 mesh bind pose와 별도로 대조한다. Blender가 표시하는 임의의 본 tail 방향만으로 UE의 bind matrix를 판정하지 않는다.

## 참조 포즈·소켓·물리 주의

처음 패키지의 USkeleton reference pose는 실제 SkeletalMesh bind pose와 달랐다. 아래는 이 전용 대상에서 수정한 사례이며, 공유 Skeleton을 쓰는 모든 체형의 mesh bind가 같아야 한다는 규칙은 아니다. 다른 SKM과 공유하는 reference를 새 메시마다 덮지 않는다. UE의 IKRigController를 해당 메시로 초기화해 읽은 실제 기준 포즈로 비교했고, Skeleton Editor의 reference pose 업데이트와 새 FBX 재임포트로 일치시켰다. 이전 USkeleton 값으로 추정했던 -90도 mesh basis는 잘못된 근거였으므로 사용하지 않는다.

보존된 Bip01 가지의 형상·기준 축이 함께 재배치되므로 복제한 헤어/치마/망토 PhysicsAsset은 기존 bone-local shape/constraint frame을 유지하고 뼈 이름을 매핑한다. 손바닥 소켓은 기존 legacy 손 local anchor를 새 legacy 손 기준에 옮긴 뒤 표준 `hand_l/r` 기준 local transform으로 변환한다. 등을 기준으로 한 `WeaponSocket_Back`은 legacy spine local anchor를 유지한다. 무기 전체 궤적과 실제 손바닥 접촉은 별도 PIE 관측을 따른다.

## 시험 브랜치의 실행 구성

프로젝트는 검증된 UE5 호환 몸체의 공통 ABP 직접 재생과, 호환 계약을 맞출 수 없는 몸체의 실시간 리타깃 두 경로를 지원한다. 현재 대검은 Complete 메시 자체에서 공통 `ABP_Player_Master`를 실행하고 `ABP_GreatSword_Body` Post Process가 InputPose → 선택적 신발 지지 보정 → 같은 입력 포즈의 손목 목표 → 일반 몸체 접촉 IK → 양손 정밀 보정 → 헤어/치마 RigidBody 두 개를 평가한다. 별도 Manny 메시와 `GreatSwordPose` 컴포넌트, Retarget Pose From Mesh는 현재 대검에 연결하지 않는다. Body·Head·의상 8개가 이 최종 포즈를 공유하고 망토는 파츠의 Chaos Cloth가 담당한다. 얼굴 가중 본 44개와 추가 가지를 유지한다.

이후 신발 조사에서는 Blender의 Armature modifier·밑창 가중치·표준/legacy 관절 기준 위치를 재검사했다. 이 범위에서 전체 재리깅을 요구하는 결함은 발견하지 못했으며 skin/bind를 다시 바꾸지는 않았다. 후속 리그 1.2에서는 무가중 helper의 Deform flag와 무기 제작 제어만 개선했다. UE의 실제 신발에 저장한 선택적 지지 표본과 몸체 후처리는 런타임 제작 데이터다. 리깅 결함을 후처리로 덮는 정책으로 일반화하지 않는다. 원인·실패·제작 절차와 미래 캐릭터/장비 예외는 [리깅·UE5 후처리 가이드](../../../../Docs/Animation/Authoring/Character_Rigging_And_UE5_Postprocess_Playbook.md)를, 최종 수치·검증 범위는 [플레이어 마스터](../../../../Docs/Architecture/Runtime/Player_Character_Master.md)를 따른다.

공통 발 Rig는 실제 메시의 초기 transform을 사용한다. 현재 GreatSword의 명시적 wrist-reference 구성은 Post Process의 입력 FK 손발을 다섯 표준 IK helper로 복사하고, 새 Complete의 source Drawn 소켓을 `ik_hand_gun`에 연결해 손 보정 결과가 무기 궤적으로 재입력되지 않게 한다. Palm→Wrist 변환과 같은 평가의 입력 본을 사용하는 GuidedHandIK는 공통 접촉 컨트롤러를 재사용한다. 직접 재생/리타깃의 선택은 그래프와 Family/Body 구성에 있으며 이동·전투·장비 상태나 예산 관리자는 복제하지 않는다.

이전 `ABP_GreatSword_Secondary`는 초기 Post Process 제작·비교 사본으로 보존한다. 다음의 전신 리타깃 결과는 직접 재생 전의 실험 이력이며, `ABP_GreatSword_Retarget`·Family/Body·접촉 Rig는 두 번째 경로의 제작/회귀 자료로 남긴다.

현재 직접 재생의 손 접촉은 기존 무기 표현 컨트롤러를 공유한다. 엔진 프레임당 전달한 목표 값과 무기 변환을 유지해 늦은 Tick이 같은 포즈를 다시 바꾸지 않는다. 접촉 가중치는 입력 포즈의 손목 목표에 한 번 적용하고 몸통/쇄골을 포함한 일반 IK가 도달을 보완한다. 발은 입력 anchor를 유지하며 두 팔 정밀 보정은 같은 목표를 사용한다. 직접 몸체의 추가 본·소켓·접촉은 몸체 자산에 두고 공통 마스터에 캐릭터별 분기를 넣지 않는다.

다음은 신발 보완 전 직접 재생 검사 이력이다(Saved/Validation/GreatSwordDirect_20261025). CoherentContacts_133247.json의 7종 montage/528표본에서 alpha > 0.99인 주손 평균/최대 오차는 약 0.00052/0.00768cm, 보조손은 0.01672/0.78494cm다. 지상·공중/착지·전신 몽타주 발 anchor와 망토 2,534정점, 움직이는 헤어 4본·치마 2본도 별도로 검사했다. 당시 이동 중 신발 최저점 약 -4.24cm가 남아 있었다. 이후 실제 밑창 데이터와 선택적 몸체 보정으로 검사한 지상/착지/공격/정지 경사의 최저점은 약 +0.15cm였다. 모든 미래 자세·지형의 무관통 보장은 아니다.

2026-10-25 후속 전체 점검(`Saved/Validation/RigPostReview_20261025`)은 38개/88,053정점·최대 influence 4·누락/합계 오류/helper weight 0, LBS 단일 Armature를 확인했다. 리그 1.2 전후 skin hash, rest matrix와 evaluated geometry 차이는 0이었다(이 점검의 hash는 다른 직렬화로 계산하므로 위 이전 SHA와 문자열을 비교하지 않음). 네 사지 FK/IK, root 이동/yaw, 독립 무기 전환을 포함한 12조건을 통과했다. 기존 FBX의 skin/bind/기본 평가 결과가 같아 이번 authoring-only 수정으로 19개 SKM을 불필요하게 재임포트하지 않았다.

접촉 솔버는 128회→32회로 낮춰 저장했다. 32회/541표본과 엄격 montage 242표본을 확인했고, 오차가 커진 20회는 채택하지 않았다. 큰 발 lift는 실제 Manny/GreatSword의 동일 raw clip을 비교해 공중 원본 pose 자체의 root-relative 음수 발 높이와 체형 잔차를 구분했다. 리깅 결함을 숨기기 위해 일괄 pelvis/사지 길이를 바꾸지 않았다. 후속 재시작/검증 결과와 제한은 런타임 문서를 따른다.

UE 5.8의 solver-controller setter는 자산의 dirty 표시를 남기지 않아 이전 저장 요청 일부가 건너뛰어졌다. 현재 접촉 리그는 소유 자산을 강제 저장하고 재로드로 검증한다. 이전 GreatSwordFix_20261025의 10시 파지 수치와 native 48개는 Manny-source 리타깃 실험의 이력이며 현재 직접 재생의 통과 증거가 아니다. 실행 구조·최종 빌드/회귀·보존 이유와 한계는 [플레이어 마스터](../../../../Docs/Architecture/Runtime/Player_Character_Master.md)를 따른다.

후속 무기 종료 조사에서는 stopped montage의 늦은 Weapon Motion Begin이 완료된 IK 뒤 무기 이동을 다시 시작하는 원인을 찾아, 공통 notify의 실행 ID 시작 승인으로 수정했다. 리깅·공통 ABP나 montage별 예외로 감추지 않았다. native 68개와 RMB 4회/655표본을 통과했다. 최종 32회/7종 517표본은 Q 복귀에 보조손 1.63cm의 실제 도달 잔차가 남았고, 신발 553표본은 최저 +0.14238cm·관통 0이었다. 64회 추가 비교는 일관된 개선이 없어 저장하지 않았다. 상세한 전후·한계는 런타임 문서에 남겼다.

현재 장착 파츠의 재질과 기존 Basic 대검을 유지한다. 패키지 무기 4개는 기존 빈 재질 슬롯을 가진 제작 후보이며 자동으로 장착하지 않는다. 새 리그의 제작 LOD0에 이전 메시의 원거리/군중 성능 결과를 재사용하지 않는다. Cook/Shipping·래그돌·원격 네트워크 부하·표정/립싱크와 모든 클립 외형은 별도 검수 대상이다.

Blender 비교 장면 분리 저장(`bpy.data.libraries.write`)에서 발생한 내부 접근 오류는 crash log에 보존했다. 일반 save_as_mainfile로 비교 파일을 복구했고 재열기를 확인했다. Desktop 원본과 수정된 제작 blend는 해당 충돌 과정에서 덮어쓰지 않았다.

## 비용 측정과 진단 자료

현재 직접 ABP의200명 비용은 [전용 보고서](../../../../Docs/Performance/Reports/ProjectJ_DirectRig_200_2026-10-25.md)를 따른다. 과거 리타깃의 worker CPU 합계·전체 frame·GPU를 구분하고, 에셋/본/LOD/물리 구성이 다른 비교를 순수 리타깃 제거 효과로 일반화하지 않는다. 제작 원본과 재임포트용 FBX는 이 폴더에 유지하며, 과정의 임시 캡처·스크립트·백업은 [해시 검증·복원 가능한 원자료](../../../../Archives/Validation/README.md)로 모은다.
