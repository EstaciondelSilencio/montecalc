#!/usr/bin/env python3
"""Builds examples/business_case.ods with the MonteCalc extension installed in a
throw-away profile (re-uses the integration-test harness).

Usage: make_example.py build/MonteCalc.oxt examples/business_case.ods
"""
import os
import sys

OUT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "examples/business_case.ods")
sys.argv = sys.argv[:2]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests"))
import integration_test as it  # noqa: E402

ROWS = [
    # label, formula or value, note
    ("Product launch business case", None, None),
    ("", None, None),
    ("Uncertain inputs", None, None),
    ("Market size (units / year)", '=FLOsimula_PERT(200000;350000;250000;"MarketSize")', "FLOsimula_PERT(Lower_Limit; Upper_Limit; Mode; Name)"),
    ("Market share", '=FLOsimula_Triangular(0.05;0.12;0.08;"Share")', "FLOsimula_Triangular(Lower_Limit; Upper_Limit; Mode; Name)"),
    ("Price per unit", '=FLOsimula_Normal(25;2;"Price";;18;32)', "Normal, truncated to 18..32 (Number empty; Min; Max)"),
    ("Unit cost", '=FLOsimula_Triangular(12;18;14;"UnitCost")', "Triangular"),
    ("Investment", '=FLOsimula_LogNormal(0;12.8992;0.1492;"Investment")', "LogNormal(Shift; Mean; StDeviation of the logarithm): about 400k +/- 60k"),
    ("Launch delayed (1 = yes)", '=FLOsimula_Bernoulli(0.2;"Delay")', "20% risk: no sales in year 1"),
    ("Annual market growth", '=FLOsimula_Normal(0.03;0.02;"Growth")', "Normal"),
    ("", None, None),
    ("Correlations", None, None),
    ("Share vs price", '=FLOsimula_correlation("Share";"Price";-0.6;0)', "Normal copula (CorType 0): higher price, lower share"),
    ("Unit cost vs market size", '=FLOsimula_correlation("UnitCost";"MarketSize";-0.4;0)', "Economies of scale"),
    ("", None, None),
    ("Fixed assumptions", None, None),
    ("Fixed costs per year", 120000, None),
    ("Discount rate", 0.08, None),
]


