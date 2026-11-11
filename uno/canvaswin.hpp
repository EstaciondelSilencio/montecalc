// MonteCalc - modeless window with a painted canvas (left) and a control panel (right)
#pragma once

#include "painter.hpp"

#include <com/sun/star/awt/XControl.hpp>
#include <com/sun/star/awt/XControlContainer.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace montecalc {

namespace css = com::sun::star;

struct MouseInfo {
    int x, y;
    bool left, right;
    int clicks;
    bool ctrl, shift;
};

class CanvasWindow {
public:
    CanvasWindow(const css::uno::Reference<css::uno::XComponentContext>& ctx, const css::uno::Reference<css::frame::XFrame>& frame,
                 const std::string& title, int width, int height, int panelWidth);
    virtual ~CanvasWindow();

    // building (dialog units, panel-relative x for the panel helpers)
    void addButton(const std::string& name, const std::string& label, int x, int y, int w, int h = 14);
    void addLabel(const std::string& name, const std::string& text, int x, int y, int w, int h = 10);
    void addEdit(const std::string& name, const std::string& text, int x, int y, int w, int h = 12, bool multiLine = false,
                 bool password = false, bool readOnly = false);
    void addList(const std::string& name, const std::vector<std::string>& items, int x, int y, int w, int h, bool dropdown,
                 bool multi = false);
    void addCheck(const std::string& name, const std::string& label, int x, int y, int w, bool checked);
    // horizontal slider (scroll bar) with integer values min..max; changes call itemChanged(name)
    void addSlider(const std::string& name, int x, int y, int w, int min, int max, int value);
    // edit fields registered here call itemChanged(name) on every text change
    void watchText(const std::string& name) { watched_.push_back(name); }
    int panelX() const { return width_ - panelW_ + 4; }
    // Resize behaviour of a control (default: controls in the right panel follow the right edge,
    // all others stay put). Combine: AnchorRight moves with the right edge, StretchW grows with
    // the width, AnchorBottom moves with the bottom edge, StretchH grows with the height.
    enum Anchor { AnchorLeft = 0, AnchorRight = 1, StretchW = 2, AnchorBottom = 4, StretchH = 8 };
    void setAnchor(const std::string& name, int flags) { anchors_[name] = flags; }
    // false for windows without a painted canvas (the AI mentor): nothing is drawn behind the controls
    virtual bool hasCanvas() const { return true; }
    int panelW() const { return panelW_ - 8; }
    void show();   // creates the peer and shows the window (modeless)

    // control access
    std::string text(const std::string& name);
    void setText(const std::string& name, const std::string& t);
    void setLabel(const std::string& name, const std::string& t);
    int selected(const std::string& name);
    std::vector<int> selectedAll(const std::string& name);
    std::string selectedItem(const std::string& name);
    void setItems(const std::string& name, const std::vector<std::string>& items, int select = -1);
    void select(const std::string& name, int index);
    bool checked(const std::string& name);
    void setChecked(const std::string& name, bool on);
    void setEnabled(const std::string& name, bool on);
    void setVisible(const std::string& name, bool on);
    int sliderValue(const std::string& name);
    void setSliderValue(const std::string& name, int v);
    void setTextColor(const std::string& name, sal_Int32 rgb);

    void repaint();
    void close();
    bool closed() const { return closed_; }
    // canvas size in drawing units (pixels divided by the zoom)
    void canvasSize(int& w, int& h);
    // zoom of the canvas (text, shapes and mouse coordinates scale together); Ctrl + / Ctrl - / Ctrl 0
    double zoom() const { return zoom_; }
    void setZoom(double z);
    static double& defaultZoom();   // shared by all windows, remembered for the next one
    // popup menu at a canvas position; returns the chosen item id (0 = none)
    int popup(int x, int y, const std::vector<std::pair<int, std::string>>& items);
    void message(const std::string& text, bool error = false);
    std::string svg(int w, int h, double scale = 1.0);   // canvas content as SVG
    // png / jpeg / bmp; w, h in drawing units, the image has w*h*scale*zoom pixels
    bool exportImage(const std::string& url, const std::string& mime, int w, int h, double scale = 1.0);
    // store the canvas as background image of a cell comment (manual: "store the window in the cell")
    bool storeAsComment(const css::uno::Reference<css::frame::XModel>& model, int sheet, int col, int row, const std::string& text);

    // events
    virtual void paint(Painter& p, int w, int h) = 0;
    virtual void mouseDown(const MouseInfo&) {}
    virtual void mouseUp(const MouseInfo&) {}
    virtual void mouseMove(const MouseInfo&, bool /*dragging*/) {}
    virtual void mouseExit() {}
    virtual void button(const std::string& /*name*/) {}
    virtual void itemChanged(const std::string& /*name*/) {}
    virtual bool key(int /*code*/, int /*ch*/, bool /*ctrl*/, bool /*shift*/) { return false; }

    // keeps the window object alive until it is closed
    static void keep(std::shared_ptr<CanvasWindow> w);

    css::uno::Reference<css::uno::XComponentContext> ctx_;
    css::uno::Reference<css::frame::XFrame> frame_;

private:
    friend class WindowListener;
    css::uno::Reference<css::lang::XMultiServiceFactory> model_;
    css::uno::Reference<css::container::XNameContainer> container_;
    css::uno::Reference<css::awt::XControl> dialog_;
    css::uno::Reference<css::uno::XInterface> listener_;
    std::vector<std::string> buttons_, lists_, checks_, sliders_, watched_;
    struct Rect { int x, y, w, h; };
    std::vector<std::pair<std::string, Rect>> rects_;   // original control rectangles (dialog units)
    std::map<std::string, int> anchors_;
    std::map<std::string, std::vector<std::string>> items_;   // English list entries
    std::string title_;
    int baseWpx_ = 0, baseHpx_ = 0;   // window size the layout was designed for (pixels)
    bool relayouting_ = false;
    void relayout();
    void rememberGeometry();
    std::string geometryKey() const;
    int width_, height_, panelW_;
    double zoom_ = defaultZoom();
    bool exporting_ = false;
    bool closed_ = false;
    bool dragging_ = false;
    css::uno::Reference<css::beans::XPropertySet> addModel(const char* type, const std::string& name, int x, int y, int w, int h);
    css::uno::Reference<css::awt::XControl> control(const std::string& name);
    int panelPixels();
};

} // namespace montecalc
