#!/usr/bin/env python3
"""MonteCalc user handbook (PDF, English) for the LibreOffice extension and the Excel add-in.

    python3 tools/make_handbook.py [out.pdf]        (default: docs/MonteCalc_Handbook.pdf)

The function reference (appendix A) comes from tools/gen_functions.py, the same table that
generates the add-ins; the version from ext/description.xml; screenshots from docs/handbook
and docs/screenshots.
"""
import os
import re
import sys
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, CondPageBreak, Frame, Image, KeepTogether, NextPageTemplate, PageBreak,
                                PageTemplate, Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.tableofcontents import TableOfContents

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_functions as gf  # noqa: E402

VERSION = re.search(r'<version value="([^"]+)"', open(os.path.join(ROOT, "ext", "description.xml"), encoding="utf-8").read()).group(1)
SHOTS = os.path.join(ROOT, "docs", "handbook")
OLD_SHOTS = os.path.join(ROOT, "docs", "screenshots")
LOGO = os.path.join(ROOT, "tools", "assets", "mcflo_logo.png")

# ------------------------------------------------------------------ fonts and styles
FONT_DIR = "/usr/share/fonts/truetype/dejavu"
fonts = {"Sans": "DejaVuSans.ttf", "Sans-Bold": "DejaVuSans-Bold.ttf", "Sans-Oblique": "DejaVuSans-Oblique.ttf",
         "Mono": "DejaVuSansMono.ttf"}
for name, file in fonts.items():
    path = os.path.join(FONT_DIR, file)
    if not os.path.exists(path):
        path = os.path.join(FONT_DIR, "DejaVuSans.ttf")
    pdfmetrics.registerFont(TTFont(name, path))
pdfmetrics.registerFontFamily("Sans", normal="Sans", bold="Sans-Bold", italic="Sans-Oblique", boldItalic="Sans-Bold")

BLUE = colors.HexColor("#1F4E79")
ACCENT = colors.HexColor("#E88428")   # the output colour of the charts
GREY = colors.HexColor("#5A6270")
LIGHT = colors.HexColor("#EEF2F7")
RULE = colors.HexColor("#C9D3DF")

base = ParagraphStyle("base", fontName="Sans", fontSize=9.6, leading=13.6, textColor=colors.HexColor("#22262E"), spaceAfter=5)
S = {
    "body": base,
    "small": ParagraphStyle("small", parent=base, fontSize=8.2, leading=11),
    "cell": ParagraphStyle("cell", parent=base, fontSize=8.4, leading=11, spaceAfter=0),
    "cellb": ParagraphStyle("cellb", parent=base, fontName="Sans-Bold", fontSize=8.4, leading=11, spaceAfter=0, textColor=BLUE),
    "fn": ParagraphStyle("fn", parent=base, fontName="Mono", fontSize=7.6, leading=9.8, spaceAfter=0, textColor=BLUE),
    "fndesc": ParagraphStyle("fndesc", parent=base, fontSize=7.8, leading=10, spaceAfter=0),
    "h1": ParagraphStyle("h1", parent=base, fontName="Sans-Bold", fontSize=19, leading=24, textColor=BLUE, spaceBefore=4, spaceAfter=12),
    "h2": ParagraphStyle("h2", parent=base, fontName="Sans-Bold", fontSize=12.8, leading=17, textColor=BLUE, spaceBefore=12, spaceAfter=5),
    "h3": ParagraphStyle("h3", parent=base, fontName="Sans-Bold", fontSize=10.4, leading=14, textColor=colors.HexColor("#2F3A4A"),
                         spaceBefore=8, spaceAfter=3),
    "caption": ParagraphStyle("caption", parent=base, fontName="Sans-Oblique", fontSize=8, leading=10.5, textColor=GREY,
                              alignment=TA_CENTER, spaceBefore=3, spaceAfter=10),
    "code": ParagraphStyle("code", parent=base, fontName="Mono", fontSize=8.3, leading=11.2, leftIndent=8, textColor=colors.HexColor("#1B2733"),
                           backColor=LIGHT, borderPadding=(4, 6, 4, 6), spaceBefore=3, spaceAfter=9),
    "bullet": ParagraphStyle("bullet", parent=base, leftIndent=14, bulletIndent=4, spaceAfter=2.5),
    "note": ParagraphStyle("note", parent=base, fontSize=9, leading=12.6, backColor=colors.HexColor("#FFF4E8"),
                           borderColor=ACCENT, borderWidth=0, borderPadding=(6, 8, 6, 8), leftIndent=4, rightIndent=4,
                           spaceBefore=6, spaceAfter=10),
    "toc1": ParagraphStyle("toc1", parent=base, fontName="Sans-Bold", fontSize=10, leading=15, leftIndent=0),
    "toc2": ParagraphStyle("toc2", parent=base, fontSize=9, leading=12.5, leftIndent=16, textColor=GREY),
}


def c(text):
    """inline code"""
    return f'<font name="Mono" size="8.6" color="#1F4E79">{escape(text)}</font>'


class Story(list):
    def h1(self, text):
        self.append(CondPageBreak(60 * mm))
        p = Paragraph(text, S["h1"])
        p.toc_level = 0
        self.append(p)

    def h2(self, text):
        self.append(CondPageBreak(35 * mm))
        p = Paragraph(text, S["h2"])
        p.toc_level = 1
        self.append(p)

    def h3(self, text):
        self.append(CondPageBreak(25 * mm))
        self.append(Paragraph(text, S["h3"]))

    def p(self, text, style="body"):
        self.append(Paragraph(text, S[style]))

    def bullets(self, items):
        for it in items:
            self.append(Paragraph(it, S["bullet"], bulletText="•"))
        self.append(Spacer(1, 4))

    def steps(self, items):
        for i, it in enumerate(items, 1):
            self.append(Paragraph(it, S["bullet"], bulletText=f"{i}."))
        self.append(Spacer(1, 4))

    def code(self, text):
        self.append(Paragraph(escape(text).replace("\n", "<br/>"), S["code"]))

    def note(self, text):
        self.append(Paragraph(text, S["note"]))

    def table(self, rows, widths, header=True, style="cell"):
        data = []
        for r, row in enumerate(rows):
            st = S["cellb"] if header and r == 0 else S[style]
            data.append([cell if not isinstance(cell, str) else Paragraph(cell, st) for cell in row])
        t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
        cmds = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
                ("TOPPADDING", (0, 0), (-1, -1), 3.2), ("BOTTOMPADDING", (0, 0), (-1, -1), 3.2),
                ("LEFTPADDING", (0, 0), (-1, -1), 4), ("RIGHTPADDING", (0, 0), (-1, -1), 4)]
        if header:
            cmds += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, BLUE)]
        t.setStyle(TableStyle(cmds))
        self.append(t)
        self.append(Spacer(1, 8))

    def figure(self, path, width_mm, caption):
        if not os.path.exists(path):
            return
        from PIL import Image as PILImage
        w, h = PILImage.open(path).size
        width = width_mm * mm
        img = Image(path, width=width, height=width * h / w)
        img.hAlign = "CENTER"
        self.append(KeepTogether([img, Paragraph(caption, S["caption"])]))


