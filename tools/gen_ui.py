#!/usr/bin/env python3
"""Generates the user interface of the extension from one table that follows the MC FLO
ribbon groups:

  ext/registry/Addons.xcu          "MC FLO" menu, "MC FLO" toolbar, Notebookbar (Extension tab), icons
  ext/registry/CalcWindowState.xcu toolbar name, docked, icons + text
  ext/registry/Sidebar.xcu         "MC FLO" sidebar deck, one panel per ribbon group
  ext/registry/Factories.xcu       the panel factory
  ext/icons/*.png                  16 and 26 pixel icons (drawn here with Pillow)

Usage: gen_ui.py   (run from anywhere; rewrites the files above)
"""
import os
import sys
from xml.sax.saxutils import escape

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
EXT = os.path.join(ROOT, "ext")
CTX = "com.sun.star.sheet.SpreadsheetDocument"
sys.path.insert(0, os.path.join(ROOT, "tools"))

# ---------------------------------------------------------------------------- ribbon table
# group -> [(command, menu title, short toolbar title, icon, on toolbar)]
RIBBON = [
    ("Model specification", "Model", [
        ("Wizard", "Choose Wizard...", "Choose Wizard", "wizard", True),
        ("InsertCorrelation", "Insert correlation...", "Insert correlation", "correlation", True),
        ("InsertOutput", "Insert output variable...", "Insert output variable", "output", True),
        ("Register", "Register variables", "Register variables", "register", True),
        ("Lock", "Lock/Unlock", "Lock/Unlock", "lock", True),
        ("Fit", "Fit distribution...", "Fit distribution", "fit", True),
    ]),
    ("Information", "Information", [
        ("Model", "Model (update names, show variables)...", "Model", "model", True),
        ("InfluenceDiagram", "Variables (influence diagram)...", "Variables", "influence", True),
        ("Functions", "Functions...", "Functions", "functions", True),
        ("Show", "Variable list...", "Variable list", "variables", False),
        ("Mentor", "AI mentor...", "AI mentor", "mentor", False),
        ("WriteContext", "FLO_Context sheet", "FLO_Context", "context", False),
    ]),
    ("Simulation", "Simulation", [
        ("Settings", "Settings (iterations, best/real/worst case, use correlations)...", "Settings", "settings", True),
        ("MultipleSimulation", "Multiple simulation", "Multiple simulation", "multiple", True),
        ("Start", "Start", "Start", "start", True),
        ("Reset", "Reset", "Reset", "reset", True),
    ]),
    ("Statistics", "Statistics", [
        ("Results", "Show statistical parameters / result window...", "Show statistical parameters", "statistics", True),
        ("SelectVariant", "Select variant...", "Select variant", "variant", False),
        ("ChangeHistogram", "Change histogram...", "Change histogram", "histogram", False),
    ]),
    ("Correlations and sensitivities", "Correlations", [
        ("CalcCorrelations", "Calc correlations...", "Calc correlations", "calccorr", True),
        ("Correlogram", "Correlogram...", "Correlogram", "correlogram", False),
    ]),
    ("Individual analysis of iteration", "Iteration", [
        ("Replay", "Analyze selected iteration...", "Analyze selected iteration", "iteration", True),
        ("Expected", "Back to expected values", "Expected values", "expected", False),
        ("Preview", "Preview (random values)", "Preview", "preview", False),
    ]),
    ("Results", "Results", [
        ("StoreResults", "Store results (result workbook)", "Store results", "store", False),
        ("SaveResults", "Close and save results", "Close and save results", "save", True),
    ]),
    ("Other", "Other", [
        ("SortAsc", "Sort asc", "Sort asc", "sortasc", False),
        ("SortDesc", "Sort des", "Sort des", "sortdesc", False),
        ("ModelCheck", "Excel-Check (model check)", "Excel-Check", "check", False),
        ("ForecastTool", "Forecast-Tool...", "Forecast-Tool", "forecast", False),
        ("StartBatch", "Start Batch", "Start Batch", "batch", False),
        ("HideFormulas", "Hide formulas", "Hide formulas", "hide", False),
        ("LoadFormulas", "Load formulas", "Load formulas", "load", False),
        ("Variables", "Variables (text summary)", "Variables (text)", "variables", False),
        ("About", "About MonteCalc", "About", "logo", False),
    ]),
]


