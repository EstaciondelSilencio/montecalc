// MonteCalc - "MC FLO" sidebar deck. Its panels mirror the groups of the MC FLO ribbon
// (Model specification, Information, Simulation, Statistics, Correlations and sensitivities,
// Individual analysis of iteration, Results, Other) including the ribbon's input fields
// (best/real/worst case, iterations, use correlations, visualize simulation, variable choices).
// Buttons dispatch the usual "org.montecalc:" commands, so the deck, the toolbar, the
// Notebookbar tab and the menu all run the same code.
#include "sidebar.hpp"
#include "engine.hpp"
#include "model.hpp"
#include "util.hpp"
#include "i18n.hpp"
#include "ui.hpp"

#include "../core/flofuncs.h"
#include "../core/simulation.h"

#include <com/sun/star/awt/PosSize.hpp>
#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/awt/XActionListener.hpp>
#include <com/sun/star/awt/XButton.hpp>
#include <com/sun/star/awt/XCheckBox.hpp>
#include <com/sun/star/awt/XControl.hpp>
#include <com/sun/star/awt/XControlContainer.hpp>
#include <com/sun/star/awt/XFocusListener.hpp>
#include <com/sun/star/awt/XItemListener.hpp>
#include <com/sun/star/awt/XListBox.hpp>
#include <com/sun/star/awt/XMouseListener.hpp>
#include <com/sun/star/awt/XTextComponent.hpp>
#include <com/sun/star/awt/XTextListener.hpp>
#include <com/sun/star/awt/XToolkit.hpp>
#include <com/sun/star/awt/XWindow.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/frame/DispatchHelper.hpp>
#include <com/sun/star/frame/XDispatchHelper.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/ui/LayoutSize.hpp>
#include <com/sun/star/ui/UIElementType.hpp>
#include <com/sun/star/ui/XSidebarPanel.hpp>
#include <com/sun/star/ui/XToolPanel.hpp>
#include <com/sun/star/ui/XUIElement.hpp>
#include <cppuhelper/implbase4.hxx>
#include <cppuhelper/implbase5.hxx>
#include <cppuhelper/implbase6.hxx>
#include <com/sun/star/awt/XWindowListener.hpp>
#include <cppuhelper/supportsservice.hxx>
#include <rtl/ref.hxx>
#include <com/sun/star/lang/IllegalArgumentException.hpp>

#include <algorithm>
#include <array>
#include <set>

#include <cstdio>
#include <functional>
#include <map>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

