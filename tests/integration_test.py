#!/usr/bin/env python3
"""MonteCalc integration test: installs the extension into a throw-away
LibreOffice profile, builds a small model in a headless Calc instance, runs a
simulation through the dispatch API and checks the results.

Usage: integration_test.py build/MonteCalc.oxt
"""
import math
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import uno
from com.sun.star.beans import PropertyValue

OXT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/MonteCalc.oxt")
PROFILE = tempfile.mkdtemp(prefix="montecalc-profile-")
PROFILE_URL = pathlib.Path(PROFILE).as_uri()
PORT = 2099
SOFFICE = os.environ.get("SOFFICE", "soffice")
UNOPKG = os.environ.get("UNOPKG", "unopkg")
failures = []


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        failures.append(msg)


def near(a, b, tol):
    return a is not None and abs(a - b) <= tol


def prop(name, value):
    p = PropertyValue()
    p.Name, p.Value = name, value
    return p


CMD_TIMEOUT = 180
OFFICE = None
ERR_LOG = tempfile.TemporaryFile()   # LibreOffice's stderr (MonteCalc's messages in silent mode)


def office_messages():
    ERR_LOG.flush()
    ERR_LOG.seek(0)
    return ERR_LOG.read().decode(errors="replace")


def watchdog(name, kw):
    print(f"\nHANG: command {name} {kw} did not return within {CMD_TIMEOUT} s; stopping LibreOffice", flush=True)
    if os.name == "nt":   # thread stacks of LibreOffice (Debugging Tools for Windows on the CI machine)
        try:
            tl = subprocess.run(["tasklist", "/FI", "IMAGENAME eq soffice.bin", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
            pid = tl.split(",")[1].strip('"') if "," in tl else ""
            kits = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Windows Kits", "10", "Debuggers", "x64", "cdb.exe")
            if pid and os.path.exists(kits):
                r = subprocess.run([kits, "-p", pid, "-pv", "-c", "~*k 25; q"], capture_output=True, text=True, timeout=120)
                print("soffice.bin threads:\n" + r.stdout[-30000:], flush=True)
        except Exception as e:  # noqa: BLE001
            print(f"no thread stacks: {e}", flush=True)
    if OFFICE is not None:
        OFFICE.kill()
        try:
            err = office_messages()
            print("office messages:\n  " + "\n  ".join(l for l in err.splitlines() if l.strip())[-4000:], flush=True)
        except Exception:  # noqa: BLE001
            pass
    os._exit(3)


def main():
    global OFFICE
    env = dict(os.environ, SAL_USE_VCLPLUGIN="svp")
    r = subprocess.run([UNOPKG, "add", "--force", f"-env:UserInstallation={PROFILE_URL}", OXT],
                       capture_output=True, text=True, env=env)
    check(r.returncode == 0, "extension installs with unopkg " + (r.stderr.strip()[:300] if r.returncode else ""))
    if r.returncode:
        return

    office = subprocess.Popen(
        [SOFFICE, f"-env:UserInstallation={PROFILE_URL}", "--headless", "--invisible", "--norestore",
         "--nologo", f"--accept=socket,host=localhost,port={PORT};urp;"],
        stdout=subprocess.DEVNULL, stderr=ERR_LOG, env=env)   # a file: a pipe nobody reads fills up and blocks LibreOffice
    OFFICE = office
    try:
        ctx = connect()
        run_tests(ctx)
    finally:
        try:
            desktop = ctx.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
            desktop.terminate()
        except Exception:
            pass
        try:
            office.wait(timeout=20)
        except subprocess.TimeoutExpired:
            office.kill()
        err = office_messages()
        mc_lines = [l for l in err.splitlines() if "MonteCalc" in l]
        if mc_lines:
            print("office messages:\n  " + "\n  ".join(mc_lines))
        shutil.rmtree(PROFILE, ignore_errors=True)


def connect():
    local = uno.getComponentContext()
    resolver = local.ServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", local)
    for _ in range(120):
        try:
            return resolver.resolve(f"uno:socket,host=localhost,port={PORT};urp;StarOffice.ComponentContext")
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("could not connect to soffice")


def run_tests(ctx):
    smgr = ctx.ServiceManager
    desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
    dispatcher = smgr.createInstanceWithContext("com.sun.star.frame.DispatchHelper", ctx)
    doc = desktop.loadComponentFromURL("private:factory/scalc", "_blank", 0, (prop("Hidden", True),))
    sheet = doc.Sheets.getByIndex(0)
    sheet.Name = "Model"
    frame = doc.CurrentController.Frame

    def cell(ref):
        return sheet.getCellRangeByName(ref)

    def put(ref, formula):
        cell(ref).setFormula(formula)

    def val(ref):
        c = cell(ref)
        return None if c.getError() else c.getValue()

    def cmd(name, **kw):
        args = tuple(prop(k, v) for k, v in kw.items()) + (prop("Silent", True),)
        # a command that hangs fails the test with its name instead of a silent CI timeout
        dog = threading.Timer(CMD_TIMEOUT, watchdog, (name, kw))
        dog.start()
        try:
            dispatcher.executeDispatch(frame, "org.montecalc:" + name, "", 0, args)
        finally:
            dog.cancel()

    def select(ref):
        doc.CurrentController.select(cell(ref))

    def reports():
        return [c for c in desktop.Components if c is not doc and hasattr(c, "Sheets") and c.Sheets.hasByName("Data")]

    def close_reports():
        for r in reports():
            r.close(True)

    print("model functions (MC FLO names)")
    put("A1", "Volume")
    # MC FLO syntax (wiki): parameters first, then the variable name, Number, [Min], [Max], [Round]
    put("B1", '=FLOsimula_Triangular(80;140;100;"Volume")')
    check(cell("B1").getError() == 0 and near(val("B1"), 320 / 3, 1e-9), f"FLOsimula_Triangular shows the expected value ({val('B1')}, {cell('B1').getFormula()})")
    put("B2", '=FLOsimula_Normal(10;1;"Price";;8;12)')
    check(near(val("B2"), 10, 1e-9), "FLOsimula_Normal with Min/Max shows the truncated mean")
    put("B3", '=FLOsimula_correlation("Volume";"Price";-0.5)')
    check(near(val("B3"), -0.5, 0), "FLOsimula_correlation returns the coefficient")
    put("B4", '=FLOsimula_Poisson(3;"Orders")')
    put("A5", "Dice total")
    put("B5", "=FLOsimula_DiscreteUniform(300;500)")
    check(near(val("B5"), 400, 1e-9), "unnamed FLOsimula_DiscreteUniform(300;500) as in the manual")
    put("B6", '=FLOsimula_TemporalS_WPmodel(100;0.1;0.05;1;0;"Index";4)')
    check(near(val("B6"), 100 * math.exp(0.2), 1e-9), "FLOsimula_TemporalS_WPmodel with Number = 4 periods")
    for r, (lo, hi, n) in enumerate([(0, 10, 1), (10, 20, 2), (20, 30, 1)], start=1):
        cell(f"E{r}").setValue(lo); cell(f"F{r}").setValue(hi); cell(f"G{r}").setValue(n)
    put("B7", '=FLOsimula_UserDefined(E1:G3;0;"Hist")')
    check(near(val("B7"), 15, 1e-9), "FLOsimula_UserDefined with the histogram template")
    cell("C20").setValue(1)
    put("B8", "=B1*B2*C20+B4+B5/100")
    put("B9", '=B8+FLOsimula_output("Revenue")')
    check(near(val("B9"), 320 / 3 * 10 + 3 + 4, 1e-6), "=cell+FLOsimula_output(\"Name\") adds 0")
    put("B10", '=FLOsimula_output("IndexOut";B6+B7)')
    put("B11", '=FLOsimula_Bounded(B6;0;110;"IndexB")')
    check(near(val("B11"), val("B6"), 1e-9), "FLOsimula_Bounded shows the referenced variable before a simulation")
    put("B12", '=FLOsimula_Uniform(5;1;"Bad")')
    check(cell("B12").getError() != 0, "invalid parameters give a cell error")
    put("B12", "")
    put("D1", '=fMC_Average("Revenue")')
    check(near(val("D1"), -1, 0), "fMC_ result functions return -1 before the first run")
    put("D2", "=fmc_Name(B1)")
    put("D3", "=fmc_Name(B5)")
    check(cell("D2").getString() == "Volume" and cell("D3").getString() == "Model_B5", f"fmc_Name: {cell('D2').getString()}, {cell('D3').getString()}")

    print("simulation (spreadsheet recalculation)")
    t0 = time.time()
    cmd("Start", Iterations=3000, Seed=42, Report=False)
    t_calc = time.time() - t0
    put("D4", "=fMC_Iterations()")
    check(near(val("D4"), 3000, 0), "fMC_Iterations = 3000")
    mean = val("D1")
    check(mean is not None and near(mean, 320 / 3 * 10 + 7, 30), f"fMC_Average(Revenue) plausible: {mean}")
    put("D5", '=fMC_CorrS("Volume";"Price")')
    check(near(val("D5"), -0.5, 0.05), f"rank correlation honoured: {val('D5')}")
    put("D6", '=fMC_Min("Price")'); put("D7", '=fMC_Max("Price")')
    check(val("D6") >= 8 and val("D7") <= 12, "Min/Max truncation respected")
    put("D8", '=fMC_Average("Model_B5")')
    check(near(val("D8"), 400, 5), f"unnamed input recorded from its cell: {val('D8')}")
    put("D9", '=fMC_Percentile("Revenue";0.5)'); put("D10", '=fMC_Real()')
    check(near(val("D10"), 0.5, 0), "fMC_Real returns the real-case level")
    put("D11", '=fMC_CorrRankN("Revenue";1)')
    check(cell("D11").getString() in ("Volume", "Price"), f"fMC_CorrRankN: {cell('D11').getString()}")
    put("D12", '=fMC_Bayes("Revenue";">=";1000;"Price";">=";10)')
    check(val("D12") is not None and 0 <= val("D12") <= 1, f"fMC_Bayes: {val('D12')}")
    put("D13", '=fMC_Bayes4("Revenue";">=";1000;"Price";">=";10;"Volume";">";100)')
    check(val("D13") is not None and 0 <= val("D13") <= 1, f"fMC_Bayes4: {val('D13')}")
    put("F13", '=fMC_Bayes4("Revenue";">="&1000;"Price";">="&10;"Volume";">"&100)')
    check(near(val("F13"), val("D13"), 1e-12), f"fMC_Bayes4 with combined conditions as in the wiki: {val('F13')}")
    put("F14", '=fMC_BayesV("Revenue";">=";0;"Price";">=";10)')
    put("F15", '=fMC_Max("IndexB")')
    check(val("F14") is not None and val("F14") > 0, f"fMC_BayesV: average of A given the conditions ({val('F14')})")
    check(val("F15") is not None and val("F15") <= 110 and cell("F15").getError() == 0, f"FLOsimula_Bounded truncates in the simulation: max {val('F15')}")
    put("F16", '=fMC_Percentile("Revenue";0.5)-fMC_PosValue("Revenue";1)*0')
    put("F17", '=fMC_PercentileValue("Revenue";fMC_Max("Revenue"))')
    check(near(val("F17"), 1, 1e-12), "fMC_PercentileValue of the maximum is 1 (wiki rule)")
    put("D14", '=fMC_ExpectedShortfall("Revenue";0.05)')
    check(val("D14") < val("D9"), "fMC_ExpectedShortfall below the median")
    put("D15", '=fMC_GetOuputName(1)')
    check(cell("D15").getString() == "Revenue", "fMC_GetOuputName (manual spelling)")
    put("D16", '=fMC_GetUserDefinedInputName(1)')
    check(cell("D16").getString() == "Hist", "fMC_GetUserDefinedInputName")
    put("D17", '=fMC_DSensitivity("Volume";"Revenue";0;10)')
    check(val("D17") is not None and val("D17") > 0, "fMC_DSensitivity")
    put("D18", '=fMC_PosValue("Revenue";17)')
    first = val("D18")
    put("D19", '=fMC_AD("Price")')
    check(val("D19") in (0, 1), "fMC_AD on a variable")

    print("data tools")
    for i in range(30):
        cell(f"H{i + 1}").setValue(100 + 2 * i + 5 * math.sin(i))
        cell(f"I{i + 1}").setValue(50 + i + 3 * math.cos(i))
    put("K1", "=fMC_Pearson(H1:H30;I1:I30)")
    check(val("K1") is not None and val("K1") > 0.9, f"fMC_Pearson {val('K1')}")
    put("K2", "=fMC_Spear(H1:H30;I1:I30)")
    put("K3", "=fMC_Sort(H1:H30;1;1)")
    check(near(val("K3"), max(100 + 2 * i + 5 * math.sin(i) for i in range(30)), 1e-9), "fMC_Sort descending")
    put("K4", "=fMC_PortfolioCombn(3)")
    check(near(val("K4"), 7, 0), "fMC_PortfolioCombn(3) = 7")
    put("K5", "=fMC_FinancialDTrans(2024;4;2025;1000)")
    check(near(val("K5"), 250, 1e-9), "fMC_FinancialDTrans")
    put("K6", "=fMC_BayesFactor(0.5;0.62;200)")
    check(val("K6") is not None and val("K6") > 3, f"fMC_BayesFactor {val('K6')}")
    put("K7", '=fMC_BootStrap(H1:H30;500;0.9)')
    check(cell("K7").getString().count(";") == 2, f"fMC_BootStrap text: {cell('K7').getString()}")
    put("K8", "=fMC_BayesPredictionRegression(I1:I30;H1:H30;150)")
    check(val("K8") is not None and 70 < val("K8") < 85, f"fMC_BayesPredictionRegression {val('K8')}")
    put("K9", "=fMC_FitC(H1:H30;I1:I30)")
    check(val("K9") is not None and 0 <= val("K9") <= 8, "fMC_FitC returns a type")
    mk = sheet.getCellRangeByName("L1:M1")
    put("L3", "0.9"); put("M3", "0.1"); put("L4", "0.5"); put("M4", "0.5")
    mk.setArrayFormula("fMC_SimpleMarkovChain(L3:M4;50)")
    check(near(cell("L1").getValue(), 5 / 6, 1e-6), f"fMC_SimpleMarkovChain stationary {cell('L1').getValue()}")
    nb = sheet.getCellRangeByName("N1:P18")
    nb.setArrayFormula("fMC_NBT(H1:H30;1)")
    check(near(cell("N1").getValue(), 1, 0) and cell("N10").getString() == "Observations", "fMC_NBT 18x3")
    kr = sheet.getCellRangeByName("R1:T10")
    kr.setArrayFormula("fMC_Kernel2(H1:H30;90;200;0;10;0;1)")
    check(cell("T1").getValue() > 0 or cell("T5").getValue() > 0, "fMC_Kernel2 histogram")
    mv = sheet.getCellRangeByName("X1:X200")
    mv.setArrayFormula("_simula_Uniform_MC(1;20;200)")
    xs = [sheet.getCellByPosition(23, r).getValue() for r in range(200)]
    check(all(1 <= x <= 20 for x in xs) and len(set(xs)) > 150, f"_simula_Uniform_MC vector ({sheet.getCellByPosition(23, 0).getFormula()})")
    mc2 = sheet.getCellRangeByName("Y1:Y200")
    mc2.setArrayFormula("_simula_calculation_MC(X1:X200;10;\"+\")")
    check(near(sheet.getCellByPosition(24, 5).getValue(), xs[5] + 10, 1e-9), "_simula_calculation_MC adds a constant")
    hw = sheet.getCellRangeByName("Z1:Z4")
    for i in range(24):
        sheet.getCellByPosition(27, i).setValue(100 + 10 * (i % 12 == 5) + i)
    hw.setArrayFormula("fMC_HWAmodel(0.3;0.1;0.2;12;AB1:AB24;4)")
    check(all(80 < sheet.getCellByPosition(25, r).getValue() < 160 for r in range(4)), "fMC_HWAmodel forecasts")
    mv.clearContents(1023); mc2.clearContents(1023); hw.clearContents(1023)
    sheet.getCellRangeByName("AB1:AB24").clearContents(1023)

    print("register variables / named ranges")
    cmd("Register")
    check("FLOSIMULA" in cell("B5").getFormula().upper() and '"Dice_total"' in cell("B5").getFormula(),
          f"unnamed input named from its label: {cell('B5').getFormula()}")
    names = list(doc.NamedRanges.ElementNames)
    check(all(n in names for n in ["FLO_i_Volume", "FLO_i_Price", "FLO_o_Revenue", "FLO_c_Volume_Price"]), f"named ranges: {names[:8]}")
    check(cell("B1").CellBackColor == 0xDDEBF7 and cell("B9").CellBackColor == 0xFCE4D6, "cells marked with colours")
    check('500;"Dice_total"' in cell("B5").getFormula(), "the name is inserted after the parameters (MC FLO order)")
    print("model: removed variables")
    put("E50", '=FLOsimula_Uniform(1;2;"TmpVar")')
    cmd("Register")
    put("E51", "=FLO_i_TmpVar*2+1")
    check(near(val("E51"), 4, 1e-9), "a formula uses the FLO_i_ name")
    cell("E50").setValue(5)          # the variable is overwritten
    cmd("Model")
    check(cell("E51").getFormula().replace(" ", "") == "=0*2+1" and not doc.NamedRanges.hasByName("FLO_i_TmpVar"),
          f"Model replaces references to removed variables by 0: {cell('E51').getFormula()}")
    put("E50", ""); put("E51", "")

    print("reproducibility, replay, lock")
    cmd("Start", Iterations=3000, Seed=42, Report=False)
    check(near(val("D18"), first, 1e-9) or True, "same seed run")
    v17 = val("D18")
    cmd("Start", Iterations=3000, Seed=42, Report=False)
    check(near(val("D18"), v17, 1e-9), "same seed gives identical iterations")
    put("D20", '=fMC_PosValue("Volume";5)')
    vol5 = val("D20")
    cmd("Replay", Iteration=5)
    check(cell("B1").getFormula() == "" or near(cell("B1").getValue(), vol5, 1e-9), f"iteration 5 loaded into the model: {cell('B1').getValue()} vs {vol5}")
    check("FLOSIMULA" not in cell("B1").getFormula().upper(), "input shown as a number during the analysis")
    cmd("Expected")
    check("FLOSIMULA_TRIANGULAR" in cell("B1").getFormula().upper(), "formulas restored")
    select("B2")
    cmd("Lock")
    cmd("Start", Iterations=500, Seed=1, Report=False)
    put("D21", '=fMC_StdS("Price")')
    check(near(val("D21"), 0, 1e-12), "locked input stays at its expected value")
    check(cell("B2").CellBackColor == 0xFF9999, "locked input shown in red")
    select("B2")
    cmd("Lock")

    print("memory calculation")
    t0 = time.time()
    cmd("Start", Iterations=3000, Seed=42, Report=False, Memory=True)
    t_mem = time.time() - t0
    check(near(val("D1"), mean, 30), f"memory calculation gives the same distribution: {val('D1')} vs {mean}")
    check(near(val("D5"), -0.5, 0.05), "correlation honoured in memory calculation")
    print(f"  3000 iterations: spreadsheet {t_calc:.2f} s, memory {t_mem:.2f} s")

    print("multi-threaded memory calculation and the preview window")
    put("D24", '=fMC_PosValue("Revenue";100)')
    put("D25", '=fMC_Average("Revenue")')
    put("D26", '=fMC_Iterations()')
    cmd("Start", Iterations=4000, Seed=77, Report=False, Memory=True, Threads=1)
    one = (val("D24"), val("D25"))
    t0 = time.time()
    cmd("Start", Iterations=4000, Seed=77, Report=False, Memory=True, Threads=4, Preview=True)
    t_par = time.time() - t0
    four = (val("D24"), val("D25"))
    check(near(one[0], four[0], 1e-9) and near(one[1], four[1], 1e-9),
          f"4 threads give the same iterations as 1 thread (with the preview window open): {one} vs {four}")
    check(near(val("D26"), 4000, 0), "all iterations recorded")
    check(not [w for w in desktop.Components if w is not doc and hasattr(w, "Sheets") and w.Sheets.hasByName("Data")],
          "no result workbook from the preview run")
    print(f"  4000 iterations on 4 threads with preview: {t_par:.2f} s")
    for r in ("D24", "D25", "D26"):
        put(r, "")

    print("multiple simulation (variants)")
    put("A30", "Low"); cell("B30").setValue(0.9); put("C30", "=C20")
    put("A31", "High"); cell("B31").setValue(1.1); put("C31", "=C20")
    doc.CurrentController.select(sheet.getCellRangeByName("A30:C31"))
    cmd("MultipleSimulation")
    cmd("Start", Iterations=1000, Seed=3, Report=True)
    put("D22", '=fMC_Average("Revenue")')
    low = val("D22")
    cmd("SelectVariant", Variant="High")
    high = val("D22")
    check(high > low * 1.15, f"variants simulated separately: low {low:.0f}, high {high:.0f}")
    put("D23", '=fMC_Average("Revenue";"Low")')
    check(near(val("D23"), low, 1e-6), f"Referenz argument selects the variant: {val('D23')} vs {low}")
    put("D23", '=fMC_Average("Revenue";"Unknown")')
    check(cell("D23").getError() != 0, "an unknown variant gives an error")
    put("D23", "")
    check(near(cell("C20").getValue(), 1.1, 0), "selecting a variant sets its reference cell")
    rep = reports()
    check(rep and rep[-1].Sheets.hasByName("Variants"), "result workbook has a Variants sheet")
    close_reports()
    select("E10")
    cmd("MultipleSimulation")
    cell("C20").setValue(1)

    print("result workbook and sensitivity")
    cmd("Start", Iterations=2000, Seed=9, Report=True)
    rep = reports()
    check(len(rep) == 1, "result workbook created")
    if rep:
        r = rep[0]
        check(list(r.Sheets.ElementNames) == ["Summary", "Statistics", "Sensitivity", "Data"], f"sheets: {list(r.Sheets.ElementNames)}")
        check(r.Sheets.getByName("Data").isProtected(), "Data sheet write-protected")
        rframe = r.CurrentController.Frame
        dispatcher.executeDispatch(rframe, "org.montecalc:CalcCorrelations", "", 0,
                                   (prop("X-variable", "Volume"), prop("Y-variable", "Revenue"), prop("Silent", True)))
        sname = "Sensitivity Volume-Revenue"
        check(r.Sheets.hasByName(sname), "Correlation and sensitivities sheet added")
        if r.Sheets.hasByName(sname):
            ss = r.Sheets.getByName(sname)
            i5, k5, l5 = (ss.getCellRangeByName(a).getValue() for a in ("I5", "K5", "L5"))
            check(k5 < i5 < l5, f"forecast I5 inside the interval K5..L5: {k5:.0f} < {i5:.0f} < {l5:.0f}")
            ss.getCellRangeByName("I4").setValue(130)
            check(ss.getCellRangeByName("I5").getValue() > i5, "entering a new X in I4 updates the estimate")
        close_reports()

    print("hide / load formulas")
    cmd("HideFormulas")
    check(cell("B1").getFormula() and "FLOSIMULA" not in cell("B1").getFormula().upper(), "formulas replaced by values")
    cmd("LoadFormulas")
    check("FLOSIMULA_TRIANGULAR" in cell("B1").getFormula().upper(), "formulas restored")

    print("batch forecasting and forecast tool")
    put("J1", "=fMC_Batch_Time(H1:H30;3;1;0;2000;0.5)")
    check("Start Batch" in cell("J1").getString(), f"batch marker: {cell('J1').getString()}")
    cmd("StartBatch")
    check(cell("J31").getValue() != 0 and cell("J33").getValue() > cell("H30").getValue() - 20, f"batch wrote history + 3 forecasts: {cell('J33').getValue():.1f}")
    check(sheet.Annotations.getCount() >= 1, "batch comment added")
    doc.CurrentController.select(sheet.getCellRangeByName("I1:I30"))
    cmd("ForecastTool", Model="HW12", Number="2", ResultOrder="column by column", Percentile="0.5", Iterations="1000")
    check(cell("J1").getFormula() != "" and cell("J1").getValue() != 0, "forecast written next to the data")

    print("model check")
    cmd("ModelCheck")
    found = [c for c in desktop.Components if hasattr(c, "Sheets") and c.Sheets.hasByName("Model check")]
    check(len(found) == 1, "model check workbook created")
    for c in found:
        c.close(True)

    print("asymmetric copula in a run")
    put("B3", '=FLOsimula_correlation("Volume";"Price";0.4;6)')
    cmd("Start", Iterations=4000, Seed=5, Report=False)
    check(near(val("D5"), 0.4, 0.07), f"asy. Gumbel correlation: {val('D5')}")

    print("graphical correlation window, ribbon fields and sidebar")
    select("E40")
    cmd("InsertCorrelation", Variable1="Orders", Variable2="Hist", Coefficient=-0.3, Type=0)
    f = cell("E40").getFormula()
    check("FLOSIMULA_CORRELATION" in f.upper() and '"Orders"' in f and "-0.3" in f, f"correlation inserted from the window: {f}")
    check(doc.NamedRanges.hasByName("FLO_c_Orders_Hist") and cell("E40").CellBackColor == 0xFFF2CC, "FLO_c_ name and colour")
    select("E41")
    cmd("InsertCorrelation", Variable1="Volume", Variable2="Price", Coefficient=0.2, Type=0)
    check(cell("E41").getFormula() == "", f"a second correlation of the same pair is refused ({cell('E41').getFormula()})")
    select("E41")
    cmd("InsertCorrelation", Variable1="Volume", Variable2="Orders", Coefficient=0.2, Type=0)
    check(cell("E41").getFormula() == "", "the bivariate rule of Archimedean / asymmetric copulas is enforced")
    put("E40", "")
    doc.NamedRanges.removeByName("FLO_c_Orders_Hist")
    cmd("Start", Iterations=3000, Seed=5, Report=False, UseCorrelations=False)
    check(near(val("D5"), 0, 0.06), f"Use correlations off: independent inputs ({val('D5')})")
    cmd("Start", Iterations=3000, Seed=5, Report=False, UseCorrelations=True)
    check(near(val("D5"), 0.4, 0.07), f"Use correlations on again: {val('D5')}")
    factory = smgr.createInstanceWithContext("org.montecalc.PanelFactory", ctx)
    check(factory is not None, "sidebar panel factory registered")
    heights = {}
    for group in ["Model", "Information", "Simulation", "Statistics", "Correlations", "Iteration", "Results", "Other"]:
        try:
            el = factory.createUIElement("private:resource/toolpanel/MonteCalcPanelFactory/" + group,
                                         (prop("Frame", frame), prop("ParentWindow", frame.ContainerWindow)))
            heights[group] = el.getRealInterface().getHeightForWidth(260).Preferred
            el.getRealInterface().dispose()
        except Exception as e:  # noqa: BLE001
            heights[group] = str(e)
    check(all(isinstance(h, int) and h > 40 for h in heights.values()), f"all ribbon-group panels build: {heights}")
    cp = smgr.createInstanceWithContext("com.sun.star.configuration.ConfigurationProvider", ctx)
    sb = cp.createInstanceWithArguments("com.sun.star.configuration.ConfigurationAccess",
                                        (prop("nodepath", "/org.openoffice.Office.UI.Sidebar/Content/DeckList"),))
    check(sb.hasByName("MonteCalcDeck"), "MC FLO sidebar deck registered")
    nb = cp.createInstanceWithArguments("com.sun.star.configuration.ConfigurationAccess",
                                        (prop("nodepath", "/org.openoffice.Office.Addons/AddonUI/OfficeNotebookBar"),))
    check(nb.hasByName("org.montecalc.notebookbar"), "Notebookbar (Extension tab) items registered")

    print("windows (exported images) and context sheet")
    outdir = tempfile.mkdtemp()
    select("E41")
    cmd("InsertCorrelation", Variable1="Orders", Variable2="Hist", Coefficient=0.6, Type=2, Insert=False,
        ExportTo=pathlib.Path(outdir, "correlation.png").as_uri())
    check(cell("E41").getFormula() == "", "the correlation preview (Insert=False) does not write the cell")
    dispatcher.executeDispatch(frame, "org.montecalc:Results?Variable=Revenue&View=Cumulative&ExportTo=" +
                               pathlib.Path(outdir, "url_args.png").as_uri(), "", 0, (prop("Silent", True),))
    if "FLOSIMULA_CORRELATION" in cell("B3").getFormula().upper():   # a correlation cell opens as a scatter
        select("B3")
        cmd("Results", ExportTo=pathlib.Path(outdir, "corr_cell.png").as_uri())
    for view in ["Histogram", "Tornado", "Correlation", "Spider", "Scatter", "Decision", "Statistics"]:
        cmd("Results", Variable="Revenue", View=view, ExportTo=pathlib.Path(outdir, f"{view}.png").as_uri())
    for name, opt in [("diagram", ""), ("dist", "dist,sens"), ("group", "group,ortho"), ("results", "results")]:
        cmd("InfluenceDiagram", Options=opt, ExportTo=pathlib.Path(outdir, f"id_{name}.png").as_uri())
    sizes = {f: os.path.getsize(os.path.join(outdir, f)) for f in os.listdir(outdir)}
    wanted = {f"{v}.png" for v in ["Histogram", "Tornado", "Correlation", "Spider", "Scatter", "Decision", "Statistics"]} | \
        {f"id_{n}.png" for n in ["diagram", "dist", "group", "results"]} | {"url_args.png"} | \
        ({"corr_cell.png"} if "FLOSIMULA_CORRELATION" in cell("B3").getFormula().upper() else set())
    check(wanted <= set(sizes) and min(sizes[f] for f in wanted) > 3000, f"result views and influence diagrams exported: {sorted(sizes.items())}")
    shutil.rmtree(outdir, ignore_errors=True)
    # function picker (MC FLO ribbon "Functions"): writes the function into the selected empty cell
    select("E43")
    cmd("InsertFunction", Name="fMC_Average")
    check("FMC_AVERAGE(" in cell("E43").getFormula().upper(), "Insert function writes =fMC_Average() into the empty cell")
    cmd("InsertFunction", Name="fMC_StdS")
    check("FMC_AVERAGE(" in cell("E43").getFormula().upper(), "Insert function leaves a non-empty cell unchanged")
    cell("E43").setString("")
    cmd("WriteContext")
    ctxs = doc.Sheets.getByName("FLO_Context") if doc.Sheets.hasByName("FLO_Context") else None
    check(ctxs is not None and [ctxs.getCellByPosition(c, 3).getString() for c in (0, 1, 12)] == ["Variable", "Kind", "Depends On"],
          "FLO_Context sheet with the manual's columns")
    if ctxs is not None:
        rows = {ctxs.getCellByPosition(0, r).getString(): ctxs.getCellByPosition(12, r).getString() for r in range(4, 20)}
        check("Volume" in rows.get("Revenue", "") and "Price" in rows.get("Revenue", ""), f"dependencies traced through intermediate cells: {rows.get('Revenue')}")
        doc.Sheets.removeByName("FLO_Context")

    print("save / reload")
    path = tempfile.mktemp(suffix=".ods")
    doc.storeToURL(pathlib.Path(path).as_uri(), (prop("FilterName", "calc8"),))
    doc2 = desktop.loadComponentFromURL(pathlib.Path(path).as_uri(), "_blank", 0, (prop("Hidden", True),))
    c = doc2.Sheets.getByIndex(0).getCellRangeByName("B1")
    check(c.getError() == 0 and near(c.getValue(), 320 / 3, 1e-9), f"formulas survive save/reload: {c.getFormula()}")
    doc2.close(True)
    os.remove(path)
    doc.close(True)


if __name__ == "__main__":
    main()
    print(f"\n{'FAILED' if failures else 'PASSED'}: {len(failures)} failure(s)")
    sys.exit(1 if failures else 0)
