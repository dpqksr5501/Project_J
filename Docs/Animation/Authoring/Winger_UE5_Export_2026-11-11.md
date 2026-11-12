# Winger UE5 제작과 플레이 검증

Regina를 Project J의 Winger 시험 몸체로 제작했다. 프로젝트 에셋은 `/Game/Characters/Winger` 아래에 있다. 이전 FBX/SMD 계층과 가중치 점검은 [Regina 골격 점검](../Diagnostics/Regina_Skeleton_Review_2026-11-11.md)에 있다.

## 현행 UE5 직접 재생

- `BP_Winger`는 `BP_Player_Master`를 상속한다. 표시되는 `CharacterMesh0` 한 개가 `SK_Winger_UE5`와 `ABP_Winger`를 사용한다. `ABP_Winger`는 `ABP_Player_Master`를 상속해 공통 Chooser, Pose Search, Slot, Mover 및 애니메이션 예산 경로를 사용한다. 기존 숨은 Manny 컴포넌트와 `WingerVisual` 연결은 제거했다.
- Blender에서 Quinn의 UE5 89본과 계층·축을 기준으로 재바인딩했다. Winger의 관절 길이와 원본 스킨 영향 값을 유지하면서 팔꿈치 평면과 손바닥 기준 방향을 맞췄다. UE에서 총 144본, 날개 가지 20본, 지면 root 스케일 1과 중립 회전, LOD0 삼각형 32,759개, 미가중 정점·잘못된 영향 본 0개를 확인했다. 기존 Winger 머티리얼 14개 슬롯을 연결했다.
- `/Game/Characters/Winger/Animation/Retargeted`에는 공용 Manny 로코모션 575개 시퀀스와 11개 Blend Space의 Winger용 에셋이 있다. Blend Space 샘플과 가산 기준 포즈는 Winger 변환본을 가리킨다. 제작용 레시피는 `/Game/Characters/Winger/Rig/UE5/RTG_Manny_to_Winger_UE5`다. FK, Pelvis, Root Motion, Curve Remap 및 네 사지 IK 마커를 사용한다. 손발 마커는 최종 FK로 작성하며, 원본과 같은 루트 추출/잠금 설정을 유지한다. 오프라인 이동량은 원본과 대상 골반 높이 비율인 약 0.813배다. 기존 모션매칭의 후보·데이터베이스는 공용 애니메이션을 직접 소비한다.
- `ABP_Winger_Wings`는 같은 표시 메시의 Input Pose에서 `a_01` 이하만 합성한다. Idle·Walk·Run/Sprint와 Mover 기반 이륙·공중 루프·착지를 사용한다. 날개 관절과 깃털의 동작은 한 세트의 Blender 액션에 베이크한다.
- LOD0·LOD1에서 날개 후처리를 평가한다. LOD2는 Idle 첫 프레임의 날개 실루엣을 스킨에 베이크하고 날개 20본을 평가 본과 섹션 스킨에서 제거한다. LOD별 삼각형은 32,759 / 8,189 / 4,094개, 평가 본은 144 / 144 / 124개다. 별도 날개 메시, 물리 솔버, 날개 복제 상태를 만들지 않았다.

Blender 제작 원본과 FBX는 [SourceArt](../../../SourceArt/Characters/Winger/UE5/)에 있다. `C:\Users\I\Desktop\Winger\UE5_Direct`에도 텍스처를 포함한 `Winger_UE5.blend`와 메시·날개 FBX 및 SHA-256 목록을 내보냈다. `/Game/Characters/Winger/Test/L_Winger_Test`에서 직접 재생을 확인할 수 있다. 이전 원본 70개, 검 및 리타깃 자료는 제작 비교용으로 보존한다.

### 반투명 의상 천

허리에서 내려오는 천은 `upper`와 `upper2` 슬롯에 걸쳐 있다. 원본 FBX와 Blender 제작 메시의 두 슬롯에 있는 삼각형 933개에는 위치가 일치하는 중복 삼각형이 없었다. 두 슬롯 경계에서는 인접 면의 방향이 뒤집힌다. `M_Winger_upper`에서 양면 렌더링을 켜고 `regina_upper`의 알파를 반투명 Opacity에 연결한다. 방향에 따른 조명 경계가 천을 다시 조각처럼 보이게 하는 현상은 비방향성 반투명 조명으로 줄였다. 기존 메시와 스킨, LOD를 사용한다.

