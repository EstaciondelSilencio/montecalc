// MonteCalc - simulation engine
#include "engine.hpp"
#include "report.hpp"
#include "ui.hpp"
#include "util.hpp"

#include "../core/stats.h"

#include <com/sun/star/awt/XToolkitExperimental.hpp>
#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/XPropertyContainer.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/frame/DispatchDescriptor.hpp>
#include <com/sun/star/frame/FeatureStateEvent.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XStatusListener.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/sheet/XSpreadsheetView.hpp>
#include <com/sun/star/sheet/XCalculatable.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/task/XStatusIndicator.hpp>
#include <com/sun/star/task/XStatusIndicatorFactory.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/util/URL.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <cppuhelper/supportsservice.hxx>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

namespace {

const char* kPrefix = "org.montecalc:";

// ---- settings stored as user-defined document properties ----
Reference<beans::XPropertyContainer> userProps(const Reference<frame::XModel>& m) {
    Reference<document::XDocumentPropertiesSupplier> s(m, UNO_QUERY);
    if (!s.is()) return {};
    return s->getDocumentProperties()->getUserDefinedProperties();
}

double readProp(const Reference<frame::XModel>& m, const char* name, double def) {
    auto c = userProps(m);
    Reference<beans::XPropertySet> ps(c, UNO_QUERY);
    if (!ps.is()) return def;
    OUString n = OUString::createFromAscii(name);
    if (!ps->getPropertySetInfo()->hasPropertyByName(n)) return def;
    double d = def;
    Any a = ps->getPropertyValue(n);
    if (!(a >>= d)) {
        OUString s;
        if (a >>= s) { bool ok; double v = parseNumber(u8(s), ok); if (ok) d = v; }
    }
    return d;
}

void writeProp(const Reference<frame::XModel>& m, const char* name, double v) {
    auto c = userProps(m);
    Reference<beans::XPropertySet> ps(c, UNO_QUERY);
    if (!ps.is()) return;
    OUString n = OUString::createFromAscii(name);
    if (ps->getPropertySetInfo()->hasPropertyByName(n))
        ps->setPropertyValue(n, Any(v));
    else
        c->addProperty(n, beans::PropertyAttribute::REMOVABLE, Any(v));
}

struct RunOptions {
    mc::Settings s;
    bool visualize = false;
    bool report = true;
};

RunOptions loadOptions(const Reference<frame::XModel>& m) {
    RunOptions o;
    o.s.iterations = int(readProp(m, "MonteCalc.Iterations", 1000));
    o.s.seed = (long long)readProp(m, "MonteCalc.Seed", 0);
    o.s.worst = readProp(m, "MonteCalc.Worst", 0.05);
    o.s.real = readProp(m, "MonteCalc.Real", 0.5);
    o.s.best = readProp(m, "MonteCalc.Best", 0.95);
    o.s.bins = int(readProp(m, "MonteCalc.Bins", 20));
    o.visualize = readProp(m, "MonteCalc.Visualize", 0) != 0;
    o.report = readProp(m, "MonteCalc.Report", 1) != 0;
    return o;
}

void saveOptions(const Reference<frame::XModel>& m, const RunOptions& o) {
    writeProp(m, "MonteCalc.Iterations", o.s.iterations);
    writeProp(m, "MonteCalc.Seed", double(o.s.seed));
    writeProp(m, "MonteCalc.Worst", o.s.worst);
    writeProp(m, "MonteCalc.Real", o.s.real);
    writeProp(m, "MonteCalc.Best", o.s.best);
    writeProp(m, "MonteCalc.Bins", o.s.bins);
    writeProp(m, "MonteCalc.Visualize", o.visualize ? 1 : 0);
    writeProp(m, "MonteCalc.Report", o.report ? 1 : 0);
}

std::string fmt(double v) {
    char b[64];
    if (std::fabs(v) >= 1e5 && std::fabs(v) < 1e15) {
        std::snprintf(b, sizeof b, "%.0f", v);
        std::string s = b, out;
        size_t start = s[0] == '-' ? 1 : 0;
        for (size_t i = 0; i < s.size(); ++i) {
            out += s[i];
            size_t rest = s.size() - i - 1;
            if (i >= start && rest > 0 && rest % 3 == 0) out += ',';
        }
        return out;
    }
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

bool argValue(const Sequence<beans::PropertyValue>& args, const char* name, Any& out) {
    for (const auto& p : args)
        if (p.Name.equalsAscii(name)) { out = p.Value; return true; }
    return false;
}

double argNum(const Sequence<beans::PropertyValue>& args, const char* name, double def) {
    Any a;
    if (!argValue(args, name, a)) return def;
    double d;
    if (a >>= d) return d;
    sal_Int32 i;
    if (a >>= i) return i;
    bool b;
    if (a >>= b) return b ? 1 : 0;
    return def;
}

bool validateOptions(const RunOptions& o, std::string& err) {
    auto q = [](double v) { return v >= 0 && v <= 1; };
    if (o.s.iterations < 1 || o.s.iterations > 100000000) err = "Iterations must be between 1 and 100,000,000.";
    else if (!q(o.s.worst) || !q(o.s.real) || !q(o.s.best)) err = "Scenario quantiles must be between 0 and 1.";
    else if (o.s.bins < 2 || o.s.bins > 1000) err = "Histogram bins must be between 2 and 1000.";
    return err.empty();
}

// Variable name referenced by the MonteCalc formula in a cell (first argument).
std::string variableNameInCell(const Reference<sheet::XSpreadsheet>& sheet, const Reference<table::XCell>& cell) {
    std::string f = u8(cell->getFormula());
    size_t p = f.find('(');
    if (f.empty() || f[0] != '=' || p == std::string::npos) return {};
    size_t i = p + 1;
    while (i < f.size() && f[i] == ' ') ++i;
    if (i < f.size() && f[i] == '"') {
        size_t e = f.find('"', i + 1);
        return e == std::string::npos ? std::string() : f.substr(i + 1, e - i - 1);
    }
    size_t e = f.find_first_of(";,)", i);
    std::string ref = f.substr(i, e == std::string::npos ? std::string::npos : e - i);
    std::string clean;
    for (char c : ref) if (c != '$' && c != ' ') clean += c;
    try {
        auto r = sheet->getCellRangeByName(ou(clean));
        return u8(Reference<text::XText>(r->getCellByPosition(0, 0), UNO_QUERY_THROW)->getString());
    } catch (...) {
        return {};
    }
}

} // namespace

// ================================ UNO plumbing ============================

void Engine::initialize(const Sequence<Any>& args) {
    if (args.getLength() > 0) args[0] >>= frame_;
}

Reference<frame::XDispatch> Engine::queryDispatch(const util::URL& url, const OUString&, sal_Int32) {
    if (url.Protocol.equalsAscii(kPrefix)) return this;
    return {};
}

Sequence<Reference<frame::XDispatch>> Engine::queryDispatches(const Sequence<frame::DispatchDescriptor>& req) {
    Sequence<Reference<frame::XDispatch>> r(req.getLength());
    for (sal_Int32 i = 0; i < req.getLength(); ++i)
        r.getArray()[i] = queryDispatch(req[i].FeatureURL, req[i].FrameName, req[i].SearchFlags);
    return r;
}

void Engine::addStatusListener(const Reference<frame::XStatusListener>& l, const util::URL& url) {
    if (!l.is()) return;
    frame::FeatureStateEvent ev;
    ev.FeatureURL = url;
    ev.IsEnabled = true;
    ev.Source = static_cast<cppu::OWeakObject*>(this);
    l->statusChanged(ev);
}

OUString Engine::getImplementationName() { return OUString::createFromAscii(kEngineImplName); }
sal_Bool Engine::supportsService(const OUString& s) { return cppu::supportsService(this, s); }
Sequence<OUString> Engine::getSupportedServiceNames() { return serviceNames(); }
Sequence<OUString> Engine::serviceNames() { return seq<OUString>({OUString::createFromAscii(kEngineServiceName)}); }

Reference<frame::XModel> Engine::model() {
    if (!frame_.is() || !frame_->getController().is()) return {};
    return frame_->getController()->getModel();
}

void Engine::recalc() {
    Reference<sheet::XCalculatable> c(model(), UNO_QUERY);
    if (c.is()) c->calculateAll();
}

void Engine::message(const std::string& title, const std::string& text, bool error) {
    if (silent_) {
        std::fprintf(stderr, "[MonteCalc] %s: %s\n", title.c_str(), text.c_str());
        return;
    }
    showMessage(ctx_, frame_, title, text, error);
}

void Engine::dispatch(const util::URL& url, const Sequence<beans::PropertyValue>& args) {
    silent_ = argNum(args, "Silent", 0) != 0;
    std::string cmd = u8(url.Path);
    try {
        Reference<sheet::XSpreadsheetDocument> doc(model(), UNO_QUERY);
        if (!doc.is() && cmd != "About") {
            message("MonteCalc", "MonteCalc works with spreadsheet documents only.", true);
            return;
        }
        if (cmd == "Run") cmdRun(args);
        else if (cmd == "Settings") cmdSettings();
        else if (cmd == "Variables") cmdVariables();
        else if (cmd == "Report") cmdReport();
        else if (cmd == "Replay") cmdReplay(args);
        else if (cmd == "Expected") cmdExpected();
        else if (cmd == "Preview") cmdPreview();
        else if (cmd == "Lock") cmdLock();
        else if (cmd == "Reset") cmdReset();
        else if (cmd == "About") cmdAbout();
        else message("MonteCalc", "Unknown command: " + cmd, true);
    } catch (const uno::Exception& e) {
        message("MonteCalc", "Error: " + u8(e.Message), true);
    } catch (const std::exception& e) {
        message("MonteCalc", std::string("Error: ") + e.what(), true);
    }
}

// ================================ commands =================================

void Engine::cmdSettings() {
    auto m = model();
    RunOptions o = loadOptions(m);
    std::vector<FormField> f = {
        {"Number of iterations", std::to_string(o.s.iterations)},
        {"Random seed (0 = random)", std::to_string(o.s.seed)},
        {"Worst case quantile", fmt(o.s.worst)},
        {"Real case quantile", fmt(o.s.real)},
        {"Best case quantile", fmt(o.s.best)},
        {"Histogram bins", std::to_string(o.s.bins)},
        {"Visualize simulation (slow)", "", true, o.visualize},
        {"Create result workbook after the run", "", true, o.report},
    };
    if (!showForm(ctx_, "MonteCalc - Simulation settings", f)) return;
    bool ok = true;
    auto num = [&](const FormField& x) { bool k; double v = parseNumber(x.value, k); ok = ok && k; return v; };
    RunOptions n;
    n.s.iterations = int(num(f[0]));
    n.s.seed = (long long)num(f[1]);
    n.s.worst = num(f[2]);
    n.s.real = num(f[3]);
    n.s.best = num(f[4]);
    n.s.bins = int(num(f[5]));
    n.visualize = f[6].checked;
    n.report = f[7].checked;
    std::string err;
    if (!ok) err = "Please enter numbers only.";
    if (!err.empty() || !validateOptions(n, err)) {
        message("MonteCalc", err, true);
        return;
    }
    saveOptions(m, n);
    mc::Simulation::instance().setSettings(n.s);
}

void Engine::cmdRun(const Sequence<beans::PropertyValue>& args) {
    auto m = model();
    auto& sim = mc::Simulation::instance();
    RunOptions o = loadOptions(m);
    // Dispatch arguments override the stored settings (used by macros and tests).
    o.s.iterations = int(argNum(args, "Iterations", o.s.iterations));
    o.s.seed = (long long)argNum(args, "Seed", double(o.s.seed));
    o.visualize = argNum(args, "Visualize", o.visualize) != 0;
    o.report = argNum(args, "Report", o.report) != 0;
    std::string err;
    if (!validateOptions(o, err)) { message("MonteCalc", err, true); return; }

    Reference<sheet::XCalculatable> calc(m, UNO_QUERY_THROW);
    // 1. registration pass
    sim.beginRegistration();
    try {
        calc->calculateAll();
    } catch (...) {
        sim.setMode(mc::Mode::Expected);
        throw;
    }
    int nin = 0, nout = 0;
    for (const auto& v : sim.variables()) (v.kind == mc::VarKind::Input ? nin : nout)++;
    if (nout == 0) {
        sim.setMode(mc::Mode::Expected);
        calc->calculateAll();
        message("MonteCalc", "No output variable found. Mark at least one result with MC_OUTPUT(\"Name\"; Value).", true);
        return;
    }
    // 2. prepare
    std::vector<std::string> warnings;
    try {
        warnings = sim.prepare(o.s);
    } catch (...) {
        sim.setMode(mc::Mode::Expected);
        calc->calculateAll();
        throw;
    }

    // 3. iterations
    Reference<task::XStatusIndicator> ind;
    Reference<task::XStatusIndicatorFactory> sif(frame_, UNO_QUERY);
    if (sif.is()) ind = sif->createStatusIndicator();
    if (ind.is()) ind->start("MonteCalc: simulating...", o.s.iterations);
    Reference<awt::XToolkitExperimental> tk;
    if (o.visualize) tk.set(awt::Toolkit::create(ctx_), UNO_QUERY);
    if (!o.visualize) m->lockControllers();
    auto t0 = std::chrono::steady_clock::now();
    int step = std::max(1, o.s.iterations / 200);
    try {
        for (int i = 0; i < o.s.iterations; ++i) {
            sim.setIteration(i);
            calc->calculateAll();
            if (o.visualize && tk.is()) tk->processEventsToIdle();
            if (ind.is() && i % step == 0) ind->setValue(i);
        }
    } catch (...) {
        sim.abort();
        if (!o.visualize) m->unlockControllers();
        if (ind.is()) ind->end();
        calc->calculateAll();
        throw;
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sim.finish();
    if (!o.visualize) m->unlockControllers();
    calc->calculateAll();  // back to expected values, refresh MCR_ result functions
    if (ind.is()) ind->end();

    // 4. report + summary
    std::string note;
    if (o.report) note = createReport(ctx_, silent_);
    std::ostringstream os;
    os << "Simulation finished: " << o.s.iterations << " iterations, " << nin << " inputs, " << nout
       << " outputs in " << fmt(secs) << " s.\n\n";
    for (const auto& name : sim.resultNames(mc::VarKind::Output)) {
        std::vector<double> v;
        sim.samples(name, v);
        os << name << ": mean " << fmt(mc::stats::mean(v)) << ", std dev " << fmt(mc::stats::stdev(v))
           << ", worst " << fmt(mc::stats::percentile(v, o.s.worst)) << ", real "
           << fmt(mc::stats::percentile(v, o.s.real)) << ", best " << fmt(mc::stats::percentile(v, o.s.best))
           << "\n";
    }
    if (!warnings.empty()) {
        os << "\nWarnings:\n";
        for (auto& w : warnings) os << " - " << w << "\n";
    }
    if (!note.empty()) os << "\n" << note << "\n";
    message("MonteCalc", os.str());
}

void Engine::cmdVariables() {
    auto& sim = mc::Simulation::instance();
    mc::Mode prev = sim.mode();
    sim.beginRegistration();
    recalc();
    auto vars = sim.variables();
    auto corr = sim.correlations();
    auto dups = sim.duplicateNames();
    sim.setMode(prev == mc::Mode::Register || prev == mc::Mode::Simulate ? mc::Mode::Expected : prev);
    recalc();
    std::ostringstream os;
    os << "Inputs:\n";
    for (auto& v : vars)
        if (v.kind == mc::VarKind::Input)
            os << "  " << v.name << "  =  " << v.description << (sim.isLocked(v.name) ? "  [locked]" : "") << "\n";
    os << "\nCorrelations:\n";
    if (corr.empty()) os << "  (none)\n";
    const char* types[] = {"Gaussian", "Student-t", "Clayton", "rotated Clayton", "Gumbel", "rotated Gumbel"};
    for (auto& c : corr) os << "  " << c.a << " ~ " << c.b << ": " << fmt(c.rho) << " (" << types[c.type] << ")\n";
    os << "\nOutputs:\n";
    for (auto& v : vars)
        if (v.kind == mc::VarKind::Output) os << "  " << v.name << "\n";
    if (!dups.empty()) {
        os << "\nNames used in more than one cell:\n";
        for (auto& d : dups) os << "  " << d << "\n";
    }
    message("MonteCalc - Variables", os.str());
}

void Engine::cmdReport() {
    std::string note = createReport(ctx_, silent_);
    if (!note.empty()) message("MonteCalc", note);
}

void Engine::cmdReplay(const Sequence<beans::PropertyValue>& args) {
    auto& sim = mc::Simulation::instance();
    if (!sim.hasResults()) { message("MonteCalc", "Run a simulation first.", true); return; }
    int it = int(argNum(args, "Iteration", 0));
    if (it == 0) {
        std::vector<FormField> f = {{"Iteration (1 - " + std::to_string(sim.iterations()) + ")", "1"}};
        if (!showForm(ctx_, "MonteCalc - Analyse iteration", f)) return;
        bool ok;
        it = int(parseNumber(f[0].value, ok));
        if (!ok) { message("MonteCalc", "Please enter a number.", true); return; }
    }
    if (it < 1 || it > sim.iterations()) {
        message("MonteCalc", "Iteration must be between 1 and " + std::to_string(sim.iterations()) + ".", true);
        return;
    }
    sim.setReplayIteration(it - 1);
    recalc();
}

void Engine::cmdExpected() {
    mc::Simulation::instance().setMode(mc::Mode::Expected);
    recalc();
}

void Engine::cmdPreview() {
    auto& sim = mc::Simulation::instance();
    sim.setMode(sim.mode() == mc::Mode::Random ? mc::Mode::Expected : mc::Mode::Random);
    recalc();
}

void Engine::cmdLock() {
    auto m = model();
    Reference<frame::XController> ctrl = m->getCurrentController();
    Reference<sheet::XSpreadsheetView> view(ctrl, UNO_QUERY_THROW);
    Reference<sheet::XSpreadsheet> sheet = view->getActiveSheet();
    Reference<view::XSelectionSupplier> sel(ctrl, UNO_QUERY_THROW);
    Reference<sheet::XCellRangeAddressable> ra(sel->getSelection(), UNO_QUERY);
    if (!ra.is()) { message("MonteCalc", "Select the cell of an input variable.", true); return; }
    auto addr = ra->getRangeAddress();
    Reference<table::XCell> cell = sheet->getCellByPosition(addr.StartColumn, addr.StartRow);
    std::string name = variableNameInCell(sheet, cell);
    if (name.empty()) { message("MonteCalc", "The selected cell does not contain a MonteCalc input.", true); return; }
    auto& sim = mc::Simulation::instance();
    sim.toggleLock(name);
    Reference<beans::XPropertySet> cp(cell, UNO_QUERY_THROW);
    cp->setPropertyValue("CellBackColor", Any(sal_Int32(sim.isLocked(name) ? 0xFFC7CE : -1)));
    message("MonteCalc", "Variable '" + name + "' is now " +
                             (sim.isLocked(name) ? "locked to its expected value." : "unlocked."));
}

void Engine::cmdReset() {
    auto& sim = mc::Simulation::instance();
    sim.reset();
    sim.setMode(mc::Mode::Expected);
    recalc();
}

void Engine::cmdAbout() {
    message("About MonteCalc",
            "MonteCalc - Monte Carlo simulation for LibreOffice Calc.\n\n"
            "1. Model uncertain inputs with MC_ functions, e.g. =MC_TRIANGULAR(\"Volume\"; 80; 100; 140).\n"
            "2. Optionally correlate inputs with MC_CORRELATE.\n"
            "3. Mark results with MC_OUTPUT(\"Name\"; cell).\n"
            "4. Run the simulation and analyse the result workbook or use the MCR_ functions.");
}

} // namespace montecalc
