// MonteCalc - modeless window with a painted canvas
#include "canvaswin.hpp"
#include "i18n.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <com/sun/star/awt/InvalidateStyle.hpp>
#include <com/sun/star/awt/Key.hpp>
#include <com/sun/star/awt/KeyModifier.hpp>
#include <com/sun/star/awt/MouseButton.hpp>
#include <com/sun/star/awt/PopupMenu.hpp>
#include <com/sun/star/awt/PopupMenuDirection.hpp>
#include <com/sun/star/awt/PosSize.hpp>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/awt/XActionListener.hpp>
#include <com/sun/star/awt/XButton.hpp>
#include <com/sun/star/awt/XCheckBox.hpp>
#include <com/sun/star/awt/XBitmap.hpp>
#include <com/sun/star/awt/XDevice.hpp>
#include <com/sun/star/awt/XItemListener.hpp>
#include <com/sun/star/awt/XKeyListener.hpp>
#include <com/sun/star/awt/XListBox.hpp>
#include <com/sun/star/awt/XMouseListener.hpp>
#include <com/sun/star/awt/XMouseMotionListener.hpp>
#include <com/sun/star/awt/XPaintListener.hpp>
#include <com/sun/star/awt/XTextComponent.hpp>
#include <com/sun/star/awt/XToolkit.hpp>
#include <com/sun/star/awt/XTopWindow.hpp>
#include <com/sun/star/awt/XTopWindowListener.hpp>
#include <com/sun/star/awt/XUnitConversion.hpp>
#include <com/sun/star/awt/XWindow.hpp>
#include <com/sun/star/awt/XWindowListener.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/graphic/GraphicProvider.hpp>
#include <com/sun/star/graphic/XGraphicProvider.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/sheet/XSheetAnnotationAnchor.hpp>
#include <com/sun/star/sheet/XSheetAnnotationShapeSupplier.hpp>
#include <com/sun/star/sheet/XSheetAnnotationsSupplier.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/table/CellAddress.hpp>
#include <com/sun/star/util/MeasureUnit.hpp>
#include <com/sun/star/awt/ScrollBarOrientation.hpp>
#include <com/sun/star/awt/XAdjustmentListener.hpp>
#include <com/sun/star/awt/XScrollBar.hpp>
#include <com/sun/star/awt/XTextListener.hpp>
#include <cppuhelper/implbase10.hxx>

#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

