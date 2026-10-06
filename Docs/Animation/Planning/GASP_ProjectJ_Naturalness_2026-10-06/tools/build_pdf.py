"""Build the complete Korean review PDF from the adjacent Markdown sources.

Requires reportlab and pypdf. Does not access Unreal Editor or alter assets.
"""
from pathlib import Path
import argparse
import html
import json
import os
import re

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate, Frame, PageTemplate, Paragraph, Spacer, PageBreak,
    Table, TableStyle, Flowable,
)
from reportlab.platypus.tableofcontents import TableOfContents
from pypdf import PdfReader

ROOT = Path(__file__).resolve().parents[1]
FILES = ["README.md", "01_EVIDENCE.md", "02_GASP.md", "03_PROJECT_J.md",
         "04_ROADMAP.md", "05_VALIDATION.md", "PROMPT.md", "Evidence/README.md"]
BLUE = colors.HexColor("#123B56")
TEAL = colors.HexColor("#007F87")
INK = colors.HexColor("#172E3B")
PALE = colors.HexColor("#EEF5F8")
GREY = colors.HexColor("#5C7180")
W, H = A4
CONTENT = W - 36 * mm


def register_fonts(font=None, bold=None):
    font = Path(font or os.environ.get("GASP_PDF_FONT", "C:/Windows/Fonts/malgun.ttf"))
    bold = Path(bold or os.environ.get("GASP_PDF_BOLD", "C:/Windows/Fonts/malgunbd.ttf"))
    if not font.exists() or not bold.exists():
        raise FileNotFoundError("Provide Korean TrueType fonts with --font and --bold-font")
    pdfmetrics.registerFont(TTFont("Korean", str(font)))
    pdfmetrics.registerFont(TTFont("KoreanBold", str(bold)))
    pdfmetrics.registerFontFamily("Korean", normal="Korean", bold="KoreanBold",
                                italic="Korean", boldItalic="KoreanBold")


def inline(text):
    text = html.escape(text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)",
                  lambda m: f'{m[1]} <font color="#5C7180">({m[2]})</font>', text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"`([^`]+)`", r'<font color="#007F87">\1</font>', text)
    return text


def styles():
    out = {}
    common = dict(fontName="Korean", fontSize=9.4, leading=15.2,
                  textColor=INK, wordWrap="CJK", splitLongWords=True)
    out["body"] = ParagraphStyle("Body", **common, spaceAfter=7)
    out["bullet"] = ParagraphStyle("Bullet", **common, leftIndent=11,
                                    firstLineIndent=-8, spaceAfter=5)
    out["h1"] = ParagraphStyle("Chapter", fontName="KoreanBold", fontSize=21,
                                leading=29, textColor=BLUE, spaceAfter=16,
                                keepWithNext=True, wordWrap="CJK")
    out["h2"] = ParagraphStyle("Section", fontName="KoreanBold", fontSize=13,
                                leading=19, textColor=BLUE, spaceBefore=15,
                                spaceAfter=8, keepWithNext=True, wordWrap="CJK")
    out["h3"] = ParagraphStyle("Subsection", fontName="KoreanBold", fontSize=10.5,
                                leading=16, textColor=TEAL, spaceBefore=11,
                                spaceAfter=6, keepWithNext=True, wordWrap="CJK")
    out["cell"] = ParagraphStyle("Cell", fontName="Korean", fontSize=8.2,
                                  leading=12.5, textColor=INK, wordWrap="CJK",
                                  splitLongWords=True)
    out["cellhead"] = ParagraphStyle("CellHead", parent=out["cell"],
                                      fontName="KoreanBold", textColor=colors.white)
    out["code"] = ParagraphStyle("Code", fontName="Korean", fontSize=8.1,
                                  leading=12, textColor=INK, wordWrap="CJK",
                                  backColor=PALE, borderPadding=9, spaceBefore=7,
                                  spaceAfter=12, splitLongWords=True)
    out["caption"] = ParagraphStyle("Caption", parent=out["cell"], textColor=GREY,
                                     spaceBefore=5, spaceAfter=12)
    out["toc1"] = ParagraphStyle("TOC1", fontName="KoreanBold", fontSize=10.5,
                                  leading=16, textColor=BLUE, spaceBefore=9)
    out["toc2"] = ParagraphStyle("TOC2", fontName="Korean", fontSize=8.7,
                                  leading=13, leftIndent=12, textColor=GREY)
    return out


