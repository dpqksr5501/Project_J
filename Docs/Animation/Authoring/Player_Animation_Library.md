# 플레이어 애니메이션 라이브러리와 추가 수입

실제 로코모션은 Manny `SK_Mannequin` 기준이다. 최초 448개에 Neutral 127개를 추가한 **시퀀스 575개·BlendSpace 11개**의 원본 계약과 결과를 [라이브러리 카탈로그](../../../Scripts/Authoring/Data/PlayerAnimationLibrary.json)에 기록한다. 현재 모션·노티파이와 독립적인 원본 무릎 표본을 검사하므로, 회귀 실행을 위해 UEFN 원본 뱅크를 다시 복원하지 않는다. OTM/Strafe와 기존 공중 재선택·재생 연속성은 유지한다.

## 현재 표현 구성

Run·Sprint와 연결된 시작/정지·방향 전환·보행 전환·점프/착지는 Neutral을 사용한다. Idle과 Walk의 일반 루프·시작/정지는 기존 Relaxed를 유지한다. 공중 Chooser의 Neutral 21개는 다시 변환하지 않았고 Ground의 점프 연결에서도 기존 Neutral 16개를 재사용했다. 신규 변환은 Run 65개·Sprint 20개·Walk 연결 4개·Jump 38개다.

[Neutral 연결 명세](../../../Scripts/Authoring/Data/PlayerNeutralLocomotion.json)는 원래 자산과 실제 대체 자산·선택 이유를 기록한다. 이는 별도 runtime 선택기나 중복 writer가 아니라 제작 근거다. 실제 선택 권위는 기존 `CHT_ProjectJ_Ground`/`CHT_ProjectJ_Air`와 C++ 상태 로직이다. Chooser 조건·행 순서·출력·mirror/sample 설정을 유지하며 자산 참조만 이주했다. Relaxed 자산은 독립 제작 variant와 기존 카탈로그 기준점이므로 삭제하지 않는다.

- Neutral에 동일한 Reface Stop/Spin 동작이 없으면 발/방향에 맞는 Neutral Stop·Pivot·Turn·Box로 연결한다. 장식적 360도 Spin은 180도 Turn으로 대체한다. 원본과 동일한 회전 연출이라는 의미가 아니며 실제 급회전 품질은 플레이에서 검수한다.
- Neutral에는 Run 오르막/내리막 전용 원본이 없어 `BS_Neutral_Run_Loop_F`를 프로젝트에서 구성했다. 기존 입력 축과 정규화 동기화 계약을 유지하고 Neutral 전방 Run을 사용한다. 기존 slope/foot placement 보정은 유지하지만, 전용 경사 모션이 추가됐다는 의미는 아니다.
- 새 시퀀스는 기존 공식 RTG의 최종 Manny IK helper 재구성을 거친다. 커브 이름·float 키, sync marker, notify timing/payload, root/additive 계약을 비교한다. 프로젝트의 `ForceRootLock`/정규화 root scale 정책은 명시적으로 적용한다. cosmetic Foley는 native notify로 변환하며 Branch In은 staging에서 제거하고 생산의 명시적 PSD 연결을 사용한다.
- 새 Sprint→Run은 Neutral 원본의 전이 구간을 사용한다. 과거 Relaxed 클립에 사용자가 만든 `Transition To Loop / Always` 구간은 그 Relaxed variant에 보존하며 Neutral에 무조건 복사하지 않는다.

2026-10-22 Sprint→Run 표현 확인에서는 OTM/Strafe 각 4개 해제 시점을 실제 PIE에서 추적했다. 양발 `M_Neutral_Transition_Sprint_to_Run_Lfoot/Rfoot`의 실제 재생과 Neutral Run Loop 복귀를 확인했고 Relaxed 전환 플레이어는 0개였다. 두 Neutral의 raw motion/재생 계약도 카탈로그와 일치하고, 구 Relaxed 전환 둘의 registry referencer는 없었다. 이는 선택 연결 확인이며 감속 제스처의 주관적 차이를 보증하지 않는다. 상세 근거는 `Saved/Validation/SprintRunStyle_20261022`다. 연결과 클립은 유지했다.

추가 원본 130개(시퀀스 127개·notify BP 의존성 3개)는 [Neutral 보관본](../../../Archives/AnimationSources/Neutral_2026-10-22/ArchiveManifest.json)에 정확한 원본 바이트로 보존한다. source kit는 공유하고, 변환 중에만 쓰는 원본 모션을 생산 Content에 계속 남기지 않는다.

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

원본 `/Game` 경로가 현재 프로젝트와 겹치면 새 에디터 세션에서 명시한 staging package와 package-remap으로 `prepare_staged_animation_sources`를 사용한다. 이 editor C++ 도구는 notify BP를 scoped linker 문맥에서 먼저 로드해 부모 class 누락에 따른 payload 손실을 막는다. upstream PSD는 임시 메모리 객체로 격리하고 staging의 Branch In만 제거하며 생산 PSD는 변경하지 않는다. 반환 개수와 strict Foley dry-run을 확인한 뒤 해당 원본만 저장한다. `Notify=None`을 정상 named marker로 간주하거나 누락 허용 목록을 추가하지 않는다. 실제 전이는 명시적 Manny 변환과 기존 참조 이주 도구로 수행한다.

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

### Neutral 확장 검증 — 2026-10-22

- direct Editor UBT 성공, 새 프로세스 자동 회귀 81개 성공(실패·미실행·경고 0). `NeutralSelection`은 master의 실제 Ground/Air Chooser, 활성 행과 fallback, embedded PSD 및 BlendSpace sample을 따라가 Run/Sprint/Jump의 Neutral 선택과 Relaxed Walk 유지를 검사한다. `GroundLoopRootMotion`은 새 Neutral Run과 기존 제작 variant의 루프 경계 root delta를 검사한다.
- 새 원본 127개의 커브 716개와 sync marker, notify timing/payload·재생 계약 비교 완료. Foley 1,121개를 strict dry-run 후 native로 변환했고 실행 payload가 없는 Foley는 0개다. Chooser column/context/출력/disabled 설정 751줄은 계획한 자산 참조 치환 외 변경이 없다. 이 수입 당시 SamplingRange는 기본 전체 클립 구간이었다. 후속 착지 검색 보정은 아래 별도 기록을 따른다.
- Manny 라이브러리 575개·authored-frame 표본 2,875개에서 손발 helper 최대 위치 오차 약 0.0202cm. 새 Neutral Run/Sprint는 독립 원본 주기 표본 대비 무릎 굽힘 최대 차이 각각 약 0.814도/0.716도다. 모든 프레임·모든 체형의 시각 품질을 보증하는 수치는 아니다.
- 실제 PIE: Ground 시작/정지·OTM/Strafe·Walk/Sprint, Sprint 170/179/181/190도 회전 양 모드 8조건, 공중 방향 변경/착지 8조건, 루프/감속 후 연속 마우스 회전 5조건 성공. 공중 재선택 2회, playhead 되감김·invalid 선택 0. 루프 표시 root와 actor yaw 최대 차이는 약 1.67도 이내다.
- import/catalog Python 12개·원본 복원 Python 7개 성공. 새 보관 ZIP 130개 파일 무결성 확인. 임시 원본 130개는 hard/soft/search/management 외부 referencer가 없는 것을 확인하고 에디터 정상 종료 후 정리했다. 생산 Manny·기존 Relaxed variant·공유 제작 kit·기존 보관 ZIP은 유지했다.

