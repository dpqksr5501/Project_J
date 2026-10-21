# 플레이어 애니메이션 라이브러리와 추가 수입

실제 로코모션은 Manny `SK_Mannequin` 기준이다. 최초 이관한 448개 시퀀스·10개 BlendSpace의 원본 계약과 결과를 [라이브러리 카탈로그](../../../Scripts/Authoring/Data/PlayerAnimationLibrary.json)에 기록한다. 현재 모션·노티파이와 독립적인 원본 무릎 표본을 검사하므로, 회귀 실행을 위해 UEFN 원본 뱅크를 다시 복원하지 않는다. OTM/Strafe와 기존 공중 재선택·재생 연속성은 이번 보관/수입 도구의 수정 대상이 아니다.

## 원본 보존과 현재 제작 도구

원본과 연관 제작 자원은 [별도 보관](../../../Archives/AnimationSources/README.md)한다. ArchiveManifest는 정확한 바이트의 복원 근거이고, PlayerAnimationLibrary는 모션 식별·재생 계약·Manny 결과의 검증 근거다. 서로 대체하지 않는다. 카탈로그가 있다고 원본을 재생성할 수 있는 것은 아니다.

`/Game/Animation_Logic/RTG_Player_UEFN_to_Manny`는 다음 수입에 필요한 공식 변환 recipe다. 현재 연결된 source rig/mesh와 그 미리보기 의존 자원은 유지한다. source mesh가 쓰는 두 Lean pose는 생산 로코모션 후보가 아니라 이 제작 도구의 의존 자원이다. `GASP58Native` 이름만 보고 전체 폴더를 지우지 않는다. 공용 모션의 Foley·노티파이·커브·압축/수정 도구도 해당 폴더에 남아 있다.

## 중복 판별

이름이나 `.uasset` 파일 해시 하나로 모션을 합치지 않는다. 파일 해시는 동일한 보관 패키지 확인에 사용하며, 수입 판별은 다음 조합을 쓴다.

| 항목 | 의미 |
| --- | --- |
| `source_id` | 제공 라이브러리와 원본의 논리 키. 현재 Epic 모션과 프로젝트 고유 Jump 원본을 구분한다. 버전이 바뀌었다고 기존 ID를 임의로 새 ID로 바꾸지 않는다. |
| `source_motion_fingerprint` | 실제 reference skeleton의 본·부모·bind pose, 정렬한 본 트랙의 원시 transform, frame rate·길이, curve·attribute를 C++에서 BLAKE3로 계산. 에셋 이름·경로·model GUID는 포함하지 않는다. |
| `source_contract` / `source_signature` | root motion·additive·가산 base raw 지문·스켈레톤 정책 파일과 재생 설정·notify timing·서버 정책·클래스·페이로드를 포함한다. BP notify 클래스 패키지가 달라져도 검토한다. |
| `profile_fingerprint` | 명시한 RTG·source/target rig·mesh 패키지 해시의 조합. 다른 보정으로 만든 결과를 동일 변환본으로 간주하지 않는다. |
| `target_package` / target 지문·계약 | 이미 승인된 Manny 결과. 제작자가 변경한 결과를 자동으로 덮어쓰거나 원래 결과라고 재사용하지 않는다. |

같은 ID·내용·recipe는 기존 결과를 재사용한다. 이름/ID가 달라도 내용·계약·recipe가 같고 결과가 하나면 재사용 후보다. 동일 내용의 기존 결과가 여러 개면 의도된 variant일 수 있으므로 사용자가 선택해야 한다. 기존 이름의 내용이 달라졌거나 recipe가 바뀌면 `review`로 막는다. 같은 요청에 내용이 같은 새 소스 둘이 있으면 새 결과는 하나만 계획한다. Windows/Unreal 이름의 대소문자 충돌도 검출한다.

지문은 동일 데이터와 계약을 판별하는 도구다. 겉보기에 비슷한 모션의 자동 병합이나 전체 의미 동등성 증명은 아니다. curve/attribute의 저장 순서, 패키지 재저장, 엔진 버전이 달라지면 보수적으로 검토가 필요할 수 있다. fingerprint 형식/엔진 변경 시 기존 baseline을 현재 결과로 무조건 재생성하지 않는다.

