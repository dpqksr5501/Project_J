# PC 구조 정리 최종 확인과 커밋 경계 — 2026-10-15

사용자가 R17–R18 변경의 최종 확인과 한글 상세 커밋을 승인했다. 부모 HEAD는 `f8ebd8a5a33fb732f1b8c88dd4c23f75b22a95d1`, branch는 `codex/runtime-retarget-profiles`다. 기존 이력 rewrite·amend·push·merge는 하지 않는다.

## 포함·제외

- 포함: 실제 source 정리·필요한 영구 회귀 시험, Class DA의 중복 AbilitySet 참조 제거와 Door BP 수정, 제작/구조 가이드·README/catalog, 요약·범위 장부·해시와 로컬 원자료의 SHA 목록.
- 제외: 이번 세션의 원시 MCP 요청·응답·실행 로그 사본120개. 기존 원본 Saved와 SHA 대조한 로컬 `Saved/Validation/CommitPreparation_20261015_PC/ExcludedReceipts/`에 보관한다. 문서 트리의 임시 Receipts는 제거했으며 재생성되어도 scoped ignore로 커밋하지 않는다. 로컬 원자료 목록의 `included_in_commit=false`로 구분한다.
- 보존: 테스트/샘플 BP·DA, 빈 CommandSet과 제작 fallback, Editor/개발 자동화 guard 안의 영구 `PCStructureOwnershipTests.cpp`. 이 시험은 새 UCLASS를 추가하지 않는다.
- 별도 사용자 변경: 인계문 끝의 요청문 삭제 diff는 이번 구현 커밋에서 제외하며 working tree에 남긴다. 그 SHA `9465b0717e3a5152e06831deffaa20fb70d62f7251ae45f9c9ae3b3253b0c5f2`를 보존한다.

기존 추적6458개 경로와 최근 커밋을 점검했다. 실제 `Saved/Binaries/Intermediate/DDC` 산출물, 세션 헤더·덤프·cache 경로는 추적되어 있지 않다. 기존 날짜별 검증 생성기와 curated 감사 근거는 역사적 재현/변경 이유가 있으므로 불필요하다고 추정해 삭제하지 않는다. 이미 제거한 세션 전용 도구7개의 기존 정리도 유지한다. 이번에는 입증되지 않은 삭제나 기존 이력 재작성을 만들지 않는다.

## 최종 확인

읽기 전용 교차 검토는 Gateway/Social/PS, UI/지도/입력 receipt, retarget/interaction/editor·Door를 나눠 현재 HEAD 대비 diff를 다시 확인했다. 커밋을 막을 실제 회귀는 발견하지 못했다. 주 작업자는 최신 source/Content/Config/Plugins3392개와 Editor binary9개가 R18 최종 검증 바이트와 일치함을 대조한다. 원자료120개의 로컬 보관 SHA도 확인한다. 문서·포함 경계만 정리하므로 검증된 제품 바이트를 바꾸거나 긴 빌드를 반복하지 않는다.

- direct UBT full Editor 성공. 마지막 제품 PS 수정은 CPP compile1/총4actions, 정상 종료0.
- `R18_RemainingFinalFocused1` 93개 clean. 마지막 Door 직렬화 뒤 `R18_DoorLinearFinalFocused1` 3개 clean 재확인. 중복 시험을 고유96개라고 합산하지 않는다.
- `R18_DoorCook2` 1365 cooked/0 incremental skip/7 platform skip, 총1372, 정상 종료0. Door/Class/Style은 실제 Cook ReferencedSet에 있다. 기존 GameplayCue 경로 경고1종은 유지한다.
- 관련 native 프로세스0, source/staged diff 및 문서 링크 확인. 새 source나 asset 수정이 생기면 그 영향 검증을 다시 판단한다.

확인한 PC source/body/caller·BP·DA의 불필요 로직·소유권 정리 범위는 마감했다. 성능 외에도 모바일/다중 LocalPlayer·전수 시각/오디오/socket 품질·임의 외부 동적 콘텐츠/운영 검증까지 모두 완료했다는 뜻은 아니다. 기존 CPU 증가·렌더 차이는 미해결이며 보류한다. [실제 변경과 범위](Project_J_PC_Asset_Review_Closure_2026-10-15.md)를 함께 읽는다.