// one UNO object receiving every event of a window
class WindowListener
    : public cppu::WeakImplHelper10<awt::XPaintListener, awt::XMouseListener, awt::XMouseMotionListener, awt::XKeyListener,
                                    awt::XActionListener, awt::XItemListener, awt::XTopWindowListener, awt::XWindowListener,
                                    awt::XAdjustmentListener, awt::XTextListener> {
public:
    explicit WindowListener(CanvasWindow* w) : w_(w) {}
    void detach() { w_ = nullptr; }
    Reference<awt::XToolkit> tk_;

    void SAL_CALL windowPaint(const awt::PaintEvent&) override {
        if (!w_ || w_->closed_) return;
        try {
            if (!w_->hasCanvas()) return;
            Reference<awt::XDevice> dev(w_->dialog_->getPeer(), UNO_QUERY);
            if (!dev.is()) return;
            Reference<awt::XGraphics> g = dev->createGraphics();
            int cw, ch;
            w_->canvasSize(cw, ch);
            awt::Rectangle r = Reference<awt::XWindow>(w_->dialog_, UNO_QUERY_THROW)->getPosSize();
            int pw = std::max(50, int(r.Width) - w_->panelPixels()), ph = std::max(50, int(r.Height));
            // draw off screen and copy in one step: no flicker and no half-drawn charts while resizing
            if (!tk_.is()) tk_.set(awt::Toolkit::create(w_->ctx_), UNO_QUERY);
            Reference<awt::XDevice> buf = tk_.is() ? tk_->createScreenCompatibleDevice(pw, ph) : Reference<awt::XDevice>();
            Reference<awt::XGraphics> bg = buf.is() ? buf->createGraphics() : Reference<awt::XGraphics>();
            Reference<awt::XGraphics> target = bg.is() ? bg : g;
            target->intersectClipRegion(nullptr);
            {
                ScreenPainter sp(target);
                Color white = col::white;
                sp.rect(0, 0, pw, ph, &white, nullptr);
                ZoomPainter p(sp, w_->zoom_);
                w_->paint(p, cw, ch);
            }
            if (bg.is()) g->copy(buf, 0, 0, pw, ph, 0, 0, pw, ph);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[MonteCalc] paint error: %s\n", e.what());
        } catch (const uno::Exception& e) {
            std::fprintf(stderr, "[MonteCalc] paint error: %s\n", u8(e.Message).c_str());
        }
    }
    MouseInfo info(const awt::MouseEvent& e) const {
        double z = w_ ? w_->zoom_ : 1.0;
        return {int(e.X / z), int(e.Y / z), (e.Buttons & awt::MouseButton::LEFT) != 0, (e.Buttons & awt::MouseButton::RIGHT) != 0 || e.PopupTrigger,
                e.ClickCount, (e.Modifiers & awt::KeyModifier::MOD1) != 0, (e.Modifiers & awt::KeyModifier::SHIFT) != 0};
    }
    template <class F>
    void safe(F f) {
        if (!w_ || w_->closed_) return;
        try {
            f();
        } catch (const std::exception& e) {
            w_->message(e.what(), true);
        } catch (const uno::Exception& e) {
            w_->message(u8(e.Message), true);
        }
    }
    void SAL_CALL mousePressed(const awt::MouseEvent& e) override {
        safe([&] { w_->dragging_ = true; w_->mouseDown(info(e)); });
    }
    void SAL_CALL mouseReleased(const awt::MouseEvent& e) override {
        safe([&] { w_->dragging_ = false; w_->mouseUp(info(e)); });
    }
    void SAL_CALL mouseEntered(const awt::MouseEvent&) override {}
    void SAL_CALL mouseExited(const awt::MouseEvent&) override { safe([&] { w_->mouseExit(); }); }
    void SAL_CALL mouseDragged(const awt::MouseEvent& e) override { safe([&] { w_->mouseMove(info(e), true); }); }
    void SAL_CALL mouseMoved(const awt::MouseEvent& e) override { safe([&] { w_->mouseMove(info(e), false); }); }
    void SAL_CALL keyPressed(const awt::KeyEvent& e) override {
        bool ctrl = (e.Modifiers & awt::KeyModifier::MOD1) != 0;
        if (ctrl && w_ && (e.KeyCode == awt::Key::ADD || e.KeyCode == awt::Key::SUBTRACT || e.KeyCode == awt::Key::NUM0)) {
            safe([&] { w_->setZoom(e.KeyCode == awt::Key::NUM0 ? 1.0 : e.KeyCode == awt::Key::ADD ? w_->zoom_ * 1.15 : w_->zoom_ / 1.15); });
            return;
        }
        safe([&] { w_->key(e.KeyCode, e.KeyChar, (e.Modifiers & awt::KeyModifier::MOD1) != 0, (e.Modifiers & awt::KeyModifier::SHIFT) != 0); });
    }
    void SAL_CALL keyReleased(const awt::KeyEvent&) override {}
    void SAL_CALL actionPerformed(const awt::ActionEvent& e) override {
        std::string cmd = u8(e.ActionCommand);
        safe([&] { w_->button(cmd); });
    }
    static std::string sourceName(const uno::Reference<uno::XInterface>& src) {
        Reference<awt::XControl> c(src, UNO_QUERY);
        OUString n;
        if (c.is()) {
            Reference<beans::XPropertySet> m(c->getModel(), UNO_QUERY);
            if (m.is()) m->getPropertyValue("Name") >>= n;
        }
        return u8(n);
    }
    void SAL_CALL adjustmentValueChanged(const awt::AdjustmentEvent& e) override {
        std::string name = sourceName(e.Source);
        safe([&] { w_->itemChanged(name); });
    }
    void SAL_CALL textChanged(const awt::TextEvent& e) override {
        std::string name = sourceName(e.Source);
        safe([&] { w_->itemChanged(name); });
    }
    void SAL_CALL itemStateChanged(const awt::ItemEvent& e) override {
        Reference<awt::XControl> c(e.Source, UNO_QUERY);
        std::string name;
        if (c.is()) {
            Reference<beans::XPropertySet> m(c->getModel(), UNO_QUERY);
            OUString n;
            if (m.is()) m->getPropertyValue("Name") >>= n;
            name = u8(n);
        }
        safe([&] { w_->itemChanged(name); });
    }
    void SAL_CALL windowOpened(const lang::EventObject&) override {}
    void SAL_CALL windowClosing(const lang::EventObject&) override {
        if (w_) w_->close();
    }
    void SAL_CALL windowClosed(const lang::EventObject&) override {}
    void SAL_CALL windowMinimized(const lang::EventObject&) override {}
    void SAL_CALL windowNormalized(const lang::EventObject&) override {}
    void SAL_CALL windowActivated(const lang::EventObject&) override {}
    void SAL_CALL windowDeactivated(const lang::EventObject&) override {}
    void SAL_CALL windowResized(const awt::WindowEvent&) override {
        if (!w_ || w_->closed_) return;
        safe([&] { w_->relayout(); w_->repaint(); });
    }
    void SAL_CALL windowMoved(const awt::WindowEvent&) override { if (w_ && !w_->closed_) w_->rememberGeometry(); }
    void SAL_CALL windowShown(const lang::EventObject&) override {}
    void SAL_CALL windowHidden(const lang::EventObject&) override {}
    void SAL_CALL disposing(const lang::EventObject&) override {}

private:
    CanvasWindow* w_;
};