## 다음 애니메이션을 가져오는 순서

1. 원본을 격리된 제작/staging 위치에서 확인한다. 기존 생산 모션에 직접 덮어쓰지 않는다. 원본 버전·라이브러리·논리 키와 원하는 Manny 경로를 명시한다.
2. [수입 사전 검사](../../../Scripts/Authoring/Plan-PlayerAnimationImport.py)를 Project J 에디터에서 실행한다. 아래 JSON의 각 행을 실제 소스에 맞게 작성한다.
3. 결과의 `reuse`는 기존 경로로 연결한다. `review`는 변경된 원본, Foley 변환 필요, recipe 차이, variant 또는 출력 충돌을 해결한 뒤 다시 검사한다. 자동 `_1` 접미사로 복제하지 않는다.
4. `new`만 공식 IK Retarget Batch Operation으로 Manny에 변환한다. `overwrite_existing_files=False`, `include_referenced_assets=False`, `retain_additive_flags=True`를 유지한다. 추가 참조 애니메이션·가산 base·BlendSpace가 필요하면 별도로 수집/계획하여 기존 결과를 재사용한다.
5. raw 모션·root/additive/notify 계약, source→Manny 골격/손발·IK helper를 검증한다. 카탈로그에 원본 식별/지문과 실제 결과를 추가한다. 새 모션을 선택할 Chooser/PSD 연결은 의도한 gameplay 변경으로 검토한 뒤 적용한다.
6. 관련 Manny 회귀와 실제 선택·포즈를 확인한 뒤 staging을 정리한다. 원본은 보관하고 현재 라이브러리 카탈로그를 갱신한다.

```json
{
  "assets": [{
    "source_id": "Epic.GameAnimationSample.UEFN/Walk/M_Relaxed_Walk_Loop_F",
    "source_package": "/Game/Developers/AnimationImport/Incoming/M_Relaxed_Walk_Loop_F",
    "target_package": "/Game/Characters/Mannequins/Animations/Locomotion/Walk/M_Relaxed_Walk_Loop_F"
  }]
}
```

```python
import runpy, unreal
from pathlib import Path
root = Path(unreal.Paths.project_dir())
runpy.run_path(str(root / 'Scripts/Authoring/Plan-PlayerAnimationImport.py'),
              run_name='__main__',
              init_globals={'PLAYER_ANIMATION_IMPORT_REQUEST': str(root / 'Saved/AnimationImport/Request.json')})
```

결과는 `Saved/AnimationImport/Plan_*.json`이다. 이 도구는 **실제 에셋을 수정하지 않는 사전 검사**다. `can_apply`와 plan 해시는 해당 catalog·recipe·소스 상태의 검토 근거이지, 다음 실행까지 유효한 자동 수입 허가가 아니다. 실제 수입 직전 소스·출력·recipe가 변하지 않았는지 재검사해야 한다. BlendSpace/몽타주의 자동 수입·Chooser/PSD 자동 확장은 구현하지 않았으며 기존 엔진 변환과 명시적 연결 절차를 사용한다.

신규 upstream 원본에 프로젝트 Foley로 변환되지 않은 notify가 있으면 raw motion이 같아도 계약 차이를 검토해야 한다. 기존 원본의 수정 상태와 새 upstream 원본의 차이를 숨겨서 재사용하지 않는다.

## 회귀와 복원

`CHT_RotationOffsetCurve`의 context는 native `FProject_JPlayerRotationOffsetQuery`다. MovementMode/MovementDirection 열은 native enum에 바인딩하고 기존 방향 커브·행 우선순위를 편집한다. BP UserDefinedStruct의 생성 필드명 접두사에 런타임 코드를 의존시키지 않는다. 구형 제작 table의 명시적 이주가 필요할 때는 `unreal.Project_JPlayerLocomotionLibrary.use_native_rotation_offset_query(table)`를 사용한다. 이 도구는 PIE 밖에서 구조를 확인하고 전체 36개 mode/direction의 결과를 비교하며, 성공 후 제작자가 해당 table만 저장한다. 현재 table에 다시 적용하면 변경하지 않는다. 함수 반환 성공만으로 임의의 다른 table을 자동 저장하거나 기존 참조 자산을 삭제하지 않는다.

