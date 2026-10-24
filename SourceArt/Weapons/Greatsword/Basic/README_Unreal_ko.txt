Gothic Greatsword · Project_J용 최적화 출력

기본 파일
SM_GothicGreatsword_MMO.fbx       LOD0: 14,980 삼각형 / Blender 정점 7,778개
SM_GothicGreatsword_MMO_LOD1.fbx  LOD1:  7,490 삼각형 / Blender 정점 4,033개
SM_GothicGreatsword_MMO_LOD2.fbx  LOD2:  3,744 삼각형 / Blender 정점 2,160개

세 단계 모두 재질 슬롯 1개입니다. 원래 v74 게임용 LOD0의 92,398 삼각형 대비 새 LOD0의 삼각형 수는 약 83.8% 감소했습니다.
UV·하드 노멀·탄젠트 경계에서 렌더 정점이 나뉩니다. 추정 렌더 정점은 LOD0 21,430 / LOD1 12,276 / LOD2 7,533개이며 실제 Unreal 임포트 결과와 차이가 날 수 있습니다.

SM_Sword 피벗·회전 기준
- 제공한 Desktop/SM_Sword.FBX와 실제 FBX 축·축 부호·센티미터 단위를 비교했습니다.
- 각 FBX의 피벗과 로컬 이동·회전은 0, 스케일은 1입니다. 검을 지오메트리 중심으로 재배치하거나 회전하지 않았습니다.
- 피벗은 손잡이에 있던 기존 원점입니다. Blender에서 검날 방향은 +Y, 폭 X, 위쪽은 Z입니다.
- 원본 SM_Sword와 같은 Unreal 임포트 옵션으로 수입하면 기존 스켈레탈 부착 소켓의 위치·회전 기준을 유지할 수 있습니다.
- 파일의 검은 Static Mesh입니다. 소켓이 달린 기존 Skeletal Mesh는 그대로 사용하며 새 검을 Skeletal Mesh로 수입할 필요가 없습니다.
- 기존 에셋의 Import Rotation / Uniform Scale / Convert Scene / Force Front X Axis 등을 바꿔 맞춘 이력이 있으면 그 설정도 동일하게 사용하세요. 기본 입력은 Import Translation 0, Rotation 0, Uniform Scale 1입니다.
- FBX 재수입 후 원점과 크기를 대조했습니다. 최대 경계 오차는 Pivot_Validation.json에 기록했습니다.

소켓 두 종류의 구분
스켈레탈 본에 만든 부착 소켓의 회전은 이번 출력 때문에 다시 맞추지 않습니다.
메인 FBX에는 원본 SM_Sword에서 읽은 WeaponGrip_R / WeaponGrip_L / WeaponGroundProbe_Tip / WeaponGroundProbe_Base 소켓을 함께 넣었습니다.
실제 Project_J의 Unreal 5.8.4에서 원본의 FBX 임포트 옵션으로 새 FBX를 임시 수입해 네 소켓의 위치·회전·스케일이 일치하는지 확인했습니다. 기존 에셋이나 스켈레탈 부착 소켓은 수정하지 않았습니다.
원본 임포트 옵션: Translation 0 / Rotation 0 / Uniform Scale 1 / Convert Scene 켬 / Convert Scene Unit 끔 / Force Front X Axis 끔 / Transform Vertex to Absolute 켬 / Bake Pivot in Vertex 끔.
위 검증은 기존 에셋과 같은 FBX 임포터를 기준으로 했습니다. 다른 임포트 파이프라인을 쓰면 소켓을 포함한 결과를 확인하세요.

LOD 등록
LOD0 FBX를 수입한 뒤 Static Mesh Editor에서 LOD1/LOD2 FBX를 해당 단계로 수입하세요.
Project_J의 현재 제작 기본값은 LOD1 화면 크기 0.35 / LOD2 0.18입니다. 시작값이며 실제 게임 화면에서 전환을 확인하세요.

텍스처·재질
Textures/의 모든 맵은 완성된 2048×2048 버전입니다. 사용자의 선택에 따라 추가 4K 베이크는 중단했으며 이 패키지에는 2K 맵만 포함했습니다.
BaseColor: sRGB 켬 → Base Color
Normal_DirectX: sRGB 끔, Normalmap 압축 → Normal. 이미 DirectX 방식이므로 Green 채널을 다시 반전하지 않습니다.
ORM: sRGB 끔, Masks 압축 → R=Ambient Occlusion, G=Roughness, B=Metallic
Normal_OpenGL은 Blender 확인용입니다. Unreal에는 DirectX 파일을 사용하세요.
FBX 재질 자동 수입만으로 ORM 채널 연결을 보장하지 않습니다. 위 채널대로 재질 하나에 연결하세요.

확인한 범위
- LOD0–2의 열린 경계·비정상 연결 엣지·퇴화 면 0개.
- 원본 FBX 축·단위·피벗 변환 비교와 FBX 재수입 검사 통과.
- Unreal에서 실제 임시 FBX 수입 후 메시 경계와 소켓 4개 좌표·회전·스케일 일치 검사 통과. 프로젝트에는 수입한 임시 에셋을 저장하지 않았습니다.
- 게임용 Blender 파일을 새로 열어 메시 3개 / 재질 1개씩 / 원점 유지 / 누락 텍스처 0개 확인.
- 검토 이미지는 최적화한 실제 메시와 베이크된 맵으로 렌더링했습니다.
- 실제 캐릭터의 플레이 중 장착·애니메이션 간섭·충돌·200명 장면 성능 측정은 수행하지 않았습니다. 삼각형 감소율을 FPS 개선율로 해석하지 않습니다.

별도 파일 GothicGreatsword_MMO_GameReady.blend에는 게임용 LOD와 연결된 재질을 넣었습니다.
LOD1/2는 겹쳐 보이지 않도록 숨겨 놓았습니다. 편집 원본은 이전 GothicGreatsword_Review 폴더에 보존되어 있습니다.