# icons of the main menu's submenus (ribbon groups and distribution pickers)
GROUP_ICONS = {
    "Model specification": "wizard", "Information": "about", "Simulation": "start", "Statistics": "statistics",
    "Correlations and sensitivities": "calccorr", "Individual analysis of iteration": "iteration", "Results": "store",
    "Other": "settings",
}
PICKERS = [("Choose continuous variable", "cont", "dist_Normal"), ("Choose discrete variable", "disc", "dist_Binomial"),
           ("Choose time series", "ts", "dist_TemporalS_ARmodel")]

# ---------------------------------------------------------------------------- fMC_ function picker
# MC FLO ribbon "Functions": the result and tool functions by category (MonteCalc's names)
FUNCTION_MENU = [
    ("Descriptive Statistics", [
        "fMC_Average", "fMC_Variance", "fMC_StdS", "fMC_Skewness", "fMC_Kurtosis", "fMC_Semivariance", "fMC_SemivarianceL",
        "fMC_SemivarianceR", "fMC_Percentile", "fMC_PercentileValue", "fMC_PercentileInterval", "fMC_CV", "fMC_Min", "fMC_Max",
        "fMC_Mode", "fMC_Sum", "fMC_Density", "fMC_PosValue", "fMC_ConditionalPosValue", "fMC_ConditionPosValue", "fMC_Mean"]),
    ("Relationship functions", [
        "fMC_CorrP", "fMC_CorrS", "fMC_CorrRank", "fMC_CorrRankN", "fMC_RegressionX", "fMC_RegressionY", "fMC_Spider",
        "fMC_DSensitivity", "fMC_AD", "fMC_ADs", "fMC_Pearson", "fMC_PearsonV", "fMC_Spear"]),
    ("Portfolio/Financial Functions", [
        "fMC_ExpGain", "fMC_ExpGainV", "fMC_ExpLoss", "fMC_ExpectedShortfall", "fMC_ExtremeValue", "fMC_FinancialValue",
        "fMC_FinancialDTrans", "fMC_BSMVal", "fMC_PortfolioCombn", "fMC_PortfolioMatrix", "fMC_PortfolioCorrMatrix",
        "fMC_PortfolioCorrMatrixF", "fMC_PortfolioManualMatrix"]),
    ("Parameter-free statistics", [
        "fMC_BootStrap", "fMC_BootStrapN", "fMC_BootStrapVector", "fMC_Kernel", "fMC_Kernel2", "fMC_AD_Data", "fMC_Sort", "fMC_FitC"]),
    ("Classical Linear Regression", [
        "fMC_PredictiveRegressionLine", "fMC_PredictiveRegressionLineValue", "fMC_BootStrapVectorLinearRegressionX",
        "fMC_BootStrapVectorLinearRegressionY", "fMC_BootStrapVectorLinearRegressionRE", "fMC_BootStrapVectorMLRegression",
        "fMC_BootStrapVectorMLRegressionParameters", "fMC_BootStrapVectorMLRegression_MC", "fMC_BootStrapVectorMLRegressionParameters_MC"]),
    ("Generative/Bayesian statistics", [
        "fMC_Bayes", "fMC_BayesV", "fMC_BayesV_MC", "fMC_Bayes4", "fMC_Bayes4V", "fMC_Bayes4V_MC", "fMC_BayesMCMC",
        "fMC_BayesMCMCA", "fMC_BayesMCMCV", "fMC_BayesMCMCAV", "fMC_BayesMCMCV_Robust", "fMC_BayesPrediction", "fMC_BayesCombine",
        "fMC_BayesClassifier", "fMC_BayesClassifier_MC", "fMC_BayesFactor", "fMC_BayesPredictionRegression",
        "fMC_BayesPredictionRegressionParameters", "fMC_BayesPredictionRegressionCIP", "fMC_BayesPredictionRegressionMAE",
        "fMC_BayesPredictionRegressionResiduals", "fMC_BayesPredictionRegressionV_MC", "fMC_BayesPredictionRegressionCIPV_MC",
        "fMC_BayesPredictionRegressionMAEV_MC", "fMC_BayesPredictionRegressionParameters_MC", "fMC_BayesPredictionRegressionResiduals_MC"]),
    ("Other functions", [
        "fMC_Iterations", "fMC_Best", "fMC_Real", "fMC_Worst", "fMC_Bin", "fMC_SimulaOn", "fmc_Name", "fMC_GetInputName",
        "fMC_GetUserDefinedInputName", "fMC_GetOutputName", "fMC_GetOuputName", "fMC_SimpleMarkovChain", "fMC_NBT", "fMC_LDT",
        "fMC_Batch_Time", "fMC_Batch_HW_D_Time", "fMC_Batch_HW_M_Time", "fMC_Batch_HW_Q_Time", "fMC_HWAmodel", "fMC_HWMmodel"]),
]
FUNCTION_ICONS = {"Descriptive Statistics": "statistics", "Relationship functions": "correlation", "Portfolio/Financial Functions": "portfolio",
                  "Parameter-free statistics": "bootstrap", "Classical Linear Regression": "regression",
                  "Generative/Bayesian statistics": "bayes", "Other functions": "functions"}


