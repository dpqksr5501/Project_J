# 애니메이션 원본 보관

`UEFN_2026-10-21/Originals.zip`은 Manny 변환 이전의 원본 모션과 연관 제작 패키지 2,505개를 원래 상대 경로 그대로 보존한다. Unreal이 검색하는 `Content` 밖에 두며 ZIP은 Git LFS 대상이다. 원본 데이터는 Epic Games Game Animation Sample 및 기존 프로젝트 라이브러리에서 온 자료다. 새 모션을 자체 제작했다는 의미가 아니다.

- [보관 명세](UEFN_2026-10-21/ArchiveManifest.json): 각 파일 SHA-256·크기·원래 package, 의존성, 삭제/유지 집합과 기준 Git 커밋.
- [수입·중복 검사 기준](../../Docs/Animation/Authoring/Player_Animation_Library.md): 현재 Manny 라이브러리, 원본 식별과 새 모션 수입 절차.
- [복원 도구](../../Scripts/Authoring/Restore-AnimationSources.py): 먼저 전체 무결성을 검사하거나 명시한 package와 보관된 의존성을 복원한다.

```powershell
python Scripts/Authoring/Restore-AnimationSources.py --verify
python Scripts/Authoring/Restore-AnimationSources.py --restore --package /Game/Characters/UEFN_Mannequin/Animations/Jump/M_Neutral_Jump_F_Start_Stand_Lfoot
```

복원은 에디터·게임을 종료한 상태에서 수행한다. 기존 파일의 내용이 다르면 덮어쓰지 않고 중단한다. 패키지의 전체 실행 문맥은 명세의 기준 Git 커밋과 LFS 원본에 있다. 이 ZIP만으로 독립 실행 프로젝트가 복원되지는 않는다. ZIP이나 LFS 이력을 정리하려면 별도 보존 판단이 필요하다.

모든 UEFN 자료가 삭제 대상은 아니다. 향후 추가 수입에 사용하는 `RTG_Player_UEFN_to_Manny`와 실제 연결된 최소 source mesh/rig/preview pose는 현재 제작 자원으로 유지한다. 공용 Manny 에셋이 참조하는 Foley·노티파이·커브·압축/수정 도구도 유지한다. 이들과 보관된 비활성 원본 모션 뱅크의 역할을 구분한다.

`Neutral_2026-10-22/Originals.zip`은 Neutral Run/Sprint/Jump 확장 때 사용한 새 upstream 시퀀스 127개와 notify BP 3개를 보존한다. 기존 보관본을 덮거나 같은 이름을 동일 revision으로 간주하지 않는다. 별도 [명세](Neutral_2026-10-22/ArchiveManifest.json)에 원본 바이트 해시·제작용 package remap·변환 recipe 지문을 남겼다. 의존성 목록은 포함된 notify BP를 보수적으로 함께 복원하며, 공유 source kit와 생산 프로젝트 전체를 이 ZIP만으로 복원하지는 않는다.

```powershell
python Scripts/Authoring/Restore-AnimationSources.py --bundle Neutral_2026-10-22 --verify
```

복원은 앞의 명시적 `--restore --package` 절차를 따르고, 새 에디터에서 staging/import 검사를 다시 수행한다. `--bundle`을 생략하면 기존 최초 보관본을 사용한다.

## 보관 ZIP의 Git 비교와 임시 파일

ZIP은 `.gitattributes`에서 `filter=lfs -diff merge=lfs -text`로 관리한다. 원본은 LFS에 보존하고 내용 검토는 옆의 JSON 명세·체크섬으로 한다. `-diff`만으로 Git의 clean filter 실행까지 차단되는 것은 아니다.

새 ZIP을 만든 뒤 전체 해시를 명세와 검증하고, 해당 ZIP만 명시적으로 `git add -- <ZIP 경로>`하여 LFS 포인터로 등록한다. 이 작업은 커밋·push가 아니다. 인덱스의 `git show :<ZIP 경로>`가 SHA-256과 크기를 담은 작은 LFS 포인터인지 확인한다. 미추적 대용량 ZIP을 자동 변경사항 비교에 오래 남기거나 `git diff --no-index`의 대상으로 반복 전달하지 않는다. 이번 ZIP은 이 방식으로 등록했으며, 새 PC의 정상 checkout에서는 이미 추적되는 파일이다.

2026-10-21에는 Codex의 미추적 ZIP 비교가 반복 중단되면서 `.git/lfs/tmp`에 부분 사본이 누적됐다. 사용 중인 Git/LFS 작업과 파일 핸들 확인 없이 이 폴더를 통째로 지우지 않는다. 당시 정리는 보관 ZIP과 전체 바이트가 일치하는 부분 사본·안정된 빈 파일만 배타적 핸들로 확인해 제거했고, 정상 `.git/lfs/objects`·Content·보관 ZIP은 유지했다. 상세 결과는 [저장 공간 정리 기록](../../Docs/Maintenance/Storage_Cleanup_2026-10-21.md#lfs-임시-사본-급증과-재발-방지)을 따른다. 정상 LFS 원본에 대한 `prune`·이력 재작성은 이 임시 파일 정리의 일부가 아니다.
