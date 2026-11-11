# Winger UE5 리깅 원본

`Winger_UE5.blend`에는 직접 재생 스킨, 텍스처, 날개 액션 일곱 개가 있다. `FBX/`는 Unreal의 메시와 날개 시퀀스 재수입 원본이다. Unreal 에셋의 경로와 플레이 검증은 [Winger 제작 기록](../../../../Docs/Animation/Authoring/Winger_UE5_Export_2026-11-11.md)에 있다.

## 다시 제작할 때

1. 원본 Winger Blender 파일과 Unreal에서 내보낸 `SKM_Quinn_Simple` FBX를 준비한다. `RebindWinger.py`에 원본 blend, Quinn FBX, 출력 폴더를 순서대로 전달한다. Blender 4.5 LTS, cm 장면, 30fps를 사용한다.
2. `BakeWingMotion.py`에 첫 단계의 `Winger_UE5.blend`와 결과용 폴더를 전달한다. 펼친 자세는 원본 바인드에서 등 쪽 01번 본과 03번 관절을 잇는 선을 날개 끝까지 연장한다. 같은 기준에서 `L_03`·`R_03`의 바깥 패널을 깃털 방향으로 접는다. 바깥 깃털의 스킨 가중 영역에서 접힘 방향을 잡고, 같은 보행 위상의 달리기·스프린트에서 자식 패널이 함께 따라가는지 확인한다. 착지 액션은 공중 기준 포즈를 양끝으로 사용하고 지상 복귀는 Unreal의 지상 상태 블렌드가 처리한다. 기본 실행은 전체 blend와 날개 FBX 일곱 개를 저장한다. `--clips Land`처럼 지정하면 전체 blend와 지정한 FBX만 내보낸다. 메시 FBX는 첫 단계의 출력을 사용한다. Unreal에서 `a_01` 가지 회전을 루트 공간으로 합성한다. 펼친 각도는 정면, 접히는 방향은 대각선에서 확인한다.
3. 메시 재수입 시 기존 재질 슬롯과 전용 Skeleton의 바인드 일치를 확인한다. 기존 Skeleton·메시의 본 이름과 부모 관계를 비교한 뒤 전용 Skeleton을 갱신한다. Root 축과 스케일, 미가중 정점을 재검사한다. Idle의 첫 포즈를 수정했으면 같은 포즈로 LOD2의 날개 실루엣을 다시 베이크한다.
4. Manny 라이브러리는 Unreal의 `RTG_Manny_to_Winger_UE5`로 변환한다. 대상 Blend Space 샘플, 가산 기준 포즈, Notify·Sync Marker·루트 설정을 확인한다. 공용 Pose Search 후보의 Branch In Notify는 제작용 사본에서 제거한다. [오프라인 검증 스크립트](../../../../Scripts/Validation/Validate-WingerDirect.py)는 새 Unreal 세션에서 시퀀스와 Blend Space 참조, 압축 포즈의 손발 마커, 루트 이동을 확인한다.
