# 캐릭터 컴포넌트 실행·수명 정리

## 범위

기본 캐릭터/PlayerState 컴포넌트 검토 후 UI 소비 수명, 메시 갱신 요구의 합성, 발도·납도 소유권, 원격 locomotion 시간 처리를 정리한다. 기존 작업 트리의 확장 기반·내부 정리를 유지한다. Blueprint·Data Asset·레벨은 변경하거나 저장하지 않는다.

## UI 소비 수명

`CharacterUIBindingComponent`는 Attribute 원천과 실제 UI 구독을 구분한다. 로컬 플레이어 HUD는 소유권 변경 이벤트로 자동 활성화한다. 전용 서버에서는 UI 요청이 있어도 ViewModel을 만들거나 Attribute를 구독하지 않는다. 관찰되지 않는 원격 캐릭터는 원천 참조와 레벨만 보관한다.

새 원격 대상창/파티창은 `AcquireCharacterViewModel(Consumer)`로 요청하고 닫히거나 파괴될 때 같은 Consumer로 `ReleaseCharacterViewModel(Consumer)`를 호출한다. 서로 다른 소비자의 요청은 독립적이며 같은 소비자의 중복 요청은 한 번으로 계산한다. 소비자마다 고유한 widget/object를 사용한다. 약한 참조가 명시적 Release 계약을 대신하지는 않는다.

기존 `GetCharacterViewModel()` 호출 경로는 보존한다. 원격에서 이 호환 getter를 사용하면 지속 요청이 되므로 더 이상 필요하지 않을 때 `ReleaseLegacyViewModelRequest()`를 호출하거나 새 소비자 API로 옮긴다. 단순 존재 여부 진단에는 `PeekCharacterViewModel()`을 사용한다. 레벨 변경은 ViewModel을 새로 생성하지 않으며, 동일한 ASC/Attribute 재초기화는 구독을 다시 만들지 않는다. 원천이 없어지면 기존 ViewModel의 체력/마나를 지워 오래된 값이 남지 않도록 한다.

## 메시의 애니메이션 갱신 요구

`BudgetedSkeletalMeshComponent`가 요청자별 일시 요구를 합성한다.

- `Presentation`: URO를 해제하고 ABA의 갱신 소유를 막는다.
- `GameplayPose`: 위 요구에 더해 숨겨진 메시/전용 서버의 본 갱신과 Notify 전달을 보장한다.

`AnimationUpdateCoordinator`는 기존 타이머/최장 만료 시간만 소유하고 Presentation을 요청·반납한다. `CombatHitValidation`은 공격 수명 동안 GameplayPose를 요청·반납한다. 같은 요청자의 재요청은 baseline을 덮어쓰지 않는다. 각 컴포넌트는 자신이 변경한 정확한 메시를 추적해 종료·파괴 시 반납한다.

첫 요구가 생기면 ABA에서 먼저 이탈한 뒤 원래 URO를 저장한다. 마지막 GameplayPose가 끝나면 본/Notify 정책을 복원하고, 마지막 전체 요구가 끝나야 URO를 복원한다. 한 공격이 끝나도 다른 공격/긴급 표현이 남아 있으면 보호를 유지한다. ABA 재진입은 다음 기존 정책 pass에서 수행한다. 프로젝트 메시가 아닌 일반 엔진 메시의 기존 단일 요청자 fallback은 유지하며, 그 경로를 프로젝트 메시와 같은 다중 요청 합성으로 간주하지 않는다.

군중 검사에서 확인된 `a.Budget.Enabled` 반복 조회 경고는 엔진 CVar 포인터를 캐시하여 해결한다. CVar 값은 매 정책 pass에 읽으므로 런타임 활성화/비활성화는 계속 반영한다.

외부 코드가 URO 등의 같은 bool을 직접 덮어쓰면 동일 값의 작성 의도를 추론할 수 없다. 새 임시 요구는 반드시 이 API를 사용한다. 서로 다른 값으로 바뀐 일부 명시적 정책은 기존 조건부 복원 규칙을 유지한다.

## 발도·납도

