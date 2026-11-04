#!/usr/bin/env python3
"""Builds examples/business_case.ods with the MonteCalc extension installed in a
throw-away profile (re-uses the integration-test harness).

The example follows the layout of the MC FLO examples: known parameters, uncertain
parameters (input variables), correlations, a calculation block with the years as
columns, output variables, result functions and a variant table for the multiple
simulation (Competition / Monopoly / Hybrid). A "Read me" sheet walks through the flow.

Usage: make_example.py build/MonteCalc.oxt examples/business_case.ods
"""
import os
import pathlib
import sys

OUT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "examples/business_case.ods")
sys.argv = sys.argv[:2]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests"))
import integration_test as it  # noqa: E402

V = "$F$1"  # variant cell: 1 Competition, 5 Monopoly, 2 Hybrid


def by_variant(competition, monopoly, hybrid):
    return f"IF({V}=5;{monopoly};IF({V}=2;{hybrid};{competition}))"


# (row, label, formula or value, note); rows are 1-based spreadsheet rows
MODEL = [
    (1, "Product launch - business case", None, None),
    (3, "Known parameters", None, None),
    (4, "Discount rate", 0.08, None),
    (5, "Tax rate", 0.20, None),
    (6, "Fixed costs per year", 120000, None),
    (7, "First year", 2027, None),
    (9, "Uncertain parameters (input variables)", None, None),
    (10, "Market size (units / year)", '=FLOsimula_PERT(200000;350000;250000;"MarketSize")', "PERT(Lower; Upper; Mode; Name)"),
    (11, "Market share", f'=FLOsimula_Triangular({by_variant(0.05, 0.10, 0.07)};{by_variant(0.12, 0.16, 0.14)};{by_variant(0.08, 0.13, 0.10)};"Share")',
     "Triangular(Lower; Upper; Mode; Name), depends on the variant"),
    (12, "Price per unit", f'=FLOsimula_Normal({by_variant(25, 28, 26)};2;"Price";;18;34)', "Normal(Mean; StDev; Name; Number; Min; Max)"),
    (13, "Unit cost (first year)", '=FLOsimula_Triangular(12;18;14;"UnitCost")', "Triangular"),
    (14, "Investment", '=FLOsimula_LogNormal(0;12.8992;0.1492;"Investment")', "LogNormal(Shift; Mean; StDev of the logarithm): about 400k +/- 60k"),
    (15, "Launch delayed (1 = yes)", '=FLOsimula_Bernoulli(0.2;"Delay")', "Bernoulli: 20% risk of no sales in the first year"),
    (16, "Annual market growth", '=FLOsimula_Normal(0.03;0.02;"Growth")', "Normal"),
    (17, "Material cost index (last year)", '=FLOsimula_TemporalS_WPmodel(1;0.08;0.01;1;0;"MaterialIndex";4)',
     "Wiener process: 4 periods from 1.00, volatility 8%, trend 1%"),
    (19, "Correlations", None, None),
    (20, "Market share vs price", '=FLOsimula_correlation("Share";"Price";-0.6;0)', "Normal copula: a higher price, a lower share"),
    (21, "Unit cost vs market size", '=FLOsimula_correlation("UnitCost";"MarketSize";-0.4;0)', "Economies of scale"),
]

YEARS = 5
COLS = "BCDEF"
CALC_TOP = 24  # header row of the calculation block
CALC = [  # (label, formula with {c} = column, {k} = 0-based year index)
    ("Units sold", "=IF(AND({k}=0;$B$15=1);0;$B$10*(1+$B$16)^{k}*$B$11)"),
    ("Revenue", "={c}25*$B$12"),
    ("Unit cost", "=$B$13*(1+($B$17-1)*{k}/4)"),
    ("Variable cost", "={c}25*{c}27"),
    ("Fixed cost", "=$B$6"),
    ("EBIT", "={c}26-{c}28-{c}29"),
    ("Taxes", "=MAX(0;{c}30)*$B$5"),
    ("Free cash flow", "={c}30-{c}31"),
]
OUTPUTS = [  # row, label, formula
    (35, "Net present value", '=NPV($B$4;B32:F32)-$B$14+FLOsimula_output("NPV")'),
    (36, "Cumulative free cash flow", '=SUM(B32:F32)+FLOsimula_output("CumulativeFCF")'),
    (37, "EBIT in the last year", '=F30+FLOsimula_output("EBIT_last_year")'),
]
RESULTS_TOP = 40
RESULTS = [
    ("Expected NPV", '=fMC_Average("NPV")', "#,##0"),
    ("Standard deviation", '=fMC_StdS("NPV")', "#,##0"),
    ("Worst case (5%)", '=fMC_Percentile("NPV";fMC_Worst())', "#,##0"),
    ("Best case (95%)", '=fMC_Percentile("NPV";fMC_Best())', "#,##0"),
    ("Probability NPV > 0", '=IF(fMC_Iterations()>0;1-fMC_PercentileValue("NPV";0);"")', "0.0%"),
    ("Expected shortfall (5%)", '=fMC_ExpectedShortfall("NPV";0.05)', "#,##0"),
    ("Main driver of the NPV", '=fMC_CorrRankN("NPV";1)', None),
    ("Its correlation", '=fMC_CorrRank("NPV";1)', "0.00"),
    ("P(NPV > 0 | price >= 27)", '=fMC_Bayes("NPV";">";0;"Price";">=";27)', "0.0%"),
    ("Expected NPV - Competition", '=fMC_Average("NPV";"Competition")', "#,##0"),
    ("Expected NPV - Monopoly", '=fMC_Average("NPV";"Monopoly")', "#,##0"),
    ("Expected NPV - Hybrid", '=fMC_Average("NPV";"Hybrid")', "#,##0"),
]
VARIANTS_TOP = 54  # heading row; variants follow
VARIANTS = [("Competition", 1), ("Monopoly", 5), ("Hybrid", 2)]