def build(ctx):
    smgr = ctx.ServiceManager
    desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
    doc = desktop.loadComponentFromURL("private:factory/scalc", "_blank", 0, (it.prop("Hidden", True),))
    sh = doc.Sheets.getByIndex(0)
    sh.Name = "Model"
    for r, (label, f, note) in enumerate(ROWS):
        sh.getCellByPosition(0, r).setString(label)
        if isinstance(f, str):
            sh.getCellByPosition(1, r).setFormula(f)
        elif f is not None:
            sh.getCellByPosition(1, r).setValue(f)
        if note:
            sh.getCellByPosition(2, r).setString(note)
    # cash-flow table (rows 20..)
    top = 20
    sh.getCellByPosition(0, top).setString("Cash flow")
    for j, h in enumerate(["Year", "Units", "Revenue", "Variable cost", "Fixed cost", "Cash flow"]):
        sh.getCellByPosition(j, top + 1).setString(h)
    for y in range(1, 6):
        r = top + 1 + y
        row = r + 1  # 1-based
        sh.getCellByPosition(0, r).setValue(y)
        sh.getCellByPosition(1, r).setFormula(
            f"=IF(AND(A{row}=1;$B$9=1);0;$B$4*(1+$B$10)^(A{row}-1)*$B$5)")
        sh.getCellByPosition(2, r).setFormula(f"=B{row}*$B$6")
        sh.getCellByPosition(3, r).setFormula(f"=B{row}*$B$7")
        sh.getCellByPosition(4, r).setFormula("=$B$17")
        sh.getCellByPosition(5, r).setFormula(f"=C{row}-D{row}-E{row}")
    first, last = top + 3, top + 7
    sh.getCellByPosition(0, last + 1).setString("Net present value")
    sh.getCellByPosition(1, last + 1).setFormula(f"=NPV($B$18;F{first}:F{last})-$B$8")
    sh.getCellByPosition(0, last + 2).setString("Output: NPV")
    sh.getCellByPosition(1, last + 2).setFormula(f'=B{last + 2}+FLOsimula_output("NPV")')
    sh.getCellByPosition(0, last + 3).setString("Output: total cash flow")
    sh.getCellByPosition(1, last + 3).setFormula(f'=SUM(F{first}:F{last})+FLOsimula_output("TotalCash")')

    res = last + 5
    sh.getCellByPosition(0, res).setString("Results (filled after MC FLO > Simulation > Start)")
    results = [
        ("Expected NPV", '=fMC_Average("NPV")'),
        ("Standard deviation", '=fMC_StdS("NPV")'),
        ("Worst case (5%)", '=fMC_Percentile("NPV";fMC_Worst())'),
        ("Best case (95%)", '=fMC_Percentile("NPV";fMC_Best())'),
        ("Probability NPV > 0", '=IF(fMC_Iterations()>0;1-fMC_PercentileValue("NPV";0);"")'),
        ("Expected shortfall (5%)", '=fMC_ExpectedShortfall("NPV";0.05)'),
        ("Main driver", '=fMC_CorrRankN("NPV";1)'),
        ("Its correlation", '=fMC_CorrRank("NPV";1)'),
        ("P(NPV > 0 | price >= 25)", '=fMC_Bayes("NPV";">";0;"Price";">=";25)'),
    ]
    for i, (label, f) in enumerate(results):
        sh.getCellByPosition(0, res + 1 + i).setString(label)
        sh.getCellByPosition(1, res + 1 + i).setFormula(f)
    sh.Columns.getByIndex(0).Width = 6500
    sh.Columns.getByIndex(1).Width = 3500
    sh.Columns.getByIndex(2).Width = 6000
    for c in range(1, 6):
        sh.Columns.getByIndex(c).OptimalWidth = True
    def fmt_key(fmt):
        k = doc.NumberFormats.queryKey(fmt, doc.CharLocale, False)
        return k if k != -1 else doc.NumberFormats.addNew(fmt, doc.CharLocale)
    formats = [("B4", "#,##0"), ("B5", "0.0%"), ("B6", "0.00"), ("B7", "0.00"), ("B8", "#,##0"),
               ("B10", "0.0%"), ("B17", "#,##0"), ("B18", "0.0%"), (f"B{first}:F{last}", "#,##0"),
               (f"B{last + 2}:B{res + 7}", "#,##0"), (f"B{res + 6}", "0.0%"), (f"B{res + 9}", "0.00"), (f"B{res + 10}", "0.0%")]
    # (addresses are 1-based: 0-based row r is spreadsheet row r + 1)
    for addr, fmt in formats:
        sh.getCellRangeByName(addr).NumberFormat = fmt_key(fmt)
    sh.getCellRangeByName(f"A{first}:A{last}").NumberFormat = fmt_key("0")
    for r in (0, 2, 11, 15, top, res):
        sh.getCellByPosition(0, r).CharWeight = 150
    # settings stored with the document (read by MonteCalc > Run simulation)
    udp = doc.DocumentProperties.UserDefinedProperties
    for k, v in [("Iterations", 5000.0), ("Seed", 2026.0), ("Worst", 0.05), ("Real", 0.5), ("Best", 0.95),
                 ("Bins", 20.0), ("Visualize", 0.0), ("Report", 0.0), ("Memory", 1.0), ("UseCorrelations", 1.0)]:
        udp.addProperty("MonteCalc." + k, 128, v)  # 128 = REMOVABLE
    disp = smgr.createInstanceWithContext("com.sun.star.frame.DispatchHelper", ctx)
    frame = doc.CurrentController.Frame
    disp.executeDispatch(frame, "org.montecalc:Register", "", 0, (it.prop("Silent", True),))
    disp.executeDispatch(frame, "org.montecalc:Start", "", 0,
                         (it.prop("Iterations", 5000), it.prop("Seed", 2026), it.prop("Silent", True), it.prop("Report", False)))
    npv = sh.getCellByPosition(1, res + 1).getValue()
    print(f"example simulated: expected NPV {npv:,.0f}, P(NPV>0) {sh.getCellByPosition(1, res + 5).getValue():.1%}, "
          f"driver {sh.getCellByPosition(1, res + 7).getString()}")
    it.check(abs(npv) > 0 and sh.getCellByPosition(1, res + 1).getError() == 0, "example model simulates")
    # NPV() gives its cell a currency format of its own; keep the plain number format
    sh.getCellRangeByName(f"B{last + 2}:B{last + 4}").NumberFormat = fmt_key("#,##0")
    doc.storeToURL(__import__("pathlib").Path(OUT).as_uri(), (it.prop("FilterName", "calc8"),))
    doc.close(True)
    print("wrote", OUT)


it.run_tests = build
it.main()