namespace {
std::mutex g_mtx;
std::set<std::shared_ptr<CanvasWindow>>& registry() {
    static std::set<std::shared_ptr<CanvasWindow>> r;
    return r;
}
} // namespace

void CanvasWindow::keep(std::shared_ptr<CanvasWindow> w) {
    std::lock_guard<std::mutex> g(g_mtx);
    auto& r = registry();
    for (auto it = r.begin(); it != r.end();) it = (*it)->closed() ? r.erase(it) : std::next(it);
    r.insert(w);
}

CanvasWindow::CanvasWindow(const Reference<uno::XComponentContext>& ctx, const Reference<frame::XFrame>& frame, const std::string& title,
                           int width, int height, int panelWidth)
    : ctx_(ctx), frame_(frame), width_(width), height_(height), panelW_(panelWidth) {
    auto smgr = ctx->getServiceManager();
    model_.set(smgr->createInstanceWithContext("com.sun.star.awt.UnoControlDialogModel", ctx), UNO_QUERY_THROW);
    Reference<beans::XPropertySet> dp(model_, UNO_QUERY_THROW);
    dp->setPropertyValue("PositionX", Any(sal_Int32(40)));
    dp->setPropertyValue("PositionY", Any(sal_Int32(30)));
    dp->setPropertyValue("Width", Any(sal_Int32(width)));
    dp->setPropertyValue("Height", Any(sal_Int32(height)));
    i18nSetContext(ctx);
    dp->setPropertyValue("Title", Any(ou(tr(title))));
    title_ = title;
    dp->setPropertyValue("Moveable", Any(true));
    dp->setPropertyValue("Closeable", Any(true));
    dp->setPropertyValue("Sizeable", Any(true));
    dp->setPropertyValue("BackgroundColor", Any(col::panel.rgb()));
    container_.set(model_, UNO_QUERY_THROW);
}

CanvasWindow::~CanvasWindow() {
    if (auto* l = dynamic_cast<WindowListener*>(listener_.get())) l->detach();
}

Reference<beans::XPropertySet> CanvasWindow::addModel(const char* type, const std::string& name, int x, int y, int w, int h) {
    Reference<beans::XPropertySet> m(model_->createInstance(OUString::createFromAscii(type)), UNO_QUERY_THROW);
    m->setPropertyValue("PositionX", Any(sal_Int32(x)));
    m->setPropertyValue("PositionY", Any(sal_Int32(y)));
    m->setPropertyValue("Width", Any(sal_Int32(w)));
    m->setPropertyValue("Height", Any(sal_Int32(h)));
    m->setPropertyValue("Name", Any(ou(name)));
    container_->insertByName(ou(name), Any(m));
    rects_.push_back({name, {x, y, w, h}});
    return m;
}