README = [
    ("MonteCalc example: product launch", True),
    ("", False),
    ("The model is laid out like the MC FLO examples: known parameters, uncertain parameters (input variables),", False),
    ("correlations, a calculation block with the years as columns, output variables, result functions and a", False),
    ("variant table for the multiple simulation. Blue cells are inputs, yellow correlations, orange outputs, grey the variants.", False),
    ("", False),
    ("The flow", True),
    ("1. Model specification: the inputs in B10:B17 use the MC FLO functions (parameters first, then the name).", False),
    ("   Select an empty cell and use Choose continuous variable / Choose Wizard to add another one.", False),
    ("2. Insert correlation: B20 and B21 relate market share to price and unit cost to market size.", False),
    ("   Select B21 and press Insert correlation to see the scatter of the two variables and change the copula.", False),
    ("3. Insert output variable: B35:B37 add +FLOsimula_output(\"Name\") to the cells to be analysed.", False),
    ("4. Register variables (or Model): creates the FLO_i_ / FLO_c_ / FLO_o_ names and colours the cells.", False),
    ("5. Multiple simulation: the variants in A55:C57 are registered; each sets F1 and the inputs follow it.", False),
    ("6. Simulation: set Iterations, best / real / worst case and Use correlations in the MC FLO sidebar, then Start.", False),
    ("7. Results: the output window opens for the NPV. Look at the correlation and dynamic sensitivity of the drivers,", False),
    ("   compare the variants (Variants, Box-Whisker) and set a target in the decision matrix (e.g. > 0).", False),
    ("   The results block in B41:B52 fills in with fMC_ functions; the Referenz argument picks a variant.", False),
    ("8. Variables: the influence diagram shows how the inputs flow into the outputs; the AI mentor explains it.", False),
    ("9. Store results / Close and save results writes the result workbook with all iterations.", False),
    ("", False),
    ("Figures before the first simulation: the inputs show their expected values and the fMC_ cells show -1.", False),
]