namespace {

constexpr const char* kFirst = "-";   // first entry of the "Choose ..." lists

std::vector<std::string> floLabels(int which) {   // 0 continuous, 1 discrete, 2 time series
    std::vector<std::string> r;
    for (const auto& f : mc::floFunctions()) {
        if (which == 2 ? f.kind == mc::FloFunction::TimeSeries
                       : f.kind == mc::FloFunction::Dist && f.discrete == (which == 1))
            r.push_back(f.label);
    }
    if (which == 0) r.push_back("UserDefined");
    if (which == 1) r.push_back("DiscreteUserDefined");
    return r;
}

class Panel;

class PanelListener : public cppu::WeakImplHelper6<awt::XActionListener, awt::XItemListener, awt::XTextListener,
                                                   awt::XMouseListener, awt::XFocusListener, awt::XWindowListener> {
public:
    explicit PanelListener(Panel* p) : p_(p) {}
    void detach() { p_ = nullptr; }
    void SAL_CALL actionPerformed(const awt::ActionEvent& e) override;
    void SAL_CALL itemStateChanged(const awt::ItemEvent& e) override;
    void SAL_CALL textChanged(const awt::TextEvent& e) override;
    void SAL_CALL mousePressed(const awt::MouseEvent&) override {}
    void SAL_CALL mouseReleased(const awt::MouseEvent&) override {}
    void SAL_CALL mouseEntered(const awt::MouseEvent&) override;
    void SAL_CALL mouseExited(const awt::MouseEvent&) override {}
    void SAL_CALL focusGained(const awt::FocusEvent& e) override;
    void SAL_CALL focusLost(const awt::FocusEvent&) override {}
    void SAL_CALL windowResized(const awt::WindowEvent& e) override;
    void SAL_CALL windowMoved(const awt::WindowEvent&) override {}
    void SAL_CALL windowShown(const lang::EventObject&) override {}
    void SAL_CALL windowHidden(const lang::EventObject&) override {}
    void SAL_CALL disposing(const lang::EventObject&) override {}

private:
    Panel* p_;
};

class Panel : public cppu::WeakImplHelper4<ui::XUIElement, ui::XToolPanel, ui::XSidebarPanel, lang::XComponent> {
public:
    Panel(const Reference<uno::XComponentContext>& ctx, const Reference<frame::XFrame>& frame, const Reference<awt::XWindow>& parent,
          const OUString& url, const std::string& group)
        : ctx_(ctx), frame_(frame), url_(url), group_(group) {
        i18nSetContext(ctx);
        auto smgr = ctx->getServiceManager();
        container_.set(smgr->createInstanceWithContext("com.sun.star.awt.UnoControlContainer", ctx), UNO_QUERY_THROW);
        Reference<awt::XControlModel> cm(smgr->createInstanceWithContext("com.sun.star.awt.UnoControlContainerModel", ctx), UNO_QUERY_THROW);
        container_->setModel(cm);
        container_->createPeer(Reference<awt::XToolkit>(awt::Toolkit::create(ctx), UNO_QUERY_THROW), Reference<awt::XWindowPeer>(parent, UNO_QUERY));
        listener_ = new PanelListener(this);
        build();
        Reference<awt::XWindow> w(container_, UNO_QUERY_THROW);
        w->setPosSize(0, 0, 260, height_, awt::PosSize::POSSIZE);
        for (const auto& c : Reference<awt::XControlContainer>(container_, UNO_QUERY_THROW)->getControls()) fitControlText(c);
        w->addMouseListener(listener_.get());
        w->addWindowListener(listener_.get());   // controls stretch with the sidebar width
        w->setVisible(true);
        refresh();
    }
    ~Panel() override { if (listener_.is()) listener_->detach(); }

    // XUIElement
    Reference<frame::XFrame> SAL_CALL getFrame() override { return frame_; }
    OUString SAL_CALL getResourceURL() override { return url_; }
    sal_Int16 SAL_CALL getType() override { return ui::UIElementType::TOOLPANEL; }
    Reference<uno::XInterface> SAL_CALL getRealInterface() override { return static_cast<ui::XToolPanel*>(this); }
    // XToolPanel
    Reference<awt::XWindow> SAL_CALL getWindow() override { return Reference<awt::XWindow>(container_, UNO_QUERY); }
    Reference<accessibility::XAccessible> SAL_CALL createAccessible(const Reference<accessibility::XAccessible>&) override {
        return Reference<accessibility::XAccessible>(container_, UNO_QUERY);
    }
    // XSidebarPanel
    ui::LayoutSize SAL_CALL getHeightForWidth(sal_Int32) override { return ui::LayoutSize(height_, height_, height_); }
    sal_Int32 SAL_CALL getMinimalWidth() override { return 230; }
    // XComponent
    void SAL_CALL dispose() override {
        if (listener_.is()) listener_->detach();
        Reference<lang::XComponent> c(container_, UNO_QUERY);
        container_.clear();
        if (c.is()) c->dispose();
    }
    void SAL_CALL addEventListener(const Reference<lang::XEventListener>&) override {}
    void SAL_CALL removeEventListener(const Reference<lang::XEventListener>&) override {}