void CanvasWindow::addButton(const std::string& name, const std::string& label, int x, int y, int w, int h) {
    addModel("com.sun.star.awt.UnoControlButtonModel", name, x, y, w, h)->setPropertyValue("Label", Any(ou(tr(label))));
    buttons_.push_back(name);
}

void CanvasWindow::addLabel(const std::string& name, const std::string& text, int x, int y, int w, int h) {
    auto m = addModel("com.sun.star.awt.UnoControlFixedTextModel", name, x, y, w, h);
    m->setPropertyValue("Label", Any(ou(tr(text))));
    m->setPropertyValue("MultiLine", Any(h > 12));
}

void CanvasWindow::addEdit(const std::string& name, const std::string& text, int x, int y, int w, int h, bool multi, bool password, bool ro) {
    auto m = addModel("com.sun.star.awt.UnoControlEditModel", name, x, y, w, h);
    m->setPropertyValue("Text", Any(ou(text)));
    if (multi) {
        m->setPropertyValue("MultiLine", Any(true));
        m->setPropertyValue("VScroll", Any(true));
        m->setPropertyValue("AutoVScroll", Any(true));
    }
    if (password) m->setPropertyValue("EchoChar", Any(sal_Int16('*')));
    if (ro) m->setPropertyValue("ReadOnly", Any(true));
}

void CanvasWindow::addList(const std::string& name, const std::vector<std::string>& items, int x, int y, int w, int h, bool dropdown, bool multi) {
    auto m = addModel("com.sun.star.awt.UnoControlListBoxModel", name, x, y, w, h);
    Sequence<OUString> s(sal_Int32(items.size()));
    for (size_t i = 0; i < items.size(); ++i) s.getArray()[i] = ou(trExact(items[i]));
    items_[name] = items;
    m->setPropertyValue("StringItemList", Any(s));
    m->setPropertyValue("Dropdown", Any(dropdown));
    m->setPropertyValue("MultiSelection", Any(multi));
    if (!items.empty() && !multi) m->setPropertyValue("SelectedItems", Any(seq<sal_Int16>({0})));
    lists_.push_back(name);
}

void CanvasWindow::addCheck(const std::string& name, const std::string& label, int x, int y, int w, bool on) {
    auto m = addModel("com.sun.star.awt.UnoControlCheckBoxModel", name, x, y, w, 12);
    m->setPropertyValue("Label", Any(ou(tr(label))));
    m->setPropertyValue("State", Any(sal_Int16(on ? 1 : 0)));
    checks_.push_back(name);
}

void CanvasWindow::addSlider(const std::string& name, int x, int y, int w, int min, int max, int value) {
    auto m = addModel("com.sun.star.awt.UnoControlScrollBarModel", name, x, y, w, 10);
    m->setPropertyValue("Orientation", Any(sal_Int32(awt::ScrollBarOrientation::HORIZONTAL)));
    m->setPropertyValue("ScrollValueMin", Any(sal_Int32(min)));
    m->setPropertyValue("ScrollValueMax", Any(sal_Int32(max)));
    m->setPropertyValue("ScrollValue", Any(sal_Int32(value)));
    m->setPropertyValue("LineIncrement", Any(sal_Int32(1)));
    m->setPropertyValue("BlockIncrement", Any(sal_Int32(10)));
    m->setPropertyValue("VisibleSize", Any(sal_Int32(1)));
    sliders_.push_back(name);
}

int CanvasWindow::sliderValue(const std::string& name) {
    return Reference<awt::XScrollBar>(control(name), UNO_QUERY_THROW)->getValue();
}

void CanvasWindow::setSliderValue(const std::string& name, int v) {
    Reference<awt::XScrollBar>(control(name), UNO_QUERY_THROW)->setValue(v);
}

void CanvasWindow::setTextColor(const std::string& name, sal_Int32 rgb) {
    Reference<beans::XPropertySet>(control(name)->getModel(), UNO_QUERY_THROW)->setPropertyValue("TextColor", Any(rgb));
}