이번 검증은 새 표현 구성의 데이터·동작 계약을 확인한다. 대규모 MMO 부하, 모든 경사/체형·전 클립 전 프레임의 주관적 품질 검수와 패키지 Cook은 수행 범위에 포함하지 않았다. 로컬 상세 근거는 `Saved/Validation/NeutralLocomotion_20261022`와 `Saved/Validation/PlayerLocomotion/Neutral_20261022_Final` 및 같은 날짜의 PIE JSON이다. 에디터는 정상 종료했고 background throttle 설정도 원래 값으로 복원했다.

### 정지·발 보정 점검 — 2026-10-22

사용자가 조정한 Neutral Sprint→Run L/R의 `Transition To Loop / Always` 구간은 현재 디스크 저장값으로 보존하고 타깃 카탈로그만 갱신했다. 원본 계약·모션 지문과 기존 L 12개/R 11개 노티파이를 유지했다. 이 조기 Loop 구간은 정지 후보 필터와 별개다.

Run 500에서 Sprint Stop이 실제 선택되는 것을 확인해 `Stand Stops`에 최근 gait Enum 필터를 추가했다. Run/Sprint 은행을 분리하되 저속 Walk 정지·좌우 발·방향·Pose Match·기존 blend 출력은 유지한다. 제작 도구 `configure_standing_stop_gait_filter`는 PIE 밖에서 명시적으로 실행하고 대상 Chooser만 저장한다. 새로운 은행/다른 후보 구조는 자동 추정하지 않으며, 새 행을 추가할 때 필터 조건도 함께 저작한다. 원본 시퀀스나 접촉 커브는 변경하지 않는다.

[정지 검사](../../../Scripts/Validation/Validate-PlayerLocomotionStopping.py)는 새 PIE의 평지와 `PLAYER_STOP_SLOPE_PITCH=12` 경사면에서 OTM/Strafe Run/Sprint를 구동한다. 실제 정지 클립·Idle 복귀·접촉이 낮아진 발의 잠금 해제와 발 보정 유지까지 기록하고, 임시 바닥 변환을 복원한다. 실행 중 다른 입력/에디터 콘솔 명령을 보내지 않는다. 이 검사는 발바닥 외형 검수를 대신하지 않는다. [루프 회전 검사](../../../Scripts/Validation/Validate-PlayerLocomotionLoopTurning.py)도 Manny와 Bip01 무릎의 부모 기준 회전을 기록해 이동 중 포즈 고정을 검사한다.

이 후속 변경 기준 direct UBT Editor 빌드와 native 회귀 83개가 성공했다(실패·경고·미실행 0). 별도 PIE의 평지/12도 경사 정지 8조건은 올바른 Stop 선택과 Idle 복귀, foot correction 활성, 낮은 접촉에서 지속 잠금 0초를 확인했다. Walk/Run/Sprint/Sprint→Run/Strafe Run 루프 회전 5조건도 성공했고 source와 대검의 무릎 포즈가 계속 갱신됐다. 근거는 `Saved/Validation/StopFeet_20261022` 및 `Saved/Validation/PlayerLocomotion/StopFeet_20261022_Final`이다.

진단 도중 제작 도구의 `Modify` 이후 변경 완료 알림 누락으로 종속 인덱스가 중단된 상태도 재현했다. 도구가 `PostEditChange`까지 완료하도록 수정하고 새 세션에서 인덱스 회귀를 확인했다. 사용자가 이전 에디터에서 겪은 포즈 고정과 같은 증상이지만, 그 세션의 원인까지 동일했다고 확정하지 않는다.

### 속도·템포 튜닝 — 2026-10-22