def check_function_menu():
    """every fMC_ function of tools/gen_functions.py is in exactly one category"""
    import runpy
    argv = sys.argv
    sys.argv = [argv[0]]
    try:
        names = [f[0] for f in runpy.run_path(os.path.join(ROOT, "tools", "gen_functions.py"), run_name="gen_functions")["F"]]
    finally:
        sys.argv = argv
    wanted = {n for n in names if n.lower().startswith("fmc_")}
    listed = [n for _, ns in FUNCTION_MENU for n in ns]
    dup = {n for n in listed if listed.count(n) > 1}
    unknown = set(listed) - set(names)
    missing = wanted - set(listed)
    if dup or unknown or missing:
        sys.exit("gen_ui.py: function menu - duplicates %s, unknown %s, missing %s" % (sorted(dup), sorted(unknown), sorted(missing)))


def function_menu_inc():
    lines = ["// generated by tools/gen_ui.py - do not edit (MC FLO \"Functions\" picker)"]
    for cat, names in FUNCTION_MENU:
        lines.append("{%s, {%s}}," % (cstr(cat), ", ".join(cstr(n) for n in names)))
    return "\n".join(lines) + "\n"



def flo_lists():
    """(continuous, discrete, time series) labels in the order of core/flofuncs.cpp"""
    import re
    src = open(os.path.join(ROOT, "core", "flofuncs.cpp"), encoding="utf-8").read()
    cont, disc, ts = [], [], []
    for label, arg in re.findall(r'\bdist\("(\w+)",\s*(true|false)', src):
        (disc if arg == "true" else cont).append(label)
    ts = re.findall(r'\bts\("(\w+)",', src)
    if len(cont) < 10 or len(disc) < 5 or len(ts) < 4:
        sys.exit("gen_ui.py: could not read the distribution list from core/flofuncs.cpp")
    return cont + ["UserDefined"], disc + ["DiscreteUserDefined"], ts


# ---------------------------------------------------------------------------- xml helpers
from i18n_catalog import CATALOG  # noqa: E402

TRANSLATIONS = {}
for _en, _de, _es in CATALOG:
    TRANSLATIONS.setdefault(_en, (_de, _es))
MISSING = set()


def prop(name, value, typ="xs:string", localized=False):
    if not localized:
        return '<prop oor:name="%s" oor:type="%s"><value>%s</value></prop>' % (name, typ, escape(str(value)))
    vals = ['<value xml:lang="en-US">%s</value>' % escape(str(value))]
    tr = TRANSLATIONS.get(value)
    if tr is None and " " in value and value != "MC FLO":   # function names stay as they are
        MISSING.add(value)
    if tr:
        vals += ['<value xml:lang="de">%s</value>' % escape(tr[0]), '<value xml:lang="es">%s</value>' % escape(tr[1])]
    return '<prop oor:name="%s" oor:type="%s">%s</prop>' % (name, typ, "".join(vals))


def cstr(s):
    out = []
    for b in s.encode("utf-8"):
        c = chr(b)
        if c in '"\\':
            out.append("\\" + c)
        elif 32 <= b < 127:
            out.append(c)
        else:
            out.append('\\x%02x""' % b)
    return '"' + "".join(out) + '"'


def catalog_inc():
    lines = ["// generated by tools/gen_ui.py from tools/i18n_catalog.py - do not edit"]
    seen = set()
    for en, de, es in CATALOG:
        if en in seen:
            continue
        seen.add(en)
        lines.append("{{%s, %s, %s}}," % (cstr(en), cstr(de), cstr(es)))
    return "\n".join(lines) + "\n"