std::map<std::string, awt::Rectangle>& geometries() {
    static std::map<std::string, awt::Rectangle> g;
    return g;
}

void CanvasWindow::show() {
    auto smgr = ctx_->getServiceManager();
    dialog_.set(smgr->createInstanceWithContext("com.sun.star.awt.UnoControlDialog", ctx_), UNO_QUERY_THROW);
    dialog_->setModel(Reference<awt::XControlModel>(model_, UNO_QUERY_THROW));
    Reference<awt::XWindowPeer> parent;
    if (frame_.is()) parent.set(frame_->getContainerWindow(), UNO_QUERY);
    dialog_->createPeer(Reference<awt::XToolkit>(awt::Toolkit::create(ctx_), UNO_QUERY_THROW), parent);
    auto* l = new WindowListener(this);
    listener_ = static_cast<cppu::OWeakObject*>(l);
    Reference<awt::XWindow> w(dialog_, UNO_QUERY_THROW);
    w->addPaintListener(l);
    w->addMouseListener(l);
    w->addMouseMotionListener(l);
    w->addKeyListener(l);
    w->addWindowListener(l);
    Reference<awt::XTopWindow> top(dialog_->getPeer(), UNO_QUERY);
    if (top.is()) top->addTopWindowListener(l);
    Reference<awt::XControlContainer> cc(dialog_, UNO_QUERY_THROW);
    for (auto& b : buttons_) {
        Reference<awt::XButton> btn(cc->getControl(ou(b)), UNO_QUERY);
        if (btn.is()) { btn->setActionCommand(ou(b)); btn->addActionListener(l); }
    }
    for (auto& n : lists_) {
        Reference<awt::XListBox> lb(cc->getControl(ou(n)), UNO_QUERY);
        if (lb.is()) lb->addItemListener(l);
    }
    for (auto& n : checks_) {
        Reference<awt::XCheckBox> cb(cc->getControl(ou(n)), UNO_QUERY);
        if (cb.is()) cb->addItemListener(l);
    }
    for (auto& n : sliders_) {
        Reference<awt::XScrollBar> sb(cc->getControl(ou(n)), UNO_QUERY);
        if (sb.is()) sb->addAdjustmentListener(l);
    }
    for (auto& n : watched_) {
        Reference<awt::XTextComponent> t(cc->getControl(ou(n)), UNO_QUERY);
        if (t.is()) t->addTextListener(l);
    }
    // keyboard shortcuts also when a control has the focus
    for (const auto& c : cc->getControls()) Reference<awt::XWindow>(c, UNO_QUERY_THROW)->addKeyListener(l);
    awt::Rectangle r0 = w->getPosSize();
    baseWpx_ = r0.Width;
    baseHpx_ = r0.Height;
    // reopen with the size and position the user gave this kind of window last time
    auto& geo = geometries();
    auto it = geo.find(geometryKey());
    if (it != geo.end()) {
        relayouting_ = true;
        w->setPosSize(it->second.X, it->second.Y, it->second.Width, it->second.Height, awt::PosSize::POSSIZE);
        relayouting_ = false;
        relayout();
    }
    for (const auto& c : cc->getControls()) fitControlText(c);   // longer translations
    w->setVisible(true);
}

std::string CanvasWindow::geometryKey() const {
    // one entry per kind of window ("... into D13" etc. ignored)
    std::string k = title_;
    for (const char* cut : {" into ", " - Preview", " - Formula"}) {
        size_t p = k.find(cut);
        if (p != std::string::npos) k = k.substr(0, p);
    }
    return k;
}

void CanvasWindow::rememberGeometry() {
    if (!dialog_.is() || relayouting_) return;
    try {
        geometries()[geometryKey()] = Reference<awt::XWindow>(dialog_, UNO_QUERY_THROW)->getPosSize();
    } catch (...) {
    }
}

