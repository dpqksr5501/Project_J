"""Render every PDF page to a temporary directory and report text/layout checks."""
from pathlib import Path
import hashlib
import json
import tempfile
import pypdfium2 as pdfium
import pdfplumber
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
PDF = ROOT / "output/pdf/GASP_ProjectJ_Naturalness_2026-10-06.pdf"
QA = Path(tempfile.mkdtemp(prefix="ProjectJ_GASP_PDF_QA_"))
pdf = pdfium.PdfDocument(str(PDF))
page_images = []
for number in range(len(pdf)):
    page = pdf[number]
    bitmap = page.render(scale=1.25)
    rendered = bitmap.to_pil().convert("RGB")
    target = QA / f"page-{number+1:02}.png"
    rendered.save(target)
    page_images.append(target)
    bitmap.close()
    page.close()
pdf.close()
font = ImageFont.truetype("C:/Windows/Fonts/malgun.ttf", 17)
sheets = []
for group in range(0, len(page_images), 10):
    sheet = Image.new("RGB", (2000, 1200), "#DCE5EB")
    draw = ImageDraw.Draw(sheet)
    for index, filename in enumerate(page_images[group:group+10]):
        picture = Image.open(filename)
        picture.thumbnail((380, 548))
        x, y = (index % 5)*400+10, (index // 5)*600+30
        sheet.paste(picture, (x, y))
        draw.text((x, y-25), f"Page {group+index+1}", fill="#123B56", font=font)
    target = QA / f"sheet-{group//10+1}.png"
    sheet.save(target)
    sheets.append(str(target))
pages = []
outside = []
with pdfplumber.open(PDF) as document:
    for number, page in enumerate(document.pages, 1):
        bad = [ch for ch in page.chars if ch.get("text", "").strip() and
               (ch["x0"] < -1 or ch["x1"] > page.width+1 or
                ch["top"] < -1 or ch["bottom"] > page.height+1)]
        if bad:
            outside.append({"page": number, "count": len(bad)})
        pages.append({"page": number, "characters": len(page.chars),
                      "text_start": (page.extract_text() or "")[:180]})
report = {"date": "2026-10-06", "pdf": str(PDF), "pages": len(pages),
          "sha256": hashlib.sha256(PDF.read_bytes()).hexdigest(),
          "all_pages_rendered": True, "outside_page_bounds": outside,
          "page_text_summary": pages,
          "visual_review": "pending", "limitations":
          "Rendering and text bounds do not validate gameplay or visual animation quality."}
(ROOT / "Evidence/pdf_validation.json").write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"qa_directory": str(QA), "contact_sheets": sheets,
                  "pages": len(pages), "outside_page_bounds": outside}, ensure_ascii=False))