60Hz D3D12 PIE에서 강제 LOD0·LOD1·LOD2와 거리 기반 LOD0→1→2를 확인했다. 정면·대각선·후면과 조명을 켠 근거리 및 LOD2 검사에서 허리 아래의 천이 연속적으로 렌더링됐다. 확인 자료는 `ArchiveStore/02_Measurements/WingerCloth_*`에 있다.

천의 앞·뒤 `jiggle_f`·`jiggle_k`와 양옆 `jiggle_leg` 스킨 본은 기존 몸체 포즈를 따른다. `ABP_Winger_Wings`의 날개 합성 뒤에 `InstallWingerClothMotion`으로 제한된 회전을 연결한다. 현재 이동 속도와 상승·낙하 속도에 반응하고, 끝단은 더 느리게 따라온 뒤 정지 시 기본 포즈로 가라앉는다. LOD0·1에서 적용하며 LOD2는 기존 스킨 포즈를 사용한다. 별도 표면 솔버나 의상 메시가 필요하지 않다.

저장한 에셋을 다시 열어 60Hz PIE 810프레임에서 달리기, 스프린트, 점프·착지, 정지, 재이동과 LOD1·2 전환을 비교했다. 앞·뒤 뿌리 본의 최대 추가 회전은 달리기 3.2°, 스프린트 4.5°, 점프 5.6°였고, 정지 안정화 후와 LOD2에서는 0°였다. 로컬 측정과 화면은 중앙 보관소 INDEX.csv에서 WingerClothMotion으로 찾을 수 있다.

### 손목을 중심으로 접히는 날개

Blender에서 등부터 손목까지는 본 바인드의 상승 각도를 기준으로 양쪽 V자를 만든다. `01`·`02`의 방향과 비틀림은 접힘 강도에 맞춰 조금씩 조정한다. `03`부터 끝까지는 같은 바깥 패널 방향을 공유한다. 손목에서 깃털 방향으로 접히는 각도와 바깥 윗선의 높이를 각각 제작한다. 정지·걷기·달리기의 바깥 윗선은 거의 수평이며 스프린트와 이륙은 살짝 내려간다. `04`의 끝 패널은 최대 2°만 늦게 따라가고 클립 경계의 키를 공유한다.

Blender의 3번 손목 접힘 기준값은 걷기 23°, 달리기 35°, 스프린트 65°다. 달리기와 스프린트의 4번 관절은 같은 프레임의 3번 접힘을 25% 적용한다. 달리기와 스프린트는 같은 2.033초 보행 주기를 쓴다. UE의 `BS_Winger_Wings_RunSprint_UE5`가 Mover가 승인한 스프린트의 실제 수평 속도를 기준으로 기존 Run 슬롯의 두 포즈를 섞는다. 이륙은 3번 관절을 43° 접은 자세에서 시작하고 공중에도 일부 접힘을 유지한다. 착지 클립은 공중 기준 포즈에서 충격을 받고 그 포즈로 돌아온다. 재생 시간은 시퀀스 길이에서 설정하고, 끝나면 기존 선택기가 실제 지상 보행 상태와 스프린트 블렌드 값을 사용한다.

같은 손목 각도라도 척추의 로컬 공간에서 합성하면 몸통 기울기가 날개를 위로 돌린다. `ABP_Winger_Wings`의 기존 Layered Blend per Bone에서 `a_01` 가지의 회전을 루트 공간으로 합성한다. 날개 뿌리의 위치는 척추를 따라가고 회전 기준은 캐릭터 방향을 따른다. [UE 5.8 레이어 합성 설정](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AnimGraphRuntime/FAnimNode_LayeredBoneBlend)

첫 깃털 방향 버전의 두 시점 캡처와 스킨 검사 기록은 `ArchiveStore/02_Measurements/WingerFeatherFold_*`에 있다.

V자 자세를 복원한 버전은 60Hz PIE에서 걷기·달리기·스프린트의 안쪽 패널 상승각을 약 38°·41°·46°로 재현했다. 각 단계의 손목 전후 고도각 차이는 약 17°·23°·42°였다. 후방 캡처와 원자료는 `ArchiveStore/02_Measurements/WingerRaisedFold_*`에 있다.

좌우 안쪽 구간을 정렬하고 바깥 접힘을 추가한 버전은 60Hz PIE 915프레임을 통과했다. 같은 스프린트 표본에서 이전 버전 대비 양쪽 바깥 패널은 위에서 보았을 때 약 13° 더 기울었고, 4번 본의 로컬 회전 차이는 약 3.7°였다. 이동·제자리 점프, 착지, LOD 전환을 확인했다. 바인드와 웨이트 지문은 이전 버전과 일치했다. 위쪽·대각선 캡처 및 원자료는 `ArchiveStore/02_Measurements/WingerAlignedFold_*`에 있다.