def item(node, url, title, ind, extra=""):
    pad = " " * ind
    return (pad + '<node oor:name="%s" oor:op="replace">\n' % node +
            pad + "  " + prop("URL", url) + "\n" +
            pad + "  " + prop("Title", title, localized=True) + "\n" +
            pad + "  " + prop("Target", "_self") + "\n" +
            pad + "  " + prop("Context", CTX) + "\n" + extra +
            pad + "</node>\n")


def menu():
    cont, disc, ts = flo_lists()
    out = []
    out.append('      <node oor:name="org.montecalc.menu" oor:op="replace">\n')
    out.append("        " + prop("Title", "MC FLO", localized=True) + "\n")
    out.append("        " + prop("Target", "_self") + "\n")
    out.append("        " + prop("Context", CTX) + "\n")
    out.append('        <node oor:name="Submenu">\n')
    n = 0

    def sub(title, children, ind=10, icon=None):
        nonlocal n
        n += 1
        pad = " " * ind
        s = pad + '<node oor:name="g%02d" oor:op="replace">\n' % n
        s += pad + "  " + prop("Title", title, localized=True) + "\n"
        if icon:   # submenus take their icon from <ImageIdentifier>_16.bmp / _26.bmp
            s += pad + "  " + prop("ImageIdentifier", "%origin%/../icons/" + icon) + "\n"
        s += pad + "  " + prop("Context", CTX) + "\n"
        s += pad + '  <node oor:name="Submenu">\n' + "".join(children) + pad + "  </node>\n" + pad + "</node>\n"
        return s

    for gi, (group, _, cmds) in enumerate(RIBBON):
        children = []
        if gi == 0:
            for title, names, icon in PICKERS:
                names = {"cont": cont, "disc": disc, "ts": ts}[names]
                kids = [item("f%02d" % i, "org.montecalc:Wizard?Function=" + nm, nm, 16) for i, nm in enumerate(names)]
                children.append(sub(title, kids, 14, icon))
        for i, (cmd, title, _, _, _) in enumerate(cmds):
            if cmd == "Functions":   # the fMC_ functions by category, as on the MC FLO ribbon
                cats = []
                for ci, (cat, names) in enumerate(FUNCTION_MENU):
                    kids = [item("x%02d" % k, "org.montecalc:InsertFunction?Name=" + nm, nm, 18) for k, nm in enumerate(names)]
                    cats.append(sub(cat, kids, 16, FUNCTION_ICONS[cat]))
                # a command in the list: LibreOffice then also loads the icons of the category submenus
                cats.append('                <node oor:name="xsep" oor:op="replace">\n                  ' + prop("URL", "private:separator") +
                            "\n                  " + prop("Context", CTX) + "\n                </node>\n")
                cats.append(item("xwiz", ".uno:FunctionDialog", "Function Wizard...", 16))
                children.append(sub("Functions", cats, 14, "functions"))
                continue
            children.append(item("m%d%02d" % (gi, i), "org.montecalc:" + cmd, title, 14))
        out.append(sub(group, children, icon=GROUP_ICONS[group]))
    # direct entries: LibreOffice loads the icons of a popup menu only if it contains a command
    out.append('          <node oor:name="z00" oor:op="replace">\n            ' + prop("URL", "private:separator") +
               "\n            " + prop("Context", CTX) + "\n          </node>\n")
    out.append(item("z01", "org.montecalc:Start", "Start simulation", 10))
    out.append(item("z02", "org.montecalc:About", "About MonteCalc", 10))
    out.append("        </node>\n      </node>\n")
    return "".join(out)


def toolbar_items(ind, prefix, notebook=False):
    out = []
    k = 0
    for gi, (group, _, cmds) in enumerate(RIBBON):
        shown = [c for c in cmds if c[4]]
        if not shown:
            continue
        if k:
            extra = ""
            out.append(" " * ind + '<node oor:name="%s%02d" oor:op="replace">\n' % (prefix, k) + " " * ind + "  " +
                       prop("URL", "private:separator") + "\n" + " " * ind + "  " + prop("Context", CTX) + "\n" + " " * ind + "</node>\n")
            k += 1
        for cmd, _, short, _, _ in shown:
            if notebook:
                # a Notebookbar item shows either its icon ("Icon") or its text ("Text"), so every
                # command gets two items next to each other: the icon, then the label (same command)
                for style in ("Icon", "Text"):
                    extra = " " * (ind + 2) + prop("Style", style) + "\n"
                    out.append(item("%s%02d" % (prefix, k), "org.montecalc:" + cmd, short, ind, extra))
                    k += 1
            else:
                out.append(item("%s%02d" % (prefix, k), "org.montecalc:" + cmd, short, ind))
                k += 1
    return "".join(out)


