#!/usr/bin/env python3
"""MonteCalc integration test: installs the extension into a throw-away
LibreOffice profile, builds a small model in a headless Calc instance, runs a
simulation through the dispatch API and checks the results.

Usage: integration_test.py build/MonteCalc.oxt
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

import uno
from com.sun.star.beans import PropertyValue

OXT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/MonteCalc.oxt")
PROFILE = tempfile.mkdtemp(prefix="montecalc-profile-")
PROFILE_URL = "file://" + PROFILE
PORT = 2099
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


def main():
    env = dict(os.environ, SAL_USE_VCLPLUGIN="svp")
    r = subprocess.run(["unopkg", "add", "--force", f"-env:UserInstallation={PROFILE_URL}", OXT],
                       capture_output=True, text=True, env=env)
    check(r.returncode == 0, "extension installs with unopkg " + (r.stderr.strip()[:300] if r.returncode else ""))
    if r.returncode:
        return

    office = subprocess.Popen(
        ["soffice", f"-env:UserInstallation={PROFILE_URL}", "--headless", "--invisible", "--norestore",
         "--nologo", f"--accept=socket,host=localhost,port={PORT};urp;"],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)
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
        err = office.stderr.read().decode(errors="replace")
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
    frame = doc.CurrentController.Frame

    def cell(ref):
        return sheet.getCellRangeByName(ref)

    def put(ref, formula):
        cell(ref).setFormula(formula)

    def val(ref):
        c = cell(ref)
        return None if c.getError() else c.getValue()

    def run(**kw):
        args = tuple(prop(k, v) for k, v in kw.items()) + (prop("Silent", True),)
        dispatcher.executeDispatch(frame, "org.montecalc:Run", "", 0, args)

    print("model functions")
    put("A1", '=MC_TRIANGULAR("Volume";80;100;140)')
    check(cell("A1").getError() == 0 and near(val("A1"), 320 / 3, 1e-9),
          f"MC_TRIANGULAR shows the expected value (got {val('A1')}, formula {cell('A1').getFormula()})")
    put("A2", '=MC_NORMAL("Price";10;1;8;12)')
    check(near(val("A2"), 10, 1e-9), "bounded MC_NORMAL shows its truncated mean")
    put("A3", '=MC_CORRELATE("Volume";"Price";-0.5)')
    check(near(val("A3"), -0.5, 0), "MC_CORRELATE returns the coefficient")
    put("A4", '=MC_POISSON("Orders";3)')
    put("A5", '=MC_GBM("Index";100;0.05;0.2;1;4)')
    check(near(val("A5"), 100 * math.exp(0.2), 1e-9), "MC_GBM shows the expected value after 4 periods")
    put("A6", '=MC_CUSTOM("Hist";E1:G3)')
    for r, (lo, hi, n) in enumerate([(0, 10, 1), (10, 20, 2), (20, 30, 1)], start=1):
        cell(f"E{r}").setValue(lo); cell(f"F{r}").setValue(hi); cell(f"G{r}").setValue(n)
    check(near(val("A6"), 15, 1e-9), "MC_CUSTOM histogram shows its mean")
    put("A7", "=A1*A2+A4")
    put("A8", '=MC_OUTPUT("Revenue";A7)')
    check(near(val("A8"), 320 / 3 * 10 + 3, 1e-6), "MC_OUTPUT passes the value through")
    put("A9", '=MC_OUTPUT("IndexOut";A5+A6)')
    put("A10", '=MC_UNIFORM("Bad";5;1)')
    check(cell("A10").getError() != 0, "invalid parameters produce a cell error")
    put("A10", "")

    put("B1", '=MCR_MEAN("Revenue")')
    check(near(val("B1"), -1, 0), "result functions return -1 before the first run")

    print("simulation")
    t0 = time.time()
    run(Iterations=3000, Seed=42, Report=True)
    secs = time.time() - t0
    print(f"  3000 iterations took {secs:.2f} s ({secs / 3000 * 1000:.2f} ms per iteration incl. report)")
    put("B2", '=MCR_ITERATIONS()')
    check(near(val("B2"), 3000, 0), "MCR_ITERATIONS = 3000")
    check(near(val("A1"), 320 / 3, 1e-9), "after the run the model shows expected values again")
    mean = val("B1")
    check(mean is not None and near(mean, 320 / 3 * 10 + 3, 25), f"MCR_MEAN(Revenue) plausible: {mean}")
    put("B3", '=MCR_CORRS("Volume";"Price")')
    check(near(val("B3"), -0.5, 0.05), f"rank correlation honoured: {val('B3')}")
    put("B4", '=MCR_MIN("Price")')
    put("B5", '=MCR_MAX("Price")')
    check(val("B4") >= 8 and val("B5") <= 12, "truncation bounds respected")
    put("B6", '=MCR_MEAN("Orders")')
    check(near(val("B6"), 3, 0.15), f"Poisson mean ~3: {val('B6')}")
    put("B7", '=MCR_PERCENTILE("Revenue";0.5)')
    put("B8", '=MCR_REAL("Revenue")')
    check(near(val("B7"), val("B8"), 1e-9), "MCR_REAL equals the 50% percentile by default")
    put("B9", '=MCR_TOPDRIVER("Revenue";1)')
    check(cell("B9").getString() in ("Volume", "Price"), f"top driver: {cell('B9').getString()}")
    put("B10", '=MCR_PROBBETWEEN("Price";8;12)')
    check(near(val("B10"), 1, 0), "MCR_PROBBETWEEN over the full bounded range is 1")
    put("B11", '=MCR_BAYES("Revenue";">=";1000;"Price";">=";10)')
    check(val("B11") is not None and 0 <= val("B11") <= 1, f"MCR_BAYES probability: {val('B11')}")
    put("B12", '=MCR_MEAN("Unknown")')
    check(cell("B12").getError() != 0, "unknown variable name gives an error")
    put("B13", '=MCR_MEAN("IndexOut")')
    check(near(val("B13"), 100 * math.exp(0.2) + 15, 3), f"second output mean: {val('B13')}")
    rng = sheet.getCellRangeByName("C1:C3000")
    rng.setArrayFormula('MCR_SAMPLES("Revenue")')
    check(near(sum(r[0] for r in rng.getDataArray()) / 3000, mean, 1e-6), "MCR_SAMPLES array matches the mean")

    print("reproducibility")
    put("B14", '=MCR_VALUE("Revenue";17)')
    first = val("B14")
    run(Iterations=3000, Seed=42, Report=False)
    check(near(val("B14"), first, 1e-9), "same seed gives identical iterations")

    print("replay")
    dispatcher.executeDispatch(frame, "org.montecalc:Replay", "", 0, (prop("Iteration", 17), prop("Silent", True)))
    check(near(val("A8"), first, 1e-9), f"replaying iteration 17 reproduces the output ({val('A8')} vs {first})")
    dispatcher.executeDispatch(frame, "org.montecalc:Expected", "", 0, (prop("Silent", True),))
    check(near(val("A1"), 320 / 3, 1e-9), "back to expected values")

    print("result workbook")
    report = None
    for comp in desktop.Components:
        if comp is not doc and hasattr(comp, "Sheets") and comp.Sheets.hasByName("Data"):
            report = comp
    check(report is not None, "result workbook was created")
    if report:
        names = list(report.Sheets.ElementNames)
        check(names == ["Summary", "Charts", "Sensitivity", "Data"], f"sheets: {names}")
        data = report.Sheets.getByName("Data")
        check(data.getCellRangeByName("A3001").getValue() == 3000, "Data sheet holds every iteration")
        check(data.isProtected(), "Data sheet is protected")
        summ = report.Sheets.getByName("Summary")
        hdr = [summ.getCellByPosition(c, 0).getString() for c in range(1, 4)]
        check(hdr[:2] == ["Revenue", "IndexOut"], f"summary columns start with outputs: {hdr}")
        charts = report.Sheets.getByName("Charts").Charts
        check(charts.getCount() >= 2, f"histogram charts created: {charts.getCount()}")
        check(report.Sheets.getByName("Sensitivity").Charts.getCount() == 2, "one tornado chart per output")
        report.close(True)

    print("save / reload")
    path = tempfile.mktemp(suffix=".ods")
    doc.storeToURL("file://" + path, (prop("FilterName", "calc8"),))
    doc2 = desktop.loadComponentFromURL("file://" + path, "_blank", 0, (prop("Hidden", True),))
    c = doc2.Sheets.getByIndex(0).getCellRangeByName("A1")
    check(c.getError() == 0 and near(c.getValue(), 320 / 3, 1e-9), f"formulas survive save/reload: {c.getFormula()}")
    doc2.close(True)
    os.remove(path)

    print("performance")
    t0 = time.time()
    run(Iterations=10000, Seed=1, Report=False)
    secs = time.time() - t0
    print(f"  10000 iterations: {secs:.2f} s")
    check(secs < 60, "10,000 iterations of a small model in under a minute")
    doc.close(True)


if __name__ == "__main__":
    main()
    print(f"\n{'FAILED' if failures else 'PASSED'}: {len(failures)} failure(s)")
    sys.exit(1 if failures else 0)