    // events
    void action(const std::string& name) {
        auto it = actions_.find(name);
        if (it != actions_.end()) safe(it->second);
    }
    void item(const std::string& name) {
        if (updating_) return;
        auto it = changes_.find(name);
        if (it != changes_.end()) safe(it->second);
    }
    // the sidebar changes the panel width: controls keep their columns and stretch proportionally
    void resized(int width) {
        if (width <= 0 || width == lastWidth_) return;
        lastWidth_ = width;
        double f = std::max(1.0, double(width - 2 * kX) / kW);
        for (auto& [name, r] : rects_) {
            auto c = control(name);
            if (!c.is()) continue;
            int x = kX + int((r[0] - kX) * f + 0.5), w = int(r[2] * f + 0.5);
            Reference<awt::XWindow>(c, UNO_QUERY_THROW)->setPosSize(x, r[1], w, r[3], awt::PosSize::POSSIZE);
        }
    }

    void refresh() {
        updating_ = true;
        try {
            loadSettings();
            refreshVariables();
        } catch (...) {
        }
        updating_ = false;
    }

private:
    friend class PanelListener;
    Reference<uno::XComponentContext> ctx_;
    Reference<frame::XFrame> frame_;
    Reference<awt::XControl> container_;
    rtl::Reference<PanelListener> listener_;
    OUString url_;
    std::string group_;
    int height_ = 10, y_ = 6;
    bool updating_ = false;
    std::map<std::string, std::function<void()>> actions_, changes_;
    std::map<std::string, std::vector<std::string>> items_;   // English list entries
    std::vector<std::string> variableLists_;
    std::vector<std::pair<std::string, std::array<int, 4>>> rects_;   // design rectangles (pixels, width 260)
    int lastWidth_ = 0;

    static constexpr int kX = 6, kW = 248, kH = 24, kGap = 4;

    void safe(const std::function<void()>& f) {
        try {
            f();
        } catch (const uno::Exception& e) {
            std::fprintf(stderr, "[MonteCalc] sidebar: %s\n", u8(e.Message).c_str());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[MonteCalc] sidebar: %s\n", e.what());
        }
    }

    Reference<frame::XModel> model() const {
        Reference<frame::XController> c;
        if (frame_.is()) c = frame_->getController();
        return c.is() ? c->getModel() : Reference<frame::XModel>();
    }

    void dispatch(const std::string& cmd, const std::vector<std::pair<std::string, Any>>& args = {}) {
        Reference<frame::XDispatchHelper> h = frame::DispatchHelper::create(ctx_);
        Sequence<beans::PropertyValue> a(sal_Int32(args.size()));
        for (size_t i = 0; i < args.size(); ++i) {
            a.getArray()[i].Name = ou(args[i].first);
            a.getArray()[i].Value = args[i].second;
        }
        h->executeDispatch(Reference<frame::XDispatchProvider>(frame_, UNO_QUERY_THROW), ou("org.montecalc:" + cmd), "_self", 0, a);
        refresh();
    }