def images(ind):
    out = []
    entries = [(cmd, "org.montecalc:" + cmd, icon) for _, _, cmds in RIBBON for cmd, _, _, icon, _ in cmds]
    # the distribution pickers of the menu: one shape icon per distribution
    entries += [("dist." + nm, "org.montecalc:Wizard?Function=" + nm, "dist_" + nm) for names in flo_lists() for nm in names]
    for cmd, url, icon in entries:
        pad = " " * ind
        out.append(pad + '<node oor:name="org.montecalc.img.%s" oor:op="replace">\n' % cmd +
                   pad + "  " + prop("URL", url) + "\n" +
                   pad + '  <node oor:name="UserDefinedImages">\n' +
                   pad + "    " + prop("ImageSmallURL", "%origin%/../icons/" + icon + "_16.png") + "\n" +
                   pad + "    " + prop("ImageBigURL", "%origin%/../icons/" + icon + "_26.png") + "\n" +
                   pad + "  </node>\n" + pad + "</node>\n")
    return "".join(out)


HEADER = '<?xml version="1.0" encoding="UTF-8"?>\n<!-- generated by tools/gen_ui.py - do not edit -->\n'


def addons():
    return (HEADER +
            '<oor:component-data xmlns:oor="http://openoffice.org/2001/registry" xmlns:xs="http://www.w3.org/2001/XMLSchema"\n'
            '                    oor:name="Addons" oor:package="org.openoffice.Office">\n'
            '  <node oor:name="AddonUI">\n'
            '    <node oor:name="OfficeMenuBar">\n' + menu() + '    </node>\n'
            '    <node oor:name="OfficeToolBar">\n'
            '      <node oor:name="org.montecalc.toolbar" oor:op="replace">\n' + toolbar_items(8, "t") + '      </node>\n'
            '    </node>\n'
            '    <node oor:name="OfficeNotebookBar">\n'
            '      <node oor:name="org.montecalc.notebookbar" oor:op="replace">\n' + toolbar_items(8, "n", True) + '      </node>\n'
            '    </node>\n'
            '    <node oor:name="Images">\n' + images(6) + '    </node>\n'
            '  </node>\n'
            '</oor:component-data>\n')


def window_state():
    return (HEADER +
            '<oor:component-data xmlns:oor="http://openoffice.org/2001/registry" xmlns:xs="http://www.w3.org/2001/XMLSchema"\n'
            '                    oor:name="CalcWindowState" oor:package="org.openoffice.Office.UI">\n'
            '  <node oor:name="UIElements">\n    <node oor:name="States">\n'
            '      <node oor:name="private:resource/toolbar/addon_org.montecalc.toolbar" oor:op="replace">\n'
            '        ' + prop("UIName", "MC FLO") + "\n"
            '        ' + prop("Docked", "true", "xs:boolean") + "\n"
            '        ' + prop("Visible", "true", "xs:boolean") + "\n"
            '        ' + prop("DockingArea", "0", "xs:int") + "\n"
            '        ' + prop("DockPos", "0,2", "xs:string") + "\n"
            '        ' + prop("Style", "2", "xs:int") + "\n"
            '      </node>\n    </node>\n  </node>\n</oor:component-data>\n')


# sidebar panels: (id, title, factory group)
PANELS = [
    ("Model", "Model specification"), ("Information", "Information"), ("Simulation", "Simulation"),
    ("Statistics", "Statistics"), ("Correlations", "Correlations and sensitivities"),
    ("Iteration", "Individual analysis of iteration"), ("Results", "Results"), ("Other", "Other"),
]