class FlowDiagram(Flowable):
    """Vector rendition of the two Markdown Mermaid ownership flowcharts."""
    def __init__(self, project):
        super().__init__()
        self.project = project
        self.width = CONTENT
        self.height = 405

    def draw(self):
        c = self.canv
        bw = self.width - 76
        x = 38
        if self.project == "GASP":
            rows = [
                ("입력 → CharacterMovement", "실제 이동·속도·충돌 캡슐"),
                ("Trajectory → 충돌 보정", "과거 경로 + 미래 이동·Facing"),
                ("상태 → Chooser", "Moving / Start / Pivot / Spin / 공중, 조건부 PSD 배열"),
                ("Motion Matching", "열린 PSD 후보 + 이전 Pose History → 자세·시간"),
                ("MM 내부 Blend Stack", "OW → Reset Root → 일반 Steering → TIP Steering"),
                ("Lean · AO · Slot → Offset Root Bone", "합성 자세와 시각 루트/캡슐 관계"),
                ("커브 처리 → Foot Placement → Leg IK", "마지막 발 접지와 자세 연결"),
                ("Pose History → 최종 출력", "후처리 자세 기록 → 다음 MM query에 피드백"),
            ]
        else:
            rows = [
                ("입력 의도 → CMC → Trajectory", "실제 이동 + Strafe 미래 Facing, 최신 snapshot"),
                ("Locomotion Component → AnimInstance", "상태·요청·수명·선택 revision"),
                ("일반 이동: 단일 PSD → MM", "Asset Set / C++ 선택, 내부 그래프 Input → Result"),
                ("단발: Runtime → 계층 Chooser → 외부 Stack", "단일 clip/time, OW → TIP 전용 Steering"),
                ("MM / 외부 Stack 선택 → Inertialization", "override 조건으로 실제 출력 결정; InAirLoop 확인 필요"),
                ("Lean → Combat Layer → Slot → AO → Root", "상체 합성, 일반 Release / TIP Interpolate"),
                ("Foot Placement → Leg IK → Pose History", "후처리 자세 + 외부 trajectory 기록 → MM 피드백"),
                ("OnFoot / Mounted → 최종 출력", "운영 leader와 최종 visual mesh는 별도 확인"),
            ]
        for i, (title, detail) in enumerate(rows):
            y = self.height - (i + 1) * 48
            c.setFillColor(PALE if i % 2 == 0 else colors.HexColor("#F7FAFB"))
            c.setStrokeColor(colors.HexColor("#B7D2DC"))
            c.roundRect(x, y, bw, 38, 6, stroke=1, fill=1)
            c.setFillColor(BLUE)
            c.setFont("KoreanBold", 9)
            c.drawCentredString(self.width / 2, y + 23, title)
            c.setFillColor(GREY)
            c.setFont("Korean", 7.6)
            c.drawCentredString(self.width / 2, y + 9, detail)
            if i < len(rows) - 1:
                mid = self.width / 2
                c.setStrokeColor(TEAL)
                c.line(mid, y, mid, y - 9)
                c.line(mid, y - 9, mid - 3, y - 5)
                c.line(mid, y - 9, mid + 3, y - 5)
        c.setFont("Korean", 7.3)
        c.setFillColor(GREY)
        c.drawString(x, 1, "역할 흐름도: 독립 분기·피드백의 상세 연결은 본문을 함께 참조")


class ReviewDoc(BaseDocTemplate):
    def __init__(self, path, **kwargs):
        super().__init__(str(path), pagesize=A4, leftMargin=18*mm, rightMargin=18*mm,
                         topMargin=20*mm, bottomMargin=18*mm, **kwargs)
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height,
                      leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
        self.addPageTemplates(PageTemplate(id="Review", frames=frame, onPage=self.page))
        self.heading_index = 0

    def beforeDocument(self):
        self.heading_index = 0

    def page(self, c, doc):
        c.saveState()
        if doc.page > 1:
            c.setFont("Korean", 7.4)
            c.setFillColor(GREY)
            c.drawString(18*mm, H-12*mm, "GASP / Project_J  |  구조·자연스러움·고도화 인계")
            c.setStrokeColor(colors.HexColor("#D8E5EC"))
            c.line(18*mm, H-14*mm, W-18*mm, H-14*mm)
        c.setFont("Korean", 7.3)
        c.setFillColor(GREY)
        c.drawString(18*mm, 10*mm, "2026-10-06 조사 snapshot  |  구현·시각·성능 검증은 구분")
        c.drawRightString(W-18*mm, 10*mm, str(doc.page))
        c.restoreState()

    def afterFlowable(self, flow):
        if isinstance(flow, Paragraph) and flow.style.name in {"Chapter", "Section"} and flow.getPlainText() != "목차":
            self.heading_index += 1
            key = f"heading-{self.heading_index}"
            self.canv.bookmarkPage(key)
            level = 0 if flow.style.name == "Chapter" else 1
            self.canv.addOutlineEntry(flow.getPlainText(), key, level, False)
            self.notify("TOCEntry", (level, flow.getPlainText(), self.page, key))


