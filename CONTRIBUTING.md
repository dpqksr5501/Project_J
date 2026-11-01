# 기여 안내

Project J는 개인 학습·기술 연구·포트폴리오를 위한 UE 5.8 C++ MMORPG 프로토타입이다. 작업 전 [프로젝트 맥락](Docs/Overview/ProjectContext.md), [작업 규칙](AGENTS.md), [변경 마감 기준](Docs/Maintenance/Change_Completion_Policy.md)을 확인한다.

## C++와 시스템 개선

실제 호출자·데이터 소비·상태 소유권을 확인하고 기존 모듈과 서비스를 우선 활용한다. 서버 권한·예측 상태·클라이언트 표현, Game Thread·worker의 책임을 구분한다. 취소·실패·재진입·캐릭터 교체·파괴·월드 종료 중 변경에 관련된 경로도 확인한다.

기존 사용자 변경을 보존하고, 교체한 경로의 잔여 호출·등록/해제를 함께 정리한다. 테스트·샘플·제작 자원은 참조가 없다는 이유만으로 삭제하지 않는다. 엔진·외부 코드 변경은 원본 버전과 프로젝트 수정, 해당 배포 조건을 구분한다.

## 콘텐츠 제작과 통합

캐릭터·모션·무기·의상·이펙트를 실제 플레이에 연결하는 작업은 다음 가이드를 따른다. 새 콘텐츠는 권리 확인 → 골격·크기·축·소켓 합의 → 제작·프로필 연결 → 플레이 품질 확인 → 원격·군중·취소/교체/파괴 검토 순으로 진행한다.

| 분야 | 연결 방식과 확인할 부분 | 제작 가이드 |
| --- | --- | --- |
| 캐릭터·몬스터 | Skeletal Mesh·프로필·AnimBP 연결. 골격·비율·리타기팅과 발/손 접촉 확인 | [리깅·UE5 후처리](Docs/Animation/Authoring/Character_Rigging_And_UE5_Postprocess_Playbook.md) |
| 의상·방어구·무기 | 장비 정의·표현 프로필과 부착 위치 구성. 호환 파츠의 Leader Pose, 소켓·그립·클리핑·LOD 확인 | [모듈러 장비](Docs/Gameplay/Authoring/ModularCharacterEquipmentGuide.md) · [LOD 제작](Docs/Animation/Authoring/Mesh_LOD_Authoring.md) |
| 이동·전투 애니메이션 | Pose Match/Chooser용 이동 데이터·무기별 프로필·공격 몽타주 구성. root motion과 전환·공격/복귀 타이밍 확인 | [이동 확장](Docs/Animation/Authoring/DataDrivenLocomotionExtensionGuide.md) · [공격·콤보](Docs/Combat/Authoring/GreatswordCombatAuthoringGuide.md) |
| Niagara 이펙트 | 공격 태그별 표현 프로필·몽타주 Notify에 연결. 무기 소켓·Trail 구간·잔상·중단 시 정리 확인 | [전투 VFX](Docs/Combat/Architecture/CombatVFXArchitecture.md#authoring) |
| 캐릭터 사운드 | 이벤트·접촉 위치·Physical Surface별 오디오 프로필 구성. 타이밍·기본음 대체·원격 청취·군중 비용 확인 | [캐릭터 오디오](Docs/Animation/Architecture/Foley_Audio_System.md) |
| 직업·스킬·콤보 | 입력 태그·AbilitySet·콤보 그래프·AttackDefinition 조합. 입력 분기와 판정·이동·연출의 일치 확인 | [직업 도구](Docs/Gameplay/Authoring/Content_Authoring_System.md#tool) · [장비·직업 데이터](Docs/Gameplay/Authoring/Content_Authoring_System.md#data) |

캐릭터 호환성은 이름이나 특정 골격 규격만으로 보장하지 않는다. 실제 골격·애니메이션·소켓·IK를 검수한다. Data Asset은 선택·튜닝, Blueprint/AnimBP는 에셋 조합·포즈 표현, C++는 권한·상태·입력·복제·작업 수명을 담당한다. 기존 데이터로 표현하기 어려운 동작은 C++ 확장 범위를 함께 정한다.

## 에셋 출처와 제출 조건

에셋 제안에는 아래 정보를 함께 제공하고 [THIRD_PARTY_ASSETS.md](THIRD_PARTY_ASSETS.md)의 해당 항목을 갱신한다.

| 항목 | 필요한 정보 |
| --- | --- |
| 식별 | 에셋명·버전·실제 경로, 구성 파일·의존 자원·파생본 |
| 출처 | 원 제작자·권리자, 공식 상품/배포 페이지, 취득 경로·원 게임. 추출자는 원 권리자와 구분 |
| 이용 조건 | 적용 라이선스 URL·버전·취득일·확인일, 수정·원본/수정본 공개·빌드·영상·공모전·상업적 이용 범위와 필수 크레딧 |
| 제작 내용 | 리깅·리타기팅·LOD·소켓·노티파이·재질 등 프로젝트 변경과 실제 검수, 미확인 사항 |

구매 증빙·개인정보·계정 정보는 공개 PR에 첨부하지 않는다. **공개 재배포 권한이 불명확한 추출 에셋이나 구매 팩 원본은 브랜치·PR·Issue 첨부·LFS로 올리지 않고 출처와 권리 근거부터 텍스트로 제시한다.** 출처 표기나 학습 목적이 이용 허락을 대신하지 않는다. 직접 제작한 리그·리소스에도 외부 아트가 포함되어 있다면 그 조건을 확인한다.

기존 파일을 교체·삭제하거나 Git/LFS 이력을 변경할 때는 정확한 경로·근거·참조 영향과 보존 방법을 설명하고 해당 작업 범위를 확인한다. 향후 임시 콘텐츠를 교체한다는 계획은 현재 미확인 파일의 공개 권한과 별개다.

## PR과 검증

PR에는 문제의 재현 조건, 변경 후 동작과 범위, 기존 구조를 유지하거나 바꾼 이유, 수행한 검증과 남은 항목을 적는다. 다른 날짜의 측정이나 이전 빌드를 현재 변경의 결과로 쓰지 않는다.

C++ 변경은 관련 빌드·집중 회귀, 에셋 변경은 참조·실제 플레이·품질과 관련 수명 경로를 확인한다. 성능 비교는 동일 조건에서 진행하고 CPU·Frame·GPU·네트워크 지표를 구분한다. 문서만 바뀌면 빌드 대신 링크·표·앵커·다이어그램을 확인한다. Unreal 빌드의 직접 UBT 사용과 기존 프로세스 정상 종료 규칙은 [AGENTS.md](AGENTS.md)를 따른다.

문서 추가·이동 시 카탈로그와 연결 링크를 갱신한다.

```powershell
node Scripts/Documentation/Update-DocumentCatalog.mjs
node Scripts/Validation/Validate-DocLinks.mjs
git diff --check -- README.md THIRD_PARTY_ASSETS.md CONTRIBUTING.md Docs
```

기존 링크 검사기는 루트의 `THIRD_PARTY_ASSETS.md`·`CONTRIBUTING.md`와 URL fragment를 검사하지 않으므로 별도로 확인한다. Git 비교는 관련 텍스트 경로로 좁힌다. 전체 에셋 diff/status가 LFS clean filter를 실행할 수 있으며, 오류를 숨기려고 필터를 끄거나 LFS 객체·인덱스·작업 에셋을 삭제하지 않는다.