// Moves / stretches the controls after a resize, following their anchors. Positions are set on
// the control models (dialog units), so the layout stays right on any screen scaling.
void CanvasWindow::relayout() {
    if (relayouting_ || !dialog_.is() || baseWpx_ <= 0) return;
    Reference<awt::XWindow> w(dialog_, UNO_QUERY_THROW);
    awt::Rectangle r = w->getPosSize();
    // minimum size: the control column and a useful canvas stay visible
    // minimum: every control that does not stretch must stay visible
    int needH = 0;
    for (auto& [name, rc] : rects_) {
        auto a = anchors_.find(name);
        int flags = a != anchors_.end() ? a->second : 0;
        needH = std::max(needH, rc.y + ((flags & StretchH) ? 12 : rc.h));   // stretching lists keep 12 units
    }
    Reference<awt::XUnitConversion> uc0(dialog_, UNO_QUERY);
    int needPx = uc0.is() ? uc0->convertSizeToPixel(awt::Size(10, needH + 6), util::MeasureUnit::APPFONT).Height : baseHpx_;
    int minW = std::max(panelPixels() + 240, baseWpx_ * 55 / 100), minH = std::max(240, std::min(baseHpx_, needPx));
    if (r.Width < minW || r.Height < minH) {
        relayouting_ = true;
        w->setPosSize(0, 0, std::max<int>(r.Width, minW), std::max<int>(r.Height, minH), awt::PosSize::SIZE);
        relayouting_ = false;
        r = w->getPosSize();
    }
    Reference<awt::XUnitConversion> uc(dialog_, UNO_QUERY);
    if (!uc.is()) return;
    awt::Size d = uc->convertSizeToLogic(awt::Size(r.Width - baseWpx_, r.Height - baseHpx_), util::MeasureUnit::APPFONT);
    int dW = d.Width, dH = d.Height;
    relayouting_ = true;
    for (auto& [name, rc] : rects_) {
        int flags;
        auto a = anchors_.find(name);
        if (a != anchors_.end()) flags = a->second;
        else flags = rc.x >= width_ - panelW_ ? AnchorRight : AnchorLeft;
        int x = rc.x + ((flags & AnchorRight) ? dW : 0), y = rc.y + ((flags & AnchorBottom) ? dH : 0);
        int ww = rc.w + ((flags & StretchW) ? dW : 0), hh = rc.h + ((flags & StretchH) ? dH : 0);
        try {
            Reference<beans::XPropertySet> m(control(name)->getModel(), UNO_QUERY_THROW);
            m->setPropertyValue("PositionX", Any(sal_Int32(x)));
            m->setPropertyValue("PositionY", Any(sal_Int32(y)));
            m->setPropertyValue("Width", Any(sal_Int32(std::max(8, ww))));
            m->setPropertyValue("Height", Any(sal_Int32(std::max(8, hh))));
        } catch (...) {
        }
    }
    relayouting_ = false;
    rememberGeometry();
}

Reference<awt::XControl> CanvasWindow::control(const std::string& name) {
    Reference<awt::XControlContainer> cc(dialog_, UNO_QUERY_THROW);
    return cc->getControl(ou(name));
}

std::string CanvasWindow::text(const std::string& name) {
    return u8(Reference<awt::XTextComponent>(control(name), UNO_QUERY_THROW)->getText());
}

void CanvasWindow::setText(const std::string& name, const std::string& t) {
    Reference<awt::XTextComponent>(control(name), UNO_QUERY_THROW)->setText(ou(t));
}

void CanvasWindow::setLabel(const std::string& name, const std::string& t) {
    Reference<beans::XPropertySet>(control(name)->getModel(), UNO_QUERY_THROW)->setPropertyValue("Label", Any(ou(tr(t))));
    if (dialog_.is()) fitControlText(control(name));
}

int CanvasWindow::selected(const std::string& name) {
    return Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->getSelectedItemPos();
}

std::vector<int> CanvasWindow::selectedAll(const std::string& name) {
    std::vector<int> r;
    for (auto p : Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->getSelectedItemsPos()) r.push_back(p);
    return r;
}

std::string CanvasWindow::selectedItem(const std::string& name) {
    // the English entry (list entries are shown translated)
    auto it = items_.find(name);
    if (it != items_.end()) {
        int pos = selected(name);
        if (pos >= 0 && pos < int(it->second.size())) return it->second[pos];
    }
    return u8(Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->getSelectedItem());
}