def table(lines, st):
    rows = [line.strip().strip("|").split("|") for line in lines]
    rows = [r for r in rows if not all(re.fullmatch(r"\s*:?-+:?\s*", c) for c in r)]
    n = len(rows[0])
    converted = []
    for i, row in enumerate(rows):
        if len(row) != n:
            raise ValueError(f"Malformed table row: {row}")
        converted.append([Paragraph(inline(cell.strip()), st["cellhead" if i == 0 else "cell"])
                          for cell in row])
    fractions = {2: [0.32, 0.68], 3: [0.25, 0.49, 0.26],
                 4: [0.17, 0.29, 0.30, 0.24], 5: [0.12, 0.12, 0.27, 0.25, 0.24]}
    widths = fractions.get(n, [1/n]*n)
    t = Table(converted, colWidths=[CONTENT*f for f in widths], repeatRows=1,
              hAlign="LEFT", splitByRow=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0,0), (-1,0), BLUE),
        ("ROWBACKGROUNDS", (0,1), (-1,-1), [colors.white, PALE]),
        ("VALIGN", (0,0), (-1,-1), "TOP"),
        ("LEFTPADDING", (0,0), (-1,-1), 6),
        ("RIGHTPADDING", (0,0), (-1,-1), 6),
        ("TOPPADDING", (0,0), (-1,-1), 6),
        ("BOTTOMPADDING", (0,0), (-1,-1), 6),
        ("LINEBELOW", (0,0), (-1,0), 0.8, TEAL),
        ("LINEBELOW", (0,1), (-1,-1), 0.3, colors.HexColor("#D6E4EB")),
    ]))
    return [t, Spacer(1, 9)]


def parse_md(path, st):
    lines = path.read_text(encoding="utf-8").splitlines()
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.startswith("```"):
            language = line[3:].strip()
            code = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                code.append(lines[i]); i += 1
            if language == "mermaid":
                project = "GASP" if path.name.startswith("02_") else "Project_J"
                out += [FlowDiagram(project), Paragraph(
                    "실제 확인한 경로를 역할별로 묶은 벡터 흐름도. 원본 Markdown에는 Mermaid 연결도가 포함된다.",
                    st["caption"])]
            else:
                body = "<br/>".join(html.escape(v).replace(" ", "&#160;") or "&#160;" for v in code)
                out.append(Paragraph(body, st["code"]))
            i += 1
            continue
        if line.startswith("|"):
            block = []
            while i < len(lines) and lines[i].startswith("|"):
                block.append(lines[i]); i += 1
            out.extend(table(block, st))
            continue
        m = re.match(r"^(#{1,3})\s+(.+)", line)
        if m:
            out.append(Paragraph(inline(m[2]), st[f"h{len(m[1])}"]))
            i += 1
            continue
        m = re.match(r"^(?:[-*]\s+|\d+\.\s+)(.+)", line)
        if m:
            prefix = "• " if line.startswith(("-", "*")) else line.split(" ", 1)[0] + " "
            out.append(Paragraph(inline(prefix+m[1]), st["bullet"]))
            i += 1
            continue
        block = [line]
        i += 1
        while i < len(lines) and lines[i].strip() and not re.match(r"^(?:#|\||```|[-*] |\d+\. )", lines[i]):
            block.append(lines[i]); i += 1
        out.append(Paragraph(inline(" ".join(block)), st["body"]))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font")
    parser.add_argument("--bold-font")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    register_fonts(args.font, args.bold_font)
    target = args.output or ROOT/"output/pdf/GASP_ProjectJ_Naturalness_2026-10-06.pdf"
    target.parent.mkdir(parents=True, exist_ok=True)
    st = styles()
    story = [Spacer(1, 33*mm)]
    cover = ParagraphStyle("Cover", fontName="KoreanBold", fontSize=30, leading=43,
                           textColor=BLUE, spaceAfter=20, wordWrap="CJK")
    story += [Paragraph("GASP / Project_J<br/>움직임 자연스러움<br/>분석과 고도화 인계", cover),
              Paragraph("실제 그래프 · 데이터 · 선택 · 보정 · 업데이트 · 원격/군중 예산", st["h3"]),
              Spacer(1, 12*mm),
              Paragraph("기준일 2026-10-06<br/>GASP CMC 5.7.4 / Project_J 5.8.2", st["body"]),
              Paragraph("확인된 구현과 개선 제안을 구분한 조사 snapshot이다. 코드·Config·에셋 변경, 새 빌드·PIE 검증을 수행한 결과가 아니다.", st["body"]),
              Spacer(1, 8*mm),
              Paragraph("기존 Cycle · Strafe 키보드 Pivot · 단발 동작 수명 · 최신 snapshot · MMORPG 예산을 보존하는 방향", st["h3"]),
              PageBreak(), Paragraph("목차", st["h1"])]
    toc = TableOfContents()
    toc.levelStyles = [st["toc1"], st["toc2"]]
    story.append(toc)
    for name in FILES:
        story.append(PageBreak())
        story.extend(parse_md(ROOT/name, st))
    doc = ReviewDoc(target, title="GASP / Project_J 움직임 자연스러움 분석과 고도화 인계",
                    author="Project_J documentation", subject="Read-only graph audit and phased roadmap")
    doc.multiBuild(story)
    reader = PdfReader(target)
    extracted = "\n".join(page.extract_text() or "" for page in reader.pages)
    for term in ["InAirLoop", "PSS_Combat", "170/190", "PROMPT", "Pose History"]:
        if term not in extracted:
            raise RuntimeError(f"PDF text missing: {term}")
    print(json.dumps({"pdf": str(target), "pages": len(reader.pages),
                      "source_documents": len(FILES), "text_characters": len(extracted)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