지상 Walk/Run slopes BlendSpace는 `bShouldMatchSyncPhases=true`, `bAllowMarkerBasedSync=false`로 검색과 실제 재생의 정규화 시각을 맞춘다. UE 5.8의 마커 위상 맞춤 경로는 실제 재생의 sample delta를 0으로 만들어 Steering이 멈출 수 있으므로, 검색 경고를 없애려고 마커 기반 동기화를 함께 켜지 않는다. 신규 지상 루프는 `unreal.Project_JPlayerLocomotionLibrary.configure_normalized_locomotion_sync(asset)`를 호출해 캐시까지 갱신한 뒤 해당 에셋만 저장하고, `ProjectJ.PlayerLocomotion.GroundLoopRootMotion` 및 [실제 루프 회전](../../../Scripts/Validation/Validate-PlayerLocomotionLoopTurning.py)을 검사한다. 다른 용도의 BlendSpace는 각 계약을 유지한다.

루프 회전 PIE 검사는 넓고 비어 있는 바닥의 새 세션에서 실행한다. 카메라 yaw와 전방 입력을 유지하며 실제 BlendStack 플레이어·표시 root·물리 yaw를 함께 기록한다. 전투 전환은 해당 PIE world의 Enhanced Input 액션으로 보내며, 에디터 Python에서 client의 GAS 능력을 직접 호출하지 않는다. 직접 호출은 다중 PIE의 RPC world 문맥에서 재귀를 일으킬 수 있다. 각 구간 시작에 중간 기록을 저장하고 종료 시 입력·카메라·걷기·background throttle을 복원한다. 테스트용 세션은 기록 후 종료하며 생산 에셋을 저장하지 않는다.

Strafe Sprint 공중 8방향 검사는 32초 연속 경로를 사용하므로 최소 40,000-unit 정방형의 비어 있는 바닥을 준비한다. 기본 맵 바닥은 약 16,335-unit으로 마지막 구간을 수용하지 못한다. 네트워크 PIE에서는 클라이언트뿐 아니라 authority에도 같은 임시 바닥이 있어야 한다. PIE 밖 생산 레벨을 수정·저장하거나 바닥 밖 추락을 착지 로직 실패로 판정하지 않는다.

2026-10-21 사용자가 저작한 Sprint→Run 왼발·오른발의 `Transition To Loop / Always` 구간은 Manny 타깃 계약에 반영했다. 원본 변환 계약과 모션 지문은 보존하고 기존 12개 노티파이의 이름·시각·기간·클래스·서버 정책이 그대로인 것을 확인했다. 추가 구간의 현행 서버 정책도 사용자 저장값을 유지한다. 새로운 저작 변경은 의도와 보존 항목을 확인한 뒤 타깃 계약만 갱신하며, 예상과 다른 모션·notify 전체를 무조건 새 baseline으로 덮지 않는다.

- [Manny 데이터·노티파이·손발 검증](../../../Scripts/Validation/Validate-MannyRetargetedKinematics.py)은 카탈로그의 독립 baseline과 현재 결과를 비교한다. 원본 뱅크를 로드하지 않는다.
- `ProjectJ.PlayerLocomotion.MannyConvertedLibrary`는 실제 에셋 registry와 카탈로그의 양방향 완전성, Manny skeleton, 재생/notify와 BlendSpace sample을 검사한다.
- `ProjectJ.PlayerLocomotion.AnimationSourceFingerprint`는 실제 모션 트랙을 임시 모델에 넣고 이름/경로 변경의 동일성, raw key/notify 변경의 구별, 원본 불변을 검사한다.
- `Scripts/Tests/test_animation_import_catalog.py`는 이름 변경·재수입·revision·recipe·경로 충돌·한 요청 안의 중복을 검사한다.
- [복원 도구](../../../Scripts/Authoring/Restore-AnimationSources.py)는 ZIP 무결성·manifest·경로·기존 파일 충돌을 먼저 검사한다. 명시한 package의 보관된 의존성을 포함하고 다른 현재 데이터를 덮어쓰지 않는다.

원본 비교가 다시 필요한 재제작·새 retarget recipe 검증은 명시적으로 복원 후 별도 작업한다. 카탈로그의 현재 baseline을 원인 확인 없이 새 결과로 덮어써 회귀를 통과시키지 않는다.
