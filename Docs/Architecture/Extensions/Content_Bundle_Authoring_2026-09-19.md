# 직업·전직 제작 도구

## 목적과 범위

DA 기반 런타임은 유지하고, 기존 직업을 참고해 새 직업/전직의 루트·전투 스타일·콤보를 만드는 반복 작업을 줄인다. `Project_JCharacterEditor` 전용 도구이며 게임 모듈에는 에디터 의존성, Tick, 비동기 작업을 추가하지 않는다.

이 작업에서는 실제 프로젝트 에셋을 생성하거나 저장하지 않았다. 아래 기능은 사용자가 에디터에서 실행하는 제작 도구다.

## 사용 방법

1. 에디터의 **Tools → Project J → Create Class / Advancement Bundle**을 연다.
2. `Kind`에서 직업 또는 전직을 선택한다. 기존 `Base Class`, 새 `Identifier`, `/Game/DataAssetSets` 아래 저장 폴더를 지정한다.
3. 필요하면 `Style Template`을 지정한다. 생략하면 기본 직업의 스타일을 사용한다. 전직은 선택적으로 같은 기본 직업에 속한 `Previous Advancement`를 지정할 수 있다.
4. **Preview & Validate**로 생성 예정 경로와 유효성을 확인한다. 설정 변경 시 미리보기 승인은 해제된다.
5. **Create Unsaved Assets**를 누르면 새 에셋들이 생성·연결되고 Content Browser에서 선택된다. 자동 저장하지 않는다.
6. 내용을 검토한 뒤 Unreal 표준 Save 명령으로 저장한다. 결과의 **Copy Registry Entry**를 사용해 표시된 줄을 `Config/DefaultGame.ini`의 `[/Script/Project_JCharacter.Project_JCharacterDataSubsystem]` 섹션에 추가한다.

직업/전직 registry 등록은 이 도구가 자동으로 수정·저장하지 않는다. 에셋 저장과 registry 등록을 마쳐야 ID 조회 경로로 사용할 수 있다. 게임 시작 시 registry를 채우므로 플레이 중 변경하지 않는다.

## 생성 및 공유 경계

```mermaid
flowchart LR
    Template[기존 직업과 스타일 선택] --> Preview[Preview: 메모리 초안 검증]
    Preview --> Create[Create: 새 루트·스타일·콤보 연결]
    Create --> Review[공유 참조와 전직 정책 검토]
    Review --> Save[사용자: 표준 Save]
    Save --> Register[사용자: 등록 줄을 DefaultGame.ini에 추가]
    Register --> Runtime[다음 게임 시작: ID로 조회]
```

| 구성 | 동작 |
|---|---|
| 직업 루트 | 선택한 기본 직업의 설정을 복제하고 새 ClassId와 새 스타일을 연결 |
| 전직 루트 | 새로운 전직을 생성하고 기존 기본 직업과 새 스타일을 연결 |
| 전투 스타일 | 별도 복제, 런타임 캐시 초기화, 공격 목록 자동 생성 모드 사용 |
| 콤보 | 원본에 있을 때만 별도 복제하여 새 스타일에 연결 |
| 공격·몽타주·능력 묶음·애니메이션 프로필·표현·능력치 | 원본 참조 공유, 자동 복제하지 않음 |

기존 AttackSet의 콤보 외 공격은 `AdditionalAttacks`로 보존한다. 콤보에 포함된 공격은 콤보에서 수집하므로 목록을 이중 유지하지 않는다. 새 콤보 노드를 수정해도 원본 콤보는 바뀌지 않지만, 공유 공격 DA 자체를 편집하면 그 공격을 사용하는 다른 콘텐츠에도 영향을 준다. 공격 동작을 독립적으로 변경할 때는 해당 공격만 별도 복제·연결한다.

새 전직에는 이전 전직의 배타 분기, 능력 교체 정책, 태그 요구사항을 자동 복사하지 않는다. 선택한 선행 전직 ID와 최소 레벨만 반영한다. `RequiredLevel`, 선행 전직 목록, `ExclusiveBranch`, 능력 부여 정책, `bOverrideEquippedGameplay`는 의도에 맞게 검토한다. 기존 스타일 태그와 입력/공격 태그는 재사용하며 GameplayTag 사전을 자동 변경하지 않는다.

## 안전성과 한계

- 경로·이름·선행 전직 관계·스타일·콤보를 검사한다. 기존 파일, 로드된 패키지, AssetRegistry에 같은 목적지가 있으면 전체 생성을 거절한다. 해당 직업/전직 에셋 종류의 ID 중복도 검사한다.
- Preview는 메모리 초안만 만들고 버린다. 실제 생성에서도 검사를 다시 수행하며, 모든 초안이 검증된 후 패키지로 이동한다. 이동 실패 시 이번에 이동한 초안만 되돌리고 비어 있는 새 패키지를 폐기한다. 성공 후에만 AssetRegistry에 알린다.
- ID 속성이 검색용 메타데이터가 아니므로 ID 검사는 해당 종류의 직업 또는 전직 DA를 로드한다. 전체 콘텐츠/애니메이션 에셋 검색 도구는 아니다. 이 종류가 매우 커지면 검색 가능한 ID 메타데이터와 에셋 검증 규칙으로 옮기는 것이 다음 개선점이다.
- 자동 저장, 자동 source-control 작업, config 수정은 없다. 묶음 생성 전체를 Ctrl+Z로 되돌리는 기능은 제공하지 않는다. 생성 이후 속성 편집은 표준 Undo를 사용하며, 생성 자체를 취소하려면 Content Browser에서 이번에 생성한 에셋들을 삭제한다.
- 이 도구는 새로운 실행 알고리즘이나 스킬 로직을 생성하지 않는다. 이미 지원하는 실행 구조에 맞춘 데이터 제작을 돕는다.

## 검증

`ProjectJ.Authoring.Bundle.*` 3개 테스트는 경로/ID 검증, 메모리 초안의 원본 격리와 공유 경계, 콤보 연결과 카탈로그 변환, 전직의 기본 직업·선행 관계·정책 초기값을 검증했다. 이 결과는 [최종 81개 통합 회귀](../Runtime/Internal_Polish_Validation_2026-09-19.json)에 포함된다. 테스트는 `/Game` 에셋을 만들거나 저장하지 않는다. 메뉴 조작, 패키지 생성/삭제, Content Browser 표시와 표준 저장은 에디터 수동 확인 범위다.