기존 `CombatIntroComponent` 이름과 캐릭터 공개 호출 경로를 유지하면서 Drawing/Sheathing/Idle 및 활성 몽타주의 소유권을 컴포넌트로 모은다. 발도와 납도는 동시에 실행하지 않는다. 취소는 상태와 revision을 먼저 종료한 다음 몽타주를 중지한다. 종료 콜백은 몽타주 참조와 재생 revision을 모두 검사하여 같은 에셋의 이전 재생 콜백을 배제한다.

캐릭터는 완료 결과에 따라 GAS 전투 진입, 원격 표현 신호, 무기 소켓/레이어를 연결한다. 서버 전투 권한과 원격 cosmetic replication은 합치지 않는다. 기존 Blueprint 공개 bool은 호환 조회값으로 유지하되 C++ 실행 판단에는 사용하지 않는다. 단순 파일 분리와 달리 실행 상태의 원천이 컴포넌트로 이동한다.

## 원격 locomotion 시간

Jump/Landing/Ground 상태의 경과 시간은 매 프레임 한 번 증가한다. 숨겨진 원격의 샘플 갱신을 건너뛰어도 시계가 느려지지 않으며, 건너뛰는 사이에 도착한 새 점프 이벤트에 그 이전 시간을 더하지 않는다. 이동 스냅샷과 파생 문맥을 다음에 샘플링할 때는 생략된 프레임 시간을 포함한다. 갱신 간격 해제/표시 재개 시에도 누적 시간을 전달한다. 비정상/0/음수 DeltaTime은 거부한다.

`HiddenRemoteUpdateInterval`의 기본값 0은 유지한다. 이 작업은 원격 갱신 간격을 새로 늘리거나 로컬 반응성을 낮추지 않는다.

## 이번에 확장하지 않은 영역

- 궤적 생성 주기·스냅샷 복사·GAS 입력 검색·인벤토리 인덱스: 현재 규모의 측정 후 결정한다.
- 판정용 포즈 보호를 hit window만으로 축소: 창을 여는 Notify와 공격 이동의 정확성 확인 없이는 줄이지 않는다.
- 전면 컴포넌트 통합, 새 worker, 캐릭터 풀링: 도입하지 않는다.
- 탈것 소환의 비동기화: 소환 요청/취소/아이템 수명 계약을 별도로 다룬다.

## 검증

직접 `UnrealBuildTool.exe` 실행으로 **Project_JEditor / Project_J Win64 Development 빌드 모두 성공**했다. Game 실행 파일까지 재링크했다.

NullRHI 자동화는 **86개 통과 / 테스트 오류 0 / 경고 0**이다. 이전 통합 기록의 81개가 모두 포함되었고 누락은 없다. 신규 5개는 다음을 검증한다.

- 실제 ABA 소유 상태에서 긴급 요청과 여러 공격 요청의 중첩, 두 종료 순서, 원래 정책 복원과 ABA 재진입.
- UI 다중 소비자, 실제 GAS Attribute 변경, 원천 소실, 소유권 변경, 호환 getter, 종료 후 재요청 방지.
- 전용 서버의 명시적/호환 UI 요청 시 할당 차단.
- 실제 몽타주 재생 시작, 발도/납도 상호 배제, 같은 몽타주의 늦은 이전 콜백 무시와 종료 정리.
- 숨겨진 원격의 누적 시간, 간격 해제, 샘플을 생략한 프레임의 상태 시계 및 비정상 시간 방어.

첫 개별 실행은 추상 PlayerCharacter를 생성한 테스트 오류로 중단됐다. 구체 캐릭터로 수정했다. 첫 통합 실행의 두 실패는 테스트 소비자로 추상 UObject를 생성한 오류와 새 납도 동적 delegate의 UFUNCTION 누락이었다. 두 오류와 반복 CVar 조회 경고를 수정한 뒤 전체 86개를 재실행하여 통과했다.

최종 빌드·보고서·소스 해시는 [검증 기록](Character_Component_Ownership_Validation_2026-09-20.json)에 기록한다. 소스 해시는 기존 미커밋 변경을 포함한 전달 시점의 파일 전체를 가리킨다.

실제 2인 PIE의 발도/납도·피격·탑승과 UI widget 수명, 렌더링 재개 품질은 NullRHI 자동화와 별도의 확인 대상이다. 이 변경으로 FPS 또는 동접 성능 개선률을 주장하지 않는다.