    // --- control creation (pixel coordinates)
    // props are set on the model before the control gets its peer (e.g. Dropdown cannot change later)
    Reference<beans::XPropertySet> add(const char* control, const char* model, const std::string& name, int x, int y, int w, int h,
                                       const std::vector<std::pair<const char*, Any>>& props = {}) {
        auto smgr = ctx_->getServiceManager();
        Reference<beans::XPropertySet> m(smgr->createInstanceWithContext(OUString::createFromAscii(model), ctx_), UNO_QUERY_THROW);
        Reference<awt::XControl> c(smgr->createInstanceWithContext(OUString::createFromAscii(control), ctx_), UNO_QUERY_THROW);
        m->setPropertyValue("Name", Any(ou(name)));
        for (auto& p : props) m->setPropertyValue(OUString::createFromAscii(p.first), p.second);
        c->setModel(Reference<awt::XControlModel>(m, UNO_QUERY_THROW));
        Reference<awt::XControlContainer>(container_, UNO_QUERY_THROW)->addControl(ou(name), c);
        Reference<awt::XWindow>(c, UNO_QUERY_THROW)->setPosSize(x, y, w, h, awt::PosSize::POSSIZE);
        rects_.push_back({name, {x, y, w, h}});
        height_ = std::max(height_, y + h + 6);
        return m;
    }
    Reference<awt::XControl> control(const std::string& name) const {
        return Reference<awt::XControlContainer>(container_, UNO_QUERY_THROW)->getControl(ou(name));
    }
    void button(const std::string& name, const std::string& label, int x, int y, int w, std::function<void()> f, const std::string& tip = "") {
        auto m = add("com.sun.star.awt.UnoControlButton", "com.sun.star.awt.UnoControlButtonModel", name, x, y, w, kH);
        m->setPropertyValue("Label", Any(ou(tr(label))));
        if (!tip.empty()) m->setPropertyValue("HelpText", Any(ou(tr(tip))));
        Reference<awt::XButton> b(control(name), UNO_QUERY_THROW);
        b->setActionCommand(ou(name));
        b->addActionListener(listener_.get());
        actions_[name] = std::move(f);
    }
    void label(const std::string& name, const std::string& text, int x, int y, int w) {
        add("com.sun.star.awt.UnoControlFixedText", "com.sun.star.awt.UnoControlFixedTextModel", name, x, y + 4, w, kH - 6)
            ->setPropertyValue("Label", Any(ou(tr(text))));
    }
    void edit(const std::string& name, int x, int y, int w, std::function<void()> f) {
        add("com.sun.star.awt.UnoControlEdit", "com.sun.star.awt.UnoControlEditModel", name, x, y, w, kH);
        Reference<awt::XTextComponent>(control(name), UNO_QUERY_THROW)->addTextListener(listener_.get());
        changes_[name] = std::move(f);
    }
    void check(const std::string& name, const std::string& text, int x, int y, int w, std::function<void()> f) {
        add("com.sun.star.awt.UnoControlCheckBox", "com.sun.star.awt.UnoControlCheckBoxModel", name, x, y, w, kH)
            ->setPropertyValue("Label", Any(ou(tr(text))));
        Reference<awt::XCheckBox>(control(name), UNO_QUERY_THROW)->addItemListener(listener_.get());
        changes_[name] = std::move(f);
    }
    void list(const std::string& name, const std::vector<std::string>& items, int x, int y, int w, std::function<void()> f) {
        add("com.sun.star.awt.UnoControlListBox", "com.sun.star.awt.UnoControlListBoxModel", name, x, y, w, kH,
            {{"Dropdown", Any(true)}, {"LineCount", Any(sal_Int16(20))}});
        setItems(name, items, 0);
        Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->addItemListener(listener_.get());
        Reference<awt::XWindow>(control(name), UNO_QUERY_THROW)->addFocusListener(listener_.get());
        changes_[name] = std::move(f);
    }
    void setItems(const std::string& name, const std::vector<std::string>& items, int sel) {
        Sequence<OUString> s(sal_Int32(items.size()));
        for (size_t i = 0; i < items.size(); ++i) s.getArray()[i] = ou(trExact(items[i]));
        items_[name] = items;
        Reference<beans::XPropertySet> m(control(name)->getModel(), UNO_QUERY_THROW);
        m->setPropertyValue("StringItemList", Any(s));
        if (sel >= 0 && sel < int(items.size())) m->setPropertyValue("SelectedItems", Any(seq<sal_Int16>({sal_Int16(sel)})));
    }
    std::string text(const std::string& name) const { return u8(Reference<awt::XTextComponent>(control(name), UNO_QUERY_THROW)->getText()); }
    void setText(const std::string& name, const std::string& t) {
        if (text(name) != t) Reference<awt::XTextComponent>(control(name), UNO_QUERY_THROW)->setText(ou(t));
    }
    bool checked(const std::string& name) const { return Reference<awt::XCheckBox>(control(name), UNO_QUERY_THROW)->getState() == 1; }
    void setChecked(const std::string& name, bool on) { Reference<awt::XCheckBox>(control(name), UNO_QUERY_THROW)->setState(on ? 1 : 0); }
    // the English entry (entries are shown translated)
    std::string selectedItem(const std::string& name) const {
        auto it = items_.find(name);
        if (it != items_.end()) {
            int pos = selected(name);
            if (pos >= 0 && pos < int(it->second.size())) return it->second[pos];
        }
        return u8(Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->getSelectedItem()); }
    int selected(const std::string& name) const { return Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->getSelectedItemPos(); }
    void selectPos(const std::string& name, int i) { Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->selectItemPos(sal_Int16(i), true); }