def sidebar():
    ctx = "Calc, any, visible ;"
    deck = ('      <node oor:name="MonteCalcDeck" oor:op="replace">\n'
            '        ' + prop("Title", "MC FLO", localized=True) + "\n"
            '        ' + prop("Id", "MonteCalcDeck") + "\n"
            '        ' + prop("IconURL", "vnd.sun.star.extension://org.montecalc.extension/icons/logo_26.png") + "\n"
            '        ' + prop("HighContrastIconURL", "vnd.sun.star.extension://org.montecalc.extension/icons/logo_26.png") + "\n"
            '        <prop oor:name="ContextList"><value oor:separator=";">%s</value></prop>\n' % ctx +
            '        ' + prop("OrderIndex", "50", "xs:int") + "\n"
            '      </node>\n')
    panels = []
    for i, (pid, title) in enumerate(PANELS):
        panels.append(
            '      <node oor:name="MonteCalcPanel%s" oor:op="replace">\n' % pid +
            '        ' + prop("Title", title, localized=True) + "\n"
            '        ' + prop("TitleBarIsOptional", "false", "xs:boolean") + "\n"
            '        ' + prop("Id", "MonteCalcPanel" + pid) + "\n"
            '        ' + prop("DeckId", "MonteCalcDeck") + "\n"
            '        <prop oor:name="ContextList"><value oor:separator=";">%s</value></prop>\n' % ctx +
            '        ' + prop("ImplementationURL", "private:resource/toolpanel/MonteCalcPanelFactory/" + pid) + "\n"
            '        ' + prop("OrderIndex", str(10 * (i + 1)), "xs:int") + "\n"
            '        ' + prop("WantsCanvas", "false", "xs:boolean") + "\n"
            '        ' + prop("WantsAWT", "true", "xs:boolean") + "\n"
            '        ' + prop("DefaultMenuCommand", "", "xs:string") + "\n"
            '      </node>\n')
    return (HEADER +
            '<oor:component-data xmlns:oor="http://openoffice.org/2001/registry" xmlns:xs="http://www.w3.org/2001/XMLSchema"\n'
            '                    oor:name="Sidebar" oor:package="org.openoffice.Office.UI">\n'
            '  <node oor:name="Content">\n'
            '    <node oor:name="DeckList">\n' + deck + '    </node>\n'
            '    <node oor:name="PanelList">\n' + "".join(panels) + '    </node>\n'
            '  </node>\n</oor:component-data>\n')


def factories():
    return (HEADER +
            '<oor:component-data xmlns:oor="http://openoffice.org/2001/registry" xmlns:xs="http://www.w3.org/2001/XMLSchema"\n'
            '                    oor:name="Factories" oor:package="org.openoffice.Office.UI">\n'
            '  <node oor:name="Registered">\n    <node oor:name="UIElementFactories">\n'
            '      <node oor:name="org.montecalc.PanelFactory" oor:op="replace">\n'
            '        ' + prop("Type", "toolpanel") + "\n"
            '        ' + prop("Name", "MonteCalcPanelFactory") + "\n"
            '        ' + prop("Module", "") + "\n"
            '        ' + prop("FactoryImplementation", "org.montecalc.PanelFactory") + "\n"
            '      </node>\n    </node>\n  </node>\n</oor:component-data>\n')


def write(rel, text):
    path = os.path.join(EXT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", os.path.relpath(path, ROOT))


def main():
    write("registry/Addons.xcu", addons())
    write("registry/CalcWindowState.xcu", window_state())
    write("registry/Sidebar.xcu", sidebar())
    write("registry/Factories.xcu", factories())
    check_function_menu()
    with open(os.path.join(ROOT, "uno", "function_menu.inc"), "w", encoding="utf-8", newline="\n") as f:
        f.write(function_menu_inc())
    print("wrote uno/function_menu.inc")
    with open(os.path.join(ROOT, "uno", "i18n_catalog.inc"), "w", encoding="utf-8", newline="\n") as f:
        f.write(catalog_inc())
    print("wrote uno/i18n_catalog.inc")
    if MISSING:
        print("not translated (menu/toolbar/sidebar):", "; ".join(sorted(MISSING)))
    import gen_icons
    icons = sorted(({c[3] for _, _, cmds in RIBBON for c in cmds} | set(FUNCTION_ICONS.values())) - {"logo"})
    gen_icons.write_all(os.path.join(EXT, "icons"), icons)
    gen_icons.write_logo(os.path.join(EXT, "icons"))   # MC FLO logo: About, sidebar deck, Extension Manager
    gen_icons.write_distributions(os.path.join(EXT, "icons"), [nm for names in flo_lists() for nm in names])
    gen_icons.write_bmp(os.path.join(EXT, "icons"), sorted(set(GROUP_ICONS.values()) | {p[2] for p in PICKERS} |
                                                     set(FUNCTION_ICONS.values())))


if __name__ == "__main__":
    main()