def build(ctx):
    smgr = ctx.ServiceManager
    desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
    doc = desktop.loadComponentFromURL("private:factory/scalc", "_blank", 0, (it.prop("Hidden", True),))
    sh = doc.Sheets.getByIndex(0)
    sh.Name = "Model"

    def cell(a):
        return sh.getCellRangeByName(a)

    def fmt_key(fmt):
        k = doc.NumberFormats.queryKey(fmt, doc.CharLocale, False)
        return k if k != -1 else doc.NumberFormats.addNew(fmt, doc.CharLocale)

    def fmt(a, f):
        cell(a).NumberFormat = fmt_key(f)

    bold = []
    for row, label, f, note in MODEL:
        cell(f"A{row}").setString(label)
        if isinstance(f, str):
            cell(f"B{row}").setFormula(f)
        elif f is not None:
            cell(f"B{row}").setValue(f)
        else:
            bold.append(row)
        if note:
            cell(f"C{row}").setString(note)
    cell("E1").setString("Variant")
    cell("F1").setValue(1)
    cell("E2").setString("1 = Competition, 5 = Monopoly, 2 = Hybrid (set by the multiple simulation)")
    cell("E2").CharHeight = 9

    # calculation block: years as columns
    cell(f"A{CALC_TOP - 1}").setString("Calculation")
    bold.append(CALC_TOP - 1)
    cell(f"A{CALC_TOP}").setString("Year")
    for k in range(YEARS):
        cell(f"{COLS[k]}{CALC_TOP}").setFormula(f"=$B$7+{k}")
    for i, (label, f) in enumerate(CALC):
        row = CALC_TOP + 1 + i
        cell(f"A{row}").setString(label)
        for k in range(YEARS):
            cell(f"{COLS[k]}{row}").setFormula(f.format(c=COLS[k], k=k))
    cell(f"A{OUTPUTS[0][0] - 1}").setString("Output variables")
    bold.append(OUTPUTS[0][0] - 1)
    for row, label, f in OUTPUTS:
        cell(f"A{row}").setString(label)
        cell(f"B{row}").setFormula(f)
    cell(f"A{RESULTS_TOP}").setString("Results (filled after MC FLO > Simulation > Start)")
    bold.append(RESULTS_TOP)
    for i, (label, f, nf) in enumerate(RESULTS):
        row = RESULTS_TOP + 1 + i
        cell(f"A{row}").setString(label)
        cell(f"B{row}").setFormula(f)
        if nf:
            fmt(f"B{row}", nf)
    cell(f"A{VARIANTS_TOP}").setString("Variant")
    cell(f"B{VARIANTS_TOP}").setString("Value")
    cell(f"C{VARIANTS_TOP}").setString("Reference")
    bold.append(VARIANTS_TOP)
    for i, (name, value) in enumerate(VARIANTS):
        row = VARIANTS_TOP + 1 + i
        cell(f"A{row}").setString(name)
        cell(f"B{row}").setValue(value)
        cell(f"C{row}").setFormula("=F1")

    # formats
    for a, f in [("B4:B5", "0.0%"), ("B6", "#,##0"), ("B7", "0"), ("B10", "#,##0"), ("B11", "0.0%"), ("B12:B13", "0.00"),
                 ("B14", "#,##0"), ("B16", "0.0%"), ("B17", "0.000"), ("B20:B21", "0.00"), (f"B{CALC_TOP}:F{CALC_TOP}", "0"),
                 (f"B{CALC_TOP + 1}:F{CALC_TOP + 8}", "#,##0"), (f"B{CALC_TOP + 3}:F{CALC_TOP + 3}", "0.00"),
                 (f"B{OUTPUTS[0][0]}:B{OUTPUTS[-1][0]}", "#,##0")]:
        fmt(a, f)
    for r in bold:
        cell(f"A{r}").CharWeight = 150
    for r in (CALC_TOP, VARIANTS_TOP):
        sh.getCellRangeByName(f"A{r}:F{r}").CharWeight = 150
    cell("A1").CharHeight = 14
    sh.Columns.getByIndex(0).Width = 7200
    for c in range(1, 6):
        sh.Columns.getByIndex(c).Width = 2900
    sh.Columns.getByIndex(2).Width = 2900

    # read-me sheet
    doc.Sheets.insertNewByName("Read me", 0)
    rm = doc.Sheets.getByName("Read me")
    for i, (text, b) in enumerate(README):
        c = rm.getCellByPosition(0, i)
        c.setString(text)
        if b:
            c.CharWeight = 150
    rm.getCellByPosition(0, 0).CharHeight = 14
    rm.Columns.getByIndex(0).Width = 26000

    # settings stored with the document (read by Start and the sidebar)
    udp = doc.DocumentProperties.UserDefinedProperties
    for k, v in [("Iterations", 10000.0), ("Seed", 2026.0), ("Worst", 0.05), ("Real", 0.5), ("Best", 0.95),
                 ("Bins", 25.0), ("Visualize", 0.0), ("Report", 0.0), ("Memory", 1.0), ("UseCorrelations", 1.0)]:
        udp.addProperty("MonteCalc." + k, 128, v)  # 128 = REMOVABLE
    disp = smgr.createInstanceWithContext("com.sun.star.frame.DispatchHelper", ctx)
    frame = doc.CurrentController.Frame
    doc.CurrentController.setActiveSheet(sh)

    def cmd(name, **kw):
        disp.executeDispatch(frame, "org.montecalc:" + name, "", 0, tuple(it.prop(k, v) for k, v in kw.items()) + (it.prop("Silent", True),))

    cmd("Register")
    doc.CurrentController.select(sh.getCellRangeByName(f"A{VARIANTS_TOP + 1}:C{VARIANTS_TOP + len(VARIANTS)}"))
    cmd("MultipleSimulation")
    cmd("Start", Iterations=10000, Seed=2026, Report=False)
    r = RESULTS_TOP + 1
    means = {n: cell(f"B{r + 9 + i}").getValue() for i, (n, _) in enumerate(VARIANTS)}
    print("example simulated:", {k: f"{v:,.0f}" for k, v in means.items()},
          f"P(NPV>0) {cell(f'B{r + 4}').getValue():.1%}, driver {cell(f'B{r + 6}').getString()}")
    it.check(means["Monopoly"] > means["Hybrid"] > means["Competition"], "variants ordered as intended")
    it.check(all(cell(f"B{RESULTS_TOP + 1 + i}").getError() == 0 for i in range(len(RESULTS))), "all result functions calculate")
    cmd("SelectVariant", Variant="Competition")
    cell("F1").setValue(1)
    fmt(f"B{OUTPUTS[0][0]}:B{OUTPUTS[-1][0]}", "#,##0")   # NPV() sets a currency format on its own
    doc.CurrentController.select(cell("B35"))
    doc.storeToURL(pathlib.Path(OUT).as_uri(), (it.prop("FilterName", "calc8"),))
    doc.close(True)
    print("wrote", OUT)


it.run_tests = build
it.main()