    // two buttons side by side
    void buttons2(const std::string& n1, const std::string& l1, std::function<void()> f1, const std::string& n2 = "",
                  const std::string& l2 = "", std::function<void()> f2 = nullptr) {
        int w = (kW - kGap) / 2;
        button(n1, l1, kX, y_, w, std::move(f1));
        if (!n2.empty()) button(n2, l2, kX + w + kGap, y_, w, std::move(f2));
        y_ += kH + kGap;
    }
    // "label: [edit]" row
    void field(const std::string& name, const std::string& text, std::function<void()> f) {
        label("l_" + name, text, kX, y_, 96);
        edit(name, kX + 100, y_, kW - 100, std::move(f));
        y_ += kH + kGap;
    }
    void chooser(const std::string& name, const std::string& text, int which) {
        auto items = floLabels(which);
        items.insert(items.begin(), text);
        list(name, items, kX, y_, kW, [this, name] {
            if (selected(name) <= 0) return;
            std::string f = selectedItem(name);
            updating_ = true;
            selectPos(name, 0);
            updating_ = false;
            dispatch("Wizard", {{"Function", Any(ou(f))}});
        });
        y_ += kH + kGap;
    }
    void variableList(const std::string& name, const std::string& text) {
        label("l_" + name, text, kX, y_, 96);
        list(name, {}, kX + 100, y_, kW - 100, [] {});
        variableLists_.push_back(name);
        y_ += kH + kGap;
    }

    // --- settings stored in the document (same as Simulation > Settings)
    void loadSettings() {
        if (group_ != "Simulation") return;
        auto m = model();
        if (!m.is()) return;
        RunOptions o = loadOptions(m);
        setText("best", fmtNum(o.s.best));
        setText("real", fmtNum(o.s.real));
        setText("worst", fmtNum(o.s.worst));
        setText("iterations", std::to_string(o.s.iterations));
        setChecked("usecorr", o.s.useCorrelations);
        setChecked("visualize", o.visualize);
        setChecked("memory", o.memory);
    }
    void storeSettings() {
        auto m = model();
        if (!m.is()) return;
        RunOptions o = loadOptions(m);
        bool ok1, ok2, ok3, ok4;
        double best = parseNumber(text("best"), ok1), real = parseNumber(text("real"), ok2), worst = parseNumber(text("worst"), ok3);
        double it = parseNumber(text("iterations"), ok4);
        if (ok1 && best > 0 && best < 1) o.s.best = best;
        if (ok2 && real > 0 && real < 1) o.s.real = real;
        if (ok3 && worst > 0 && worst < 1) o.s.worst = worst;
        if (ok4 && it >= 1 && it <= 10000000) o.s.iterations = int(it);
        o.s.useCorrelations = checked("usecorr");
        o.visualize = checked("visualize");
        o.memory = checked("memory");
        saveOptions(m, o);
        mc::Simulation::instance().setSettings(o.s);
    }

