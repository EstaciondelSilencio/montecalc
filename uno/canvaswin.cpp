// MonteCalc - modeless window with a painted canvas
#include "canvaswin.hpp"
#include "ui.hpp"
#include "util.hpp"

#include <com/sun/star/awt/InvalidateStyle.hpp>
#include <com/sun/star/awt/KeyModifier.hpp>
#include <com/sun/star/awt/MouseButton.hpp>
#include <com/sun/star/awt/PopupMenu.hpp>
#include <com/sun/star/awt/PopupMenuDirection.hpp>
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
#include <cppuhelper/implbase8.hxx>

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
    : public cppu::WeakImplHelper8<awt::XPaintListener, awt::XMouseListener, awt::XMouseMotionListener, awt::XKeyListener,
                                   awt::XActionListener, awt::XItemListener, awt::XTopWindowListener, awt::XWindowListener> {
public:
    explicit WindowListener(CanvasWindow* w) : w_(w) {}
    void detach() { w_ = nullptr; }

    void SAL_CALL windowPaint(const awt::PaintEvent&) override {
        if (!w_ || w_->closed_) return;
        try {
            Reference<awt::XDevice> dev(w_->dialog_->getPeer(), UNO_QUERY);
            if (!dev.is()) return;
            Reference<awt::XGraphics> g = dev->createGraphics();
            int cw, ch;
            w_->canvasSize(cw, ch);
            g->intersectClipRegion(nullptr);
            ScreenPainter p(g);
            w_->paint(p, cw, ch);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[MonteCalc] paint error: %s\n", e.what());
        } catch (const uno::Exception& e) {
            std::fprintf(stderr, "[MonteCalc] paint error: %s\n", u8(e.Message).c_str());
        }
    }
    static MouseInfo info(const awt::MouseEvent& e) {
        return {e.X, e.Y, (e.Buttons & awt::MouseButton::LEFT) != 0, (e.Buttons & awt::MouseButton::RIGHT) != 0 || e.PopupTrigger,
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
        safe([&] { w_->key(e.KeyCode, e.KeyChar, (e.Modifiers & awt::KeyModifier::MOD1) != 0, (e.Modifiers & awt::KeyModifier::SHIFT) != 0); });
    }
    void SAL_CALL keyReleased(const awt::KeyEvent&) override {}
    void SAL_CALL actionPerformed(const awt::ActionEvent& e) override {
        std::string cmd = u8(e.ActionCommand);
        safe([&] { w_->button(cmd); });
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
    void SAL_CALL windowResized(const awt::WindowEvent&) override { if (w_) w_->repaint(); }
    void SAL_CALL windowMoved(const awt::WindowEvent&) override {}
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
    dp->setPropertyValue("Title", Any(ou(title)));
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
    return m;
}

void CanvasWindow::addButton(const std::string& name, const std::string& label, int x, int y, int w, int h) {
    addModel("com.sun.star.awt.UnoControlButtonModel", name, x, y, w, h)->setPropertyValue("Label", Any(ou(label)));
    buttons_.push_back(name);
}

void CanvasWindow::addLabel(const std::string& name, const std::string& text, int x, int y, int w, int h) {
    auto m = addModel("com.sun.star.awt.UnoControlFixedTextModel", name, x, y, w, h);
    m->setPropertyValue("Label", Any(ou(text)));
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
    for (size_t i = 0; i < items.size(); ++i) s.getArray()[i] = ou(items[i]);
    m->setPropertyValue("StringItemList", Any(s));
    m->setPropertyValue("Dropdown", Any(dropdown));
    m->setPropertyValue("MultiSelection", Any(multi));
    if (!items.empty() && !multi) m->setPropertyValue("SelectedItems", Any(seq<sal_Int16>({0})));
    lists_.push_back(name);
}

void CanvasWindow::addCheck(const std::string& name, const std::string& label, int x, int y, int w, bool on) {
    auto m = addModel("com.sun.star.awt.UnoControlCheckBoxModel", name, x, y, w, 12);
    m->setPropertyValue("Label", Any(ou(label)));
    m->setPropertyValue("State", Any(sal_Int16(on ? 1 : 0)));
    checks_.push_back(name);
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
    // keyboard shortcuts also when a control has the focus
    for (const auto& c : cc->getControls()) Reference<awt::XWindow>(c, UNO_QUERY_THROW)->addKeyListener(l);
    w->setVisible(true);
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
    Reference<beans::XPropertySet>(control(name)->getModel(), UNO_QUERY_THROW)->setPropertyValue("Label", Any(ou(t)));
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
    return u8(Reference<awt::XListBox>(control(name), UNO_QUERY_THROW)->getSelectedItem());
}

void CanvasWindow::setItems(const std::string& name, const std::vector<std::string>& items, int sel) {
    Sequence<OUString> s(sal_Int32(items.size()));
    for (size_t i = 0; i < items.size(); ++i) s.getArray()[i] = ou(items[i]);
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
    w = std::max(50, int(r.Width) - panelPixels());
    h = std::max(50, int(r.Height));
}

void CanvasWindow::repaint() {
    if (closed_ || !dialog_.is()) return;
    Reference<awt::XWindowPeer> peer(dialog_->getPeer(), UNO_QUERY);
    if (!peer.is()) return;
    int w, h;
    canvasSize(w, h);
    peer->invalidateRect(awt::Rectangle(0, 0, w, h), awt::InvalidateStyle::NOCHILDREN);
}

void CanvasWindow::close() {
    if (closed_) return;
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
        else menu->insertItem(sal_Int16(id), ou(text), 0, pos++);
    }
    Reference<awt::XWindowPeer> peer(dialog_->getPeer(), UNO_QUERY_THROW);
    return menu->execute(peer, awt::Rectangle(x, y, 1, 1), awt::PopupMenuDirection::EXECUTE_DEFAULT);
}

void CanvasWindow::message(const std::string& text, bool error) {
    std::fprintf(stderr, "[MonteCalc] %s\n", text.c_str());
    try {
        showMessage(ctx_, frame_, "MonteCalc", text, error);
    } catch (...) {
    }
}

std::string CanvasWindow::svg(int w, int h, double scale) {
    SvgPainter p(w, h, scale);
    paint(p, w, h);
    return p.svg();
}

bool CanvasWindow::exportImage(const std::string& url, const std::string& mime, int w, int h, double scale) {
    // draw with the screen painter on an off-screen device (identical to the window)
    int W = int(w * scale), H = int(h * scale);
    Reference<awt::XToolkit> tk(awt::Toolkit::create(ctx_), UNO_QUERY_THROW);
    Reference<awt::XDevice> dev = tk->createScreenCompatibleDevice(W, H);
    if (!dev.is()) return false;
    Reference<awt::XGraphics> g = dev->createGraphics();
    {
        ScreenPainter p(g);
        Color white = col::white;
        p.rect(0, 0, W, H, &white, nullptr);
        paint(p, W, H);
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
    std::string base = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/montecalc_comment";
    int w = 640, h = 400;
    if (!exportImage("file://" + base + ".png", "image/png", w, h)) return false;
    Reference<graphic::XGraphicProvider> gp = graphic::GraphicProvider::create(ctx_);
    beans::PropertyValue url;
    url.Name = "URL";
    url.Value <<= ou("file://" + base + ".png");
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