착지 포즈에서 Idle 키로 직접 돌아가던 보간을 제거했다. 60Hz PIE에서 약 0.333초 충격 구간 동안 양쪽 끝이 손목 기준 약 8.9cm·9.8cm 내려갔다. 제자리 착지는 Idle, 스프린트 입력을 유지한 착지는 약 750cm/s의 Run/Sprint 포즈로 이어졌다. 검사한 전환 구간에서 날개 끝 높이가 전환 후 높이를 지나쳐 솟는 현상은 관측되지 않았다. LOD2를 거친 착지는 근거리 복귀 때 재생되지 않았다. 원자료는 `ArchiveStore/02_Measurements/WingerLandingRecovery_*`에 있다.

펼친 자세는 원본 `Regina.FBX`와 UE5 재바인딩본에서 일치하는 01번 본부터 03번 관절까지의 정면 각도 37.05°를 사용한다. 양쪽 날개 윗선을 이 방향으로 끝까지 연장하고, 이동 시 같은 기준 포즈의 03번 관절에서 깃털 방향으로 접는다. Blender에서 날개 액션 일곱 개를 다시 베이크했다. 바인드와 스킨 웨이트 지문은 일치했다.

달리기·스프린트 각도 조정 전 60Hz D3D12 PIE 915프레임에서 정지 중 양쪽 윗선의 각 구간이 원본 기준에서 최대 0.14° 이내였다. 03번 관절을 기준으로 04번 본이 캐릭터 뒤쪽으로 이동한 양은 걷기 약 15cm, 달리기 약 18cm, 스프린트 약 28cm, 이륙 약 29cm였다. 스프린트 중 착지와 제자리 착지에서 양쪽 날개 끝이 약 10–11cm 눌린 뒤, 각각 스프린트와 Idle 포즈로 복귀했다. 복귀 포즈를 지나쳐 위로 솟는 현상은 관측되지 않았다. LOD2는 새 Idle 실루엣으로 베이크했고 날개 본 20개를 제거한다. 원거리 패널은 척추에 붙으므로 몸통이 기울면 좌우 높이가 달라질 수 있다. 캡처와 원자료는 `ArchiveStore/02_Measurements/WingerStraightOpen_*`에 있다. 이전 수평 정렬 버전의 원자료는 `ArchiveStore/02_Measurements/WingerOuterSpar_*`에 있다.

새 달리기·스프린트 시퀀스는 D3D12 PIE 915프레임에서 양쪽 4번 관절의 접힘을 약 8.7°·16.1°로 재현했다. 이동 및 제자리 착지에서 복귀 목표를 지나쳐 위로 튀는 현상은 관측되지 않았다. Blender 바인드·웨이트 지문은 일치했다. 원자료는 `ArchiveStore/02_Measurements/WingerRunSprintFold_*`에 있다.

`BP_Player_Master`의 `DefaultAbilities`가 기존 `Project_JGameplayAbility_Sprint`를 지급한다. Winger와 기존 플레이어 직업이 같은 Sprint 능력과 입력을 상속한다. `AS_Spellblade_Base`와 `DA_CombatStyle_SpellbladeKit`의 Sprint 항목을 정리해 PlayerState가 Sprint 스펙을 한 번 보유하도록 했다. Greatsword와 Spellblade의 첫 빙의와 재스폰에서 동일한 Sprint 스펙이 한 개 유지되는지 자동화 테스트로 확인했다.

#### 이전 뒤쪽 회전 포즈의 검증 기록

Blender에서 본 바인드·스킨 정점·가중치의 지문과 내장 텍스처 34개의 경로 및 데이터가 제작 전후 일치했다. 동일 보행 위상에서 스프린트의 바깥쪽 끝 본은 달리기보다 왼쪽 6.91–17.48cm, 오른쪽 11.95–23.80cm 뒤로 이동했다. 검사한 스프린트 표본에서 날개 에지 길이의 5–95백분위 범위는 바인드 길이의 약 0.961–1.015였다. 이 수치는 표면의 늘어남을 확인한다.