    void refreshVariables() {
        if (variableLists_.empty()) return;
        auto& sim = mc::Simulation::instance();
        std::vector<std::string> names;
        std::set<std::string> inputs, outputs;
        if (sim.hasResults()) {
            for (auto& v : sim.resultNames(mc::VarKind::Input)) inputs.insert(v);
            for (auto& v : sim.resultNames(mc::VarKind::Output)) outputs.insert(v);
            for (auto& v : sim.resultNames(mc::VarKind::Output)) names.push_back(v);
            for (auto& v : sim.resultNames(mc::VarKind::Input)) names.push_back(v);
        } else if (auto m = model(); m.is()) {
            ModelDoc d(m);
            if (d.valid())
                for (auto& v : d.scan())
                    if (v.kind != mc::VarKind::Correlation && !v.name.empty()) {
                        names.push_back(v.name);
                        (v.kind == mc::VarKind::Output ? outputs : inputs).insert(v.name);
                    }
        }
        for (auto& n : variableLists_) {
            std::string cur = selectedItem(n);
            int sel = 0;
            bool found = false;
            for (size_t i = 0; i < names.size(); ++i) if (names[i] == cur) { sel = int(i); found = true; }
            if (!found) {   // X: the first input; Y and the statistics: the first output
                const auto& want = n == "xvar" ? inputs : outputs;
                for (size_t i = 0; i < names.size(); ++i) if (want.count(names[i])) { sel = int(i); break; }
            }
            setItems(n, names, sel);
        }
    }