void CanvasWindow::setItems(const std::string& name, const std::vector<std::string>& items, int sel) {
    Sequence<OUString> s(sal_Int32(items.size()));
    for (size_t i = 0; i < items.size(); ++i) s.getArray()[i] = ou(trExact(items[i]));
    items_[name] = items;
    auto m = Reference<beans::XPropertySet>(control(name)->getModel(), UNO_QUERY_THROW);
    m->setPropertyValue("StringItemList", Any(s));
    if (sel >= 0 && sel < int(items.size())) m->setPropertyValue("SelectedItems", Any(seq<sal_Int16>({sal_Int16(sel)})));
}

void CanvasWindow::select(const std::string& name, int index) {
    Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->selectItemPos(sal_Int16(index), true);
}

bool CanvasWindow::checked(const std::string& name) {
    return Reference<awt::XCheckBox>(control(name), UNO_QUERY_THROW)->getState() == 1;
}

void CanvasWindow::setChecked(const std::string& name, bool on) {
    Reference<awt::XCheckBox>(control(name), UNO_QUERY_THROW)->setState(on ? 1 : 0);
}

void CanvasWindow::setEnabled(const std::string& name, bool on) {
    Reference<beans::XPropertySet>(control(name)->getModel(), UNO_QUERY_THROW)->setPropertyValue("Enabled", Any(on));
}

int CanvasWindow::panelPixels() {
    Reference<awt::XUnitConversion> uc(dialog_, UNO_QUERY);
    if (!uc.is()) return panelW_ * 2;
    return uc->convertSizeToPixel(awt::Size(panelW_, 10), util::MeasureUnit::APPFONT).Width;
}

void CanvasWindow::canvasSize(int& w, int& h) {
    awt::Rectangle r = Reference<awt::XWindow>(dialog_, UNO_QUERY_THROW)->getPosSize();
    w = std::max(50, int((int(r.Width) - panelPixels()) / zoom_));
    h = std::max(50, int(int(r.Height) / zoom_));
}

double& CanvasWindow::defaultZoom() {
    static double z = 1.25;
    return z;
}

void CanvasWindow::setZoom(double z) {
    zoom_ = std::min(3.0, std::max(0.6, z));
    defaultZoom() = zoom_;
    repaint();
}

void CanvasWindow::repaint() {
    if (closed_ || !dialog_.is()) return;
    Reference<awt::XWindowPeer> peer(dialog_->getPeer(), UNO_QUERY);
    if (!peer.is()) return;
    awt::Rectangle r = Reference<awt::XWindow>(dialog_, UNO_QUERY_THROW)->getPosSize();
    peer->invalidateRect(awt::Rectangle(0, 0, std::max(50, int(r.Width) - panelPixels()), std::max(50, int(r.Height))),
                         awt::InvalidateStyle::NOCHILDREN | awt::InvalidateStyle::NOERASE);
}

void CanvasWindow::close() {
    if (closed_) return;
    rememberGeometry();
    closed_ = true;
    if (auto* l = dynamic_cast<WindowListener*>(listener_.get())) l->detach();
    try {
        Reference<lang::XComponent>(dialog_, UNO_QUERY_THROW)->dispose();
    } catch (...) {
    }
}

int CanvasWindow::popup(int x, int y, const std::vector<std::pair<int, std::string>>& items) {
    Reference<awt::XPopupMenu> menu = awt::PopupMenu::create(ctx_);
    sal_Int16 pos = 0;
    for (auto& [id, text] : items) {
        if (text.empty()) menu->insertSeparator(pos++);
        else menu->insertItem(sal_Int16(id), ou(tr(text)), 0, pos++);
    }
    Reference<awt::XWindowPeer> peer(dialog_->getPeer(), UNO_QUERY_THROW);
    return menu->execute(peer, awt::Rectangle(int(x * zoom_), int(y * zoom_), 1, 1), awt::PopupMenuDirection::EXECUTE_DEFAULT);
}

void CanvasWindow::message(const std::string& text, bool error) {
    std::fprintf(stderr, "[MonteCalc] %s\n", text.c_str());
    try {
        showMessage(ctx_, frame_, "MonteCalc", text, error);   // translated there
    } catch (...) {
    }
}

