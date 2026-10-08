# GASP Foley 노티파이 교체 — 2026-10-08

## 범위와 보존 계약

사용자의 요청에 따라 Project_J 애니메이션에 남은 GASP BP Foley 노티파이를 `UProject_JAnimNotify_FoleyEvent`로 교체한다. 대상은 Asset Registry에서 원본 Foley BP 클래스들의 실제 참조를 가진 `/Game/Characters/UEFN_Mannequin/Animations/` 아래 애니메이션 985개다. 다른 캐릭터·전투·탑승 에셋, 레벨, 소리·재질·음원 프로필을 생성하거나 수정하는 작업은 아니다.

기존의 누락된 클래스를 이름만 보고 추정하지 않는다. 원본 GASP의 Blueprint / Audio 폴더를 에디터에서 잠시 읽기용 mount로 연결해 실제 BP 인스턴스의 Event·Side·VolumeMultiplier·PitchMultiplier를 읽는다. 프로젝트에 같은 폴더가 있으면 도구가 거부한다. source mount는 각 호출 뒤 해제하며 GASP 파일을 저장하지 않는다. BP 클래스나 오디오 에셋을 Project_J Content에 복사하지 않는다.

교체는 기존 FAnimNotifyEvent 배열의 Notify 객체 포인터를 바꾸는 방식이다. 전체 배열을 삭제·재생성하지 않으며 GUID, 시간과 trigger offset, 트랙, Trigger Weight, 확률, LOD/filter, follower 정책, branching/link 데이터와 다른 notify/state를 보존한다. BP의 실수 변수가 double인 경우 native float로 변환하며 실제 저장값은 오차 1e-6 안에서 검증한다. Notify 객체의 에디터 색상도 이어받는다. 의도적인 메타데이터 변경은 Foley의 `bTriggerOnDedicatedServer=false`다.

일부 Pivot에는 VolumeMultiplier=0으로 음소거한 접촉이 있다. 이 값을 1로 바꾸지 않고 보존하며 런타임 admission이 소리·트레이스 작업 전에 해당 무음 요청을 폐기한다. 클래스 이름과 Side가 반대로 덮어쓴 인스턴스, Walk 자식에서 Run/WalkBackwds로 바꾼 인스턴스도 실제 설정을 기준으로 처리한다.

Jump/Land 노티파이도 원래 위치와 태그를 유지한다. 플레이어의 기본 `bUseMovementEventsForJumpAndLand=true` 정책이 이를 필터링하고 기존 이동 이벤트가 실제 재생을 요청하므로 양쪽에서 중복 소리를 만들지 않는다. Notify만 쓰는 NPC는 해당 컴포넌트 설정을 false로 바꿀 수 있다.

## 이름만 있는 이벤트의 예외

원본 GASP UE 5.7.4를 Python commandlet으로 읽어 Roll 두 시퀀스의 이름 노티파이를 별도로 대조했다. 원본에서도 Notify와 NotifyStateClass가 모두 None이며, 잘못 이관된 BP 객체가 아니다.

| 시퀀스 | 이름 | GUID |
| --- | --- | --- |
| M_Neutral_Jump_F_Land_Roll_Lfoot | FoleyEvent: Tumble | 78F779AE46B9E095758FA899B056B77B |
| M_Neutral_Jump_F_Land_Roll_Rfoot | FoleyEvent: Land | C0EA8ED848A06C66008E02BCB2EDAF37 |

이 두 항목은 원본의 전체 export metadata가 일치하는지 확인하고 그대로 보존한다. 이름만으로 Tumble이나 추가 Land 음원을 새로 만들어 넣지 않는다. 허용 키는 패키지·GUID·이름의 정확한 조합이며 그 밖의 미해결 Foley 객체는 사전 검사를 실패시킨다.

## 도구와 실행 절차

- `UProject_JFoleyMigrationLibrary`: Editor 모듈에만 있는 C++ 편집 도구. 명시한 패키지 목록만 처리하며 dirty 패키지·프로젝트 밖 경로·예상하지 못한 BP 스키마·루프 이벤트를 거부한다. 이미 native인 노티파이는 다시 추가하지 않는다.
- `Scripts/Editor/Migrate-GaspFoley.py`: 백업 hash 확인 → 전체 dry-run → apply → persisted verify를 구분한다. 24개씩 처리하고 배치마다 JSON 기록을 남긴다. 첫 오류에서 중단한다. Windows에서 기록 파일을 읽는 동안 atomic rename이 잠기면 재시도한다. 중단 후 resume은 완료 prefix의 native 클래스·metadata와 나머지 파일의 원본 hash를 확인하고 이어서 처리한다.
- Unreal MCP로 실행 에디터를 연결하고 loopback-only Python 세션을 잠시 켜 엔진 편집 API를 호출한다. 설정은 저장하지 않고 작업 후 복원한다.