    void build() {
        auto cmd = [this](const std::string& c) { return [this, c] { dispatch(c); }; };
        if (group_ == "Model") {
            chooser("cont", "Choose continuous variable", 0);
            chooser("disc", "Choose discrete variable", 1);
            chooser("ts", "Choose time series", 2);
            buttons2("wizard", "Choose Wizard", cmd("Wizard"), "corr", "Insert correlation", cmd("InsertCorrelation"));
            buttons2("output", "Insert output variable", cmd("InsertOutput"), "register", "Register variables", cmd("Register"));
            buttons2("lock", "Lock/Unlock", cmd("Lock"), "fit", "Fit distribution", cmd("Fit"));
        } else if (group_ == "Information") {
            buttons2("model", "Model", cmd("Model"), "influence", "Variables (diagram)", cmd("InfluenceDiagram"));
            buttons2("check", "Excel-Check", cmd("ModelCheck"), "context", "FLO_Context sheet", cmd("WriteContext"));
            buttons2("mentor", "AI mentor", cmd("Mentor"), "about", "About", cmd("About"));
        } else if (group_ == "Simulation") {
            auto store = [this] { storeSettings(); };
            field("best", "Best-case:", store);
            field("real", "Real-case:", store);
            field("worst", "Worst-case:", store);
            field("iterations", "Iterations:", store);
            int w = (kW - kGap) / 2;
            check("usecorr", "Use correlations", kX, y_, w, store);
            check("visualize", "Visualize simulation", kX + w + kGap, y_, w, store);
            y_ += kH;
            check("memory", "Memory Calculation", kX, y_, kW, store);
            y_ += kH + kGap;
            buttons2("multi", "Multiple simulation", cmd("MultipleSimulation"), "settings", "Settings...", cmd("Settings"));
            buttons2("start", "Start", [this] { storeSettings(); dispatch("Start"); }, "reset", "Reset", cmd("Reset"));
        } else if (group_ == "Statistics") {
            variableList("statvar", "Variable:");
            buttons2("stats", "Statistical parameters", [this] {
                dispatch("Results", {{"Variable", Any(ou(selectedItem("statvar")))}, {"View", Any(OUString("Statistics"))}});
            }, "results", "Result window", [this] { dispatch("Results", {{"Variable", Any(ou(selectedItem("statvar")))}}); });
        } else if (group_ == "Correlations") {
            variableList("xvar", "X-variable:");
            variableList("yvar", "Y-variable:");
            buttons2("calccorr", "Calc correlations", [this] {
                dispatch("CalcCorrelations", {{"X", Any(ou(selectedItem("xvar")))}, {"Y", Any(ou(selectedItem("yvar")))}});
            }, "correlogram", "Correlogram", cmd("Correlogram"));
        } else if (group_ == "Iteration") {
            field("iteration", "Choose iteration:", [] {});
            buttons2("analyze", "Analyze iteration", [this] {
                bool ok;
                double it = parseNumber(text("iteration"), ok);
                if (ok && it >= 1) dispatch("Replay", {{"Iteration", Any(it)}});
                else dispatch("Replay");
            }, "expected", "Expected values", cmd("Expected"));
        } else if (group_ == "Results") {
            buttons2("store", "Store results", cmd("StoreResults"), "save", "Close and save", cmd("SaveResults"));
            buttons2("hist", "Change histogram", cmd("ChangeHistogram"), "variant", "Select variant", cmd("SelectVariant"));
        } else {   // Other
            buttons2("sortasc", "Sort asc", cmd("SortAsc"), "sortdesc", "Sort des", cmd("SortDesc"));
            buttons2("forecast", "Forecast-Tool", cmd("ForecastTool"), "batch", "Start Batch", cmd("StartBatch"));
            buttons2("hide", "Hide formulas", cmd("HideFormulas"), "load", "Load formulas", cmd("LoadFormulas"));
            buttons2("preview", "Preview", cmd("Preview"), "vars", "Variables", cmd("Variables"));
        }
        height_ = std::max(height_, y_ + 2);
    }
};

std::string controlName(const Reference<uno::XInterface>& src) {
    Reference<awt::XControl> c(src, UNO_QUERY);
    OUString n;
    if (c.is()) {
        Reference<beans::XPropertySet> m(c->getModel(), UNO_QUERY);
        if (m.is()) m->getPropertyValue("Name") >>= n;
    }
    return u8(n);
}

void PanelListener::actionPerformed(const awt::ActionEvent& e) { if (p_) p_->action(u8(e.ActionCommand)); }
void PanelListener::itemStateChanged(const awt::ItemEvent& e) { if (p_) p_->item(controlName(e.Source)); }
void PanelListener::textChanged(const awt::TextEvent& e) { if (p_) p_->item(controlName(e.Source)); }
void PanelListener::mouseEntered(const awt::MouseEvent&) { if (p_) p_->refresh(); }
void PanelListener::focusGained(const awt::FocusEvent&) { if (p_) p_->refresh(); }
void PanelListener::windowResized(const awt::WindowEvent& e) { if (p_) p_->resized(e.Width); }

} // namespace

// ---------------------------------------------------------------- factory

Reference<ui::XUIElement> SAL_CALL PanelFactory::createUIElement(const OUString& url, const Sequence<beans::PropertyValue>& args) {
    Reference<frame::XFrame> frame;
    Reference<awt::XWindow> parent;
    for (const auto& a : args) {
        if (a.Name == "Frame") a.Value >>= frame;
        else if (a.Name == "ParentWindow") a.Value >>= parent;
    }
    if (!parent.is()) throw lang::IllegalArgumentException("no parent window", static_cast<cppu::OWeakObject*>(this), 1);
    std::string u = u8(url);
    std::string group = u.substr(u.rfind('/') + 1);
    return new Panel(ctx_, frame, parent, url, group);
}

OUString SAL_CALL PanelFactory::getImplementationName() { return OUString::createFromAscii(kPanelFactoryImplName); }
sal_Bool SAL_CALL PanelFactory::supportsService(const OUString& s) { return cppu::supportsService(this, s); }
Sequence<OUString> SAL_CALL PanelFactory::getSupportedServiceNames() { return serviceNames(); }
Sequence<OUString> PanelFactory::serviceNames() { return seq<OUString>({OUString("com.sun.star.ui.UIElementFactory")}); }

} // namespace montecalc