Run 500 / Sprint 750cm/s, 지상 동적 재생률 1.0 유지와 Stride Warping 상한 1.5를 적용했다. Walk 165와 기존 OTM/Strafe·전방 세 방향 Sprint 승인·공중 재선택·공중 관성·몽타주 시간은 유지한다. [이동 계약](../Architecture/Player_Locomotion_Architecture.md#이동과-점프-계약)에 속도 목표/승인 상한, 프로필의 `Run Speed Limit` 표시(기존 직렬화 이름 `WalkSpeed`), GT→proxy 게시와 Pivot의 Sprint 관성 수용 범위를 설명한다. 향후 동적 지상 템포가 필요하면 프로필의 `bScaleGroundPlayRateWithSpeed`로 명시적으로 선택한다.

- direct Editor·Game Development UBT 성공. 최종 자동 회귀 82개 성공, 실패·미실행·경고 0. `GroundSpeedAndCadence`는 실제 master/대검의 목표·승인 속도와 컴파일된 Stride 인스턴스 전부의 상한·FK 길이 보호를 검사한다. 작성 노드 1개가 BlendStack 상태 플레이어별로 5개 인스턴스로 컴파일되는 것은 중복된 BP 계산이 아니다.
- 실제 네트워크 PIE의 OTM/Strafe Run·Sprint 4조건에서 정상 구간 속도 500/750, 실제 루프 재생률 1.0 확인. 지상 시작/정지·Walk/Sprint 전환, 750 Sprint의 170~190도 회전 8조건, 일반 점프/착지 8조건, Sprint 점프 후 입력을 Run으로 낮추고 방향을 바꾸는 착지 8조건, 연속 루프 회전 5조건 성공. Sprint 해제 뒤 공중 수평 속도 750 유지와 공중 재선택·playhead 연속성을 확인했다.
- [속도·템포 PIE 검사](../../../Scripts/Validation/Validate-PlayerLocomotionSpeedCadence.py)는 접촉 커브 구간의 source foot 세계 좌표 변화도 관측한다. 발 뼈의 움직임에는 발 구름/회전이 포함되므로 이를 고정 접촉점의 미끄러짐과 동일시하거나 전 구간 미끄러짐 0으로 승인하지 않는다. 표본에서 무릎 굽힘이 유지됐지만 모든 체형·경사·프레임의 시각 품질 승인은 별개다.
- Sprint 관성 착지 검사는 [기존 공중 검사](../../../Scripts/Validation/Validate-PlayerLocomotionAirLanding.py)에 `PLAYER_AIR_SPRINT_BEFORE_JUMP=True`를 전달한다. 임시 40,000-unit 바닥은 `get_pie_worlds(True)`로 dedicated authority까지 포함해 확장한 뒤 복원한다. 최초 client-only 바닥 확장에서는 서버의 기본 바닥 밖 추락으로 실패했고, 양쪽을 맞춘 재실행에서 통과했다. 생산 맵은 저장·변경하지 않는다.
- ABP 재컴파일 후 최초 PIE에서 PoseSearch 비동기 index 준비 경고 1회가 있었고 캐시 완료 후 후속 검사에서 재현되지 않았다. 최종 자동 회귀의 경고 0과 cold-DDC 초기 로드의 무경고 보장은 구분한다. 애니메이션·커브·Chooser·PSD를 다시 변환하거나 카탈로그 baseline을 덮지 않았다.

상세 근거는 `Saved/Validation/SpeedTuning_20261022`, `Saved/Validation/PlayerLocomotion/Speed500750_20261022_Verified`와 같은 날짜의 SpeedCadence/Ground/SprintTurn/AirLanding/AirSprintLanding/LoopTurning JSON이다. 에디터 정상 종료와 원래 background throttle 복원을 확인했다. Cook·Shipping·대규모 군중 부하는 이번 검증에 포함하지 않는다.
### 착지 접근 구간 재생 수정 — 2026-10-22

Shift를 계속 누른 Sprint 점프에서 물리 접지 직후 `M_Neutral_Jump_F_Land_Sprint_Light_Rfoot`가 약 0.01초부터 시작하는 것을 실제 PIE로 확인했다. 클립의 처음 약 0.5초는 낙하 접근 구간이다. 일부 Run Light와 달리 Sprint 원본에는 그 구간을 제외하는 노티파이가 없어 전체 구간의 Pose Match가 접근 포즈를 다시 골랐다. 이 기록에서 별도 Fall Loop가 재생됐다고 설명하지 않는다. 500/750 튜닝에 의한 순수 물리 오류라고 단정하지 않으며, 튜닝 이후 두드러진 기존 검색 범위의 결함으로 분류한다.

제작 함수 `configure_grounded_landing_search`로 Ground Chooser의 착지 전용 32행에 검색 시작 하한을 적용했다. 기준은 기존 native Land 이벤트와 전환 금지 구간 시작이며 한 원본 프레임의 접근 블렌드는 남긴다. 행 선택·후보·재생 출력·커브·노티파이·애니메이션 원본·보관 ZIP·카탈로그는 바꾸지 않았다. 독립 export 비교에서도 32행의 SamplingRange 외 변경은 없었다. 앞으로 착지 은행을 교체/추가한 뒤 이 함수를 PIE 밖에서 실행하고 해당 Chooser만 저장한다. 기존 구간과 양립하지 않는 제작 데이터는 추측해서 고치지 않고 실패한다. `GroundedLandingPhase`는 실제 embedded PSD 인덱스의 검색 가능 표본과 후보 존재를 확인한다.

[착지 검사](../../../Scripts/Validation/Validate-PlayerLocomotionAirLanding.py)는 `PLAYER_AIR_SPRINT_THROUGH_LANDING=True`로 Shift를 착지까지 유지한다. 각 OTM/Strafe에서 직선과 착지 직전/직후 90·180도 방향 변경, 총 10조건이다. 기본 Run 8조건과 `PLAYER_AIR_SPRINT_BEFORE_JUMP=True`의 공중 Sprint 해제 8조건도 보존한다. 실제 접지 이후 Mover의 재낙하, 공중 상태 재진입, 클립 접근 구간 재생과 직선 Sprint의 750 속도 손실을 함께 확인한다. 실행 중 다른 키 입력·콘솔 명령을 보내지 않는다.

착지 Foley는 기존 물리 착지 이벤트가 소유한다. 검색 시작 시각이 시퀀스의 Foley 마커 뒤로 이동해도 해당 오디오를 노티파이로 중복 재생하거나 별도 타이머로 보충하지 않는다. 원래 마커·발 접촉·IK 잠금 해제·경사 보정과 공중 방향 재선택/관성은 유지했다.

이 변경 기준 direct Editor UBT와 새 프로세스 native 회귀 **84개**가 성공했다(실패·경고·미실행 0). Shift 유지 착지 10조건, 공중 Shift 해제 8조건, 일반 Run 착지 8조건, Run/Sprint 속도·재생률 4조건, Sprint 170~190도 급회전 8조건, 연속 루프 회전 5조건과 지상 시작/정지·Walk/Sprint 전환이 통과했다. Shift 유지 직선 착지는 약 0.51초부터 시작하고, 속도 750을 유지하며 접지 후 다시 Falling으로 진입하지 않았다. 부모 기준 무릎 관측에서도 Manny와 대검 몸체가 정지 포즈로 굳지 않았다.

추가로 PIE 인스턴스의 점프 속도를 임시 900으로 올린 무거운 착지 10조건을 확인했다. 최초 40,000-unit 바닥에서는 마지막 경로가 바닥 밖으로 나가 실패했고, 120,000-unit 임시 바닥을 양 네트워크 월드에 적용한 재실행이 통과했다. 생산 맵·기본 점프 속도는 저장/변경하지 않았다. 이 시험을 모든 낙하 높이·지형의 검증으로 일반화하지 않는다.

Mover의 승인 속도, SmoothWalking의 속도별 가속/회전, trajectory snapshot, 공중 관성, Pivot 범위와 Chooser의 시작/감속/정지 조건을 함께 검토했다. 추가적인 Mover 강제 reset·속도 클램프 교체가 필요한 근거는 발견하지 못했다. 최종 발바닥 외형, 모든 경사·체형, Cook/Shipping·대규모 부하와 cache를 삭제한 cold-DDC 시작은 이번 검증 범위가 아니다. 인덱스 변경 직후 첫 PIE에는 비동기 재구성 대기 경고가 있었으며, 최종 native 회귀 경고 0과 구분한다. 상세 근거는 `Saved/Validation/LandingSpeed_20261022`와 `Saved/Validation/PlayerLocomotion/LandingSpeed_20261022_Final`이다.

에디터를 완전히 종료한 뒤 새 세션의 네트워크 PIE에서도 Shift 유지 착지 10조건을 재실행해 통과했다. 기존 DDC를 보존한 프로세스 재시작 검증이며, 캐시 삭제 시험은 아니다. 마지막에 미저장 패키지 0개를 확인하고 에디터를 정상 종료했다.

### 전환 검색 범위 추가 점검 — 2026-10-22

새 `TransitionSearchAvailability` 검사는 실제 Ground Chooser에서 활성 행과 fallback을 따라 도달하는 40개 테이블의 검색 행 308개를 확인한다. embedded PSD의 실제 인덱스에서 진입 가능한 포즈가 남아 있고, 시각이 유효하며 시퀀스의 검색 제외 구간이 선택 가능한 진입 프레임으로 돌아오지 않는 것을 확인했다. 비활성/도달 불가능한 제작 테이블을 생산 경로로 오인하지 않는다. direct Editor UBT와 확장된 native 회귀 85개가 성공했고 실패·경고·미실행은 0개다.

에디터에서 Sprint 착지의 진입 구간을 확인하려면 `CHT_ProjectJ_Ground` → `Locomotion Transitions` → `Sprint Mode Transitions`의 해당 착지 행 Pose Match 데이터를 확인한다. `SamplingRange`는 검색 후보 구간이며 고정 재생 시작값이 아니다. 실제 시작 시각은 Pose Match가 출력하는 StartTime이고, native BlendStack 입력으로 전달된다. 시퀀스에 새 Notify State를 추가하지 않았다. 기존 Land 이벤트/BlockTransition 시각을 제작 단계 계산 기준으로만 사용하며 기존 커브·노티파이·카탈로그를 보존한다.

[보행 전환 검사](../../../Scripts/Validation/Validate-PlayerLocomotionGaitTransitions.py)는 OTM/Strafe 각각 Walk→Run, Run→Walk, Run→Sprint, Sprint→Run, Walk→Sprint, Sprint→Walk의 12조건에서 실제 신규 재생 진입과 전환 전후 속도를 확인했다. 예상한 각 전환 클립이 모두 관측됐고 다른 출발/도착 gait의 신규 전환 선택은 없었다. 입력 적용 직전의 이전 클립 잔여 재생과 새 선택을 구분한다. 속도 후보 범위가 겹친다는 이유만으로 최근 gait 필터를 추가하거나 기존 전환 후보를 제거하지 않았다.

[중단·복귀 검사](../../../Scripts/Validation/Validate-PlayerLocomotionInterruptions.py)는 시작 중 취소, 빠른 Sprint→Run→Sprint, 정지 중 180도 재출발, 감속 중 점프를 양 회전 모드에서 총 8조건으로 확인했다. 정상 속도 복귀·valid 선택·발 보정 유지·점프 후 접지 복귀가 통과했고 Manny/대검 무릎의 부모 기준 포즈 고정도 관측되지 않았다. 기존 landing·공중 재선택과 발 접지 로직은 변경하지 않았다.

최초 보행 fixture는 모드 전환이 확정되기 전 검사해 중단됐다. 이후 모드 수립 대기와 실제 신규 클립 진입 관측을 보강한 독립 PIE에서 12조건이 통과했다. 이 최초 결과는 실행 근거와 함께 보존하며 생산 버그로 분류하지 않는다. 이번 점검에서 추가로 확정된 전환 결함은 없어서 생산 Chooser·ABP·시퀀스·카탈로그를 수정하지 않았다. 영구 검사와 문서를 보강했고, 미저장 패키지 0개 확인 후 에디터를 정상 종료했다. 상세 근거는 `Saved/Validation/TransitionAudit_20261022`와 `Saved/Validation/PlayerLocomotion/TransitionAudit_20261022_First`다. 전 시점/체형·경사에서의 주관적 품질, 모든 입력 순서와 네트워크 지연 조합의 무결함을 보장하는 검사는 아니다.


### 후진 이동 중 시선 급회전 — 2026-10-22

Strafe에서 S를 유지하고 시선을 급히 돌릴 때는 최근 방향이 Backward로 유지된다. 기존 Run B 전환은 저속·비 Pivot Reface 출발 또는 다른 방향에서 들어오는 고속 Pivot/Box로 나뉘어, 후진→후진 급회전은 후보 없이 Loop로 복귀했다. 전방 급회전의 Turn 선택과 차이가 있음을 실제 입력 기록으로 확인했다.

Ground Chooser의 `Run B Loco Transitions`에 기존 Neutral 후진 Reface 좌우 90/180도를 재사용하는 4행을 추가했다. 최근 방향 Backward와 native Pivot 판정, 기존 미래 시선각 필터를 함께 적용한다. 블렌드 시간·프로필·태그는 기존 이동 중 Pivot 행의 출력 설정을 제작 시 복사한다. 마지막 Pose Match가 실제 진입 시각을 결정한다. 저속 출발 6행을 포함한 기존 24행과 다른 48개 테이블의 결과·필터·출력·검색 설정은 텍스트 export 비교에서 동일했다. 애니메이션 추가 수입·복제·커브/노티파이 수정은 없으며 라이브러리 카탈로그도 유지한다.

C++ 제작 함수 `ConfigureBackwardFacingTransitions`는 행 구성과 binding을 먼저 검증하고, Pose Match 열을 통째로 복사하지 않고 개별 행과 원래 embedded PSD 연결을 보존한다. 행 수 변경 후 엔진 Initialize로 전체 column→database 매핑을 갱신하고, 해당 PSD의 Modify/PostEditChange와 Chooser Compile/PostEditChange를 완료한다. 재실행은 0행 추가이며 PIE 중 실행하지 않는다. 새 키 처리·강제 클립 선택·회전 reset·IK 비활성화는 런타임에 추가하지 않았다.

최초 시험에서는 새 행의 매핑 갱신 누락을 실제 PIE와 인덱스 검사에서 포착했다. 해당 시험 데이터의 이전 행 번호를 담은 DDC 결과도 발견했고, 별도 엔진 강제 재색인 비교는 불일치 경고 1개와 함께 통과했다. 이 진단 결과를 최종 경고 0 결과로 합산하지 않는다. 전체 캐시나 원본 에셋을 삭제하지 않았고 진단 cvar를 프로젝트 설정에 저장하지 않았다. 최종 제작 데이터와 기본 설정의 새 프로세스에서 native 회귀 **86개가 모두 성공**, 실패·경고·미실행은 0개다. `TransitionSearchAvailability`는 현재 키로 새 요청을 생성하고 실제 40개 테이블의 검색 행 312개를 검사한다.

[후진 회전 PIE 검사](../../../Scripts/Validation/Validate-PlayerLocomotionBackwardTurning.py)는 후진 좌우 90/135/170/190도 8조건과 전방 Strafe/OTM 좌우 170도 4조건, 총 12조건이 통과했다. 큰 후진 회전에서는 기존처럼 Loop만 재선택하지 않고 실제 L/R Reface가 선택됐으며, 진입 시각은 약 0.017~0.183초로 포즈에 따라 달랐다. `PLAYER_BACKWARD_REPEAT_TURNS=True`의 좌우 각 4회, 총 8회 반복 회전도 통과했다. 정상 속도·Root 방향 복귀, Manny와 대검 몸체의 무릎 포즈 갱신, 발 보정 유지와 valid 선택을 검사했다. 별도 OTM/Strafe 점프·착지 8조건도 통과했다.

에디터 미저장 패키지 0개와 진단 cvar 3개의 기본값 0을 확인하고 정상 종료했다. 근거는 `Saved/Validation/BackwardReface_20261022`, 새 프로세스 회귀는 `Saved/Validation/PlayerLocomotion/BackwardReface_20261022_Validated`에 있다. 이 결과를 모든 경사·체형·입력 속도의 주관적 외형 승인으로 일반화하지 않는다. 최종 체감은 현재 대검 캐릭터의 S+마우스 급회전에서 확인한다.

### 완료된 이관 코드 정리 — 2026-10-22

이번 정리는 직전 후진 Reface 수정 이후의 C++·제작 도구 정리다. AssetRegistry의 runtime/editor 모듈 참조와 관련 캐릭터·애니메이션 BP/ABP 39개를 조회하고, 실제 함수 호출·프로퍼티 경로·연결 핀을 export했다. Source/Scripts/Config의 호출과 현재 회귀·추가 수입 경로도 함께 확인했다. 직접 참조가 없다는 이유로 제작·테스트 자산을 삭제하지 않았다.

완료된 초기 설치 API 6개(`PrepareFoundation`, `RestoreFoundationTrajectory`, `RebindFoundationChoosers`, `ConfigureStandingLocomotion`, `InstallNativeLocomotionLogic`, `PreparePlayerMaster`)와 전용 helper/설치 파일을 제거했다. 현행 구조 대신 옛 원본 그래프·초기 프로토타입 설정을 다시 설치하던 경로다. 현재 자식 BP 제작, 그래프/핀/발 Rig 수리, 상속 템플릿 보정, 새 모션 수입·복원·중복 판별·Manny 변환·Foley 변환 도구와 테스트는 유지한다. 실제 BP 바인딩이 있는 Ragdoll 관측, native 계산과 검사에서 소비하는 가속 정보도 보존한다.

런타임은 중복 대입·전달 전용 transform 멤버와 소비처가 없는 transient 4필드만 제거했다. BlendStack 진단 순회는 요청 시에만 수행하고 같은 완료 traversal의 조회는 캐시를 공유한다. 실제 갱신 frame/revision을 사용해 메시가 정지해도 관측 나이를 새로 꾸미지 않는다. 이동·상태 선택·재생·노드 실행 경계와 발 보정은 바꾸지 않는다. 상세 소유권과 [관측 검사](../../../Scripts/Validation/Validate-PlayerPlaybackObservation.py)는 [아키텍처](../Architecture/Player_Locomotion_Architecture.md#완료된-이관-코드와-런타임-관측-정리)를 따른다.

direct Editor UBT 최종 빌드와 새 프로세스 native 회귀 **87개**가 성공했다(실패·경고·미실행 0). import/catalog/원본 복원 Python 검사 **19개**도 통과했다. 변경 후 같은 BP/ABP 39개를 전부 로드·컴파일해 `BS_UP_TO_DATE`와 디스크 파일 해시 동일, 미저장 패키지 0개를 확인했다. 이번 정리에서는 애니메이션·Chooser·PSD·BP/ABP·리그를 수정하거나 저장하지 않았다. 직전 Reface 수정으로 이미 변경돼 있던 Ground Chooser도 이번 정리 시작 해시 그대로 유지한다.

MCP로 시작한 실제 PIE에서 지상 시작/정지·OTM/Strafe·Walk/Sprint, 후진/전방 급회전 12조건, 공중 재선택/착지 8조건, 연속 루프 회전 5조건과 전신 몽타주 6개가 통과했다. Manny source와 대검 몸체의 무릎 포즈 갱신·발 보정과 몽타주 하체를 함께 검사했다. 별도 진단 관측 검사 17쌍은 반복 조회의 동일 결과, 테스트 메시 정지 중 고정된 capture frame과 증가하는 나이, 재개 후 정상 갱신을 확인했다. 이동 속도·재생률·공중 관성·접촉 커브·잠금 해제·경사 보정과 몸체 리타깃 설정을 변경하지 않았다.

미확정 보존 항목은 `/Game/ThirdPerson/Blueprints/BP_Player_Third`다. 전체 로드 때 이미 삭제된 `Project_JMotionMatchingTrajectoryComponent` subobject의 Linker 경고가 발생했다. 현재 hard/soft/search/management referencer와 Source/Scripts/Config 호출은 없고, 과거 점검에도 독립 샘플로 남아 있다. 제작 용도 폐기 근거가 부족하므로 삭제·기본값 재설정·강제 저장하지 않았다. 현행 GameMode의 생산 Pawn은 `BP_GreatSword`다. 이 샘플을 새 제작의 시작점으로 쓰지 않고 `BP_Player_Master` 자식을 사용한다. 향후 샘플을 유지·수리할지 별도 퇴역할지 결정할 항목이며, 경고가 해결됐다고 보고하지 않는다.

GUI 전체 경고 0건을 뜻하지 않는다. MCP의 라이선스 안내, 위 샘플 Linker 경고, 반복 진단 명령 dispatch에서 `DumpAnimBudget` 콘솔 검색 성능 안내가 남았다. 마지막 추가 조회 스크립트의 잘못된 Python getter도 읽기 전용 오류 후 수정해 재실행한 근거를 보존한다. 최종 native 회귀의 경고 0건과 구분한다. Cook/Shipping·대규모 군중 성능·모든 경사/체형/프레임의 외형 검수는 이번 범위가 아니다.

상세 근거는 `Saved/Validation/LocomotionCleanup_20261022`와 `Saved/Validation/PlayerLocomotion/LocomotionCleanup_20261022_First`, 같은 날짜 12:29~12:40의 PlaybackObservation/Ground/BackwardTurning/AirLanding/LoopTurning/MontagePose JSON이다. 종료 시 미저장 패키지 0개, 점검 에셋 39개와 Ground Chooser 해시 동일, 진단 cvar 기본값 0과 원래 background throttle 설정 복원을 확인하고 에디터를 정상 종료했다.

### 구형 ThirdPerson 플레이어 퇴역과 마감 재점검 — 2026-10-22

앞 절에서 보류했던 `BP_Player_Third`는 사용자가 향후 사용하지 않기로 확정했다. AssetRegistry의 hard/soft/search/management 참조와 `FindPackageReferencersForAsset`의 실제 로드 확인, Source/Scripts/Config·제작/테스트 진입점을 재검사해 참조 없음과 현행 마스터에서의 비사용을 확인했다. 삭제 전 원본을 로컬 검증 폴더에 복사하고 SHA-256을 대조했다. 에디터 삭제 API의 성공 반환 이후에도 디스크 파일이 남은 것을 포착했으므로 완료로 간주하지 않았다. 미저장 패키지 0개로 정상 종료한 뒤 백업 해시가 같은 해당 파일 하나만 제거했다. 새 세션에서 디스크·AssetRegistry·참조 모두 없음과 나머지 공유 의존 자원의 존재를 확인했다. Git 이력의 원본도 유지한다.

BP가 사용하던 공유 입력 4개·Quinn Simple·Unarmed ABP는 함께 삭제하지 않았다. 제작/복원 kit·기존 테스트·노티파이·현재 그래프 수리 도구도 유지한다. 현행 GameMode/Pawn·Master/대검 자식·ABP·리타깃/발 Rig의 기존 역할은 바꾸지 않는다. `ConfigureGameplayAvatar`에 남았던 삭제된 궤적 컴포넌트의 이름 기반 정리 분기와 플레이어 헤더의 고아 주석을 제거했다. 남은 BP/ABP 38개의 함수 참조·프로퍼티 바인딩·BP 변수에서 퇴역 API/필드/컴포넌트 이름이 소비되지 않는 것을 확인했다. 같은 38개를 새 세션에서 컴파일하고 정리 전 파일 해시 동일을 재확인했다.

최종 direct Editor UBT 빌드는 28 actions/약 53.7초로 성공했고, 삭제 이후 새 프로세스 native 회귀 **87개**가 실패·경고·미실행 0개로 통과했다. Python import/catalog/복원 **19개**도 다시 통과했다. 빌드 중 UBA가 메모리 압력으로 자체 재시도한 기록은 보존하며, 빌드를 수동 중단하거나 다른 빌드와 겹치지 않았다. Game/Cook/Shipping과 대규모 부하는 이번에 재실행하지 않았다.

이전 절의 실제 후진/전방 회전 12조건·공중/착지 8조건·루프 회전 5조건·전신 몽타주 6개·관측 수명 검사는 동일한 생산 에셋과 런타임 로직의 근거로 재사용한다. 이번 추가 변경은 비사용 BP 퇴역과 에디터의 불필요 분기 제거이며, 새 세션의 기본 마스터 진입·지상 이동을 별도로 확인한다. 로컬 상세 근거는 `Saved/Validation/PlayerThirdRetirement_20261022`, native 결과는 `Saved/Validation/PlayerLocomotion/PlayerThirdRetired_20261022_Final`이다. API 삭제 직후의 미완료 검증과 추가 조회 스크립트의 `None` 반환 처리 오류도 보존하고 최종 성공 결과와 구분한다.

삭제 후 새 네트워크 PIE의 지상 검사 2,372표본에서 OTM/Strafe·Start/Stop·Walk/Run/Sprint와 발 보정 유지가 통과했다(`Ground_20261022_151429_333760.json`). 새 세션 전체 대상 로드와 PIE에서 위 삭제된 클래스의 Linker 경고는 없었다. MCP 라이선스 안내는 별개다. 종료 시 미저장 패키지 0개, 나머지 38개 BP/ABP와 직전 Reface Ground Chooser 해시 동일, 진단 cvar 0과 원래 background throttle을 확인하고 에디터를 정상 종료했다. 포즈·속도·재생률·점프·IK의 추가 튜닝이나 모든 클립/경사/체형의 시각 검수는 수행하지 않았다.

### 속도 커브·메타데이터 재확인 — 2026-10-22

Run 500/Sprint 750 튜닝 이후 속도 관련 에셋 값의 갱신 누락 여부를 읽기 전용으로 재확인했다. 수입 카탈로그의 시퀀스 575개와 BlendSpace 11개를 확인했으며, 모든 시퀀스의 raw motion/curve/attribute 지문과 `RateScale`이 기존 target 기준과 같았다. 커브를 새 게임 목표 속도로 일괄 덮어쓰거나 카탈로그 기준을 갱신하지 않았다.

- `MoveData_Speed`는 438개 시퀀스, `Enable_Warping`은 400개, `MaxDynamicPlayRate`는 16개에 있다. 조회한 관련 커브 키는 finite 값이다. 시퀀스의 별도 `UAnimMetaData`는 없었다. 누락된 Min/MaxDynamicPlayRate의 native 기본값은 각각 1.0/1.25이며, 명시된 Max 커브 16개는 1.1을 유지한다.
- Neutral 전방 루프의 원본 기준 speed 커브는 Run 약 500/Sprint 약 700이다. Manny raw root-track을 주기 내 120구간으로 관측한 평균 이동 속도는 약 516.53/723.14cm/s다. 이 세 값(원본 커브·변환된 모션·게임 목표)은 같은 설정이 아니며, 원본 커브를 게임 목표 500/750으로 바꿀 근거가 아니다.
- 실제 마스터의 Stride 노드는 `LocomotionSpeed`를 native `PlayerSpeed2D`로 바인딩한다. 설치 엔진의 graph-driven Stride 계산은 실제 root-motion delta 속도와의 비율을 사용한다. `EvaluateDynamicPlayRate`는 현재 지상 정책에서 커브 계산 전에 1.0을 반환한다. Ground/Air Chooser와 이전 관련 BP/ABP contract에도 위 speed/play-rate 커브의 별도 직접 바인딩은 없었다. 기존 함수의 비지상 분기와 원본 커브는 보존한다.
- 두 생산 BP의 effective profile은 직접 필드가 아니라 `CharacterAnimProfile`이 가리키는 `DA_Player_Profile`로 확인했다. 승인 상한 500/750, Mover Walk/Run/Sprint 165/500/750과 지상 동적 배속 비활성 값이 일치했다. 모든 BlendSpace sample rate는 1.0이며, 유효 축은 slope·aim·pose 값으로 게임 이동 속도 축이 아니다. 시퀀스 RateScale은 573개가 1.0이고 Ragdoll flail 제작 시퀀스 2개만 기존 1.2를 유지한다.

새 네트워크 PIE의 OTM/Strafe Run/Sprint 4조건, 1,188표본에서 정상 구간 실제 속도 500/750과 활성 루프 재생률 1.0이 통과했다. 원본 커브를 보존한 현재 구조에서 추가 수정이 필요한 근거는 발견하지 못했다. 모든 클립/전 프레임/경사/체형의 외형 승인이나 미래 동적 배속 정책의 검증을 뜻하지 않는다. 에셋·소스 변경은 없어서 동일 커밋의 직전 빌드/native 87개 근거를 재사용하며 이번에는 새 C++ 빌드를 실행하지 않았다.

상세 근거는 `Saved/Validation/AnimationSpeedMetadata_20261022`와 `SpeedCadence_20261022_154139_937968.json`이다. 최초 BlendSpace에 존재하지 않는 RateScale 필드를 읽은 오류와 직접 profile 필드만 확인한 조회 실패는 소스의 실제 소비 계약을 따라 수정 후 재실행했다. 최종 575/11개 점검과 effective profile 검증은 모두 통과했으며, 이 진단 시행착오를 생산 오류로 분류하지 않는다.

마지막에 미저장 패키지 0개, 보호 대상 BP/ABP 38개와 Ground Chooser 해시 동일, 원래 background throttle을 확인하고 에디터를 정상 종료했다. 이번 작업의 Git 변경은 설명·검증 결과를 보강한 문서 2개뿐이다.

### 마스터 그래프 설명 정리 — 2026-10-22

`ABP_Player_Master`의 설명 상자를 실제 상태 결정·포즈 선택·레이어 합성·루트/보폭/발 보정 순서에 맞춰 한글로 정리했다. 원본의 MM 대체 경로·슬라이드 상태·빈 사전 보정 영역·예전 슬롯 위치 설명 4개를 제거하고, 남은 설명 상자 21개와 Pose History/Aim Offset 노드 말풍선 2개를 갱신했다. 포즈 이력 상자는 현재 전신 슬롯·Pose History 위치에 맞춰 축소했다. 엔진이 생성하는 노드 이름·함수/상태 식별자와 실험 노드 진단은 변경하지 않았다.

주석 제외 75개 그래프의 native 계약과 실행 노드 373개의 직렬화된 설정·위치·핀 연결이 정리 전과 같음을 확인했다. 비교에서는 주석/말풍선 표시, 렌더된 노드 크기와 같은 편집기 정보만 제외했고, 저장 시 생성되는 FText localization key는 실제 문자열이 같은지 정규화해 확인했다. ABP는 `BS_UP_TO_DATE`로 컴파일·저장됐다. 이번 작업은 설명/배치 편집이며 C++ 빌드나 이동 로직 변경·추가 전체 회귀는 수행하지 않았다. 에디터 화면에서 한글 표시도 확인했다. 로컬 원자료와 원본 복구 사본은 `Saved/Validation/PlayerMasterComments_20261022`에 있다.

### 고정 카메라 Strafe 키보드 반전 — 2026-10-23

현재 방향과 예측 궤적의 게시 프레임이 달라 Run 전진↔후진에서 Pivot 판정 전에 비 Pivot Box 전환이 선택됐다. 공통 `NativeThreadSafeUpdateAnimation` 시작에서 최종 GT snapshot을 소비하도록 옮겼다. 대각선의 늦은 키 해제에서는 누적 facing이 각도 경계를 넘을 때 궤적 계산이 현재/최근 방향을 Backward로 덮어쓰는 두 번째 writer도 재현했다. 이 쓰기를 제거해 방향 이력은 기존 `UpdateNativeStates`가 소유한다. 180도를 넘는 누적 회전 정보와 불연속 전환 조건은 유지한다.

키/클립별 런타임 예외, 입력을 모으는 대기 버퍼, 새 전환 타이머·강제 reset은 추가하지 않았다. 기존 방향 분류와 이력 유지, Chooser 후보·포즈 검색·블렌드·발 보정과 생산 에셋은 유지했다. 별도 후보 필터나 대각선 전용 상태가 필요하지 않았다. 동일 snapshot 소비·공통 핀·정반대 궤적 판정, 양쪽 각도 경계에서 여섯 방향의 이력 보존을 기존 `SnapshotPublicationAndReinitialize` 회귀에 통합했다.

최종 direct Editor UBT는 5 actions/13.34초로 성공했고 새 프로세스 native **100개**가 실패·경고·미실행 0개로 통과했다. `Validate-PlayerLocomotionStrafePivot.py`는 평지의 고정 카메라에서 Walk/Run 8방향 동시 반전 16조건과 Run 대각선의 축별 16.7/50ms 시차·100ms 늦은 해제·50ms 전체 해제 20조건, 총 **36조건·19,423표본**을 통과했다. 앞뒤 반전의 올바른 클립 계열, 실제 BlendStack 평가, 최종 이동 방향·속도 회복, 방향 복구, 보정 유지와 source/몸체·의상 10개 메시의 무릎 포즈 진행을 검사했다. 안정 구간 facing 오차는 최대 0.83도 미만, 관측된 동일 무릎 포즈 유지 최장은 0.034초 미만이었다. 자동 입력과 평가 포즈의 근거이며 모든 키 시차·지형·프레임률 및 손 조작 외형의 전수 승인은 아니다.

초기 스냅샷 수정만 적용한 36조건 검사에서 늦은 해제 1조건의 Pivot 분류 실패를 확인한 뒤 두 번째 writer를 제거했다. 검사 초기화 API 조회·입력 준비 시간 오류와 추가 테스트의 protected proxy 접근 컴파일 실패는 각각 fixture 준비 및 기존 테스트 경계 통합으로 수정했으며, 실패 기록도 보존한다. 최종 성공과 이 시행착오를 합산하지 않는다. 로컬 근거는 `Saved/Validation/StrafePivot_20261023`, native는 `Saved/Validation/PlayerLocomotion/StrafePivotFinal_20261023`, 최종 입력은 `StrafePivot_20261023_022618_301841.json`이다.

같은 최종 바이너리의 실제 PIE에서 기존 지상 2,376표본, 공중/착지 8조건·2,392표본, 완만한 루프 회전 5조건·1,490표본, 전신 몽타주 6개·281포즈 표본도 통과했다. 순차 검사 러너가 루프의 중간 체크포인트를 완료로 오인한 기록과 전투 전환 몽타주가 남은 상태의 준비 실패는 보존하고, 남은 검사를 준비 상태의 새 세션에서 완료했다. 각 최종 성공 파일은 `Ground_20261023_023144_476794.json`, `AirLanding_20261023_023224_212011.json`, `LoopTurning_20261023_023356_439056.json`, `MontagePose_20261023_023622_959962.json`이다. 생산 Content/Config 변경은 없고 임시 검증 레벨은 저장하지 않았으며 에디터를 정상 종료했다. Game/Cook/Shipping과 새 군중 성능·원격 역할별 플레이는 이번에 실행하지 않았다.

### 로코모션 전환·갱신 잔여 정리 — 2026-10-23

상태 진입·전환 조건, 궤적/방향 이력, AO·lean, 공중 재선택과 물리 점프/착지·원격 이벤트의 연결을 점검했다. C++·Config·검증 스크립트와 실제 BP/ABP 38개의 노드·property-access 바인딩, 중첩 Chooser 57개의 열을 확인해 미소비 transient 7개를 제거했다. 대상은 `PlayerTrajectoryPastVelocity`, `PlayerTrajectoryPreviousFutureVelocity`, `PlayerTrajectoryPastAngularVelocity`, `PlayerPreviousAO`, `PlayerPreviousBlendStackInputs`, `PlayerAccelerationAmount`, `PlayerHasAcceleration`이다. 소비 없는 과거 속도 샘플링·상태 복사와 항상 0인 legacy 최대 가속도로 계산하던 정규화 값을 함께 제거했다. 실제 circling 판정의 과거 각속도 지역 값과 AO 경계 판정의 직전 AO 지역 값은 유지한다. 마지막 필드를 진단 출력하던 지상·공중/착지·질주 점프 fixture 3개도 수정했다. Pivot·notify의 gait 판정은 숫자/실수 변환을 거치지 않고 기존 enum을 직접 비교한다.

검토 후 유지한 시간·각도 기준은 원본 상태 전환의 블렌드 보호·이력 안정화와 기존 프로필 튜닝이다. 공중 후보 재선택의 cooldown/진행률·잔여 시간 기준은 같은 물리 에피소드의 playhead 연속성을 보호한다. 점프/착지 컴포넌트의 timeout은 게임플레이 태그·Foley·순서 있는 원격 이벤트의 수명을 관리하며 시각 Land 전환과 분리돼 있다. 이를 새 timer나 강제 상태 reset으로 교체하지 않았다. 공유 BP 인터페이스가 넘기는 `ForceBlend` 인자는 호환 서명을 유지하고, 실제 native 재생 정책은 선택된 non-loop 에셋의 상태 진입 재생과 loop의 playhead 보존으로 결정됨을 코드에 명시했다. 원본 속성 구조체·향후 상태 enum·제작/테스트 자산도 이번 미소비 내부 상태 정리와 구분해 보존했다.

새 direct Editor UBT는 18 actions/47.27초로 성공했고 기존 자동 회귀 **100개**가 실패·경고·미실행 0개로 통과했다. 관련 BP/ABP 38개를 새 바이너리에서 다시 컴파일해 모두 `BS_UP_TO_DATE`와 파일 해시 동일을 확인했다. 로컬 사용처·컴파일 근거는 `Saved/Validation/LocomotionResidual_20261023`, 자동 회귀는 `Saved/Validation/PlayerLocomotion/LocomotionResidual_20261023`이다. 최초 Chooser 조회의 타입 경로를 수정한 뒤 57개 중첩 테이블까지 확인했으며, 빈 조회를 미사용 근거로 사용하지 않았다. 전체 프로젝트의 미사용 소스·모든 전환 외형을 전수 검수한 결과는 아니며 Game/Cook/Shipping·새 원격 역할별 플레이는 실행하지 않았다. 앞 절의 Strafe 36조건·루프/몽타주 결과는 정리 직전 기준점이고 이번 바이너리의 새 실행으로 합산하지 않는다.

같은 새 바이너리의 임시 평지 PIE에서 지상 **2,399표본**, OTM/Strafe 공중·착지 **8조건·2,393표본**, Strafe 질주 점프 **8조건·1,920표본**이 모두 통과했다. 실제 선택·재생과 포즈/물리 관측을 사용했으며 제거한 진단 필드에 대한 접근 오류도 없었다. 결과는 각각 `Ground_20261023_091859_805264.json`, `AirLanding_20261023_091939_898798.json`, `SprintAir_20261023_092019_936008.json`이다. 생산 Content/Config를 변경하거나 임시 레벨을 저장하지 않았고, 검증 후 에디터를 정상 종료해 관련 프로세스가 남지 않은 것을 확인했다.


### 뒤대각선 Strafe 출발의 계열 선택 — 2026-10-23

뒤대각선이 입력 순서에 따라 전방 Reface와 후진 Start 사이에서 바뀌던 선택은 유효한 Locomotion Profile의 `StrafeDirectionPolicy`로 보완했다. 기존 후진 모션·회전 오프셋 Curve Chooser·Pose Match·Steering/Warping을 사용하며 시퀀스 추가 수입이나 생산 에셋 저장은 없다. 방향 구간과 각도 hysteresis의 제작 기준은 [아키텍처](../Architecture/Player_Locomotion_Architecture.md), 실제 사용자 재현·소스 수명 정리·최종 검증과 한계는 [소스·출발 점검 기록](../Diagnostics/Animation_Source_Review_2026-10-23.md)을 따른다. 뒤대각선 입력 순서·정지/감속 재출발 검사는 `Validate-PlayerLocomotionStrafeStarts.py`에 유지한다.


### 전신 액션 뒤 Start·Stop·Land 복귀 — 2026-10-24

달리기 뒤 짧은 공격을 쓰고 입력을 놓으면 공격이 완전히 끝난 뒤에도 Run Stop이 약 1.38초 더 평가됐다. 출발 직후 공격하거나 공격 중 이동을 시작한 경우도 OTM/Strafe에서 가려진 Start가 약 0.26~0.97초 남는 것을 확인했다. 강제 Flying을 사용하는 스킬은 실제 Falling 없이 Land를 선택했고, 카메라 방향이 바뀐 경우 TIP 예외가 그 Land 후보를 다시 여는 경계도 재현했다.

GT snapshot은 실제 DefaultSlot montage instance 수명, Mover montage root-motion 소유권과 실제 Falling을 별도로 전달한다. native 상태 계산은 전신 액션이 덮어쓴 지상 전환의 재생 권한을 회수하고 현재 입력에 맞는 기존 Locomotion/Idle Loop를 선택한다. 액션 중 새로 요청된 지상 전환도 같은 계약을 사용한다. 새 독립 이동과 액션 이후 실제 Falling은 정상 Stop·착지 권한을 복구하며, TIP 예외는 기존 최근 지상 모드 이력이 확인된 뒤 허용한다. 입력·물리 속도·상태 이력 시간·montage clock을 강제로 초기화하지 않는다.

GASP State Controller·Chooser·BlendStack·Pose History·slot/보정 순서, 원본 모션/커브와 Run/Sprint Stop 후보 필터를 유지했다. 캐릭터/스킬별 그래프·키/클립 이름 예외·복귀 대기 timer를 추가하지 않았다. instance 수명은 첫 blend-in과 blend-out을 포함하고 UpperBody 장착을 제외하므로, 기존 slot weight나 GAS 공격 태그만으로 소유권을 판정하지 않는다. AnimInstance source 교체·재초기화에서는 이 복귀 이력을 해제한다.

최종 direct Editor UBT는 523 actions/336.92초로 성공했다. 같은 바이너리의 native 53개가 실패·경고·미실행 0개로 통과했고, 평지 생산 BP_GreatSword에서 액션 복귀 22조건·9,057표본을 통과했다. OTM/Strafe 진입의 일반 출발/정지, 이동/출발/정지 중 공격, 공격 중 새 이동, 입력 유지, 정지 root motion, UpperBody, 취소와 실제 점프를 검사했다. 강제 Flying의 두 GAS 조건은 필요한 Strafe 전투 모드에서 정지/이동 진입으로 구분했다. 정상 Start/Stop/실제 Land와 발 보정을 유지하면서, 액션에 가려진 Start/Stop/가짜 Land가 사라지고 필요한 TIP는 남는 것을 실제 선택·재생/속도/물리 모드로 판정했다. 제작/검수 계약은 [아키텍처](../Architecture/Player_Locomotion_Architecture.md), 재실행은 [액션 복귀 PIE 검사](../../../Scripts/Validation/Validate-PlayerLocomotionActionRecovery.py)를 따른다.

초기 검수의 Python 비공개 API, 전신 슬롯 아래의 정상적인 0 가중치, GAS 전투 준비 및 장착 이후 모드 고정 판정 오류는 스크립트를 보완하고 실패 자료를 보존했다. 테스트 접근 범위/지역 변수 충돌로 실패한 두 빌드도 정상 종료까지 기다린 뒤 수정했다. 중간 에디터 충돌은 PIE 종료 요청과 에디터 종료를 한 콘솔 호출에서 실행해 생긴 `TearDownPlaySession` world 검사/`GIsPlayInEditorWorld` assertion이었다. 이후 MCP StopPIE 완료와 PIE world 0을 확인한 뒤 콘솔 호출 밖에서 에디터를 종료하도록 절차를 분리했다. 엔진/캐시/에셋을 삭제해 처리하지 않았다.

같은 최종 바이너리의 2-player listen-server PIE도 652프레임으로 통과했다. authority 2개·autonomous 1개·simulated 1개 표현에서 실제 스킬/공격 뒤 다시 움직였다가 멈출 때 정상 Stop을 각각 32/33/32/33프레임 확인했다. 기존 Mover의 simulated-proxy input container 동기화 경로를 재사용하며 복제 필드나 별도 원격 전환 writer를 추가하지 않았다. [기존 네트워크 검사](../../../Scripts/Validation/Validate-PlayerLocomotionNetwork.py)를 38초의 공격 후 재이동/정지 구간까지 확장했다.

최종 자료는 로컬 `Saved/AttackStop_20261024`의 `BuildGroundTIP.log`, `AutomationGroundTIP` 및 `Saved/Validation/PlayerLocomotion/ActionRecovery_20261024_232959_185812.json`, `Network_20261024_233443_098870.json`에 있다. PIE 종료 완료와 dirty content/map 0을 확인했고, 검증 설정을 원래 PIE_Client/1명으로 복원한 뒤 에디터를 정상 종료했다. 최종 종료에서는 새 크래시가 발생하지 않았다. 생산 Content/Config는 변경하지 않았다. 현재 기본 공격·R 취소·T 강제 Flying과 공통 이동 계약의 집중 검사이며, 모든 스킬/지형의 외형 전수 승인이나 Cook/Shipping/새 군중 성능 검증을 뜻하지 않는다.

2026-10-25 정리에서 위 큰 ActionRecovery/Network JSON과 일부 에디터 로그는 원문 SHA-256을 확인한 인접 `.gz`로 보관했다. 원래 이름을 입력으로 요구하는 분석에는 먼저 해제한다. native 결과 index·빌드 로그·영구 PIE 검사기는 유지하고, 완료된 세션 준비 스크립트는 `Saved/StorageCleanup_20261025/RetiredScripts.zip`에 보관했다.

마감 중 검사기의 정규식 캡처를 재확인했다. Length는 캡처 대상이 아니므로 기존 Weight 인덱스가 맞으며 임시 수정은 원복했다. 그 임시 수정의 0표본 실패와 이후 에디터 종료로 미완료인 추가 PIE 재실행은 10월 25일 자료로 구분해 남겼다. 위 최종 22조건·네트워크 통과 결과를 새 실행으로 대체하지 않는다.