UE 5.8 직접 UBT와 60Hz D3D12 PIE 915프레임을 통과했다. 실제 입력으로 달리기 약 500cm/s와 스프린트 약 750cm/s를 재현했다. 같은 보행 위상에서 좌우 손목의 추가 회전은 각각 약 13.6–14.5°였다. 두 안쪽 관절은 비교한 위상에서 일치했다. UE 압축 시퀀스도 62키에서 양쪽 끝의 뒤쪽 변위를 재현했다. 이동 점프, LOD1 제자리 점프, 착지, 낭떠러지 진입 및 LOD2 복귀를 확인했다. 원자료와 캡처는 중앙 보관소의 `ArchiveStore/02_Measurements/WingerWrist_*`에 있다. 군중 부하와 네트워크 역할별 재생은 별도 검증 범위다.

루트 공간 적용 뒤 같은 보행 위상의 PIE를 다시 비교했다. 캐릭터 기준 왼쪽·오른쪽 끝 본은 스프린트에서 각각 31.7·27.3cm 안으로, 5.6·10.6cm 뒤로 이동했고, 높이는 2.8·4.1cm 낮아졌다. 스프린트 점프에서 손목 기준 두 바깥 패널의 최대 폭은 공중 포즈 재제작 전 219.6cm에서 104.7cm로 줄었다. 새 시퀀스의 스킨 바인드·정점·가중치 지문은 이전 제작본과 일치했다. 비교 원자료와 후방 캡처는 `ArchiveStore/02_Measurements/WingerFoldDirection_*`에 있다.

후속 강도 조정은 같은 위상의 정수직 PIE로 검증했다. 이전 포즈보다 바깥 패널의 수평 방향이 양쪽 모두 달리기에서 약 3°, 스프린트에서 약 4°, 이륙·공중·착지에서 약 2–3° 더 뒤로 돌았다. 같은 스프린트 프레임에서 왼쪽과 오른쪽 끝 본은 각각 5.4cm와 8.2cm 더 안쪽으로 움직였다. 원자료와 캡처는 `ArchiveStore/02_Measurements/WingerFoldTuning_*`에 있다.

### 직접 재생 전환 당시 검증

최종 Development Editor 직접 UBT 빌드와 D3D12 PIE를 통과했다. 653프레임에서 걷기·달리기·이동 점프·제자리 점프·착지, LOD2 진입·복귀 및 루트 모션 몽타주로 126.66cm 이동을 확인했다. 근거리 이동·점프 구간의 프레임 사이 최대 회전 변화는 손목 13.13°, 팔꿈치 20.51°였다. 몽타주는 공용 루트 이동 검증에 사용했으며 Winger 전용 무기 소켓과 타격은 별도 제작 범위다.

새 에디터 세션에서 압축 포즈 1,725개를 평가했다. 네 사지 IK 위치 오차는 일반 시퀀스 최대 0.013cm, 가산 포즈 포함 최대 0.263cm였다. 원본 대비 굽힘 각도 차이는 손목 최대 11.07°, 팔꿈치 최대 4.21°였다. 재수입 후 메시·Skeleton 사이에서 발견된 바인드 8개를 일치시켰고, 가산 Lean 5개를 새 바인드에서 다시 압축했다. 최종 검증에서 바인드 불일치는 0개이며, 제작용 라이브러리를 참조하는 외부 패키지는 발견되지 않았다.

원자료는 중앙 보관소 `ArchiveStore/02_Measurements/WingerDirect_*.json`과 `ArchiveStore/03_Logs/WingerDirect_*.log`에 있다. LOD2 강제 근접 캡처에서는 얇은 의상과 깃털 디테일이 줄어든다. 자연 거리 전환의 전체 시각 검수, 200명 성능, 멀티플레이와 Cook은 별도 검증 범위다. [Epic의 Skeletal Mesh LOD 제작 기준](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-lods-in-unreal-engine)

## 이전 실시간 리타깃 제작 기록

