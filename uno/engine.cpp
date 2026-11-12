// MonteCalc - simulation engine and MC FLO commands
#include "engine.hpp"
#include "i18n.hpp"
#include "model.hpp"
#include "report.hpp"
#include "ui.hpp"
#include "util.hpp"
#include "windows.hpp"
#ifdef MONTECALC_XLL
#include "../xll/com.hpp"
#endif

#include "../core/fitting.h"
#include "../core/flofuncs.h"
#include "../core/formula.h"
#include "../core/stats.h"

#ifndef MONTECALC_XLL
#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/awt/XToolkitExperimental.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/XPropertyContainer.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/DispatchDescriptor.hpp>
#include <com/sun/star/frame/FeatureStateEvent.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XStatusListener.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/awt/PopupMenu.hpp>
#include <com/sun/star/awt/PopupMenuDirection.hpp>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/awt/XPopupMenu.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/frame/DispatchHelper.hpp>
#include <com/sun/star/frame/XDispatchHelper.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/sheet/CellFlags.hpp>
#include <com/sun/star/sheet/XCalculatable.hpp>
#include <com/sun/star/sheet/XCellAddressable.hpp>
#include <com/sun/star/sheet/XCellRangeAddressable.hpp>
#include <com/sun/star/sheet/XCellRangeData.hpp>
#include <com/sun/star/sheet/XCellRangesQuery.hpp>
#include <com/sun/star/sheet/XSheetAnnotationsSupplier.hpp>
#include <com/sun/star/sheet/XSheetCellRanges.hpp>
#include <com/sun/star/sheet/XSpreadsheets.hpp>
#include <com/sun/star/table/CellContentType.hpp>
#include <com/sun/star/task/XStatusIndicator.hpp>
#include <com/sun/star/task/XStatusIndicatorFactory.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/util/URL.hpp>
#include <com/sun/star/util/XCloseable.hpp>
#include <com/sun/star/container/XNamed.hpp>
#include <com/sun/star/sheet/XNamedRanges.hpp>
#include <com/sun/star/table/CellAddress.hpp>
#include <com/sun/star/container/XEnumeration.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <cppuhelper/supportsservice.hxx>
#else
#include "../xll/uno_shim.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <sstream>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
#ifndef MONTECALC_XLL
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;
#endif

