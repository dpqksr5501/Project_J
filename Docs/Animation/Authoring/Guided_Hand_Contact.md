# 손바닥 접촉 기준과 Guided Hand IK

이 문서의 상세 본문은 [무기 파지와 손 접촉 통합 가이드](Weapon_Hand_Contact_System.md#body)에 통합했다. 설정·예외·수식·검증 내용을 유지하며, [이전 전체 원문](../../Archive/SourceDocuments/2026-10-03/Animation/Authoring/Guided_Hand_Contact.md)도 보존한다.

## 기존 절 바로 찾기

<a id="손바닥-접촉-기준과-guided-hand-ik"></a>
- [손바닥 접촉 기준과 Guided Hand IK](Weapon_Hand_Contact_System.md#body-손바닥-접촉-기준과-guided-hand-ik)
<a id="1-적용-범위와-현재-그래프"></a>
- [1. 적용 범위와 현재 그래프](Weapon_Hand_Contact_System.md#body-1-적용-범위와-현재-그래프)
<a id="2-소켓의-역할과-idle-조정"></a>
- [2. 소켓의 역할과 Idle 조정](Weapon_Hand_Contact_System.md#body-2-소켓의-역할과-idle-조정)
<a id="정상-부착의-공통-접촉-기준"></a>
- [정상 부착의 공통 접촉 기준](Weapon_Hand_Contact_System.md#body-정상-부착의-공통-접촉-기준)
<a id="3-솔버와-접촉-변환의-분리"></a>
- [3. 솔버와 접촉 변환의 분리](Weapon_Hand_Contact_System.md#body-3-솔버와-접촉-변환의-분리)
<a id="4-몸체-프로필과-에디터-연결"></a>
- [4. 몸체 프로필과 에디터 연결](Weapon_Hand_Contact_System.md#body-4-몸체-프로필과-에디터-연결)
<a id="5-팔꿈치-안정화-수치"></a>
- [5. 팔꿈치 안정화 수치](Weapon_Hand_Contact_System.md#body-5-팔꿈치-안정화-수치)
<a id="6-보조-뼈수명mmorpg-비용"></a>
- [6. 보조 뼈·수명·MMORPG 비용](Weapon_Hand_Contact_System.md#body-6-보조-뼈수명mmorpg-비용)
<a id="7-진단-로그"></a>
- [7. 진단 로그](Weapon_Hand_Contact_System.md#body-7-진단-로그)
<a id="8-검증-이력과-한계"></a>
- [8. 검증 이력과 한계](Weapon_Hand_Contact_System.md#body-8-검증-이력과-한계)