# ------------------------------------------------------------------ document with TOC and page decoration
class Handbook(BaseDocTemplate):
    def __init__(self, filename):
        super().__init__(filename, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=22 * mm, bottomMargin=20 * mm,
                         title="MonteCalc User Handbook", author="MonteCalc", subject=f"MonteCalc {VERSION}: LibreOffice Calc and Microsoft Excel",
                         creator="tools/make_handbook.py")
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="body")
        self.addPageTemplates([PageTemplate(id="cover", frames=[frame], onPage=self.cover_page),
                               PageTemplate(id="body", frames=[frame], onPage=self.body_page)])

    def afterFlowable(self, flowable):
        level = getattr(flowable, "toc_level", None)
        if level is not None:
            text = re.sub(r"<[^>]+>", "", flowable.text)
            key = f"h{level}-{self.seq.nextf('heading')}"
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(text, key, level=level, closed=level > 0)
            self.notify("TOCEntry", (level, text, self.page, key))

    def cover_page(self, canv, doc):
        canv.saveState()
        canv.setFillColor(BLUE)
        canv.rect(0, A4[1] - 18 * mm, A4[0], 18 * mm, stroke=0, fill=1)
        canv.setFillColor(ACCENT)
        canv.rect(0, A4[1] - 20 * mm, A4[0], 2 * mm, stroke=0, fill=1)
        canv.restoreState()

    def body_page(self, canv, doc):
        canv.saveState()
        canv.setStrokeColor(RULE)
        canv.setLineWidth(0.5)
        canv.line(20 * mm, A4[1] - 14 * mm, A4[0] - 20 * mm, A4[1] - 14 * mm)
        canv.setFont("Sans", 7.5)
        canv.setFillColor(GREY)
        canv.drawString(20 * mm, A4[1] - 12 * mm, "MonteCalc User Handbook")
        canv.drawRightString(A4[0] - 20 * mm, A4[1] - 12 * mm, f"Version {VERSION} · LibreOffice Calc and Microsoft Excel")
        canv.line(20 * mm, 13 * mm, A4[0] - 20 * mm, 13 * mm)
        canv.drawRightString(A4[0] - 20 * mm, 9 * mm, str(doc.page))
        canv.restoreState()