std::string CanvasWindow::svg(int w, int h, double scale) {
    SvgPainter sp(w, h, scale);
    ZoomPainter p(sp, 1.0);   // same fonts and translations as on screen
    paint(p, w, h);
    return sp.svg();
}

bool CanvasWindow::exportImage(const std::string& url, const std::string& mime, int w, int h, double scale) {
    // draw with the screen painter on an off-screen device (identical to the window)
    scale *= zoom_;   // the image looks like the window
    int W = int(w * scale), H = int(h * scale);
    Reference<awt::XToolkit> tk(awt::Toolkit::create(ctx_), UNO_QUERY_THROW);
    Reference<awt::XDevice> dev = tk->createScreenCompatibleDevice(W, H);
    if (!dev.is()) return false;
    Reference<awt::XGraphics> g = dev->createGraphics();
    {
        ScreenPainter sp(g);
        Color white = col::white;
        sp.rect(0, 0, W, H, &white, nullptr);
        ZoomPainter p(sp, scale);   // shapes and text scale together
        paint(p, w, h);
    }
    Reference<awt::XBitmap> bmp = dev->createBitmap(0, 0, W, H);
    Reference<graphic::XGraphicProvider> gp = graphic::GraphicProvider::create(ctx_);
    beans::PropertyValue b;
    b.Name = "Bitmap";
    b.Value <<= bmp;
    Reference<graphic::XGraphic> gr = gp->queryGraphic(seq<beans::PropertyValue>({b}));
    if (!gr.is()) return false;
    beans::PropertyValue u, m;
    u.Name = "URL";
    u.Value <<= ou(url);
    m.Name = "MimeType";
    m.Value <<= ou(mime);
    gp->storeGraphic(gr, seq<beans::PropertyValue>({u, m}));
    return true;
}

bool CanvasWindow::storeAsComment(const Reference<frame::XModel>& model, int sheet, int col, int row, const std::string& text) {
    std::string base = tempDir() + "/montecalc_comment";
    int w = 640, h = 400;
    if (!exportImage(fileUrl(base + ".png"), "image/png", w, h)) return false;
    Reference<graphic::XGraphicProvider> gp = graphic::GraphicProvider::create(ctx_);
    beans::PropertyValue url;
    url.Name = "URL";
    url.Value <<= ou(fileUrl(base + ".png"));
    Reference<graphic::XGraphic> g = gp->queryGraphic(seq<beans::PropertyValue>({url}));
    Reference<sheet::XSpreadsheetDocument> doc(model, UNO_QUERY_THROW);
    Reference<container::XIndexAccess> ia(doc->getSheets(), UNO_QUERY_THROW);
    Reference<sheet::XSpreadsheet> sh(ia->getByIndex(sheet), UNO_QUERY_THROW);
    table::CellAddress a(sal_Int16(sheet), col, row);
    auto anns = Reference<sheet::XSheetAnnotationsSupplier>(sh, UNO_QUERY_THROW)->getAnnotations();
    // existing comments are overwritten (as in MC FLO)
    for (sal_Int32 i = anns->getCount() - 1; i >= 0; --i) {
        Reference<sheet::XSheetAnnotation> an(anns->getByIndex(i), UNO_QUERY);
        if (an.is() && an->getPosition().Column == col && an->getPosition().Row == row) anns->removeByIndex(i);
    }
    anns->insertNew(a, ou(text.empty() ? " " : text));
    Reference<sheet::XSheetAnnotationAnchor> anchor(sh->getCellByPosition(col, row), UNO_QUERY_THROW);
    Reference<sheet::XSheetAnnotationShapeSupplier> ss(anchor->getAnnotation(), UNO_QUERY_THROW);
    Reference<drawing::XShape> shape = ss->getAnnotationShape();
    Reference<beans::XPropertySet> sp(shape, UNO_QUERY_THROW);
    sp->setPropertyValue("FillStyle", Any(drawing::FillStyle_BITMAP));
    sp->setPropertyValue("FillBitmap", Any(Reference<awt::XBitmap>(g, UNO_QUERY_THROW)));
    shape->setSize(awt::Size(w * 20, h * 20));  // 1/100 mm
    std::remove((base + ".png").c_str());
    return true;
}

} // namespace montecalc
