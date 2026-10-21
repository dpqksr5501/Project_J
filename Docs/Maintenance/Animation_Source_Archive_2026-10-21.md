# 애니메이션 원본 보관과 Manny 제작·회귀 전환

UEFN 원본 모션 뱅크를 현재 생산 로코모션에서 분리하고, 재제작 가능한 보관본·독립 회귀 기준·추가 수입의 중복 판별 기반을 마련했다. 생산 모션은 Manny 448개 시퀀스·10개 BlendSpace이며 OTM/Strafe·현재 점프 정책·몽타주/IK 그래프를 변경하지 않았다.

## 보관·삭제·유지

| 항목 | 결과 |
| --- | --- |
| 원본 및 관련 제작 패키지 보관 | 2,505파일, 원래 상대 경로·각 SHA-256·크기·의존성·기준 Git 커밋 기록 |
| 보관 위치 | `Archives/AnimationSources/UEFN_2026-10-21/Originals.zip`, Unreal Content 밖, Git LFS 대상 |
| 원본 논리 크기 / ZIP 크기 | 2,866,442,673 / 393,590,513 bytes |
| 실제 제거한 패키지 | 2,192개, 논리 합계 2,844,295,395 bytes |
| 제거 내역 | 원본 시퀀스 1,960, PSD 80, 몽타주 75, PSS 20, Chooser 4 및 미사용 원본 BlendSpace·표본/골격/리그 등 |
| 현재 사용/제작 자원 유지 | 조사 집합 중 313개. Foley·notify·curve·압축/수정 도구와 명시적인 다음 수입용 retarget kit |
| 생산 결과 보존 | Manny 458개 및 보호한 graph·rig·profile·source mesh 등 14개 파일 해시 불변 |

전체 후보의 hard/soft·검색 이름·관리 참조를 조회하고, 외부 참조자가 있는 패키지와 그 의존성을 보존했다. 직접 참조 0이나 `GASP`/`UEFN` 이름만으로 삭제하지 않았다. 다음 수입용 `RTG_Player_UEFN_to_Manny`를 의도된 제작 자원으로 지정하여 해당 source/target rig·mesh·미리보기 의존성도 보존했다. Source preview의 Lean pose 두 개는 남아 있다. 생산 참조 경로에 UEFN 시퀀스가 남아 있다는 뜻은 아니다.

보관 후 ZIP 전체를 다시 읽어 CRC와 각 파일 SHA-256을 확인했다. 엔진·게임·빌드 프로세스가 없는 상태에서 전체 삭제 목록의 현재 파일을 먼저 검사하고, 삭제 직전 각 파일의 경로·크기·해시를 다시 확인해 `Remove-Item -LiteralPath`로 개별 제거했다. 강제 에셋 삭제/로드된 그래프 GC를 사용하지 않았으며 새 redirector도 만들지 않았다. 기존 내용이 다른 파일은 삭제하거나 덮어쓰지 않는다.

크기는 논리 합계다. ZIP·명세·새 도구가 생긴 크기와 빌드/캐시 변화가 있으므로 이를 실제 드라이브 여유 공간 증가량으로 보고하지 않는다. Git/LFS 이력·현재 빌드·DDC·개인 저장 자료는 정리하지 않았다. 보관본과 이번 변경의 새 커밋/push는 이 작업에서 수행하지 않았다. 원본의 기존 기준 커밋과 LFS 이력은 유지한다.

## 회귀·제작 기준

[라이브러리 카탈로그](../../Scripts/Authoring/Data/PlayerAnimationLibrary.json)는 원본이 남아 있을 때 확인한 source→Manny 매핑과 원본 재생·노티파이 계약, 실제 raw motion/bind pose·curve/attribute 지문, 변환 recipe, Manny 결과를 기록한다. 원본 무릎 굽힘은 네 주기의 61개 시점에서 독립 캡처했다. skeleton 정책 해시와 25개의 가산 base raw 지문은 삭제 전 registry/보관 명세 및 기존 원본 지문에서 보강했다. 원본 기준을 현재 target으로 다시 생성한 것이 아니다.

`MannyConvertedLibrary`와 Foley 접촉 테스트는 현재 Manny 에셋을 검사한다. Kinematics 검사는 원본 뱅크를 로드하지 않고 카탈로그와 독립 원본 표본을 사용한다. 새 지문 테스트는 실제 모션 트랙을 새 임시 모델에 구성해 이름/경로 변경, 작은 raw key 변경, notify 변경과 원본 불변을 확인한다. 기존 upstream 비교 audit도 현재 Manny 라이브러리와 카탈로그의 원본 대응을 사용하도록 바꿨다.

