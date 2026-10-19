# 월드 제작 도구

`Content/World/MainWorld/Source`를 보존하고 현행 읽기 전용 검증은 manifest의 `L_MainWorld`를 따른다. 초기 `L_StarterRegion` 제작 도구는 아래에 명시한 단계별 이력과 지원 제한을 따른다. 시작점과 미완료 상태는 [인수인계](../../Docs/World/WORLD_HANDOFF_2026-10-18.md)를 따른다. Python 파일은 게임 런타임의 시작 훅이 아니다.

- `Generate-*` / `Plan-*`: 높이·도로·장소·배치 입력 작성. 기존 Source를 덮지 않는지 각 파일의 조건을 먼저 확인한다.
- `Build-*` / `Apply-StarterWorldBiomes.py` / `Save-StarterForests.py`: Source를 에디터의 실제 에셋·액터로 작성한다. 이미 배치된 월드를 일괄 재생성하는 용도로 실행하지 않는다.
- `Starter-*`: 지형 좌표, 건축·다리·습지 기하, 공간 계약 등 공통 제작 함수다.
- `Verify-*`: 저장본과 Source의 연결·기존 공간 계약을 확인한다. 수치 통과는 시각 품질 합격이 아니다.
- `WorldVisualHarness.py <새 label> --cameras panorama montane user_far`: 고정 Editor 화면과 설정 기록. `Capture-StarterPerformance.py`는 PIE 성능 캡처이며 별도 실행 조건이 있다.
- MCP 연결과 콘솔 입력은 `Scripts/Editor/World-MCP.py`, `WorldEditorConsole.py`를 사용한다. 연결 상태와 실행 완료는 별개다.

제작기 일부는 로컬 `Saved/Validation/StarterWorld` 체크포인트를 입력·중복 실행 방지에 사용한다. Saved는 Git 제외이므로 새 PC에서 초기 저작을 그대로 재실행할 수 있다고 보장하지 않는다. 현재 Source·Native 상태를 먼저 조사한다. 이미 적용한 일회성 이행·보정, 반복 진단/캡처, 철회한 물 시험, 퇴역 Region A 제작기는 커밋 전 정리에서 제거했다. HLOD 등록 수정 후 프록시 빌드·PIE 전환은 아직 미검증이며 기존 실패 경로를 재실행하지 않는다.

2026-10-18 감사 범위: 현행 읽기 전용 World 검증은 manifest의 MainWorld를 따른다. Source 경로와 논리 ID는 Starter를 유지한다. Build/Apply 등 초기 Starter 제작기는 단계별 이력·체크포인트용이며 MainWorld 재생성/이행기로 지원하지 않는다. 재사용하려면 대상 맵, 실제 에셋·redirector, 정확한 변경 패키지와 실패 복구를 작업 사본에서 재검토해야 한다. 공용 Settlement/Forest/Lighting 저장은 시작 dirty 상태와 동시 외부 dirty 상태를 거절하고 명시적으로 소유한 패키지만 저장한다.

WP에서 새 액터 폴더를 만들면 폴더의 외부 오브젝트와 공유 맵도 dirty가 될 수 있다. 액터 소유권은 이 패키지들의 저장 권한을 포함하지 않으므로 저장은 중단된다. 기존 맵의 폴더·맵 메타데이터 변경은 별도의 명시적 저작 작업으로 검토하고 저장해야 한다. dirty 상태를 지우거나 경로 전체를 소유 목록에 추가해서 재시도하지 않는다. 새 월드 bootstrap만 자신이 새로 만든 맵 패키지를 명시적으로 소유할 수 있다.

2026-10-19 정리: 미커밋 `Build-StarterMountainHLOD.py`는 Starter 전용 산악 시험 경계·저장 경로를 사용하는 일회성 파일이므로 로컬 원형과 SHA를 보관한 뒤 퇴역했다. 자동 실행·다른 제작기의 호출은 없으며, 현재 MainWorld의 지원 HLOD 빌더로 사용하지 않는다. 이 정리는 HLOD 품질·스트리밍 문제 해결이나 빌드 성공을 뜻하지 않는다.

2026-10-19 최종 폴더 정리: 제작 입력은 `Content/World/MainWorld/Source`의 `MainWorld.*`·`HM_MainWorld.r16`·`T_MainWorld*.png`다. Source의 수치·안정 ID·높이 픽셀은 보존했다. manifest의 `owned_content_root`는 월드/DA/설정을 소유하고, `resource_content_root=/Game/Map_Resource/MainWorld`는 월드 전용 환경 자원을 소유한다. Settlement 공용 도구와 자원 경로 생성 계획도 이 구분을 따른다. 초기 생성기의 삭제된 Legacy 대상 guard와 단계별 실행/저장 제한은 유지하므로 MainWorld 재생성기로 실행하지 않는다. `Build-StarterCorridor.py`는 다른 제작기의 공통 함수 호출 때문에 유지한다. 완료된 일회성 bootstrap `Build-StarterWorld.py`와 이번 이행 실행기는 검증 후 퇴역했고 원형·지문은 로컬 실행 기록에 보관한다. 지원 도구의 `Starter-` 파일명, 제작 소유 태그와 안정 ID는 에셋 표시 이름과 별개다.
