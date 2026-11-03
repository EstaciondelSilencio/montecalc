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
        ("Show", "Model (show variables)...", "Model", "model", True),
        ("InfluenceDiagram", "Influence diagram...", "Influence diagram", "influence", True),
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
        ("Variables", "Variables (text list)", "Variables", "variables", False),
        ("About", "About MonteCalc", "About", "about", False),
    ]),
]


def flo_lists():
    """(continuous, discrete, time series) labels in the order of core/flofuncs.cpp"""
    import re
    src = open(os.path.join(ROOT, "core", "flofuncs.cpp"), encoding="utf-8").read()
    cont, disc, ts = [], [], []
    for kind, label, arg in re.findall(r'\b(dist|ts)\("(\w+)",\s*(\w+)', src):
        (ts if kind == "ts" else disc if arg == "true" else cont).append(label)
    if len(cont) < 10 or len(disc) < 5 or len(ts) < 4:
        sys.exit("gen_ui.py: could not read the distribution list from core/flofuncs.cpp")
    return cont + ["UserDefined"], disc + ["DiscreteUserDefined"], ts


# ---------------------------------------------------------------------------- xml helpers
def prop(name, value, typ="xs:string", localized=False):
    lang = ' xml:lang="en-US"' if localized else ""
    return '<prop oor:name="%s" oor:type="%s"><value%s>%s</value></prop>' % (name, typ, lang, escape(str(value)))


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

    def sub(title, children, ind=10):
        nonlocal n
        n += 1
        pad = " " * ind
        s = pad + '<node oor:name="g%02d" oor:op="replace">\n' % n
        s += pad + "  " + prop("Title", title, localized=True) + "\n"
        s += pad + "  " + prop("Context", CTX) + "\n"
        s += pad + '  <node oor:name="Submenu">\n' + "".join(children) + pad + "  </node>\n" + pad + "</node>\n"
        return s

    for gi, (group, _, cmds) in enumerate(RIBBON):
        children = []
        if gi == 0:
            for title, names in (("Choose continuous variable", cont), ("Choose discrete variable", disc), ("Choose time series", ts)):
                kids = [item("f%02d" % i, "org.montecalc:Wizard?Function=" + nm, nm, 16) for i, nm in enumerate(names)]
                children.append(sub(title, kids, 14))
        for i, (cmd, title, _, _, _) in enumerate(cmds):
            children.append(item("m%d%02d" % (gi, i), "org.montecalc:" + cmd, title, 14))
        out.append(sub(group, children))
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
            # Notebookbar items show either text or an icon ("Text" / "Icon"); text reads like the ribbon
            extra = (" " * (ind + 2) + prop("Style", "Text") + "\n") if notebook else ""
            out.append(item("%s%02d" % (prefix, k), "org.montecalc:" + cmd, short, ind, extra))
            k += 1
    return "".join(out)


def images(ind):
    out = []
    for _, _, cmds in RIBBON:
        for cmd, _, _, icon, _ in cmds:
            pad = " " * ind
            out.append(pad + '<node oor:name="org.montecalc.img.%s" oor:op="replace">\n' % cmd +
                       pad + "  " + prop("URL", "org.montecalc:" + cmd) + "\n" +
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
            '        ' + prop("IconURL", "vnd.sun.star.extension://org.montecalc.extension/icons/deck_26.png") + "\n"
            '        ' + prop("HighContrastIconURL", "vnd.sun.star.extension://org.montecalc.extension/icons/deck_26.png") + "\n"
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
    import gen_icons
    icons = sorted({c[3] for _, _, cmds in RIBBON for c in cmds} | {"deck"})
    gen_icons.write_all(os.path.join(EXT, "icons"), icons)


if __name__ == "__main__":
    main()