첫 검사에서는 notify 시각을 Python의 `GetTriggerTime()`으로 저장하고 C++의 `GetTime()`과 비교한 불일치를 검출했다. 엔진 API와 동일한 trigger offset 포함 시각으로 비교하도록 수정했다. Imported sequence를 그대로 복제할 때 생긴 linker/compression PostLoad 경고는 loaded import 의존성을 복사하지 않는 새 임시 트랙 모델로 테스트를 구성해 제거했다. 허용 오차를 늘리거나 엔진 로그를 끄지 않았다. 이전 실패/경고 결과는 로컬 원자료에 보존한다.

## 추가 수입의 중복 방지

[수입 가이드](../Animation/Authoring/Player_Animation_Library.md)와 [사전 검사](../../Scripts/Authoring/Plan-PlayerAnimationImport.py)는 원본 논리 ID, 실제 데이터·재생/notify 계약, skeleton 정책·가산 기준, retarget recipe 및 결과 경로를 비교한다. 동일한 원본/recipe와 이름만 달라진 동일 내용은 재사용 후보로, revision·recipe·동명 경로·다중 결과·미저장 관련 에셋은 검토 대상으로 구분한다. 입력뿐 아니라 기존 Manny 결과가 제작 중 바뀌지 않았는지도 확인한다. 장기 유지되는 에디터 Python 모듈의 경로 캐시에 오래된 motion 지문을 재사용하지 않는다.

현재 448개 source signature는 모두 서로 다른 그룹이다. 이 사실을 전체 모션의 의미/외형 중복이 없다는 증명으로 확대하지 않는다. 사전 검사는 읽기 전용이며 실제 batch retarget·Chooser/PSD 자동 확장은 구현하지 않았다. `new`만 명시적으로 변환하고, `reuse`는 기존 결과를 연결하며, `review`는 차이를 해결한 뒤 다시 검사한다. 이름 뒤 `_1`을 붙여 무조건 복제하지 않는다.

## 검증과 남은 경계

- 설치 엔진 direct UBT Editor 빌드 성공.
- 원본 뱅크가 없는 새 프로세스에서 native 집중 회귀 **62개 성공**, 경고 동반·실패·미실행 0.
- 실제 생산 BP/ABP·Chooser·retarget/foot rig 12개 로드와 1,248개 package 참조 경로 확인. 삭제 패키지 참조 0, 생산 참조 경로의 UEFN 시퀀스 0.
- Manny 448개·2,240 authored-frame 표본의 helper 오차 최대 약 0.000967cm, 독립 원본 대비 대표 무릎 차이 최대 약 0.926도. Metadata/raw motion/notify와 보존한 파일 해시 검사 포함.
- 추가 수입 중복 판별 Python 테스트 12개, 복원 테스트 5개 성공. 실제 source/alias 재사용과 revision 차단, 미저장 source skeleton 상태의 재사용 차단도 에디터에서 확인. 마지막 검사는 private process에서 package dirty 표식만 만들고 저장하지 않았으며 원본 파일 해시 불변을 확인했다.
- 보관된 전체 2,505파일의 ZIP/명세/해시 확인. 복원 도구의 의존성 포함, 충돌 시 쓰기 전 중단, 경로 이탈·손상 차단, 검증만 할 때 무수정 유지 검사.

이번에 새 실제 입력 5종·네트워크 부하·Cook/Shipping·전체 클립 외형 검수는 실행하지 않았다. 기존 IK 단계의 실제 OTM/Strafe·점프·회전·몽타주 결과는 당시 조건으로 유지하며 이번 수치에 합산하지 않는다. Core 이동/선택 계산과 생산 에셋의 변경 없이 원본을 퇴역한 작업이다. 신규 animation의 실제 변환·선택 연결은 후속 명시적 제작 작업이다.

로컬 상세 근거는 `Saved/AnimationSourceRetirement_20261021`의 AuditFinal·BootstrapCatalog·DeletionPlan·DeletedSummary·검사 로그와 `Saved/Validation/PlayerLocomotion/MannyLibraryAfterRetirement_20261021/Automation/index.json`이다. 보관 원본의 복원에는 [보관 안내](../../Archives/AnimationSources/README.md)를 사용한다.
