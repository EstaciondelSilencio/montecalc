// MonteCalc - the callout wedge between a cell and its result window (see callout.hpp)
#include "callout.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

#include <algorithm>
namespace Gdiplus {   // the GDI+ headers of Visual Studio use min/max, which NOMINMAX removes
using std::max;
using std::min;
}
#include <gdiplus.h>

#include <map>
#include <vector>

namespace montecalc {
namespace callout {
namespace {

struct Entry {
    HWND overlay = nullptr;
    CellRect cell;
    RECT lastCell{}, lastWin{};
    bool shown = false;
};
std::map<HWND, Entry>& entries() {
    static std::map<HWND, Entry> m;
    return m;
}
HWND g_timerWnd = nullptr;
constexpr UINT_PTR kTimer = 1;

HINSTANCE module() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&entries), &m);
    return m;
}

void ensureGdiplus() {
    static ULONG_PTR token = 0;
    if (!token) {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&token, &in, nullptr);
    }
}

LRESULT CALLBACK overlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;   // clicks go through to the sheet
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, msg, wp, lp);
}

void refreshAll();

LRESULT CALLBACK timerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kTimer) {
        refreshAll();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void registerClasses() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = overlayProc;
    wc.hInstance = module();
    wc.lpszClassName = L"MonteCalcCallout";
    RegisterClassExW(&wc);
    WNDCLASSEXW tc{};
    tc.cbSize = sizeof tc;
    tc.lpfnWndProc = timerProc;
    tc.hInstance = module();
    tc.lpszClassName = L"MonteCalcCalloutTimer";
    RegisterClassExW(&tc);
    done = true;
}

// the outline of cell and window together (convex hull of their corners)
std::vector<Gdiplus::PointF> hull(const RECT& a, const RECT& b) {
    std::vector<std::pair<double, double>> p = {{double(a.left), double(a.top)},  {double(a.right), double(a.top)},
                                                {double(a.right), double(a.bottom)}, {double(a.left), double(a.bottom)},
                                                {double(b.left), double(b.top)},  {double(b.right), double(b.top)},
                                                {double(b.right), double(b.bottom)}, {double(b.left), double(b.bottom)}};
    std::sort(p.begin(), p.end());
    auto cross = [](const std::pair<double, double>& o, const std::pair<double, double>& x, const std::pair<double, double>& y) {
        return (x.first - o.first) * (y.second - o.second) - (x.second - o.second) * (y.first - o.first);
    };
    std::vector<std::pair<double, double>> h(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) --k;
        h[k++] = p[i - 1];
    }
    h.resize(k > 0 ? k - 1 : 0);
    std::vector<Gdiplus::PointF> r;
    for (auto& q : h) r.push_back(Gdiplus::PointF(Gdiplus::REAL(q.first), Gdiplus::REAL(q.second)));
    return r;
}

void hide(Entry& e) {
    if (e.overlay && e.shown) ShowWindow(e.overlay, SW_HIDE);
    e.shown = false;
}

void draw(HWND window, Entry& e, const RECT& cell, const RECT& win) {
    RECT box{std::min(cell.left, win.left) - 2, std::min(cell.top, win.top) - 2, std::max(cell.right, win.right) + 2,
             std::max(cell.bottom, win.bottom) + 2};
    int w = box.right - box.left, h = box.bottom - box.top;
    if (w <= 0 || h <= 0 || w > 16000 || h > 16000) { hide(e); return; }
    if (!e.overlay) {
        registerClasses();
        // owned like the result window (above the spreadsheet), placed just behind the result window
        HWND owner = GetWindow(window, GW_OWNER);
        e.overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"MonteCalcCallout", L"",
                                    WS_POPUP, box.left, box.top, w, h, owner, nullptr, module(), nullptr);
        if (!e.overlay) return;
    }
    ensureGdiplus();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        {
            Gdiplus::Bitmap bmp(w, h, w * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
            Gdiplus::Graphics g(&bmp);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            g.TranslateTransform(Gdiplus::REAL(-box.left), Gdiplus::REAL(-box.top));
            auto pts = hull(cell, win);
            // never over the window itself (whatever the stacking order)
            g.ExcludeClip(Gdiplus::Rect(INT(win.left), INT(win.top), INT(win.right - win.left), INT(win.bottom - win.top)));
            Gdiplus::SolidBrush wedge(Gdiplus::Color(78, 236, 96, 96));   // MC FLO: translucent red
            if (pts.size() >= 3) g.FillPolygon(&wedge, pts.data(), INT(pts.size()));
            Gdiplus::Pen frame(Gdiplus::Color(230, 205, 45, 45), 2.0f);
            g.DrawRectangle(&frame, Gdiplus::REAL(cell.left), Gdiplus::REAL(cell.top), Gdiplus::REAL(cell.right - cell.left - 1),
                            Gdiplus::REAL(cell.bottom - cell.top - 1));
        }
        POINT dst{box.left, box.top}, src{0, 0};
        SIZE size{w, h};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(e.overlay, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA);
        SelectObject(mem, old);
        DeleteObject(dib);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    SetWindowPos(e.overlay, window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // the window above the wedge
    e.shown = true;
    e.lastCell = cell;
    e.lastWin = win;
}

bool ourForeground() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void refresh(HWND window, Entry& e) {
    if (!IsWindowVisible(window) || IsIconic(window) || !ourForeground()) { hide(e); return; }
    int x = 0, y = 0, w = 0, h = 0;
    bool visible = false;
    try {
        visible = e.cell && e.cell(x, y, w, h);
    } catch (...) {
        return;   // the program is busy (e.g. a cell is being edited): keep the last picture
    }
    if (!visible || w <= 0 || h <= 0) { hide(e); return; }
    RECT cell{x, y, x + w, y + h}, win;
    GetWindowRect(window, &win);
    if (e.shown && EqualRect(&cell, &e.lastCell) && EqualRect(&win, &e.lastWin)) {
        // keep it just behind the window when that comes to the front
        SetWindowPos(e.overlay, window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    draw(window, e, cell, win);
}

void refreshAll() {
    auto& m = entries();
    for (auto it = m.begin(); it != m.end();) {
        if (!IsWindow(it->first)) {
            if (it->second.overlay) DestroyWindow(it->second.overlay);
            it = m.erase(it);
            continue;
        }
        refresh(it->first, it->second);
        ++it;
    }
    if (m.empty() && g_timerWnd) KillTimer(g_timerWnd, kTimer);
}

} // namespace

void attach(void* window, CellRect cell) {
    HWND h = static_cast<HWND>(window);
    if (!h) return;
    Entry& e = entries()[h];
    e.cell = std::move(cell);
    e.lastCell = RECT{};
    refresh(h, e);
    registerClasses();
    if (!g_timerWnd) g_timerWnd = CreateWindowExW(0, L"MonteCalcCalloutTimer", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module(), nullptr);
    if (g_timerWnd) SetTimer(g_timerWnd, kTimer, 200, nullptr);   // follows moves of the window and scrolling of the sheet
}

void detach(void* window) {
    auto& m = entries();
    auto it = m.find(static_cast<HWND>(window));
    if (it == m.end()) return;
    if (it->second.overlay) DestroyWindow(it->second.overlay);
    m.erase(it);
}

} // namespace callout
} // namespace montecalc

#else   // no layered windows: no callout

namespace montecalc {
namespace callout {
void attach(void*, CellRect) {}
void detach(void*) {}
} // namespace callout
} // namespace montecalc

#endif