- `BP_Winger`는 `BP_Player_Master`의 자식이다. Manny source에서 `ABP_Player_Master`를 상속한 `ABP_Winger`가 공통 모션매칭을 평가한다. 숨은 source는 포즈 평가를 유지한다.
- `WingerVisual`은 `VisualFollower`로 지정된 별도 메시다. `ABP_Winger_Body`가 `RTG_Manny_to_Winger`로 source 포즈를 받고, `a_01` 이하의 Idle 날개 레이어를 합성한다. Family/Body 프로필은 Winger의 골격 쌍을 명시한다.
- 기존 123본의 피부 가중치와 계층을 유지하고 발바닥 기준 `root`를 추가했다. 원본의 inch 태그를 그대로 적용하면 몸체가 지나치게 커져 제작 사본에서 원본 수치 1을 1cm로 해석했다. 메시와 모션에 같은 축척을 적용했다.
- UE에 원본 애니메이션 70개와 날개 레이어 5개를 저장했다. 원본 SMD의 프레임 속도가 제공되지 않아 30fps로 변환했다. Manny Idle, Walk, Jog도 Winger 골격으로 베이크하고 확인용 FBX를 내보냈다.
- 검은 자체 루트와 보조 본 두 개를 보존한 별도 FBX/UE 에셋이다. 몸체 FBX와 SMD의 오른손 부착 마커 차이가 90.27cm여서, 실제 장착은 Winger 전용 그립 위치를 정한 뒤 연결한다.

`/Game/Characters/Winger/Test/L_Winger_Test`를 열고 Play를 실행하면 `BP_Winger`를 확인할 수 있다. UE 5.8 Development Editor direct UBT 빌드와 D3D12 PIE 시험을 실행했다. PIE에서 저장한 마스터 자식 ABP와 follower ABP가 생성됐고, Winger 머티리얼이 메시 슬롯에 연결됐다. 실제 Enhanced Input 이동과 점프 입력을 사용했다. 약 308.8cm 수평 이동, 120.7cm 높이 변화, follower의 왼발 54.7cm 및 날개 66.2cm 최대 위치 변화를 관측했다. 측정값은 시험 시작 포즈 대비 변화량이다.

Blender FBX 왕복에서 124본의 이름과 부모, 20,960정점을 비교했다. 최대 위치 차이는 본 0.000103cm, 정점 0.000031cm였다. UE는 첫 변환의 루트 스케일 100을 노출했고, 센티미터 장면으로 다시 내보낸 사본에서 루트 스케일 1을 확인했다. UE의 legacy FBX 임포터는 복수 메시의 bind pose를 재생성했다. 재생성 후 UE에서 124본, 32,759삼각형, 미가중 정점과 잘못된 영향 본 0개를 확인했다. Blender MCP 연결 실패로 공식 포터블 Blender 4.5.15를 별도 프로세스에서 사용했다.

비행, 보조 본 물리, LOD·접지 튜닝, 무기 장착, 직업 선택과 Winger 스킬은 제작이 남았다. PIE 시험은 단일 로컬 플레이로 진행했다. 멀티플레이·Cook·전체 클립의 시각 검수는 진행하지 않았다. 첫 PIE에서 공통 프로필의 전투 진입 몽타주 미지정 경고가 발생했다. Winger 전용 전투 프로필 작업 시 해당 설정을 확인한다.

## 지상 이동·점프·착지 날개 전환

`ABP_Winger_Body`의 기존 `a_01` 레이어에 Idle, Walk, Run, Jump, Land를 연결했다. `UProject_JWingerAnimInstance`는 실제 Mover 수평 속도와 공중·지상 상태를 읽는다. 속도 경계에는 히스테리시스를 적용하고, 지면 복귀 때 착지 클립을 한 번 재생한다. 기존 Manny source와 공통 모션매칭을 유지했다. 마스터 BP/ABP는 HEAD의 LFS 해시와 일치했고, `BP_Winger`, source `ABP_Winger`, 메시, Skeleton은 이전 내보내기 사본과 동일했다.

Blender에서 원본 Idle·Falling·Sky Begin을 참고해 30fps의 날개 전용 Jump 0.667초와 Land 0.4초를 제작했다. Blender에서 본체·머리 장식 20,960개 정점의 영향 본과 개별 가중치를 원본 FBX와 비교했다. 최대 가중치 차이는 0이고, 원본 123본과 추가 지면 루트가 유지됐다. 텍스처는 원본 파일을 Blender에 상대 경로로 연결했으며 파일 연결 20개를 확인했다.

UE 5.8 Development Editor direct UBT와 D3D12 PIE를 통과했다. Enhanced Input을 통해 걷기 165cm/s, 달리기 500cm/s, 점프 상승 약 121.6cm, 착지 후 달리기 및 정지 복귀를 확인했다. 340개 포즈를 기록했다. 날개 앵커 공간의 왼쪽 끝 본은 공중 약 25.5cm, 착지 약 21.3cm 이동했고, 연속 표본 사이 최대 변화는 2.74cm였다. 바탕화면 Winger의 `Verification/WingMotionsPIE.json`, `Verification/WingMotionBlenderSkin.json`에 원자료가 있다.
