# 200명 Mover 측정 원자료

- `Summary.json`과 `SourceProvenance.json`: 보고서의 최종 ABBA. 각 실행의 CSV·workload·프로세스 메모리·검증 결과와 manifest를 해당 이름의 폴더에 보존한다.
- `VariationSeries.json`과 `VariationSourceProvenance.json`: NPC solver 수정 후 먼저 실행한 ABBA다. 공격 36.880ms의 큰 편차를 포함해 보존한다. 이후 fixture의 준비 완료 캡처와 활성 montage 품질 검사를 추가했다. 이 시리즈를 버리고 좋은 값만 선택하지 않는다.
- `PredictionCandidate.json`과 `CandidateSourceProvenance.json`: 앞선 같은 바이너리의 조건부 30Hz 후보와 60Hz 대조 실행이다. 후보의 독립 이득을 확인하지 못해 기본값은 60Hz다.
- `MoverFinalSanity50/100`: 최초 인원 단계의 실제 이동·반전·착지·montage 검증이다. 최종 ABBA와 별도 실행이다.
- `SHA256.json`: 공개 원자료 지문이다. CPU scope 합을 frame 시간으로 해석하지 않는다. 실제 소켓 검증은 보고서의 별도 결과를 따른다.