실행 근거는 `Saved/Validation/FoleyMigration_20261008/`에 보관한다. Candidates.json은 실제 참조 목록, BackupManifest.json은 원본/백업 SHA-256, OriginalNamedEvents.json은 원본 GASP 명명 이벤트, dry-run.json과 apply.json은 이벤트별 타이밍·GUID·설정·전체 metadata, verify.json은 저장 후 검증이다. 백업은 `Backup/Content/` 아래 원래 경로 구조로 남긴다. 재실행 전에는 해당 작업의 목록·백업·사전 검사 기록을 다시 준비해야 한다. 이미 변경된 파일을 이전 백업 기준으로 덮어쓰는 호출은 Python 드라이버가 거부한다.

실제 청취는 별도 단계다. 현재 소리 프로필이 None인 상태에서도 native 노티파이 연결과 기존 이동 이벤트 경로는 준비되며, 음원이 없는 요청은 안전하게 폐기된다. 음원이 준비되면 [Foley 시스템 제작 절차](../Architecture/Foley_Audio_System.md#지형별-데이터-작성)에 따라 프로필과 표면별 음원을 연결한다.

## 적용 결과와 검증

애니메이션 985개에 포함된 BP Foley 인스턴스 9,003개를 native로 교체해 저장했다. 원본 이름 이벤트 2개와 VolumeMultiplier=0 접촉 4개를 보존했다. 모든 후보 파일은 사전에 SHA-256을 비교한 원본 백업을 남겼으며 백업 크기는 1,492,188,972 bytes다. Git에서도 관련 애니메이션 변경 수가 985개로 확인됐다.

- 최종 Editor Win64 Development: 직접 UBT 빌드 성공. 변경한 교체 도구는 Editor 모듈에만 있으며 Game 런타임 코드는 초기 기반 검증 이후 바꾸지 않았다.
- GASP mount 없는 새 UE5.8 프로세스: 985개 파일을 다시 로드해 9,003개 Foley의 native 클래스·정확한 태그·좌우·배율·trigger 시간·Foley event metadata 비교 통과. 프로세스 exit 0.
- 원본 클래스 읽기 문맥의 새 프로세스: 10,385개 이벤트의 전체 metadata 비교 통과. 전용 서버 플래그와 객체 포인터를 제외한 Foley 설정, 다른 notify/state와 이름 이벤트를 검증했다. 모든 패키지에서 재교체 요청은 replacements=0 / saved=0이었다.
- 적용 후 NullRHI/nosound 관련 자동화: 12개 통과, 테스트별 오류·경고 0.

추가 원본 문맥 검증은 내부 비교가 모두 통과했지만 프로세스 exit은 1이다. UE5.7 GASP의 BFL_HelpfulFunctions가 Project_J UE5.8에서 읽힐 때 VisualLogger 관련 unknown struct와 K2 타입 호환 오류가 발생했다. 해당 원본 함수·BP는 저장하거나 Project_J로 복사하지 않았으며 이 로그를 성공한 무오류 엔진 실행으로 표현하지 않는다.

원래 Project_J에 없는 `BP_NotifyState_EarlyTransition` 참조도 일부 시퀀스에 남아 있다. 이 객체는 원본을 연결하지 않은 문맥에서 None으로 읽혀 비-Foley의 전체 문자열 비교를 막았다. Foley 독립 검증과 전체 배열 검증을 별도로 실행한 이유이며, 기존 참조·타이밍·GUID는 보존했다. 이 작업은 EarlyTransition을 native로 바꾸거나 누락 의존성을 복구한 것으로 주장하지 않는다. 후속 작업에서는 기존 native locomotion early-transition 정책과 대응을 별도로 확인해야 한다.

배치 672개 완료 시 진행 기록 파일의 일시적인 Windows 잠금으로 멈춘 적이 있다. 완전히 작성된 다음 배치 기록을 복구하고 저장 prefix를 검증해 이어서 처리했으며 최종 apply 오류 수는 0이다. 레벨·음원·재질·음원 프로필·다른 캐릭터 에셋은 저장하지 않았다. 구체적인 숫자와 근거 hash는 [교체 검증 기록](Foley_Notify_Migration_Validation_2026-10-08.json)에 보관한다.