# ------------------------------------------------------------------ content
def build(out):
    s = Story()
    W = 170  # text width in mm

    # ---- cover
    s.append(Spacer(1, 32 * mm))
    if os.path.exists(LOGO):
        logo = Image(LOGO, width=34 * mm, height=34 * mm)
        logo.hAlign = "CENTER"
        s.append(logo)
    s.append(Spacer(1, 10 * mm))
    s.append(Paragraph("MonteCalc", ParagraphStyle("t", parent=base, fontName="Sans-Bold", fontSize=34, leading=40, alignment=TA_CENTER, textColor=BLUE)))
    s.append(Paragraph("User Handbook", ParagraphStyle("t2", parent=base, fontSize=19, leading=26, alignment=TA_CENTER, textColor=GREY)))
    s.append(Spacer(1, 8 * mm))
    s.append(Paragraph("Monte Carlo simulation for LibreOffice Calc and Microsoft Excel<br/>following the MC FLO workflow",
                       ParagraphStyle("t3", parent=base, fontSize=12, leading=17, alignment=TA_CENTER)))
    s.append(Spacer(1, 50 * mm))
    s.append(Paragraph(f"Version {VERSION} · November 2026", ParagraphStyle("t4", parent=base, fontSize=10, alignment=TA_CENTER, textColor=GREY)))
    s.append(Paragraph("LibreOffice extension (.oxt) · Excel add-in (.xll, 64-bit and 32-bit)",
                       ParagraphStyle("t5", parent=base, fontSize=9, alignment=TA_CENTER, textColor=GREY)))
    s.append(NextPageTemplate("body"))
    s.append(PageBreak())

    # ---- contents
    s.append(Paragraph("Contents", S["h1"]))
    toc = TableOfContents()
    toc.levelStyles = [S["toc1"], S["toc2"]]
    toc.dotsMinLevel = 0
    s.append(toc)
    s.append(PageBreak())

    # ================================================================ 1 Introduction
    s.h1("1  Introduction")
    s.p("MonteCalc adds Monte Carlo simulation to spreadsheets. You describe the uncertain inputs of a model with "
        "probability distributions, mark the results you are interested in, and MonteCalc recalculates the model "
        "thousands of times. The result is not a single number but a distribution: how likely a loss is, which input "
        "drives the risk, what the worst and best cases are.")
    s.p("MonteCalc follows the workflow, function names and conventions of <b>MC FLO</b> (user guide and wiki on "
        "www.mcflosim.ch). Models written for MC FLO calculate in MonteCalc. MonteCalc is an independent implementation; "
        "it contains no MC FLO code and is not affiliated with MC FLOsim. The name MC FLO is used only to describe compatibility.")
    s.h2("1.1  Two editions, one program")
    s.p("MonteCalc comes in two editions built from the same source code. The functions, the windows, the commands and "
        "the results are the same; a model gives the same results for the same seed in both.")
    s.table([
        ["", "LibreOffice Calc", "Microsoft Excel"],
        ["File", c("MonteCalc-windows_x86_64.oxt") + " / " + c("MonteCalc-linux_x86_64.oxt"),
         c("MonteCalc-Excel-x64.xll") + " (64-bit Office) / " + c("MonteCalc-Excel-x86.xll") + " (32-bit Office)"],
        ["Requires", "LibreOffice 7.6 or later (64-bit)", "Excel 2010 or later, Windows"],
        ["Commands", "MC FLO sidebar deck, MC FLO menu and toolbar, Extension tab (tabbed interface)", "MC FLO ribbon tab"],
        ["Argument separator", c(";") + " (e.g. " + c("=FLOsimula_Normal(100;15;\"Demand\")") + ")",
         c(",") + " in English Excel, " + c(";") + " in German or Spanish Excel"],
        ["Result workbook", c(".ods"), c(".xlsx")],
    ], [30 * mm, 70 * mm, 70 * mm])
    s.p("In this handbook formulas are written with " + c(";") + " as in LibreOffice and in German or Spanish Excel. "
        "In English Excel type " + c(",") + " instead.")
    s.h2("1.2  What you can do with MonteCalc")
    s.bullets([
        "<b>Model uncertain inputs</b> with 33 distributions (20 continuous, 8 discrete, 5 time series, including your own data), optionally truncated, rounded or summed.",
        "<b>Correlate inputs</b> with nine copula types (normal, Clayton, Gumbel, Student-t and asymmetric types), calibrated to the rank correlation you ask for.",
        "<b>Simulate</b> thousands of iterations, with a live preview window, pause and stop, and Memory Calculation on all processor cores.",
        "<b>Analyse</b> results in the result window (histogram, cumulative curve, tornado chart, sensitivities, scatter, decision matrix, statistics), "
        "with more than 100 " + c("fMC_") + " result and tool functions in your own cells, in a result workbook and in the influence diagram.",
        "<b>Compare variants</b> of a decision with multiple simulation.",
        "<b>Fit</b> distributions and time-series models to historical data and forecast with Holt-Winters.",
        "<b>Ask the AI mentor</b> about your model and its results.",
        "Work in <b>English, German or Spanish</b>.",
    ])

    # ================================================================ 2 Installation
    s.h1("2  Installation")
    s.h2("2.1  LibreOffice extension")
    s.steps([
        "Download " + c("MonteCalc-windows_x86_64.oxt") + " (Windows) or " + c("MonteCalc-linux_x86_64.oxt") + " (Linux) from the latest release.",
        "In LibreOffice choose <b>Tools → Extension Manager… → Add…</b>, select the file and accept. "
        "Alternatively double-click the .oxt file.",
        "<b>Restart LibreOffice completely.</b> On Windows also close the Quickstarter in the system tray. "
        "Until LibreOffice has restarted, the MonteCalc functions show " + c("#NAME?") + ".",
        "Open " + c("business_case.ods") + " (attached to every release). You find the <b>MC FLO</b> sidebar deck (icon at the right edge, "
        "or <i>View → Sidebar</i>), the <b>MC FLO</b> menu and toolbar, and the <b>Extension</b> tab of the tabbed interface "
        "(<i>View → User Interface → Tabbed</i>).",
    ])
    s.p("To update, remove the old version in the Extension Manager, add the new one and restart LibreOffice. "
        "To uninstall, select MonteCalc in the Extension Manager and choose <b>Remove</b>.")
    s.h2("2.2  Excel add-in")
    s.steps([
        "Find out whether your Office is 64-bit or 32-bit: <i>File → Account → About Excel</i>; the first line ends in “64-bit” or “32-bit”. "
        "Download " + c("MonteCalc-Excel-x64.xll") + " for 64-bit or " + c("MonteCalc-Excel-x86.xll") + " for 32-bit Office.",
        "Windows may block downloaded add-ins: right-click the file, choose <i>Properties</i>, tick <b>Unblock</b> and click OK.",
        "In Excel go to <b>File → Options → Add-ins</b>, choose <i>Manage: Excel Add-ins → Go… → Browse…</i>, select the .xll file and confirm. "
        "The add-in now loads every time Excel starts.",
        "Excel shows the <b>MC FLO</b> ribbon tab. Open " + c("business_case.xlsx") + " and click <b>Start</b> in the Simulation group.",
    ])
    s.p("No Visual C++ runtime or other installation is needed; the add-in registers its ribbon for the current user only "
        "(no administrator rights). To update, replace the .xll file and restart Excel. To remove it, untick it in the Add-ins dialog.")
    s.note("<b>If the MC FLO tab does not appear:</b> open <i>File → Options → Add-ins → Manage: COM Add-ins → Go…</i> and tick "
           "<b>MonteCalc</b>. All commands are also on the <b>Add-ins</b> tab (MC FLO menu) as a fallback.")
    s.h2("2.3  Language")
    s.p("Windows, dialogs, messages, ribbon labels and chart captions are available in English, German and Spanish. "
        "<b>Auto</b> follows the language of LibreOffice or of Windows. To choose another language use <b>Settings… → Language</b> "
        "(LibreOffice) or <b>Other → Language</b> on the Excel ribbon. The choice is stored in "
        + c("Documents\\MC_FLO_language.txt") + " and shared by both editions.")
    s.p("Function names (" + c("FLOsimula_…") + ", " + c("fMC_…") + "), variable names and the sheets of the result workbook are "
        "the same in every language, so a model works in every language version.")
    s.h2("2.4  AI mentor (optional)")
    s.p("The AI mentor needs an Anthropic API key. Put it into " + c("Documents\\MC_FLO_API.txt") + " or type it into the mentor window. "
        "The mentor sends the structure and the statistics of your model, never the cell contents.")

    # ================================================================ 3 Quick start
    s.h1("3  Quick start: a product launch")
    s.p("The example workbook " + c("business_case") + " (.ods for LibreOffice, .xlsx for Excel, attached to every release) "
        "evaluates the launch of a new product over five years. Open it and follow these steps.")
    s.h3("The inputs (cells B4 to B10)")
    s.table([
        ["Cell", "Input", "Formula"],
        ["B4", "Market size (units / year)", c('=FLOsimula_PERT(200000;350000;250000;"MarketSize")')],
        ["B5", "Market share", c('=FLOsimula_Triangular(0.05;0.12;0.08;"Share")')],
        ["B6", "Price per unit", c('=FLOsimula_Normal(25;2;"Price";;18;32)') + "<br/>(normal, truncated to 18…32)"],
        ["B7", "Unit cost", c('=FLOsimula_Triangular(12;18;14;"UnitCost")')],
        ["B8", "Investment", c('=FLOsimula_LogNormal(0;12.8992;0.1492;"Investment")')],
        ["B9", "Launch delayed (1 = yes)", c('=FLOsimula_Bernoulli(0.2;"Delay")')],
        ["B10", "Annual market growth", c('=FLOsimula_Normal(0.03;0.02;"Growth")')],
    ], [14 * mm, 42 * mm, 114 * mm])
    s.p("Between simulations each input cell shows its expected value, so the spreadsheet stays a normal, readable model.")
    s.h3("Correlations, outputs and results")
    s.bullets([
        c('=FLOsimula_correlation("Share";"Price";-0.6;0)') + " in B13: a higher price means a lower market share.",
        c('=FLOsimula_correlation("UnitCost";"MarketSize";-0.4;0)') + " in B14: economies of scale.",
        "The cash flow table (rows 22–27) and the net present value (B29) are ordinary spreadsheet formulas.",
        c('=B29+FLOsimula_output("NPV")') + " in B30 and the total cash flow in B31 mark the outputs.",
        "Rows 34–42 use result functions, e.g. " + c('=fMC_Average("NPV")') + ", " + c('=fMC_Percentile("NPV";fMC_Worst())') + " and "
        + c('=fMC_Bayes("NPV";">";0;"Price";">=";25)') + ".",
    ])
    s.h3("Run it")
    s.steps([
        "Select cell B30 (the NPV output), so that the preview shows the NPV.",
        "Click <b>Start</b> (Simulation group). The preview window opens and the histogram grows as the iterations come in.",
        "When the run is finished, the result window opens for the NPV and rows 34–42 show the results.",
        "Choose <b>Tornado chart</b> in the <i>View</i> list to see which inputs drive the NPV; click a bar for the scatter of that input.",
        "Enter " + c("0") + " as <i>Magic stripe</i> to read the probability of a negative NPV off the cumulative curve.",
    ])
    s.figure(os.path.join(SHOTS, "results.png"), 150, "Figure 1 – Result window: histogram of the NPV with the magic stripe (red), "
             "worst and best case (dashed) and the 95% certainty range (green)")

    # ================================================================ 4 Concepts
    s.h1("4  Concepts")
    s.h2("4.1  Inputs, outputs and correlations")
    s.table([
        ["Kind", "Written as", "Named range", "Cell colour"],
        ["Input variable", c('=FLOsimula_<Distribution>(parameters; "Name"; …)'), c("FLO_i_Name"), "blue"],
        ["Output variable", c('=<formula>+FLOsimula_output("Name")'), c("FLO_o_Name"), "orange"],
        ["Correlation", c('=FLOsimula_correlation("A"; "B"; r; type)'), c("FLO_c_A_B"), "yellow"],
        ["Locked input", "an input fixed at its expected value (Lock)", "–", "red"],
        ["Variant reference", "rows Variant | Value | Reference (Multiple simulation)", "–", "grey"],
    ], [30 * mm, 70 * mm, 34 * mm, 36 * mm])
    s.p("<b>Inputs</b> draw a new value in every iteration. <b>Outputs</b> are recorded in every iteration; " + c("FLOsimula_output")
        + " adds 0, so " + c('=B29+FLOsimula_output("NPV")') + " shows the value of B29. <b>Correlations</b> link two named inputs.")
    s.p("An MC FLO function must be the <b>principal function of its cell</b>: MonteCalc records the value of the cell, so "
        + c('=FLOsimula_Normal(100;15;"Demand")*2') + " records the doubled value under the name Demand.")
    s.h2("4.2  Arguments of the distributions")
    s.p("All input functions take their parameters first, then the name and four optional settings:")
    s.code('=FLOsimula_<Distribution>(parameters…; Variable_name; [Number]; [Min]; [Max]; [Round])')
    s.table([
        ["Argument", "Meaning"],
        ["Variable_name", "Name of the input, used in correlations, result functions and charts. Without a name the input is "
                          "drawn independently and <i>Register variables</i> can name it later."],
        ["Number", "Number of occurrences per iteration; the draws are summed (compound distribution). Empty or 0 = 1."],
        ["Min, Max", "Truncate the distribution to this range."],
        ["Round", "Number of decimals (TRUE = whole numbers)."],
    ], [28 * mm, 142 * mm])
    s.p("Example: " + c('=FLOsimula_Normal(25;2;"Price";;18;32)') + " is a normal distribution with mean 25 and standard deviation 2, "
        "truncated to 18…32 (Number left empty).")
    s.h2("4.3  Modes of the spreadsheet")
    s.table([
        ["Mode", "The input cells show"],
        ["Expected values (normal state)", "the expected value of each distribution"],
        ["During a simulation", "the values of the current iteration"],
        ["Preview mode (seed −1)", "a new random value on every recalculation"],
        ["Analyse iteration (Replay)", "the input values of one stored iteration, written into the cells; <b>Expected values</b> restores the formulas"],
    ], [55 * mm, 115 * mm])

    # ================================================================ 5 Building a model
    s.h1("5  Building a model")
    s.h2("5.1  Choosing a distribution: the wizard")
    s.p("Select the cell for the input and choose a distribution from <b>Choose continuous variable</b>, <b>Choose discrete variable</b> "
        "or <b>Choose time series</b>, or open the <b>Wizard</b>. The wizard shows the distribution with a sample, its statistics and "
        "the parameters:")
    s.bullets([
        "Change the parameters and click <b>Show</b> to redraw. <i>Number of iterations / bins</i> controls the sample.",
        "Drag the red <b>magic stripe</b> over the chart, or enter " + c("x") + " (a value) or " + c(";p") + " (a percentile) to read probabilities.",
        "<b>Load real data</b> overlays historical data from the selection; <b>Custom defined (from selection)</b> builds a "
        + c("FLOsimula_UserDefined") + " distribution from the selected data.",
        "Enter the <b>Name of distribution</b> and click <b>Save into the selected cell</b>. MonteCalc writes the formula, creates the "
        + c("FLO_i_") + " name and colours the cell.",
    ])
    s.figure(os.path.join(SHOTS, "wizard.png"), 140, "Figure 2 – The wizard with a PERT distribution (80, 140, mode 100)")
    s.p("<b>Changing a variable.</b> Select a cell that already holds an input and open the wizard: it shows the cell's distribution, "
        "parameters, name and options. <b>Update the selected cell</b> replaces only the " + c("FLOsimula_") + " call, so the rest of the "
        "formula (e.g. " + c("*2") + ") stays. Choosing another distribution from the menu keeps the name and the options.")
    s.p("Numbers may be typed with a decimal comma (" + c("0,5") + "); the wizard writes " + c("0.5") + " into the formula.")
    s.h2("5.2  Fitting distributions to data")
    s.p("Select a column of historical data and choose <b>Fit distribution</b>. MonteCalc fits all suitable distributions by least squares "
        "against the empirical distribution function and ranks them with the Kolmogorov–Smirnov test and the AIC; a Q-Q plot shows the "
        "quality of the selected fit. <i>Insert formula into current cell</i> writes the chosen distribution. For data over time, "
        "<i>Fit time series</i> compares AR, MA, ARMA, ARCH and Wiener models (with Theil's U and a Holt-Winters comparison).")
    s.h2("5.3  Correlations")
    s.p("Select an empty cell and choose <b>Insert correlation</b>. Pick the two inputs, type the coefficient or use the slider, and "
        "choose the correlation type. The scatter shows 1,000 sample points: both values beyond the 90% band green (upper) or red "
        "(lower), one value beyond it black, the rest blue. The rank correlation actually reached is shown in grey under the coefficient. "
        "<b>OK</b> writes the formula, for example " + c('=FLOsimula_correlation("Price";"Investment";0.6;3)') + ".")
    s.figure(os.path.join(OLD_SHOTS, "correlation_window.png"), 140, "Figure 3 – Correlation window: Price and Investment, Gumbel copula, r = 0.6")
    s.table([
        ["Code", "Type", "Character"],
        ["0", "Normal", "symmetric; the default"],
        ["1", "Clayton", "strong joint movement in the lower tail"],
        ["2", "rClayton (rotated)", "strong joint movement in the upper tail"],
        ["3", "Gumbel", "upper tail dependence"],
        ["4", "rGumbel (rotated)", "lower tail dependence"],
        ["5", "Student-t", "joint extremes in both tails"],
        ["6", "asymmetric Gumbel", "asymmetric (Khoudraji) Gumbel"],
        ["7", "asymmetric Triangular", "asymmetric (Khoudraji) Clayton"],
        ["8", "asymmetric Fréchet", "Marshall–Olkin type"],
    ], [16 * mm, 46 * mm, 108 * mm])
    s.p("The coefficient is calibrated so that the simulated <b>rank correlation</b> matches it. Inconsistent correlation matrices are "
        "repaired with a warning. Archimedean and asymmetric types (1–4, 6–8) are bivariate: an input can be part of only one such "
        "correlation, and a pair can be correlated only once; the window checks both.")
    s.h2("5.4  Outputs")
    s.p("Select the cell with a result and choose <b>Insert output variable</b>. MonteCalc asks for a name (the label to the left is "
        "proposed) and turns " + c("=B29") + " into " + c('=B29+FLOsimula_output("NPV")') + ".")
    s.h2("5.5  Register, check and lock")
    s.table([
        ["Command", "What it does"],
        ["Register variables", "Names unnamed inputs (from the label to the left), creates the " + c("FLO_i_ / FLO_c_ / FLO_o_") + " names and colours the cells."],
        ["Model", "Brings a changed model up to date: variables that were overwritten or deleted are removed and every reference to them "
                  "is replaced by 0; then the variables are listed."],
        ["Show variables", "Lists all inputs, outputs and correlations with their cells and statistics; <i>Details</i> opens the result window."],
        ["Lock / Unlock", "Fixes the selected input at its expected value for the next runs (red cell), e.g. to see its effect."],
        ["Excel-Check (model check)", "Writes a check workbook: volatile and lookup functions, MC FLO functions that are not the principal "
                                      "function of their cell, constants in formulas, empty references, unnamed inputs, duplicate names, "
                                      "unknown correlation partners."],
        ["FLO_Context sheet", "A sheet describing every variable with the cells it depends on."],
    ], [38 * mm, 132 * mm])
    s.h2("5.6  Multiple simulation (variants)")
    s.p("To compare decisions, enter rows of <i>Variant | Value | Reference</i> (the reference is the cell that takes the value, "
        "e.g. a price), select them and choose <b>Multiple simulation</b>. Each Start then simulates every variant. In the result window "
        "a variant list appears at the top and the <b>Variants</b> view overlays their distributions; result functions take the variant "
        "as optional last argument: " + c('=fMC_Average("NPV";"High")') + ". Choose <b>Multiple simulation</b> on an empty cell to reset.")

    # ================================================================ 6 Running a simulation
    s.h1("6  Running a simulation")
    s.h2("6.1  Settings")
    s.p("The Simulation group holds the most used settings (Best-case, Real-case, Worst-case, Iterations and check boxes); "
        "<b>Settings…</b> holds all of them. They are stored in the workbook.")
    s.table([
        ["Setting", "Meaning", "Default"],
        ["# iterations", "Number of iterations of a run.", "1000"],
        ["Seed", "0 = a new random sequence each run; any other number repeats the same iterations; −1 = preview mode "
                 "(every recalculation draws random values).", "0"],
        ["Worst / Real / Best case level", "Percentiles used for the worst, real and best case (result functions, charts, summary).", "0.05 / 0.5 / 0.95"],
        ["Histogram bins", "Default number of bins of the histograms.", "20"],
        ["Use correlations", "Off: all inputs are simulated independently (to see the effect of the correlations).", "on"],
        ["Memory Calculation", "Evaluate the model with MonteCalc's own formula engine, on all processor cores (6.3).", "off"],
        ["Preview window", "Show the live preview during the run (6.2).", "on"],
        ["Visualize simulation", "Let the spreadsheet show every iteration (slow).", "off"],
        ["Store results in a result workbook", "Write the result workbook after each run (7.4).", "off"],
        ["Use DIP", "Store the outputs as " + c("FLO_r_") + " arrays in the model.", "off"],
        ["Language", "Auto, English, Deutsch, Español.", "Auto"],
    ], [44 * mm, 100 * mm, 26 * mm])
    s.h2("6.2  Start, preview, pause and stop")
    s.p("<b>Start</b> registers the variables with one recalculation, prepares the (correlated) random numbers and runs the iterations. "
        "The <b>preview window</b> shows the live histogram of the variable in the selected cell, or of a randomly chosen output when no "
        "variable is selected; the drop-down switches to any input or output. Below the chart are the progress, the speed, the "
        "remaining time and the running mean, standard deviation, P5 and P95.")
    s.bullets([
        "<b>Pause</b> holds the simulation; the bar turns yellow and <b>Resume</b> continues it.",
        "<b>Stop</b> ends the run early. The completed iterations become the result (the summary says "
        "“Simulation stopped after N of M iterations”); a multiple simulation skips the remaining variants. In Excel, <b>Esc</b> also stops.",
        "After the run, a summary lists the mean, standard deviation and the worst, real and best case of every output, and the result "
        "window opens for the selected variable.",
        "<b>Reset</b> forgets the results.",
    ])
    s.figure(os.path.join(SHOTS, "preview.png"), 135, "Figure 4 – Preview window during a run on four threads")
    s.figure(os.path.join(SHOTS, "paused.png"), 135, "Figure 5 – The same run paused: the bar turns yellow, the button reads Resume")
    s.figure(os.path.join(SHOTS, "lo_preview.png"), 160, "Figure 6 – The preview window in LibreOffice Calc, in front of the example model")
    s.h2("6.3  Memory Calculation and multiple cores")
    s.p("Normally every iteration recalculates the spreadsheet. LibreOffice and Excel allow that only on their main thread, so one "
        "processor core does the work. With <b>Memory Calculation</b>, MonteCalc reads the formulas of the model once and evaluates "
        "them with its own engine, which is several times faster and runs the iterations on <b>all processor cores</b>. The results "
        "do not depend on the number of cores: the same seed gives the same iterations.")
    s.bullets([
        "If the model uses a function the engine does not know, MonteCalc says so and recalculates the spreadsheet instead.",
        "Models whose time series continue from one iteration to the next run in order on one core.",
        "In Excel the workbook is busy during a run; only the MonteCalc windows respond.",
    ])
    s.h2("6.4  Seeds and reproducibility")
    s.p("With a seed other than 0, every run repeats the same iterations, so results can be compared after a change of the model. "
        "Seed −1 turns on <b>preview mode</b>: every recalculation shows new random values, a quick way to watch the model move.")

    # ================================================================ 7 Results
    s.h1("7  Analysing the results")
    s.h2("7.1  The result window")
    s.p("Open it with <b>Result window</b> (Statistics group), from the variable list, or after a run. Choose the variable at the top "
        "(" + c("[out]") + ", " + c("[in]") + " and " + c("[corr]") + " entries) and the view in the <i>View</i> list:")
    s.table([
        ["View", "Shows"],
        ["Histogram, Cumulative", "Distribution with the magic stripe (drag it, or enter " + c("x") + " or " + c(";p") + "), worst and best "
                                  "case markers and the certainty level (e.g. 95%) with its lower and upper limit."],
        ["Tornado chart", "For each input the mean of the output when the input lies in its lowest 10% (blue) and highest 10% (orange), "
                          "sorted by swing. Click a bar for the scatter of that input."],
        ["Correlation (Spearman)", "Rank correlation of every input and output with the variable."],
        ["Dynamic sensitivity", "Range of the output by interval of an input, the expected value in orange."],
        ["Spider diagram", "Pearson correlation by 10% quantile, isolated or cumulative."],
        ["Scatter / regression", "The variable against its strongest input driver, or against the input you clicked. For a correlation: "
                                 "the scatter of its two variables with the rank correlation reached."],
        ["Variants, Box-Whisker", "The variants of a multiple simulation overlaid; boxes 25–75%, whiskers 15–85%."],
        ["Decision matrix", "For a target (" + c(">") + " the result should exceed it, " + c("<") + " stay below): the conditional "
                            "probability that a driver breaches its ambition level when the target is missed, against the conditional "
                            "damage. The zones blend from green (lower left) to red (upper right)."],
        ["Statistics", "Mean, variance, skewness, kurtosis, minimum, maximum, mode, percentiles P1…P99 and more."],
        ["Overview", "Several variables at a glance: select their cells before opening the result window."],
    ], [38 * mm, 132 * mm])
    s.p("The buttons store the results in a result workbook, export the chart as an image, store it as a cell comment, "
        "start a new run and zoom (also Ctrl +, Ctrl −, Ctrl 0). All windows can be resized; they reopen with their last size.")
    s.figure(os.path.join(SHOTS, "tornado.png"), 140, "Figure 7 – Tornado chart: the NPV with each input at its lowest and highest 10%")
    s.figure(os.path.join(SHOTS, "scatter.png"), 140, "Figure 8 – Scatter / regression of the NPV against Price, with the dynamic sensitivity below")
    s.figure(os.path.join(SHOTS, "decision.png"), 140, "Figure 9 – Decision matrix: which drivers threaten the target, and how much")
    s.figure(os.path.join(SHOTS, "statistics.png"), 140, "Figure 10 – Statistics view")
    s.h2("7.2  Result functions in your cells")
    s.p("The " + c("fMC_") + " functions bring results into the spreadsheet, so reports update with every run. A few examples:")
    s.table([
        ["Formula", "Result"],
        [c('=fMC_Average("NPV")'), "mean of the NPV"],
        [c('=fMC_Percentile("NPV";fMC_Worst())'), "the NPV at the worst-case level (e.g. 5%)"],
        [c('=1-fMC_PercentileValue("NPV";0)'), "probability that the NPV is positive"],
        [c('=fMC_ExpectedShortfall("NPV";0.05)'), "mean of the worst 5% of the NPV"],
        [c('=fMC_CorrRankN("NPV";1)'), "name of the input with the strongest rank correlation"],
        [c('=fMC_Bayes("NPV";">";0;"Price";">=";25)'), "P(NPV > 0 given Price ≥ 25)"],
        [c('=fMC_Average("NPV";"High")'), "mean of the NPV in variant High"],
    ], [80 * mm, 90 * mm])
    s.p("The <b>Functions</b> menu (Information group) lists every " + c("fMC_") + " function by category and writes the chosen one "
        "into the selected empty cell; the Function Wizard (" + c("fx") + ") shows a description of each function and argument. "
        "Appendix A lists all functions. Functions that return a table spill automatically in Microsoft 365 and Excel 2021 and in LibreOffice "
        "when entered as array formula (Ctrl+Shift+Enter); in older Excel versions select the output range and confirm with Ctrl+Shift+Enter.")
    s.h2("7.3  Analysing a single iteration")
    s.p("Enter an iteration number in <b>Choose iteration</b> and click <b>Analyze iteration</b> (Excel: <i>Replay</i>): the input "
        "values of that iteration are written into the model, so you can follow how the result came about. <b>Expected values</b> "
        "restores the formulas.")
    s.h2("7.4  The result workbook")
    s.p("<b>Store results</b> writes a result workbook with the sheets <i>Data</i> (all iterations, write-protected), <i>Summary</i> "
        "(scenarios, 5% steps, 1% extremes), <i>Statistics</i> (histogram, cumulative curve, confidence interval of the mean), "
        "<i>Sensitivity</i> (tornado) and <i>Variants</i>. <b>Change histogram</b> sets the number of bins; <b>Close and save results</b> "
        "saves it as " + c("<model>_dd_mm_yyyy_hh_mm_ss_MC_FLO") + " (.ods in LibreOffice, .xlsx in Excel). "
        "<b>Calc correlations</b> adds a regression sheet for an X- and Y-variable: enter a new X in I4 to get the estimate in I5 and "
        "the 95% forecast interval in K5/L5.")
    s.h2("7.5  Influence diagram")
    s.p("<b>Influence diagram</b> (Information group) draws the model: inputs on the left, relationships in the middle, outputs on "
        "the right, with arrows traced through intermediate cells. Hover for details, drag nodes, double-click to jump to the cell. "
        "The context menu adds distributions inside the nodes, sensitivity-weighted arrows, focus on a selection, a group view and "
        "orthogonal arrows; the Results tab overlays histograms. Export as PNG, JPEG or BMP.")
    s.figure(os.path.join(OLD_SHOTS, "influence_diagram.png"), 165, "Figure 11 – Influence diagram: plain, with distributions, "
             "with sensitivity-weighted arrows and as group view")
    s.h2("7.6  AI mentor")
    s.p("<b>AI mentor</b> opens a chat about your model. It knows the structure of the model and the statistics of the last run "
        "(not the cell contents) and can look up MC FLO function syntax. In <i>Settings</i> choose the answer language and add your own "
        "instructions. The conversation can be copied or exported to HTML, Word or PDF.")

    # ================================================================ 8 Interface reference
    s.h1("8  Interface reference")
    s.h2("8.1  Excel: the MC FLO ribbon tab")
    s.table([
        ["Group", "Commands"],
        ["Model specification", "Continuous, discrete and time-series distribution menus (each opens the wizard; on a cell that holds a "
                                "variable it opens that variable), Wizard, Insert correlation, Insert output, Register, Lock, Fit distribution"],
        ["Information", "Influence diagram, Functions (the " + c("fMC_") + " functions by category, plus the Function Wizard), Model, "
                        "Show variables, AI mentor"],
        ["Simulation", "Best / Real / Worst levels and Iterations, check boxes Use correlations, Visualize, Memory Calculation and "
                       "Preview window, Multiple simulation, Start, Reset, Settings"],
        ["Statistics", "Variable drop-down, Show statistical parameters, Result window, Select variant"],
        ["Correlations", "X / Y drop-downs, Calc correlations, Correlogram"],
        ["Iteration", "Iteration number, Replay, Expected values"],
        ["Results", "Close and save results, Store results, Change histogram"],
        ["Other", "Sort ascending / descending, Model check, Forecast tool, Batch, Hide / load formulas, Variables, FLO_Context sheet, "
                  "Preview (random values), Language, About MonteCalc (also the large MC FLO logo button)"],
    ], [34 * mm, 136 * mm])
    s.p("The commands are also macros (" + c("MCFLO.Start") + ", " + c("MCFLO.Reset") + ", " + c("MCFLO.Settings") + ", …) that VBA "
        "can call with " + c('Application.Run "MCFLO.Start"') + ".")
    s.h2("8.2  LibreOffice: sidebar, menu, toolbar and tab")
    s.p("LibreOffice extensions cannot add a ribbon tab of their own, so the same groups appear in four places that run the same commands:")
    s.bullets([
        "the <b>MC FLO sidebar deck</b> (icon at the right edge), one panel per group, with the input fields of the Simulation group;",
        "the <b>MC FLO menu</b>, whose submenus follow the groups and include the distribution pickers with a small picture of every distribution;",
        "the <b>MC FLO toolbar</b>;",
        "the <b>Extension</b> tab of the tabbed interface (<i>View → User Interface → Tabbed</i>).",
    ])
    s.figure(os.path.join(OLD_SHOTS, "mcflo_interface.png"), 165, "Figure 12 – LibreOffice Calc with the MC FLO sidebar deck (right), the "
             "Extension tab (top) and the correlation window")
    s.h2("8.3  Keyboard")
    s.table([
        ["Keys", "Where", "Action"],
        ["Ctrl +, Ctrl −, Ctrl 0", "all chart windows", "zoom in, zoom out, reset"],
        ["Tab", "all windows", "next field"],
        ["Esc", "Excel, during a run", "stop the simulation (keeps the completed iterations)"],
        ["F5, Ctrl+R/L/S/E/F/T/G/D/I/H/K/J, Ctrl+M", "influence diagram", "the commands of its context menu"],
    ], [52 * mm, 42 * mm, 76 * mm])

    # ================================================================ 9 Differences
    s.h1("9  Differences between the editions")
    s.table([
        ["Topic", "LibreOffice", "Excel"],
        ["Commands", "sidebar deck, menu, toolbar, Extension tab", "MC FLO ribbon tab (Add-ins tab as fallback)"],
        ["Formula separator", c(";"), c(",") + " (English) or " + c(";") + " (German, Spanish); MonteCalc handles both"],
        ["Result workbook", ".ods", ".xlsx"],
        ["Function descriptions", "in the Function Wizard, in the chosen language", "in the Function Wizard, in English (categories in the chosen language)"],
        ["During a run", "LibreOffice keeps redrawing; the preview window responds", "Excel is busy; only the MonteCalc windows respond"],
    ], [36 * mm, 64 * mm, 70 * mm])
    s.p("Everything else – functions, windows, settings, results – is the same code in both editions.")

    # ================================================================ 10 Troubleshooting
    s.h1("10  Troubleshooting")
    faq = [
        ("LibreOffice shows #NAME? for every MonteCalc function.",
         "LibreOffice has not loaded the extension. Restart LibreOffice completely (on Windows including the Quickstarter in the "
         "system tray). If it persists, check in <i>Tools → Extension Manager</i> that MonteCalc is installed and enabled, remove "
         "and add it again, and make sure you installed the file for your system (Windows or Linux, 64-bit)."),
        ("Excel does not show the MC FLO tab.",
         "Enable <b>MonteCalc</b> under <i>File → Options → Add-ins → Manage: COM Add-ins</i>. All commands are also on the "
         "<b>Add-ins</b> tab. If Excel refuses to load the .xll, unblock the file (Properties → Unblock) and check that you use the "
         "64-bit file for 64-bit Office and the 32-bit file for 32-bit Office."),
        ("A formula entered with commas shows the wrong values (German or Spanish Excel).",
         "Excel in German or Spanish uses " + c(";") + " between arguments. " + c("=FLOsimula_PERT(0,5;1;0,8;\"Share\")") + " is "
         "correct there; in the MonteCalc windows you may type decimal commas, they are converted."),
        ("A function returns -1 or an error after opening the workbook.",
         "Result functions (" + c("fMC_…") + ") need a simulation and return −1 until one has run. Run <b>Start</b>."),
        ("The simulation is slow.",
         "Turn on <b>Memory Calculation</b>. Turn off <i>Visualize simulation</i>. Reduce the iterations for a first look."),
        ("Memory Calculation reports that a function is not supported.",
         "The model uses a spreadsheet function that MonteCalc's engine does not evaluate; the run recalculates the spreadsheet "
         "instead and the results are correct, only slower."),
        ("The result differs from the last run.",
         "Seed 0 draws new random numbers every run. Set a seed (e.g. 2026) in <i>Settings</i> to repeat the same iterations."),
        ("A correlation is refused.",
         "The pair is already correlated in another cell, or one of the inputs is already part of an Archimedean or asymmetric "
         "correlation (these are bivariate). The correlation window explains which."),
        ("Saving from the wizard or the correlation window fails in Excel.",
         "Make sure no cell is being edited in Excel (press Esc in the sheet) and try again. The error message shows the formula "
         "Excel refused and Excel's own description; please include it when reporting a problem."),
    ]
    for q, a in faq:
        s.append(KeepTogether([Paragraph(q, S["h3"]), Paragraph(a, S["body"])]))

    # ================================================================ Appendix A: functions
    s.append(PageBreak())
    s.h1("Appendix A  Function reference")
    s.p(f"All {len(gf.F)} functions of MonteCalc, generated from the table that also generates the add-ins. Arguments in "
        "[brackets] are optional. Separate arguments with " + c(";") + " (" + c(",") + " in English Excel).")
    for key, title in gf.SECTIONS:
        items = [f for f in gf.F if f[5] == key]
        if not items:
            continue
        s.h2(escape(title))
        rows = [["Function", "Description"]]
        for name, ret, desc, params, body, sec, note in items:
            args = "; ".join(f"[{n}]" if t == gf.A and n not in ("Value",) and not (sec in ("cont", "disc", "ts") and n != "Name" and n not in ("Min", "Max", "Round", "Number"))
                             else n for n, t, _ in params)
            d = escape(desc) + (f'<br/><font color="#5A6270" size="7.2">{escape(note)}</font>' if note else "")
            rows.append([Paragraph(escape(f"{name}({args})"), S["fn"]), Paragraph(d, S["fndesc"])])
        t = Table(rows, colWidths=[82 * mm, 88 * mm], repeatRows=1)
        t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, RULE),
                               ("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, BLUE),
                               ("FONTNAME", (0, 0), (-1, 0), "Sans-Bold"), ("FONTSIZE", (0, 0), (-1, 0), 8.4),
                               ("TEXTCOLOR", (0, 0), (-1, 0), BLUE),
                               ("TOPPADDING", (0, 0), (-1, -1), 2.4), ("BOTTOMPADDING", (0, 0), (-1, -1), 2.4),
                               ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3)]))
        s.append(t)
        s.append(Spacer(1, 6))

    # ================================================================ Appendix B: files
    s.h1("Appendix B  Files and names")
    s.table([
        ["File or name", "Purpose"],
        [c("Documents\\MC_FLO_language.txt"), "the chosen language (shared by both editions)"],
        [c("Documents\\MC_FLO_API.txt"), "the Anthropic API key of the AI mentor"],
        [c("Documents\\MC_FLO_mentor.txt"), "the mentor's answer language and your instructions"],
        [c("FLO_i_ / FLO_o_ / FLO_c_ <name>"), "named ranges of inputs, outputs and correlations"],
        [c("FLO_r_<name>"), "stored results of an output (Use DIP)"],
        [c("MonteCalc_…"), "hidden names in an Excel workbook holding the settings (LibreOffice: document properties)"],
        [c("<model>_dd_mm_yyyy_hh_mm_ss_MC_FLO.ods / .xlsx"), "saved result workbook"],
    ], [70 * mm, 100 * mm])
    s.p("On Linux, " + c("Documents") + " is the Documents folder in your home directory.", "small")

    doc = Handbook(out)
    doc.multiBuild(s)
    print("wrote", os.path.relpath(out, ROOT))


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "MonteCalc_Handbook.pdf"))