namespace montecalc {

namespace {

const char* kPrefix = "org.montecalc:";

// ---------------------------------------------------------------- document properties
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

std::string argText(const Sequence<beans::PropertyValue>& args, const char* name) {
    Any a;
    OUString s;
    if (argValue(args, name, a)) a >>= s;
    return u8(s);
}

bool validateOptions(const RunOptions& o, std::string& err) {
    auto q = [](double v) { return v >= 0 && v <= 1; };
    if (o.s.iterations < 1 || o.s.iterations > 100000000) err = "# iterations must be between 1 and 100,000,000.";
    else if (!q(o.s.worst) || !q(o.s.real) || !q(o.s.best)) err = "Scenario levels must be between 0 and 1.";
    else if (o.s.bins < 2 || o.s.bins > 1000) err = "Histogram bins must be between 2 and 1000.";
    return err.empty();
}

// ---------------------------------------------------------------- variants (multiple simulation)

bool parseRange(const ModelDoc& d, const std::string& text, table::CellRangeAddress& r) {
    std::string t;
    for (char c : text) if (c != '$' && c != ' ') t += c;
    size_t colon = t.find(':');
    std::string a = t.substr(0, colon), b = colon == std::string::npos ? a : t.substr(colon + 1);
    table::CellAddress ca, cb;
    if (!d.parseAddress(a, ca)) return false;
    if (b.find('.') == std::string::npos && b.find('!') == std::string::npos) {
        int col, row;
        if (!mc::formula::parseCellName(b, col, row)) return false;
        cb = ca;
        cb.Column = col;
        cb.Row = row;
    } else if (!d.parseAddress(b, cb)) {
        return false;
    }
    r = table::CellRangeAddress(ca.Sheet, std::min(ca.Column, cb.Column), std::min(ca.Row, cb.Row),
                                std::max(ca.Column, cb.Column), std::max(ca.Row, cb.Row));
    return true;
}

std::string rangeText(const ModelDoc& d, const table::CellRangeAddress& r) {
    table::CellAddress a(r.Sheet, r.StartColumn, r.StartRow), b(r.Sheet, r.EndColumn, r.EndRow);
    return d.cellName(a, true) + ":" + d.cellName(b, false);
}

} // namespace

std::vector<Variant> loadVariants(const ModelDoc& d) {
    std::vector<Variant> v;
    std::string t = d.propertyText("MonteCalc.Variants");
    table::CellRangeAddress r;
    if (t.empty() || !parseRange(d, t, r) || r.EndColumn - r.StartColumn < 2) return v;
    for (sal_Int32 row = r.StartRow; row <= r.EndRow; ++row) {
        Variant x;
        auto c0 = d.cell(table::CellAddress(r.Sheet, r.StartColumn, row));
        auto c1 = d.cell(table::CellAddress(r.Sheet, r.StartColumn + 1, row));
        auto c2 = d.cell(table::CellAddress(r.Sheet, r.StartColumn + 2, row));
        x.label = u8(c0.text());
        x.value = c1.value();
        std::string ref = c2.type() == CellType::Formula ? u8(c2.formula())
                                                                           : u8(c2.text());
        if (!ref.empty() && ref[0] == '=') ref = ref.substr(1);
        if (x.label.empty() || !d.parseAddress(ref, x.ref)) continue;
        if (ref.find('.') == std::string::npos) x.ref.Sheet = r.Sheet;
        v.push_back(x);
    }
    return v;
}

// saved formulas of input cells while an iteration is shown ("Analyse selected iteration")
std::map<const void*, std::vector<std::pair<table::CellAddress, std::string>>>& replayStore() {
    static std::map<const void*, std::vector<std::pair<table::CellAddress, std::string>>> s;
    return s;
}

void restoreReplay(const ModelDoc& d) {
    auto& store = replayStore();
    auto it = store.find(d.key());
    if (it == store.end()) return;
    for (auto& [a, f] : it->second) d.cell(a).setFormula(f);
    store.erase(it);
}

namespace {

// span of the first FLOsimula call in a formula: [nameStart, openParen, closeParen]
bool callSpan(const std::string& f, size_t& open, size_t& close) {
    std::string u = f;
    for (auto& c : u) c = char(std::toupper((unsigned char)c));
    size_t p = u.find("FLOSIMULA_");
    if (p == std::string::npos) return false;
    open = u.find('(', p);
    if (open == std::string::npos) return false;
    int depth = 0;
    bool str = false;
    for (size_t k = open + 1; k < f.size(); ++k) {
        char c = f[k];
        if (c == '"') str = !str;
        if (str) continue;
        if (c == '(') ++depth;
        if (c == ')') {
            if (depth == 0) { close = k; return true; }
            --depth;
        }
    }
    return false;
}

std::string quote(const std::string& s) {
    std::string r = "\"";
    for (char c : s) r += c == '"' ? std::string("\"\"") : std::string(1, c);
    return r + "\"";
}

} // namespace

RunOptions loadOptions(const ModelDoc& d) {
    RunOptions o;
    auto readProp = [&](const char* n, double def) { return d.property(n, def); };
    o.s.iterations = int(readProp("MonteCalc.Iterations", 1000));
    o.s.seed = (long long)readProp("MonteCalc.Seed", 0);
    o.s.worst = readProp("MonteCalc.Worst", 0.05);
    o.s.real = readProp("MonteCalc.Real", 0.5);
    o.s.best = readProp("MonteCalc.Best", 0.95);
    o.s.bins = int(readProp("MonteCalc.Bins", 20));
    o.visualize = readProp("MonteCalc.Visualize", 0) != 0;
    o.report = readProp("MonteCalc.Report", 0) != 0;
    o.memory = readProp("MonteCalc.Memory", 0) != 0;
    o.dip = readProp("MonteCalc.DIP", 0) != 0;
    o.s.useCorrelations = readProp("MonteCalc.UseCorrelations", 1) != 0;
    return o;
}

void saveOptions(const ModelDoc& d, const RunOptions& o) {
    auto writeProp = [&](const char* n, const Any& v) { d.setProperty(n, v); };
    writeProp("MonteCalc.Iterations", Any(double(o.s.iterations)));
    writeProp("MonteCalc.Seed", Any(double(o.s.seed)));
    writeProp("MonteCalc.Worst", Any(o.s.worst));
    writeProp("MonteCalc.Real", Any(o.s.real));
    writeProp("MonteCalc.Best", Any(o.s.best));
    writeProp("MonteCalc.Bins", Any(double(o.s.bins)));
    writeProp("MonteCalc.Visualize", Any(o.visualize ? 1.0 : 0.0));
    writeProp("MonteCalc.Report", Any(o.report ? 1.0 : 0.0));
    writeProp("MonteCalc.Memory", Any(o.memory ? 1.0 : 0.0));
    writeProp("MonteCalc.DIP", Any(o.dip ? 1.0 : 0.0));
    writeProp("MonteCalc.UseCorrelations", Any(o.s.useCorrelations ? 1.0 : 0.0));
}

// ============================================================ simulation run

double cellValue(const Cell& c) {
    if (c.error() != 0) return std::nan("");
    if (c.type() == CellType::Text) return std::nan("");
    return c.value();
}

void storeDip(const ModelDoc& d0) {
    auto& sim = mc::Simulation::instance();
    auto outs = sim.resultNames(mc::VarKind::Output);
    ModelDoc d = d0;
    int sheetIdx = d.ensureSheet("FLO_Results", false);
    d.removeNamedRangesWithPrefix("FLO_r_");
    for (size_t j = 0; j < outs.size(); ++j) {
        std::vector<double> v;
        sim.samples(outs[j], v);
        if (v.empty() || v.size() > 1000000) continue;
        std::vector<std::vector<Any>> data(v.size() + 1);
        data[0] = {Any(ou(outs[j]))};
        for (size_t i = 0; i < v.size(); ++i) data[i + 1] = {std::isfinite(v[i]) ? Any(v[i]) : Any()};
        d.writeTable(sheetIdx, 0, int(j), data);
        // FLO_r_<name> refers to the values (without the header)
        d.setNamedArea("FLO_r_" + mc::formula::sanitizeName(outs[j]), sheetIdx, int(j), 1, int(v.size()));
    }
}

#ifndef MONTECALC_XLL
namespace {

struct Recorder {
    std::vector<VarCell> cells;   // inputs and outputs to record
    std::vector<Cell> xcells;
    std::vector<int> index;       // variable index in the simulation state
};

void runOne(const Reference<uno::XComponentContext>& ctx, const Reference<frame::XFrame>& frame, const ModelDoc& d,
            const RunOptions& o, const std::string& label, RunResult& rr, const Reference<task::XStatusIndicator>& ind) {
    auto& sim = mc::Simulation::instance();
    Reference<sheet::XCalculatable> calc(d.model(), UNO_QUERY_THROW);
    // 1. registration
    sim.beginRegistration();
    std::vector<VarCell> cells;
    try {
        calc->calculateAll();
        cells = d.scan();
    } catch (...) {
        sim.setMode(mc::Mode::Expected);
        throw;
    }
    Recorder rec;
    int nout = 0;
    for (const auto& c : cells) {
        if (c.kind == mc::VarKind::Correlation) continue;
        sim.setVariableInfo(c.name, c.kind, c.description, d.cellName(c.addr));
        rec.cells.push_back(c);
        rec.xcells.push_back(d.cell(c.addr));
        nout += c.kind == mc::VarKind::Output;
    }
    if (nout == 0) {
        sim.setMode(mc::Mode::Expected);
        calc->calculateAll();
        throw std::runtime_error("No output variable found. Mark a result with =<cell>+FLOsimula_output(\"Name\") (Insert output variable).");
    }
    // 2. prepare
    std::vector<std::string> warn;
    try {
        warn = sim.prepare(o.s);
        for (const auto& c : rec.cells) rec.index.push_back(sim.variableIndex(c.name));
    } catch (...) {
        sim.setMode(mc::Mode::Expected);
        calc->calculateAll();
        throw;
    }
    for (auto& w : warn) rr.warnings.push_back((label.empty() ? "" : label + ": ") + w);
    // memory calculation model
    std::unique_ptr<mc::formula::Model> mem;
    std::unique_ptr<mc::formula::Source> src;
    if (o.memory) {
        src = d.formulaSource();
        mem = std::make_unique<mc::formula::Model>();
        std::vector<mc::formula::CellAddr> roots;
        for (const auto& c : rec.cells) roots.push_back({c.addr.Sheet, c.addr.Column, c.addr.Row});
        std::string why;
        if (!mem->build(*src, roots, why)) {
            rr.warnings.push_back("Memory calculation not possible (" + why + "); the spreadsheet was recalculated instead.");
            mem.reset();
        }
    }
    rr.memoryUsed = mem != nullptr;
    // 3. iterations
    Reference<awt::XToolkitExperimental> tk;
    if (o.visualize && !mem) tk.set(awt::Toolkit::create(ctx), UNO_QUERY);
    bool locked = !o.visualize && !mem;
    if (locked) d.model()->lockControllers();
    int step = std::max(1, o.s.iterations / 200);
    try {
        for (int i = 0; i < o.s.iterations; ++i) {
            sim.setIteration(i);
            if (mem) {
                mem->beginIteration();
                for (size_t k = 0; k < rec.cells.size(); ++k) {
                    const auto& a = rec.cells[k].addr;
                    mc::Value v = mem->value({a.Sheet, a.Column, a.Row});
                    sim.recordAt(rec.index[k], v.kind == mc::Value::Number || v.kind == mc::Value::Bool ? v.num : std::nan(""));
                }
            } else {
                calc->calculateAll();
                for (size_t k = 0; k < rec.cells.size(); ++k) sim.recordAt(rec.index[k], cellValue(rec.xcells[k]));
                if (tk.is()) tk->processEventsToIdle();
            }
            if (ind.is() && i % step == 0) ind->setValue(i);
        }
    } catch (...) {
        sim.abort();
        if (locked) d.model()->unlockControllers();
        calc->calculateAll();
        throw;
    }
    sim.finish(label);
    if (locked) d.model()->unlockControllers();
    (void)frame;
}

} // namespace

RunResult runSimulation(const Reference<uno::XComponentContext>& ctx, const Reference<frame::XFrame>& frame, const RunOptions& o) {
    RunResult rr;
    ModelDoc d(frame->getController()->getModel());
    if (!d.valid()) throw std::runtime_error("MonteCalc works with spreadsheet documents only.");
    restoreReplay(d);
    auto& sim = mc::Simulation::instance();
    Reference<sheet::XCalculatable> calc(d.model(), UNO_QUERY_THROW);
    auto variants = loadVariants(d);
    Reference<task::XStatusIndicator> ind;
    Reference<task::XStatusIndicatorFactory> sif(frame, UNO_QUERY);
    if (sif.is()) ind = sif->createStatusIndicator();
    if (ind.is()) ind->start("MonteCalc: simulating...", o.s.iterations);
    auto t0 = std::chrono::steady_clock::now();
    sim.clearResultSets();
    try {
        if (variants.empty()) {
            runOne(ctx, frame, d, o, "", rr, ind);
        } else {
            // remember the reference cells and restore them afterwards
            std::map<std::string, std::pair<table::CellAddress, std::string>> saved;
            for (auto& v : variants) {
                std::string k = d.cellName(v.ref);
                if (!saved.count(k)) saved[k] = {v.ref, d.cell(v.ref).formula()};
            }
            try {
                for (auto& v : variants) {
                    d.cell(v.ref).setValue(v.value);
                    runOne(ctx, frame, d, o, v.label, rr, ind);
                }
            } catch (...) {
                for (auto& [k, s] : saved) d.cell(s.first).setFormula(s.second);
                throw;
            }
            for (auto& [k, s] : saved) d.cell(s.first).setFormula(s.second);
            sim.selectResultSet(0);
        }
    } catch (...) {
        if (ind.is()) ind->end();
        throw;
    }
    rr.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sim.setMode(o.s.seed == -1 ? mc::Mode::Random : mc::Mode::Expected);
    calc->calculateAll();  // expected values again, refresh the fMC_ result functions
    if (ind.is()) ind->end();
    if (o.dip) storeDip(d);

    std::ostringstream os;
    auto labels = sim.resultSetLabels();
    os << "Simulation finished: " << o.s.iterations << " iterations" << (labels.size() > 1 ? " x " + std::to_string(labels.size()) + " variants" : "")
       << " in " << fmtNum(rr.seconds) << " s" << (rr.memoryUsed ? " (memory calculation)" : "") << "\n\n";
    int active = sim.activeResultSet();
    for (size_t vi = 0; vi < labels.size(); ++vi) {
        if (labels.size() > 1) os << "Variant " << labels[vi] << ":\n";
        sim.selectResultSet(int(vi));
        for (const auto& name : sim.resultNames(mc::VarKind::Output)) {
            std::vector<double> v;
            sim.samples(name, v);
            os << "  " << name << ": mean " << fmtNum(mc::stats::mean(v)) << ", std dev " << fmtNum(mc::stats::stdev(v))
               << ", worst " << fmtNum(mc::stats::percentile(v, o.s.worst)) << ", real " << fmtNum(mc::stats::percentile(v, o.s.real))
               << ", best " << fmtNum(mc::stats::percentile(v, o.s.best)) << "\n";
        }
    }
    if (active >= 0) sim.selectResultSet(active);
    if (!rr.warnings.empty()) {
        os << "\nWarnings:\n";
        for (auto& w : rr.warnings) os << " - " << w << "\n";
    }
    rr.summary = os.str();
    return rr;
}

#endif   // LibreOffice simulation driver (Excel: xll/xll.cpp)

// ============================================================ UNO plumbing

#ifndef MONTECALC_XLL

void Engine::initialize(const Sequence<Any>& args) {
    if (args.getLength() > 0) args[0] >>= frame_;
}

Reference<frame::XDispatch> Engine::queryDispatch(const util::URL& url, const OUString&, sal_Int32) {
    if (url.Protocol.equalsAscii(kPrefix)) return this;
    return {};
}

Sequence<Reference<frame::XDispatch>> Engine::queryDispatches(const Sequence<frame::DispatchDescriptor>& req) {
    Sequence<Reference<frame::XDispatch>> r(req.getLength());
    for (sal_Int32 i = 0; i < req.getLength(); ++i) r.getArray()[i] = queryDispatch(req[i].FeatureURL, req[i].FrameName, req[i].SearchFlags);
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
#else
Reference<frame::XModel> Engine::model() { return {}; }   // ModelDoc: the active workbook
#endif

void Engine::message(const std::string& title, const std::string& text, bool error) {
    if (silent_) {
        std::fprintf(stderr, "[MonteCalc] %s: %s\n", title.c_str(), text.c_str());
        return;
    }
    showMessage(ctx_, frame_, title, text, error);
}

bool Engine::ask(const std::string& title, std::vector<FormField>& fields) {
    if (silent_) {
        // non-interactive use: values come from the dispatch arguments named after the labels' first word
        for (auto& f : fields) {
            std::string key = f.label.substr(0, f.label.find(' '));
            Any a;
            if (!argValue(args_, key.c_str(), a)) continue;
            double d;
            OUString s;
            bool b;
            if (a >>= s) f.value = u8(s);
            else if (a >>= b) f.checked = b;
            else if (a >>= d) { f.value = fmtNum(d); f.checked = d != 0; char buf[64]; std::snprintf(buf, sizeof buf, "%.15g", d); f.value = buf; }
        }
        return true;
    }
    return showForm(ctx_, title, fields);
}

#ifndef MONTECALC_XLL
void Engine::dispatch(const util::URL& url, const Sequence<beans::PropertyValue>& args) {
    args_ = args;
    i18nSetContext(ctx_);
    std::string cmd = u8(url.Path);
    // menu entries pass arguments in the URL: org.montecalc:Wizard?Function=Normal&x=1
    std::string query = u8(url.Arguments);
    size_t qm = cmd.find('?');
    if (qm != std::string::npos) { query = cmd.substr(qm + 1); cmd = cmd.substr(0, qm); }
    if (!query.empty()) {
        std::vector<beans::PropertyValue> extra(args_.begin(), args_.end());
        size_t pos = 0;
        while (pos <= query.size()) {
            size_t amp = query.find('&', pos);
            std::string kv = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            size_t eq = kv.find('=');
            if (eq != std::string::npos) {
                beans::PropertyValue p;
                p.Name = ou(kv.substr(0, eq));
                p.Value <<= ou(kv.substr(eq + 1));
                extra.push_back(p);
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
        args_ = Sequence<beans::PropertyValue>(extra.data(), sal_Int32(extra.size()));
    }
    silent_ = argNum(args_, "Silent", 0) != 0;
    execute(cmd);
}
#endif

#ifdef MONTECALC_XLL
void Engine::run(const std::string& command, const std::vector<beans::PropertyValue>& args, bool silent) {
    args_ = Sequence<beans::PropertyValue>(sal_Int32(args.size()));
    for (size_t i = 0; i < args.size(); ++i) args_[sal_Int32(i)] = args[i];
    silent_ = silent || argNum(args_, "Silent", 0) != 0;
    std::string cmd = command, query;
    size_t qm = cmd.find('?');   // "Wizard?Function=Normal" as in the LibreOffice menus
    if (qm != std::string::npos) {
        query = cmd.substr(qm + 1);
        cmd = cmd.substr(0, qm);
        std::vector<beans::PropertyValue> extra(args.begin(), args.end());
        size_t pos = 0;
        while (pos <= query.size()) {
            size_t amp = query.find('&', pos);
            std::string kv = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            size_t eq = kv.find('=');
            if (eq != std::string::npos) {
                beans::PropertyValue p;
                p.Name = ou(kv.substr(0, eq));
                std::string val = kv.substr(eq + 1);
                bool ok;
                double num = parseNumber(val, ok);
                p.Value = ok ? Any(num) : Any(ou(val));
                extra.push_back(p);
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
        args_ = Sequence<beans::PropertyValue>(sal_Int32(extra.size()));
        for (size_t i = 0; i < extra.size(); ++i) args_[sal_Int32(i)] = extra[i];
    }
    execute(cmd);
}
#endif

void Engine::execute(const std::string& cmd) {
    try {
        ModelDoc d(model());
        if (!d.valid() && cmd != "About" && cmd != "Mentor") {
            message("MonteCalc", "MonteCalc works with spreadsheet documents only.", true);
            return;
        }
        static const std::map<std::string, void (Engine::*)()> cmds = {
            {"Run", &Engine::cmdRun}, {"Start", &Engine::cmdRun}, {"Settings", &Engine::cmdSettings}, {"Reset", &Engine::cmdReset},
            {"StoreResults", &Engine::cmdStoreResults}, {"Report", &Engine::cmdStoreResults}, {"SaveResults", &Engine::cmdSaveResults},
            {"Replay", &Engine::cmdReplay}, {"Expected", &Engine::cmdExpected}, {"Preview", &Engine::cmdPreview},
            {"SelectVariant", &Engine::cmdSelectVariant}, {"ChangeHistogram", &Engine::cmdChangeHistogram},
            {"CalcCorrelations", &Engine::cmdCalcCorrelations}, {"Register", &Engine::cmdRegister}, {"Model", &Engine::cmdModel}, {"InsertOutput", &Engine::cmdInsertOutput},
            {"InsertCorrelation", &Engine::cmdInsertCorrelation}, {"Lock", &Engine::cmdLock}, {"MultipleSimulation", &Engine::cmdMultipleSimulation},
            {"Variables", &Engine::cmdVariables}, {"HideFormulas", &Engine::cmdHideFormulas}, {"LoadFormulas", &Engine::cmdLoadFormulas},
            {"ModelCheck", &Engine::cmdModelCheck}, {"ForecastTool", &Engine::cmdForecastTool}, {"StartBatch", &Engine::cmdStartBatch},
            {"WriteContext", &Engine::cmdWriteContext}, {"About", &Engine::cmdAbout}, {"Functions", &Engine::cmdFunctions},
            {"InsertFunction", &Engine::cmdInsertFunction}};
        auto it = cmds.find(cmd);
        if (it != cmds.end()) (this->*(it->second))();
        else if (cmd == "SortAsc") cmdSort(true);
        else if (cmd == "SortDesc") cmdSort(false);
        else if (cmd == "Wizard") openWizard(ctx_, frame_, argText(args_, "Function"));
        else if (cmd == "Fit") openFitWindow(ctx_, frame_);
        else if (cmd == "Results") openResultWindow(ctx_, frame_, argText(args_, "Variable"), argText(args_, "View"), argText(args_, "ExportTo"));
        else if (cmd == "Correlogram") openCorrelogram(ctx_, frame_);
        else if (cmd == "InfluenceDiagram") openInfluenceDiagram(ctx_, frame_, argText(args_, "Options"), argText(args_, "ExportTo"));
        else if (cmd == "Mentor") openMentor(ctx_, frame_);
        else if (cmd == "Show") openVariableList(ctx_, frame_);
        else message("MonteCalc", "Unknown command: " + cmd, true);
    } catch (const uno::Exception& e) {
        message("MonteCalc", "Error: " + u8(e.Message), true);
    } catch (const std::exception& e) {
        message("MonteCalc", std::string("Error: ") + e.what(), true);
    }
}

// ============================================================ simulation commands

void Engine::cmdSettings() {
    auto m = model();
    RunOptions o = loadOptions(ModelDoc(m));
    std::vector<FormField> f = {
        {"# iterations", std::to_string(o.s.iterations)},
        {"Seed (0 = random, -1 = preview mode)", std::to_string(o.s.seed)},
        {"Worst case level", fmtNum(o.s.worst)},
        {"Real case level", fmtNum(o.s.real)},
        {"Best case level", fmtNum(o.s.best)},
        {"Histogram bins", std::to_string(o.s.bins)},
        {"Visualize simulation (slow)", "", true, o.visualize},
        {"Memory Calculation (fast, supported functions only)", "", true, o.memory},
        {"Store results in a result workbook after each run", "", true, o.report},
        {"Use DIP (store outputs as FLO_r_ arrays in the model)", "", true, o.dip},
        {"Use correlations", "", true, o.s.useCorrelations},
        {"Language (MonteCalc windows and messages)", languageSetting(), false, false, {"Auto", "English", "Deutsch", "Español"}},
    };
    if (!ask("MonteCalc - Settings", f)) return;
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
    n.memory = f[7].checked;
    n.report = f[8].checked;
    n.dip = f[9].checked;
    n.s.useCorrelations = f[10].checked;
    std::string err;
    if (!ok) err = "Please enter numbers only.";
    if (!err.empty() || !validateOptions(n, err)) { message("MonteCalc", err, true); return; }
    saveOptions(ModelDoc(m), n);
    if (f[11].value != languageSetting()) setLanguageSetting(f[11].value);   // menus follow LibreOffice's language
    auto& sim = mc::Simulation::instance();
    sim.setSettings(n.s);
    // seed -1: preview mode (every recalculation draws random values)
    if (!sim.busy()) {
        sim.setMode(n.s.seed == -1 ? mc::Mode::Random : mc::Mode::Expected);
        ModelDoc(m).recalc();
    }
}

void Engine::cmdRun() {
    auto m = model();
    RunOptions o = loadOptions(ModelDoc(m));
    o.s.iterations = int(argNum(args_, "Iterations", o.s.iterations));
    o.s.seed = (long long)argNum(args_, "Seed", double(o.s.seed));
    o.visualize = argNum(args_, "Visualize", o.visualize) != 0;
    o.report = argNum(args_, "Report", o.report) != 0;
    o.memory = argNum(args_, "Memory", o.memory) != 0;
    o.dip = argNum(args_, "DIP", o.dip) != 0;
    o.s.useCorrelations = argNum(args_, "UseCorrelations", o.s.useCorrelations) != 0;
    std::string err;
    if (!validateOptions(o, err)) { message("MonteCalc", err, true); return; }
    // remember the selected variable (the result window opens for it)
    ModelDoc d(m);
    std::string selected;
    table::CellAddress cur;
    if (d.currentCell(cur))
        for (const auto& v : d.scan())
            if (v.addr.Sheet == cur.Sheet && v.addr.Column == cur.Column && v.addr.Row == cur.Row && v.kind != mc::VarKind::Correlation)
                selected = v.name;
    mc::Settings s = o.s;
    if (s.seed == -1) s.seed = 0;
    RunOptions run = o;
    run.s = s;
    RunResult rr = runSimulation(ctx_, frame_, run);
    if (o.s.seed == -1) { mc::Simulation::instance().setMode(mc::Mode::Random); d.recalc(); }
    std::string note;
    if (o.report) note = createReport(ctx_, silent_, nullptr, m);
    if (silent_) {
        message("MonteCalc", rr.summary + (note.empty() ? "" : "\n" + note));
        return;
    }
    if (selected.empty()) {
        auto outs = mc::Simulation::instance().resultNames(mc::VarKind::Output);
        if (!outs.empty()) selected = outs[0];
    }
    if (!rr.warnings.empty()) message("MonteCalc", rr.summary);
    openResultWindow(ctx_, frame_, selected);
}

void Engine::cmdReset() {
    auto& sim = mc::Simulation::instance();
    ModelDoc d(model());
    restoreReplay(d);
    sim.reset();
    sim.setMode(mc::Mode::Expected);
    d.recalc();
}

void Engine::cmdStoreResults() {
    std::string note = createReport(ctx_, silent_, nullptr, model());
    if (!note.empty()) message("MonteCalc", note);
}

void Engine::cmdSaveResults() {
    // "Close and save results": store the result workbook next to the model
    auto m = model();
    std::string modelUrl = ModelDoc(m).propertyText("MonteCalc.Model");
    if (modelUrl.empty()) { message("MonteCalc", "The active document is not a MonteCalc result workbook.", true); return; }
    std::string dir = modelUrl.substr(0, modelUrl.rfind('/') + 1), base = modelUrl.substr(modelUrl.rfind('/') + 1);
    base = base.substr(0, base.rfind('.'));
    std::time_t t = std::time(nullptr);
    char stamp[64];
    std::strftime(stamp, sizeof stamp, "%d_%m_%Y_%H_%M_%S", std::localtime(&t));
#ifdef MONTECALC_XLL
    std::string url = dir + base + "_" + stamp + "_MC_FLO.xlsx";
    ModelDoc d(m);
    d.saveAs(url);
    message("MonteCalc", "Results saved as " + url);
    if (!silent_) d.close();
#else
    std::string url = dir + base + "_" + stamp + "_MC_FLO.ods";
    Reference<frame::XStorable> st(m, UNO_QUERY_THROW);
    beans::PropertyValue pv;
    pv.Name = "FilterName";
    pv.Value <<= OUString("calc8");
    st->storeAsURL(ou(url), seq<beans::PropertyValue>({pv}));
    Reference<util::XCloseable> cl(m, UNO_QUERY);
    message("MonteCalc", "Results saved as " + url);
    if (cl.is() && !silent_) cl->close(true);
#endif
}

void Engine::cmdReplay() {
    // "Analyse selected iteration": load the input values of one iteration into the model
    auto& sim = mc::Simulation::instance();
    if (!sim.hasResults()) { message("MonteCalc", "Run a simulation first.", true); return; }
    std::vector<FormField> f = {{"Iteration (1 - " + std::to_string(sim.iterations()) + ")", "1"}};
    double itArg = argNum(args_, "Iteration", 0);
    if (itArg > 0) f[0].value = std::to_string(int(itArg));
    else if (!ask("MonteCalc - Individual analysis of iteration", f)) return;
    bool ok;
    int it = int(parseNumber(f[0].value, ok));
    if (!ok || it < 1 || it > sim.iterations()) { message("MonteCalc", "Iteration must be between 1 and " + std::to_string(sim.iterations()) + ".", true); return; }
    ModelDoc d(model());
    restoreReplay(d);
    auto& saved = replayStore()[d.key()];
    for (const auto& v : d.scan()) {
        if (v.kind != mc::VarKind::Input) continue;
        std::vector<double> s;
        if (!sim.samples(v.name, s) || it > int(s.size()) || !std::isfinite(s[it - 1])) continue;
        auto c = d.cell(v.addr);
        saved.push_back({v.addr, c.formula()});
        c.setValue(s[it - 1]);
    }
    d.recalc();
}

void Engine::cmdExpected() {
    ModelDoc d(model());
    restoreReplay(d);
    mc::Simulation::instance().setMode(mc::Mode::Expected);
    d.recalc();
}

void Engine::cmdPreview() {
    auto& sim = mc::Simulation::instance();
    sim.setMode(sim.mode() == mc::Mode::Random ? mc::Mode::Expected : mc::Mode::Random);
    ModelDoc(model()).recalc();
}

void Engine::cmdSelectVariant() {
    auto& sim = mc::Simulation::instance();
    auto labels = sim.resultSetLabels();
    if (labels.size() < 2) { message("MonteCalc", "No multiple simulation results available.", true); return; }
    std::vector<FormField> f = {{"Variant", labels[std::max(0, sim.activeResultSet())]}};
    f[0].options = labels;
    std::string a = argText(args_, "Variant");
    if (!a.empty()) f[0].value = a;
    else if (!ask("MonteCalc - Select variant", f)) return;
    ModelDoc d(model());
    for (size_t i = 0; i < labels.size(); ++i)
        if (labels[i] == f[0].value) {
            sim.selectResultSet(int(i));
            for (auto& v : loadVariants(d))
                if (v.label == labels[i]) d.cell(v.ref).setValue(v.value);
        }
    d.recalc();
}

void Engine::cmdChangeHistogram() {
    auto& sim = mc::Simulation::instance();
    mc::Settings s = sim.settings();
    std::vector<FormField> f = {{"Set bins", std::to_string(s.bins)}};
    if (!ask("MonteCalc - Change histogram", f)) return;
    bool ok;
    int b = int(parseNumber(f[0].value, ok));
    if (!ok || b < 2 || b > 1000) { message("MonteCalc", "Bins must be between 2 and 1000.", true); return; }
    s.bins = b;
    sim.setSettings(s);
    auto m = model();
    if (!ModelDoc(m).propertyText("MonteCalc.Model").empty()) rebuildStatistics(ctx_, m, b);
}

void Engine::cmdCalcCorrelations() {
    auto& sim = mc::Simulation::instance();
    if (!sim.hasResults()) { message("MonteCalc", "Run a simulation first.", true); return; }
    std::vector<std::string> names;
    for (auto& v : sim.resultVariables()) names.push_back(v.name);
    auto outs = sim.resultNames(mc::VarKind::Output), ins = sim.resultNames(mc::VarKind::Input);
    std::vector<FormField> f = {{"X-variable", ins.empty() ? names[0] : ins[0]}, {"Y-variable", outs.empty() ? names[0] : outs[0]}};
    f[0].options = names;
    f[1].options = names;
    if (!argText(args_, "X").empty() && !argText(args_, "Y").empty()) { f[0].value = argText(args_, "X"); f[1].value = argText(args_, "Y"); }
    else if (!ask("MonteCalc - Correlation and sensitivities", f)) return;
    addSensitivitySheet(ctx_, model(), f[0].value, f[1].value, silent_);
    // and the scatter of X against Y, as in MC FLO
    if (!silent_ && f[0].value != f[1].value) openResultWindow(ctx_, frame_, f[1].value, "", "", f[0].value);
}

// ============================================================ model definition

// "Model" (MC FLO "Modell"): brings the model up to date. Variables whose cell no longer holds an
// MC FLO formula are removed, and every reference to them is replaced by 0 (wiki "Umgang mit
// Variablen"); then the variables are registered again and the variable list is shown.
void Engine::cmdModel() {
    ModelDoc d(model());
    auto cells = d.scan();
    std::set<std::string> current;
    for (auto& c : cells) {
        std::string prefix = c.kind == mc::VarKind::Input ? "FLO_i_" : c.kind == mc::VarKind::Output ? "FLO_o_" : "FLO_c_";
        current.insert(mc::lower(prefix + mc::formula::sanitizeName(c.name)));
    }
    std::vector<std::string> stale;
    for (const char* pre : {"FLO_i_", "FLO_o_", "FLO_c_"})
        for (auto& [n, r] : d.namedRanges(pre))
            if (!current.count(mc::lower(n))) stale.push_back(n);
    int replaced = 0;
    if (!stale.empty()) {
        auto names = d.sheetNames();
        for (size_t si = 0; si < names.size(); ++si) {
            for (auto& [addr, f0] : d.formulaCells(int(si))) {
                std::string f = f0, uf = mc::lower(f);
                bool changed = false;
                for (auto& n : stale) {
                    std::string ln = mc::lower(n);
                    size_t pos = 0;
                    while ((pos = uf.find(ln, pos)) != std::string::npos) {
                        auto isId = [](char ch) { return std::isalnum((unsigned char)ch) || ch == '_' || ch == '.'; };
                        bool left = pos == 0 || !isId(uf[pos - 1]), right = pos + ln.size() >= uf.size() || !isId(uf[pos + ln.size()]);
                        if (left && right) {
                            f.replace(pos, ln.size(), "0");
                            uf.replace(pos, ln.size(), "0");
                            changed = true;
                            pos += 1;
                        } else {
                            pos += ln.size();
                        }
                    }
                }
                if (changed) { d.cell(addr).setFormula(f); ++replaced; }
            }
        }
        for (auto& n : stale) d.removeNamedRange(n);
    }
    bool was = silent_;
    silent_ = true;
    cmdRegister();
    silent_ = was;
    if (!silent_ && (!stale.empty()))
        message("MC FLO - Model", std::to_string(stale.size()) + " removed variable(s); references in " + std::to_string(replaced) +
                                      " formula(s) were replaced by 0.");
    if (!silent_) openVariableList(ctx_, frame_);
}


void Engine::cmdRegister() {
    ModelDoc d(model());
    auto cells = d.scan();
    std::set<std::string> used;
    for (auto& c : cells) if (c.named) used.insert(mc::lower(c.name));
    int renamed = 0;
    for (auto& c : cells) {
        if (c.kind != mc::VarKind::Input || c.named) continue;
        // name from the label left of the cell, else <Sheet>_<Cell>
        std::string name;
        if (c.addr.Column > 0) {
            auto l = d.cell(table::CellAddress(c.addr.Sheet, c.addr.Column - 1, c.addr.Row));
            if (l.type() == CellType::Text)
                name = mc::formula::sanitizeName(u8(l.text()));
        }
        if (name.empty() || used.count(mc::lower(name))) name = d.defaultName(c.addr);
        used.insert(mc::lower(name));
        std::string f = c.formula;
        size_t open, close;
        if (!callSpan(f, open, close)) continue;
        const mc::FloFunction* fl = mc::findFlo(c.function);
        if (!fl) continue;
        // MC FLO order: the name follows the parameters
        std::string func;
        std::vector<std::string> args;
        mc::formula::findCall(f, "FLOSIMULA_", func, args);
        size_t idx = mc::nameArgIndex(*fl, args);
        while (args.size() < idx) args.push_back("");
        if (args.size() > idx) args[idx] = quote(name);
        else args.push_back(quote(name));
        std::string inner;
        for (size_t i = 0; i < args.size(); ++i) inner += (i ? ";" : "") + args[i];
        std::string nf = f.substr(0, open + 1) + inner + f.substr(close);
        d.cell(c.addr).setFormula(ou(nf));
        c.name = name;
        c.named = true;
        ++renamed;
    }
    d.removeNamedRangesWithPrefix("FLO_i_");
    d.removeNamedRangesWithPrefix("FLO_c_");
    d.removeNamedRangesWithPrefix("FLO_o_");
    int ni = 0, nc = 0, no = 0;
    for (auto& c : cells) {
        std::string prefix = c.kind == mc::VarKind::Input ? "FLO_i_" : c.kind == mc::VarKind::Output ? "FLO_o_" : "FLO_c_";
        d.setNamedRange(prefix + mc::formula::sanitizeName(c.name), c.addr);
        bool lockedCell = c.kind == mc::VarKind::Input && mc::Simulation::instance().isLocked(c.name);
        d.setBackground(c.addr, lockedCell ? kColorLocked
                                           : c.kind == mc::VarKind::Input ? kColorInput : c.kind == mc::VarKind::Output ? kColorOutput : kColorCorrelation);
        (c.kind == mc::VarKind::Input ? ni : c.kind == mc::VarKind::Output ? no : nc)++;
    }
    d.recalc();
    message("MonteCalc - Register variables", std::to_string(ni) + " input, " + std::to_string(nc) + " correlation and " + std::to_string(no) +
                                                  " output variables registered" + (renamed ? " (" + std::to_string(renamed) + " inputs named)" : "") + ".");
}

void Engine::cmdInsertOutput() {
    ModelDoc d(model());
    table::CellAddress a;
    if (!d.currentCell(a)) { message("MonteCalc", "Select the cell with the result to analyse.", true); return; }
    std::vector<FormField> f = {{"Name", ""}};
    if (a.Column > 0) {
        auto l = d.cell(table::CellAddress(a.Sheet, a.Column - 1, a.Row));
        if (l.type() == CellType::Text) f[0].value = mc::formula::sanitizeName(l.text());
    }
    if (!ask("MonteCalc - Insert output variable", f)) return;
    std::string name = mc::formula::sanitizeName(f[0].value);
    if (f[0].value.empty()) { message("MonteCalc", "Please enter a name.", true); return; }
    auto c = d.cell(a);
    std::string cur = u8(c.formula()), nf;
    std::string out = "FLOsimula_output(" + quote(name) + ")";
    if (cur.empty()) nf = "=+" + out;
    else if (cur[0] == '=') {
        std::string inner = cur.substr(1);
        int col, row;
        bool simpleRef = mc::formula::parseCellName(inner, col, row);
        nf = simpleRef ? "=" + inner + "+" + out : "=(" + inner + ")+" + out;
    } else {
        nf = "=" + cur + "+" + out;
    }
    c.setFormula(ou(nf));
    d.setNamedRange("FLO_o_" + name, a);
    d.setBackground(a, kColorOutput);
}

void Engine::cmdInsertCorrelation() {
    // graphical window as in the guide; with Silent=true and the arguments Variable1, Variable2,
    // Coefficient and Type the correlation is inserted without user interaction (macros, tests);
    // ExportTo writes the preview scatter as PNG.
    bool insertNow = silent_ && !argText(args_, "Variable1").empty() && argNum(args_, "Insert", 1) != 0;
    openCorrelationWindow(ctx_, frame_, argText(args_, "Variable1"), argText(args_, "Variable2"), argNum(args_, "Coefficient", 0.5),
                          int(argNum(args_, "Type", 0)), argText(args_, "ExportTo"), insertNow);
}

void Engine::cmdLock() {
    ModelDoc d(model());
    table::CellAddress a;
    if (!d.currentCell(a)) { message("MonteCalc", "Select the cell of an input variable.", true); return; }
    for (auto& v : d.scan()) {
        if (v.addr.Sheet != a.Sheet || v.addr.Column != a.Column || v.addr.Row != a.Row) continue;
        if (v.kind != mc::VarKind::Input) break;
        if (!v.named) { message("MonteCalc", "Only named input variables can be locked (use Register variables).", true); return; }
        auto& sim = mc::Simulation::instance();
        sim.toggleLock(v.name);
        bool locked = sim.isLocked(v.name);
        d.setBackground(a, locked ? kColorLocked : kColorInput);
        message("MonteCalc", "Variable '" + v.name + "' is " + (locked ? "locked to its expected value." : "unlocked."));
        return;
    }
    message("MonteCalc", "The selected cell does not contain an input variable.", true);
}

void Engine::cmdMultipleSimulation() {
    ModelDoc d(model());
    table::CellRangeAddress r;
    if (!d.selectionRange(r)) return;
    // remove the previous selection's marking
    std::string old = d.propertyText("MonteCalc.Variants");
    table::CellRangeAddress or_;
    if (!old.empty() && parseRange(d, old, or_))
        for (sal_Int32 row = or_.StartRow; row <= or_.EndRow; ++row)
            for (sal_Int32 col = or_.StartColumn; col <= or_.EndColumn; ++col) d.setBackground(table::CellAddress(or_.Sheet, col, row), -1);
    bool single = r.StartColumn == r.EndColumn && r.StartRow == r.EndRow;
    if (single || r.EndColumn - r.StartColumn < 2) {
        d.setProperty("MonteCalc.Variants", Any());
        message("MonteCalc", "Multiple simulation reset.");
        return;
    }
    d.setProperty("MonteCalc.Variants", Any(ou(rangeText(d, r))));
    auto v = loadVariants(d);
    if (v.empty()) {
        d.setProperty("MonteCalc.Variants", Any());
        message("MonteCalc", "Select rows with Variant, Value and Reference (=cell), without headings.", true);
        return;
    }
    for (sal_Int32 row = r.StartRow; row <= r.EndRow; ++row)
        for (sal_Int32 col = r.StartColumn; col <= r.EndColumn; ++col) d.setBackground(table::CellAddress(r.Sheet, col, row), kColorVariant);
    std::string list;
    for (auto& x : v) list += "\n  " + x.label + ": " + d.cellName(x.ref) + " = " + fmtNum(x.value);
    message("MonteCalc - Multiple simulation", std::to_string(v.size()) + " variants registered:" + list);
}

void Engine::cmdVariables() {
    if (!silent_) { openVariableList(ctx_, frame_); return; }
    ModelDoc d(model());
    std::ostringstream os;
    for (auto& v : d.scan())
        os << (v.kind == mc::VarKind::Input ? "input " : v.kind == mc::VarKind::Output ? "output " : "correlation ") << v.name << " "
           << d.cellName(v.addr) << " " << v.description << "\n";
    message("MonteCalc - Variables", os.str());
}

// ============================================================ tools

void Engine::cmdHideFormulas() {
    ModelDoc d(model());
    auto cells = d.scan();
    if (cells.empty()) { message("MonteCalc", "No MC FLO formulas found.", true); return; }
    int h = d.ensureSheet("FLO_Hidden", false);
    // append below existing entries
    sal_Int32 row = 0;
    while (d.cell(table::CellAddress(sal_Int16(h), 0, row)).type() != CellType::Empty) ++row;
    for (auto& c : cells) {
        auto x = d.cell(c.addr);
        d.cell(table::CellAddress(sal_Int16(h), 0, row)).setText(d.cellName(c.addr));
        d.cell(table::CellAddress(sal_Int16(h), 1, row)).setText(x.formula());
        ++row;
        double v = x.value();
        x.setValue(v);
    }
    message("MonteCalc - Hide formulas", std::to_string(cells.size()) + " MC FLO formulas replaced by their values. Use 'Load formulas' to restore them.");
}

void Engine::cmdLoadFormulas() {
    ModelDoc d(model());
    int h = d.sheetIndex("FLO_Hidden");
    if (h < 0) { message("MonteCalc", "There are no hidden formulas.", true); return; }
    int n = 0;
    for (sal_Int32 row = 0;; ++row) {
        auto a = d.cell(table::CellAddress(sal_Int16(h), 0, row));
        if (a.type() == CellType::Empty) break;
        table::CellAddress addr;
        if (!d.parseAddress(a.text(), addr)) continue;
        d.cell(addr).setFormula(d.cell(table::CellAddress(sal_Int16(h), 1, row)).text());
        ++n;
    }
    d.removeSheet("FLO_Hidden");
    d.recalc();
    message("MonteCalc - Load formulas", std::to_string(n) + " formulas restored.");
}

void Engine::cmdModelCheck() {
    // "Excel-Check": analyses the model and writes the findings to a new workbook
    ModelDoc d(model());
    struct Finding { std::string category, cell, detail; };
    std::vector<Finding> out;
    auto names = d.sheetNames();
    long formulas = 0, constants = 0;
    static const std::set<std::string> volatileFns = {"NOW", "TODAY", "RAND", "RANDBETWEEN", "OFFSET", "INDIRECT", "INFO", "CELL"};
    static const std::set<std::string> slowFns = {"VLOOKUP", "HLOOKUP", "INDEX", "MATCH", "LOOKUP", "SUMIF", "SUMIFS", "COUNTIF"};
    auto src = d.formulaSource();
    for (size_t s = 0; s < names.size(); ++s) {
        if (names[s].rfind("FLO_", 0) == 0) continue;
        constants += long(d.valueCells(int(s)).size());
        for (auto& [addr, f] : d.formulaCells(int(s))) {
            ++formulas;
            std::string cell = d.cellName(addr);
            if (d.cell(addr).error() != 0) out.push_back({"Error value", cell, f});
            mc::formula::NodePtr tree;
            try { tree = mc::formula::parse(f, int(s), src.get()); } catch (const std::exception& e) {
                out.push_back({"Not supported by Memory Calculation", cell, e.what()});
                continue;
            }
            std::vector<const mc::formula::Node*> calls;
            mc::formula::collectCalls(tree, calls);
            for (auto* call : calls) {
                std::string b = mc::formula::baseName(call->str);
                if (volatileFns.count(b)) out.push_back({"Volatile function (slows every iteration)", cell, b});
                else if (slowFns.count(b)) out.push_back({"Lookup function (slow in simulations)", cell, b});
                else if (!mc::formula::supportedFunctions().count(b) && !mc::findFlo(b) && b.rfind("FMC_", 0) != 0)
                    out.push_back({"Not supported by Memory Calculation", cell, b});
            }
            // MC FLO function must be the principal function of the cell
            std::string u = f;
            for (auto& ch : u) ch = char(std::toupper((unsigned char)ch));
            size_t p = u.find("FLOSIMULA_");
            if (p != std::string::npos && u.find("FLOSIMULA_OUTPUT") == std::string::npos) {
                size_t open, close;
                if (callSpan(f, open, close)) {
                    std::string tail = f.substr(close + 1);
                    size_t start = u.rfind('=', p);
                    std::string head = f.substr(1, p - 1);
                    bool qualifiedOnly = head.find_first_not_of("ORG.MONTECALC.MONTECALC.ORGmontecalc") == std::string::npos;
                    if (!tail.empty() || !qualifiedOnly || start != 0)
                        out.push_back({"MC FLO function is not the principal function of the cell", cell, f});
                }
            }
            // numbers typed into formulas (hard-coded assumptions)
            if (p == std::string::npos) {
                std::vector<mc::formula::NodePtr> stack{tree};
                int nums = 0;
                while (!stack.empty()) {
                    auto n = stack.back();
                    stack.pop_back();
                    if (n->kind == mc::formula::Node::Num && n->num != 0 && n->num != 1 && n->num != 100) ++nums;
                    for (auto& a : n->args) stack.push_back(a);
                }
                if (nums > 0) out.push_back({"Constant typed into a formula", cell, f});
            }
            // references to empty cells
            std::vector<mc::formula::RangeAddr> refs;
            mc::formula::collectRanges(tree, refs);
            for (auto& r : refs)
                if (r.a == r.b && src->cell(r.a).type == mc::formula::CellContent::Empty)
                    out.push_back({"Reference to an empty cell", cell, mc::formula::cellName(r.a, true, [&](int i) { return names[i]; })});
        }
    }
    // MC FLO variables
    std::map<std::string, int> count;
    auto cells = d.scan();
    std::set<std::string> inputs;
    for (auto& v : cells) {
        if (v.kind != mc::VarKind::Correlation) count[mc::lower(v.name)]++;
        if (v.kind == mc::VarKind::Input) inputs.insert(mc::lower(v.name));
        if (v.kind == mc::VarKind::Input && !v.named) out.push_back({"Unnamed input (cannot be correlated or locked)", d.cellName(v.addr), v.description});
    }
    for (auto& [n, c] : count) if (c > 1) out.push_back({"Variable name used more than once", n, std::to_string(c) + " cells"});
    for (auto& v : cells)
        if (v.kind == mc::VarKind::Correlation && (!inputs.count(mc::lower(v.corrA)) || !inputs.count(mc::lower(v.corrB))))
            out.push_back({"Correlation refers to an unknown input", d.cellName(v.addr), v.corrA + " / " + v.corrB});
    if (std::none_of(cells.begin(), cells.end(), [](auto& v) { return v.kind == mc::VarKind::Output; }))
        out.push_back({"No output variable", "", "Insert at least one output variable."});

    ModelDoc rep = ModelDoc::newDocument(ctx_, silent_);
    rep.renameSheet(0, "Model check");
    std::vector<std::vector<Any>> rows = {{Any(OUString("Category")), Any(OUString("Cell")), Any(OUString("Detail"))}};
    rows.push_back({Any(OUString("Summary")), Any(OUString("")), Any(ou(std::to_string(formulas) + " formulas, " + std::to_string(constants) + " constants, " +
                                                                          std::to_string(cells.size()) + " MC FLO variables, " + std::to_string(out.size()) + " findings"))});
    for (auto& f : out) rows.push_back({Any(ou(f.category)), Any(ou(f.cell)), Any(ou(f.detail))});
    rep.writeTable(0, 0, 0, rows);
    rep.optimalWidth(0, 0, 3);
    if (silent_) std::fprintf(stderr, "[MonteCalc] Model check: %zu findings\n", out.size());
}

void Engine::cmdSort(bool asc) {
    ModelDoc d(model());
    table::CellRangeAddress r;
    if (!d.selectionRange(r)) return;
    std::vector<double> v;
    for (auto& row : d.selectionValues())
        for (double x : row) if (std::isfinite(x)) v.push_back(x);
    std::sort(v.begin(), v.end());
    if (!asc) std::reverse(v.begin(), v.end());
    bool column = r.EndRow > r.StartRow || r.StartColumn == r.EndColumn;
    for (size_t i = 0; i < v.size(); ++i) {
        table::CellAddress a = column ? table::CellAddress(r.Sheet, r.EndColumn + 1, r.StartRow + sal_Int32(i))
                                      : table::CellAddress(r.Sheet, r.StartColumn + sal_Int32(i), r.EndRow + 1);
        d.cell(a).setValue(v[i]);
    }
}

namespace {

struct ForecastResult {
    std::string model;
    std::vector<double> fitted, forecast;
    double theilU = 0;
};

// models: "auto", "AR(1)", "MA(1)", "ARMA(1,1)", "ARCH(1)", "Wiener process", "HW7", "HW12", "HW4"
ForecastResult forecastSeries(const std::vector<double>& y, const std::string& model, int selection, int h, double p, int iterations) {
    ForecastResult r;
    std::mt19937_64 rng(4242);
    if (model.rfind("HW", 0) == 0) {
        int season = std::atoi(model.c_str() + 2);
        mc::HoltWinters hw = mc::bestHoltWinters(y, season, selection);
        if (!hw.ok) throw std::runtime_error("Holt-Winters model could not be fitted");
        r.model = std::string(hw.multiplicative ? "Holt-Winters multiplicative" : "Holt-Winters additive") + " (season " + std::to_string(hw.season) + ")";
        r.fitted = hw.fitted;
        r.forecast = hw.forecastPercentile(h, p, iterations, rng);
        r.theilU = hw.theilU;
        return r;
    }
    auto fits = mc::fitTimeSeries(y);
    const mc::TsFit* best = nullptr;
    for (auto& f : fits) {
        if (!f.ok) continue;
        if (model == "auto" || model == f.name) {
            if (!best || (model == "auto" && (selection == 0 ? f.aic < best->aic : f.rss < best->rss))) best = &f;
        }
    }
    if (!best) throw std::runtime_error("the time series could not be fitted with " + model);
    r.model = best->name + " " + best->formula;
    r.fitted = best->fitted;
    r.forecast = mc::forecastTimeSeries(best->ts, y, h, p, iterations, rng);
    r.theilU = best->theilU;
    return r;
}

void writeSeries(const ModelDoc& d, table::CellAddress start, bool rowWise, const std::vector<double>& v) {
    for (size_t i = 0; i < v.size(); ++i) {
        table::CellAddress a = start;
        if (rowWise) a.Column += sal_Int32(i); else a.Row += sal_Int32(i);
        d.cell(a).setValue(v[i]);
    }
}

void annotate(const ModelDoc& d, const table::CellAddress& a, const std::string& text) { d.setComment(a, text); }

} // namespace

void Engine::cmdForecastTool() {
    ModelDoc d(model());
    table::CellRangeAddress r;
    if (!d.selectionRange(r)) { message("MonteCalc", "Select the historical data first.", true); return; }
    std::vector<double> y;
    for (auto& row : d.selectionValues())
        for (double x : row) if (std::isfinite(x)) y.push_back(x);
    if (y.size() < 8) { message("MonteCalc", "Select at least 8 historical values.", true); return; }
    std::vector<std::string> models = {"auto", "AR(1)", "MA(1)", "ARMA(1,1)", "ARCH(1)", "Wiener process", "HW7", "HW12", "HW4"};
    std::vector<FormField> f = {{"Model (auto = best by AIC; HWn = Holt-Winters, season n)", "auto"},
                                {"Number of forecast periods", "1"},
                                {"ResultOrder", "column by column"},
                                {"Percentile (0.5 = standard forecast)", "0.5"},
                                {"Iterations", "50000"}};
    f[0].options = models;
    f[2].options = {"line by line", "column by column"};
    if (!ask("MonteCalc - Forecast-Tool", f)) return;
    bool ok1, ok2, ok3;
    int h = int(parseNumber(f[1].value, ok1));
    double p = parseNumber(f[3].value, ok2);
    int its = int(parseNumber(f[4].value, ok3));
    if (!ok1 || !ok2 || !ok3 || h < 1 || h > 10000 || !(p > 0 && p < 1) || its < 1) { message("MonteCalc", "Please check the numbers.", true); return; }
    bool rowWise = f[2].value == "line by line";
    auto res = forecastSeries(y, f[0].value, 0, h, p, its);
    bool dataInColumn = r.EndRow > r.StartRow;
    table::CellAddress start = dataInColumn ? table::CellAddress(r.Sheet, r.EndColumn + 1, r.StartRow) : table::CellAddress(r.Sheet, r.StartColumn, r.EndRow + 1);
    writeSeries(d, start, rowWise, res.forecast);
    annotate(d, start, "Forecast " + res.model + ", percentile " + fmtNum(p) + ", Theil's U " + fmtNum(res.theilU));
    if (silent_) message("MonteCalc", "Forecast: " + res.model);
}

void Engine::cmdStartBatch() {
    ModelDoc d(model());
    int done = 0;
    std::vector<std::string> errors;
    auto names = d.sheetNames();
    for (size_t s = 0; s < names.size(); ++s) {
        std::vector<std::pair<table::CellAddress, std::string>> batch;
        for (auto& fc : d.formulaCells(int(s))) {
            std::string u = fc.second;
            for (auto& ch : u) ch = char(std::toupper((unsigned char)ch));
            if (u.find("FMC_BATCH_") != std::string::npos) batch.push_back(fc);
        }
        for (auto& [addr, formula] : batch) {
            try {
                std::string func;
                std::vector<std::string> args;
                if (!mc::formula::findCall(formula, "FMC_BATCH_", func, args) || args.empty())
                    throw std::runtime_error("cannot read the batch formula");
                auto num = [&](size_t i, double def) {
                    if (i >= args.size()) return def;
                    std::string t = args[i];
                    bool ok;
                    double v = parseNumber(mc::formula::unquote(t, ok), ok);
                    if (ok) return v;
                    table::CellAddress ra;
                    if (d.parseAddress(t, ra)) return d.cell(ra).value();
                    return def;
                };
                table::CellRangeAddress rg;
                std::string rtext = args[0];
                if (!parseRange(d, rtext, rg)) throw std::runtime_error("cannot read the data range " + rtext);
                if (rtext.find('.') == std::string::npos) rg.Sheet = sal_Int16(s);
                std::vector<double> y;
                for (sal_Int32 row = rg.StartRow; row <= rg.EndRow; ++row)
                    for (sal_Int32 col = rg.StartColumn; col <= rg.EndColumn; ++col) {
                        auto x = d.cell(table::CellAddress(rg.Sheet, col, row));
                        if (x.type() == CellType::Value || (x.type() == CellType::Formula && x.error() == 0))
                            y.push_back(x.value());
                    }
                int h = int(num(1, 1));
                bool rowWise = num(2, 0) == 0;
                int selection = int(num(3, 0));
                int its = int(num(4, 50000));
                double p = num(5, 0.5);
                if (!(p > 0 && p < 1)) p = 0.5;
                std::string model = "auto";
                if (func.find("HW_D") != std::string::npos) model = "HW7";
                else if (func.find("HW_M") != std::string::npos) model = "HW12";
                else if (func.find("HW_Q") != std::string::npos) model = "HW4";
                auto res = forecastSeries(y, model, selection, h, p, its);
                std::vector<double> all = res.fitted;
                all.insert(all.end(), res.forecast.begin(), res.forecast.end());
                table::CellAddress start = addr;
                if (rowWise) start.Column += 1; else start.Row += 1;
                writeSeries(d, start, rowWise, all);
                std::time_t t = std::time(nullptr);
                char stamp[64];
                std::strftime(stamp, sizeof stamp, "%d.%m.%Y %H:%M", std::localtime(&t));
                annotate(d, addr, std::string("Batch ") + stamp + "\n" + res.model + "\nU-test (Theil's U): " + fmtNum(res.theilU) +
                                      "\nPercentile " + fmtNum(p));
                ++done;
            } catch (const std::exception& e) {
                errors.push_back(d.cellName(addr) + ": " + e.what());
            }
        }
    }
    std::string msg = std::to_string(done) + " batch forecast(s) done.";
    for (auto& e : errors) msg += "\n" + e;
    message("MonteCalc - Start Batch", done == 0 && errors.empty() ? "No fMC_Batch_ formulas found." : msg, !errors.empty());
}

void Engine::cmdWriteContext() { writeContextSheet(ctx_, frame_); }

// ============================================================ fMC_ function picker (MC FLO ribbon "Functions")

const std::vector<FunctionCategory>& functionMenu() {
    static const std::vector<FunctionCategory> m = {
#include "function_menu.inc"
    };
    return m;
}

#ifdef MONTECALC_XLL
void Engine::cmdFunctions() { message("MonteCalc", "Use the Functions menu of the MC FLO tab.", false); }
#else
void Engine::cmdFunctions() {
    // a popup menu with one submenu per category, opened at the top left of the sheet
    Reference<awt::XPopupMenu> menu = awt::PopupMenu::create(ctx_);
    std::vector<Reference<awt::XPopupMenu>> subs;
    const auto& cats = functionMenu();
    for (size_t c = 0; c < cats.size(); ++c) {
        Reference<awt::XPopupMenu> sub = awt::PopupMenu::create(ctx_);
        for (size_t k = 0; k < cats[c].functions.size(); ++k)
            sub->insertItem(sal_Int16(100 * (c + 1) + k), ou(cats[c].functions[k]), 0, sal_Int16(k));
        sal_Int16 id = sal_Int16(c + 1);
        menu->insertItem(id, ou(tr(cats[c].name)), 0, sal_Int16(c));
        menu->setPopupMenu(id, sub);
        subs.push_back(sub);
    }
    Reference<awt::XWindowPeer> peer(frame_->getComponentWindow(), UNO_QUERY);
    if (!peer.is()) return;
    int chosen = menu->execute(peer, awt::Rectangle(40, 4, 1, 1), awt::PopupMenuDirection::EXECUTE_DOWN);
    if (chosen < 100) return;
    size_t c = size_t(chosen / 100 - 1), k = size_t(chosen % 100);
    if (c >= cats.size() || k >= cats[c].functions.size()) return;
    insertFunction(cats[c].functions[k]);
}
#endif

void Engine::cmdInsertFunction() { insertFunction(argText(args_, "Name")); }

void Engine::insertFunction(const std::string& name) {
    // writes =fMC_...() into the selected empty cell and opens LibreOffice's Function Wizard on it,
    // which shows the description and one field per argument (MC FLO opens Excel's dialog)
    if (name.empty()) return;
    ModelDoc d(model());
    table::CellAddress a;
    if (!d.currentCell(a)) { message("MonteCalc - Insert function", "Select a cell for the function.", true); return; }
    auto c = d.cell(a);
    if (!u8(c.formula()).empty()) { message("MonteCalc - Insert function", "Select an empty cell for the function.", true); return; }
    c.setFormula(ou("=" + name + "()"));
    if (silent_) return;
#ifdef MONTECALC_XLL
    com::application().obj("Dialogs", {com::Var(450)}).call("Show");   // xlDialogFunctionWizard
#else
    Reference<frame::XDispatchHelper> h = frame::DispatchHelper::create(ctx_);
    h->executeDispatch(Reference<frame::XDispatchProvider>(frame_, UNO_QUERY_THROW), ".uno:FunctionDialog", "", 0, {});
#endif
}

void Engine::cmdAbout() {
    message("About MonteCalc",
#ifdef MONTECALC_XLL
            "MonteCalc - Monte Carlo simulation for Microsoft Excel, following the MC FLO workflow.\n\n"
#else
            "MonteCalc - Monte Carlo simulation for LibreOffice Calc, following the MC FLO workflow.\n\n"
#endif
            "1. Model uncertain inputs with FLOsimula_ functions (Choose Wizard or Fit distribution).\n"
            "2. Insert correlations and output variables; Register variables.\n"
            "3. Start the simulation and analyse the result window, the result workbook or the fMC_ functions.");
}

} // namespace montecalc
