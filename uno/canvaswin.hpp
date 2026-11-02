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
    int panelX() const { return width_ - panelW_ + 4; }
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

    void repaint();
    void close();
    bool closed() const { return closed_; }
    void canvasSize(int& w, int& h);
    // popup menu at a canvas position; returns the chosen item id (0 = none)
    int popup(int x, int y, const std::vector<std::pair<int, std::string>>& items);
    void message(const std::string& text, bool error = false);
    std::string svg(int w, int h, double scale = 1.0);   // canvas content as SVG
    bool exportImage(const std::string& url, const std::string& mime, int w, int h, double scale = 1.0);  // png / jpeg / bmp
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
    std::vector<std::string> buttons_, lists_, checks_;
    int width_, height_, panelW_;
    bool closed_ = false;
    bool dragging_ = false;
    css::uno::Reference<css::beans::XPropertySet> addModel(const char* type, const std::string& name, int x, int y, int w, int h);
    css::uno::Reference<css::awt::XControl> control(const std::string& name);
    int panelPixels();
};

} // namespace montecalc
