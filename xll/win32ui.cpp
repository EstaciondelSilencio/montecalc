// MonteCalc for Excel - the window toolkit of uno/canvaswin.hpp and uno/ui.hpp on Win32.
//
// The windows of the extension (result window, wizard, correlation window, fit, influence diagram,
// ...) are written against CanvasWindow; here it is a native window: GDI+ draws the canvas
// (anti-aliased, double-buffered), the control panel uses standard Windows controls laid out in
// dialog units, with the same resize anchors as in LibreOffice.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <algorithm>
namespace Gdiplus {   // the GDI+ headers of Visual Studio use min/max, which NOMINMAX removes
using std::max;
using std::min;
}
#include <gdiplus.h>

#include "../uno/canvaswin.hpp"
#include "../uno/i18n.hpp"
#include "../uno/model.hpp"
#include "../uno/ui.hpp"
#include "../uno/util.hpp"
#include "com.hpp"
#include "xlimpl.hpp"
#include "win32ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>

namespace montecalc {

using com::wide;
using com::narrow;

// ================================================================ basics

namespace w32 {

void ensureGdiplus() {
    static ULONG_PTR token = 0;
    if (token) return;
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&token, &in, nullptr);
}

HINSTANCE module() {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ensureGdiplus), &m);
    return m;
}

namespace {
BOOL CALLBACK findMain(HWND h, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    wchar_t cls[32];
    GetClassNameW(h, cls, 32);
    if (wcscmp(cls, L"XLMAIN") == 0) {
        *reinterpret_cast<HWND*>(out) = h;
        return FALSE;
    }
    return TRUE;
}
} // namespace

HWND excelWindow() {
    HWND h = nullptr;
    EnumWindows(findMain, reinterpret_cast<LPARAM>(&h));
    return h;
}

HFONT uiFont(int shrink) {
    static std::map<int, HFONT> fonts;
    auto it = fonts.find(shrink);
    if (it != fonts.end()) return it->second;
    NONCLIENTMETRICSW m{};
    m.cbSize = sizeof m;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    LOGFONTW lf = m.lfMessageFont;
    if (shrink > 0) lf.lfHeight = lf.lfHeight < 0 ? lf.lfHeight + shrink : lf.lfHeight - shrink;
    HFONT f = CreateFontIndirectW(&lf);
    fonts[shrink] = f;
    return f;
}

HICON logoIcon(bool small) {
    static HICON icons[2] = {nullptr, nullptr};
    HICON& h = icons[small ? 1 : 0];
    if (!h) {
        int cx = GetSystemMetrics(small ? SM_CXSMICON : SM_CXICON), cy = GetSystemMetrics(small ? SM_CYSMICON : SM_CYICON);
        h = static_cast<HICON>(LoadImageW(module(), MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cy, LR_SHARED));
        if (!h) h = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return h;
}

int messageBox(HWND owner, const std::wstring& text, const std::wstring& title, bool error, UINT buttons) {
    if (error || !FindResourceW(module(), MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */))
        return MessageBoxW(owner, text.c_str(), title.c_str(), buttons | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    MSGBOXPARAMSW p{};
    p.cbSize = sizeof p;
    p.hwndOwner = owner;
    p.hInstance = module();
    p.lpszText = text.c_str();
    p.lpszCaption = title.c_str();
    p.dwStyle = buttons | MB_USERICON;
    p.lpszIcon = MAKEINTRESOURCEW(1);
    return MessageBoxIndirectW(&p);
}

// dialog base units of the user interface font (as Windows dialogs: average letter width, height)
void baseUnits(int& bx, int& by) {
    static int x = 0, y = 0;
    if (!x) {
        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, uiFont(0));
        SIZE sz;
        const wchar_t* letters = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        GetTextExtentPoint32W(dc, letters, 52, &sz);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
        x = (sz.cx / 26 + 1) / 2;
        y = tm.tmHeight;
        // never smaller than Segoe UI 9pt (7 x 16 at 96 dpi): the charts need the room
        HDC sdc = GetDC(nullptr);
        int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
        ReleaseDC(nullptr, sdc);
        if (dpi <= 0) dpi = 96;
        x = std::max(x, MulDiv(7, dpi, 96));
        y = std::max(y, MulDiv(16, dpi, 96));
    }
    bx = x;
    by = y;
}

std::wstring editText(const std::string& s) {   // "\n" -> "\r\n" for edit controls
    std::string t;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) t += '\r';
        t += s[i];
    }
    return wide(t);
}

std::string fromEdit(const std::wstring& w) {
    std::string s = narrow(w.c_str(), int(w.size())), t;
    for (char c : s) if (c != '\r') t += c;
    return t;
}

std::wstring windowText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring w(size_t(n) + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize(size_t(n));
    return w;
}

std::string pathOfUrl(const std::string& url) {
    if (url.rfind("file:", 0) != 0) return url;
    std::string p = url.substr(5);
    while (p.size() > 1 && p[0] == '/' && p[1] == '/') p = p.substr(1);
    if (p.size() > 2 && p[0] == '/' && p[2] == ':') p = p.substr(1);
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '%' && i + 2 < p.size()) {
            out += char(std::stoi(p.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += p[i] == '/' ? '\\' : p[i];
        }
    }
    return out;
}

bool encoderFor(const wchar_t* mime, CLSID& out) {
    UINT n = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(n, size, codecs);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(codecs[i].MimeType, mime) == 0) { out = codecs[i].Clsid; return true; }
    return false;
}

bool copyToClipboard(const std::string& text) {
    std::wstring w = wide(text);
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (mem) {
        void* p = GlobalLock(mem);
        memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
    return mem != nullptr;
}

std::string convertHtml(const std::string& htmlPath, const std::string& kind) {
    CLSID cls;
    if (FAILED(CLSIDFromProgID(L"Word.Application", &cls))) return {};
    IDispatch* raw = nullptr;
    if (FAILED(CoCreateInstance(cls, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&raw))) || !raw) return {};
    com::Disp word(raw, false);
    std::string out = htmlPath.substr(0, htmlPath.rfind('.')) + (kind == "pdf" ? ".pdf" : ".docx");
    try {
        word.put("Visible", com::Var(false));
        com::Disp doc = word.obj("Documents").obj("Open", {com::Var(htmlPath)});
        doc.call("SaveAs2", {com::Var(out), com::Var(kind == "pdf" ? 17 : 16)});   // wdFormatPDF / wdFormatDocumentDefault
        doc.call("Close", {com::Var(false)});
        word.call("Quit");
        DeleteFileW(wide(htmlPath).c_str());
        return out;
    } catch (...) {
        try { word.call("Quit"); } catch (...) {}
        return {};
    }
}

} // namespace w32

// ================================================================ GDI+ painter

namespace {
Gdiplus::Color gc(const Color& c) { return Gdiplus::Color(255, BYTE(c.r), BYTE(c.g), BYTE(c.b)); }

Gdiplus::Font& fontOf(int size, bool bold) {
    static std::map<int, std::unique_ptr<Gdiplus::Font>> fonts;
    int key = size * 2 + (bold ? 1 : 0);
    auto it = fonts.find(key);
    if (it != fonts.end()) return *it->second;
    static Gdiplus::FontFamily* family = nullptr;
    if (!family) {
        for (const wchar_t* name : {L"Segoe UI", L"Arial", L"Tahoma", L"Microsoft Sans Serif"}) {
            family = new Gdiplus::FontFamily(name);
            if (family->GetLastStatus() == Gdiplus::Ok && family->IsAvailable()) break;
            delete family;
            family = nullptr;
        }
        if (!family) family = Gdiplus::FontFamily::GenericSansSerif()->Clone();
    }
    auto f = std::make_unique<Gdiplus::Font>(family, Gdiplus::REAL(size) * 96.0f / 72.0f, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                                             Gdiplus::UnitPixel);
    auto& ref = *f;
    fonts[key] = std::move(f);
    return ref;
}

struct Smooth {
    Gdiplus::Graphics& g;
    Gdiplus::SmoothingMode old;
    Smooth(Gdiplus::Graphics& gr, bool on) : g(gr), old(gr.GetSmoothingMode()) {
        g.SetSmoothingMode(on ? Gdiplus::SmoothingModeAntiAlias : Gdiplus::SmoothingModeNone);
    }
    ~Smooth() { g.SetSmoothingMode(old); }
};
} // namespace

void GdiPainter::line(int x1, int y1, int x2, int y2, const Color& c, int width, bool dashed) {
    Smooth s(g_, x1 != x2 && y1 != y2);
    Gdiplus::Pen pen(gc(c), Gdiplus::REAL(std::max(1, width)));
    if (dashed) pen.SetDashStyle(Gdiplus::DashStyleDash);
    g_.DrawLine(&pen, x1, y1, x2, y2);
}

void GdiPainter::rect(int x, int y, int w, int h, const Color* fill, const Color* stroke) {
    Smooth s(g_, false);
    if (fill) {
        Gdiplus::SolidBrush b(gc(*fill));
        g_.FillRectangle(&b, x, y, w, h);
    }
    if (stroke) {
        Gdiplus::Pen pen(gc(*stroke), 1);
        g_.DrawRectangle(&pen, x, y, std::max(0, w - 1), std::max(0, h - 1));
    }
}

void GdiPainter::roundRect(int x, int y, int w, int h, int r, const Color* fill, const Color* stroke) {
    Smooth s(g_, true);
    r = std::max(1, std::min(r, std::min(w, h) / 2));
    Gdiplus::GraphicsPath path;
    int d = 2 * r;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    if (fill) {
        Gdiplus::SolidBrush b(gc(*fill));
        g_.FillPath(&b, &path);
    }
    if (stroke) {
        Gdiplus::Pen pen(gc(*stroke), 1);
        g_.DrawPath(&pen, &path);
    }
}

void GdiPainter::ellipse(int x, int y, int w, int h, const Color* fill, const Color* stroke) {
    Smooth s(g_, true);
    if (fill) {
        Gdiplus::SolidBrush b(gc(*fill));
        g_.FillEllipse(&b, x, y, w, h);
    }
    if (stroke) {
        Gdiplus::Pen pen(gc(*stroke), 1);
        g_.DrawEllipse(&pen, x, y, w, h);
    }
}

void GdiPainter::polygon(const std::vector<int>& xs, const std::vector<int>& ys, const Color* fill, const Color* stroke) {
    if (xs.size() < 2) return;
    Smooth s(g_, true);
    std::vector<Gdiplus::Point> pts;
    for (size_t i = 0; i < xs.size() && i < ys.size(); ++i) pts.emplace_back(xs[i], ys[i]);
    if (fill) {
        Gdiplus::SolidBrush b(gc(*fill));
        g_.FillPolygon(&b, pts.data(), INT(pts.size()));
    }
    if (stroke) {
        Gdiplus::Pen pen(gc(*stroke), 1);
        g_.DrawPolygon(&pen, pts.data(), INT(pts.size()));
    }
}

void GdiPainter::polyline(const std::vector<int>& xs, const std::vector<int>& ys, const Color& c, int width) {
    if (xs.size() < 2) return;
    Smooth s(g_, true);
    std::vector<Gdiplus::Point> pts;
    for (size_t i = 0; i < xs.size() && i < ys.size(); ++i) pts.emplace_back(xs[i], ys[i]);
    Gdiplus::Pen pen(gc(c), Gdiplus::REAL(std::max(1, width)));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g_.DrawLines(&pen, pts.data(), INT(pts.size()));
}

void GdiPainter::text(int x, int y, const std::string& s, const Color& c, bool bold, int size) {
    std::wstring w = wide(s);
    Gdiplus::SolidBrush b(gc(c));
    g_.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    g_.DrawString(w.c_str(), INT(w.size()), &fontOf(size > 0 ? size : kTextFont, bold), Gdiplus::PointF(Gdiplus::REAL(x), Gdiplus::REAL(y)),
                  Gdiplus::StringFormat::GenericTypographic(), &b);
}

int GdiPainter::textWidth(const std::string& s, int size) {
    std::wstring w = wide(s);
    if (w.empty()) return 0;
    Gdiplus::StringFormat f(Gdiplus::StringFormat::GenericTypographic());
    f.SetFormatFlags(f.GetFormatFlags() | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    Gdiplus::RectF box;
    g_.MeasureString(w.c_str(), INT(w.size()), &fontOf(size > 0 ? size : kTextFont, false), Gdiplus::PointF(0, 0), &f, &box);
    return int(std::ceil(box.Width));
}

int GdiPainter::textHeight(int size) { return int(std::ceil(fontOf(size > 0 ? size : kTextFont, false).GetHeight(&g_))); }

// ================================================================ CanvasWindow

namespace {
const wchar_t* kClass = L"MonteCalcWindow";
std::mutex g_mtx;
std::set<std::shared_ptr<CanvasWindow>>& registry() {
    static std::set<std::shared_ptr<CanvasWindow>> r;
    return r;
}
std::map<std::string, RECT>& geometries() {
    static std::map<std::string, RECT> g;
    return g;
}

int mapKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return ::com::sun::star::awt::Key::A + int(vk - 'A');
    if (vk >= '0' && vk <= '9') return ::com::sun::star::awt::Key::NUM0 + int(vk - '0');
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return ::com::sun::star::awt::Key::NUM0 + int(vk - VK_NUMPAD0);
    namespace K = ::com::sun::star::awt::Key;
    switch (vk) {
    case VK_LEFT: return K::LEFT;
    case VK_RIGHT: return K::RIGHT;
    case VK_UP: return K::UP;
    case VK_DOWN: return K::DOWN;
    case VK_HOME: return K::HOME;
    case VK_END: return K::END;
    case VK_PRIOR: return K::PAGEUP;
    case VK_NEXT: return K::PAGEDOWN;
    case VK_RETURN: return K::RETURN;
    case VK_ESCAPE: return K::ESCAPE;
    case VK_TAB: return K::TAB;
    case VK_BACK: return K::BACKSPACE;
    case VK_SPACE: return K::SPACE;
    case VK_DELETE: return K::KEY_DELETE;
    case VK_ADD: case VK_OEM_PLUS: return K::ADD;
    case VK_SUBTRACT: case VK_OEM_MINUS: return K::SUBTRACT;
    case VK_F1: return K::F1;
    case VK_F2: return K::F2;
    default: return 0;
    }
}
} // namespace

struct Win32Window {
    static CanvasWindow* of(HWND h) { return reinterpret_cast<CanvasWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

    template <class F>
    static void safe(CanvasWindow* w, F f) {
        if (!w || w->closed_) return;
        try {
            f();
        } catch (const std::exception& e) {
            w->message(e.what(), true);
        } catch (const ::com::sun::star::lang::IllegalArgumentException& e) {
            w->message(u8(e.Message), true);
        }
    }

    static bool handleKey(CanvasWindow* w, WPARAM vk) {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        int code = mapKey(vk);
        namespace K = ::com::sun::star::awt::Key;
        if (ctrl && (code == K::ADD || code == K::SUBTRACT || code == K::NUM0)) {
            safe(w, [&] { w->setZoom(code == K::NUM0 ? 1.0 : code == K::ADD ? w->zoom_ * 1.15 : w->zoom_ / 1.15); });
            return true;
        }
        if (!code) return false;
        bool used = false;
        safe(w, [&] { used = w->key(code, 0, ctrl, shift); });
        return used;
    }

    // keys typed in a control also reach the window (Return in the mentor, Ctrl + / -, arrows)
    static LRESULT CALLBACK childProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto prev = reinterpret_cast<WNDPROC>(GetPropW(h, L"MonteCalcProc"));
        HWND owner = GetParent(h);
        if (msg == WM_KEYDOWN) {
            CanvasWindow* w = of(owner);
            bool ctrl = GetKeyState(VK_CONTROL) < 0;
            wchar_t cls[16];
            GetClassNameW(h, cls, 16);
            bool edit = _wcsicmp(cls, L"Edit") == 0;
            bool multiline = edit && (GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE);
            if (w && ((wp == VK_RETURN && !multiline) || (ctrl && (wp == VK_ADD || wp == VK_SUBTRACT || wp == VK_OEM_PLUS || wp == VK_OEM_MINUS || wp == '0')))) {
                if (handleKey(w, wp)) return 0;
            }
        }
        if (msg == WM_CHAR && wp == VK_RETURN) {
            wchar_t cls[16];
            GetClassNameW(h, cls, 16);
            if (_wcsicmp(cls, L"Edit") == 0 && !(GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE)) return 0;   // no beep
        }
        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(prev));
            RemovePropW(h, L"MonteCalcProc");
        }
        return CallWindowProcW(prev, h, msg, wp, lp);
    }

    static MouseInfo info(CanvasWindow* w, LPARAM lp, bool left, bool right, int clicks) {
        double z = w->zoom_;
        return {int(GET_X_LPARAM(lp) / z), int(GET_Y_LPARAM(lp) / z), left, right, clicks, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0};
    }

    static void paint(CanvasWindow* w, HWND h) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        int W = std::max(1, int(rc.right)), H = std::max(1, int(rc.bottom));
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
        HGDIOBJ old = SelectObject(mem, bmp);
        {
            Gdiplus::Graphics g(mem);
            Color panel = col::panel, white = col::white;
            Gdiplus::SolidBrush pb(gc(panel));
            g.FillRectangle(&pb, 0, 0, W, H);
            int pw = w->panelPixels();
            if (w->hasCanvas()) {
                Gdiplus::SolidBrush wb(gc(white));
                g.FillRectangle(&wb, 0, 0, std::max(1, W - pw), H);
                g.SetClip(Gdiplus::Rect(0, 0, std::max(1, W - pw), H));
                int cw, ch;
                w->canvasSize(cw, ch);
                try {
                    GdiPainter gp(g);
                    ZoomPainter p(gp, w->zoom_);
                    w->paint(p, cw, ch);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "[MonteCalc] paint error: %s\n", e.what());
                }
            }
        }
        BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
    }

    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        CanvasWindow* w = of(h);
        switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            break;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
            if (w) { paint(w, h); return 0; }
            break;
        case WM_SIZE:
            if (w && !w->closed_ && w->shown_) safe(w, [&] { w->relayout(); w->repaint(); });
            return 0;
        case WM_EXITSIZEMOVE:
            if (w) w->rememberGeometry();
            return 0;
        case WM_GETMINMAXINFO:
            if (w && w->baseWpx_ > 0) {
                auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
                int needH = 0;
                for (auto& [name, rc] : w->rects_) {
                    auto a = w->anchors_.find(name);
                    int flags = a != w->anchors_.end() ? a->second : 0;
                    needH = std::max(needH, rc.y + ((flags & CanvasWindow::StretchH) ? 12 : rc.h));
                }
                RECT r{0, 0, std::max(w->panelPixels() + 240, w->baseWpx_ * 55 / 100), std::max(240, std::min(w->baseHpx_, w->duY(needH + 6)))};
                AdjustWindowRectEx(&r, DWORD(GetWindowLongW(h, GWL_STYLE)), FALSE, DWORD(GetWindowLongW(h, GWL_EXSTYLE)));
                mm->ptMinTrackSize.x = r.right - r.left;
                mm->ptMinTrackSize.y = r.bottom - r.top;
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            static HBRUSH panelBrush = CreateSolidBrush(RGB(col::panel.r, col::panel.g, col::panel.b));
            HDC dc = reinterpret_cast<HDC>(wp);
            HWND c = reinterpret_cast<HWND>(lp);
            wchar_t cls[16];
            GetClassNameW(c, cls, 16);
            if (_wcsicmp(cls, L"Edit") == 0) break;   // read-only edits keep their colour
            SetBkMode(dc, TRANSPARENT);
            if (w)
                for (auto& ct : w->ctls_)
                    if (ct.hwnd == c) {
                        auto it = w->textColors_.find(ct.name);
                        if (it != w->textColors_.end()) SetTextColor(dc, RGB((it->second >> 16) & 255, (it->second >> 8) & 255, it->second & 255));
                    }
            return reinterpret_cast<LRESULT>(panelBrush);
        }
        case WM_COMMAND:
            if (w && lp) {
                int id = LOWORD(wp), code = HIWORD(wp);
                for (auto& ct : w->ctls_) {
                    if (ct.id != id) continue;
                    std::string name = ct.name;
                    if (ct.type == "button" && code == BN_CLICKED) safe(w, [&] { w->button(name); });
                    else if (ct.type == "check" && code == BN_CLICKED) safe(w, [&] { w->itemChanged(name); });
                    else if (ct.type == "combo" && code == CBN_SELCHANGE) safe(w, [&] { w->itemChanged(name); });
                    else if (ct.type == "list" && code == LBN_SELCHANGE) safe(w, [&] { w->itemChanged(name); });
                    else if (ct.type == "list" && code == LBN_DBLCLK) safe(w, [&] { w->itemChanged(name); });
                    else if (ct.type == "edit" && code == EN_CHANGE &&
                             std::find(w->watched_.begin(), w->watched_.end(), name) != w->watched_.end())
                        safe(w, [&] { w->itemChanged(name); });
                    break;
                }
                return 0;
            }
            break;
        case WM_HSCROLL:
            if (w && lp) {
                HWND sb = reinterpret_cast<HWND>(lp);
                for (auto& ct : w->ctls_) {
                    if (ct.hwnd != sb) continue;
                    SCROLLINFO si{};
                    si.cbSize = sizeof si;
                    si.fMask = SIF_ALL;
                    GetScrollInfo(sb, SB_CTL, &si);
                    int pos = si.nPos;
                    switch (LOWORD(wp)) {
                    case SB_LINELEFT: pos -= 1; break;
                    case SB_LINERIGHT: pos += 1; break;
                    case SB_PAGELEFT: pos -= 10; break;
                    case SB_PAGERIGHT: pos += 10; break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                    case SB_LEFT: pos = si.nMin; break;
                    case SB_RIGHT: pos = si.nMax; break;
                    default: return 0;
                    }
                    pos = std::max(si.nMin, std::min(si.nMax, pos));
                    SetScrollPos(sb, SB_CTL, pos, TRUE);
                    std::string name = ct.name;
                    safe(w, [&] { w->itemChanged(name); });
                    break;
                }
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (w && GET_X_LPARAM(lp) < w->canvasPixels()) {
                SetFocus(h);
                SetCapture(h);
                w->dragging_ = true;
                MouseInfo m = info(w, lp, true, false, msg == WM_LBUTTONDBLCLK ? 2 : 1);
                safe(w, [&] { w->mouseDown(m); });
            }
            return 0;
        case WM_RBUTTONDOWN:
            if (w && GET_X_LPARAM(lp) < w->canvasPixels()) {
                MouseInfo m = info(w, lp, false, true, 1);
                safe(w, [&] { w->mouseDown(m); });
            }
            return 0;
        case WM_LBUTTONUP:
            if (w) {
                ReleaseCapture();
                bool was = w->dragging_;
                w->dragging_ = false;
                if (was) {
                    MouseInfo m = info(w, lp, true, false, 1);
                    safe(w, [&] { w->mouseUp(m); });
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (w) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                if (GET_X_LPARAM(lp) < w->canvasPixels() || w->dragging_) {
                    MouseInfo m = info(w, lp, (wp & MK_LBUTTON) != 0, (wp & MK_RBUTTON) != 0, 0);
                    bool drag = w->dragging_;
                    safe(w, [&] { w->mouseMove(m, drag); });
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (w) safe(w, [&] { w->mouseExit(); });
            return 0;
        case WM_MOUSEWHEEL:
            if (w && (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)) {
                int d = GET_WHEEL_DELTA_WPARAM(wp);
                safe(w, [&] { w->setZoom(d > 0 ? w->zoom_ * 1.1 : w->zoom_ / 1.1); });
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (w && handleKey(w, wp)) return 0;
            break;
        case WM_CLOSE:
            if (w) w->close();
            else DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            if (w) {
                w->closed_ = true;
                w->hwnd_ = nullptr;
            }
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            return 0;
        default: break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }

    static void registerClass() {
        static bool done = false;
        if (done) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = proc;
        wc.hInstance = w32::module();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClass;
        wc.hIcon = w32::logoIcon(false);
        wc.hIconSm = w32::logoIcon(true);
        RegisterClassExW(&wc);
        done = true;
    }
};

void CanvasWindow::keep(std::shared_ptr<CanvasWindow> w) {
    std::lock_guard<std::mutex> g(g_mtx);
    auto& r = registry();
    for (auto it = r.begin(); it != r.end();) it = (*it)->closed() ? r.erase(it) : std::next(it);
    r.insert(w);
}

CanvasWindow::CanvasWindow(const css::uno::Reference<css::uno::XComponentContext>& ctx, const css::uno::Reference<css::frame::XFrame>& frame,
                           const std::string& title, int width, int height, int panelWidth)
    : ctx_(ctx), frame_(frame), title_(title), width_(width), height_(height), panelW_(panelWidth) {
    w32::ensureGdiplus();
}

CanvasWindow::~CanvasWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(static_cast<HWND>(hwnd_), GWLP_USERDATA, 0);
        DestroyWindow(static_cast<HWND>(hwnd_));
    }
}

int CanvasWindow::duX(int du) const {
    int bx, by;
    w32::baseUnits(bx, by);
    return MulDiv(du, bx, 4);
}

int CanvasWindow::duY(int du) const {
    int bx, by;
    w32::baseUnits(bx, by);
    return MulDiv(du, by, 8);
}

int CanvasWindow::panelPixels() { return duX(panelW_); }

int CanvasWindow::canvasPixels() {
    RECT rc{};
    if (hwnd_) GetClientRect(static_cast<HWND>(hwnd_), &rc);
    return hasCanvas() ? int(rc.right) - panelPixels() : 0;
}

void* CanvasWindow::ctl(const std::string& name) const {
    for (auto& c : ctls_)
        if (c.name == name) return c.hwnd;
    throw std::runtime_error("no control " + name);
}

namespace {
HWND createControl(HWND parent, const std::string& type, const std::wstring& text, int x, int y, int w, int h, unsigned style, int id) {
    const wchar_t* cls = L"Static";
    DWORD st = WS_CHILD | WS_VISIBLE | style, ex = 0;
    if (type == "button") { cls = L"Button"; st |= BS_PUSHBUTTON | WS_TABSTOP; }
    else if (type == "check") { cls = L"Button"; st |= BS_AUTOCHECKBOX | WS_TABSTOP; }
    else if (type == "edit") { cls = L"Edit"; st |= WS_TABSTOP; ex = WS_EX_CLIENTEDGE; }
    else if (type == "combo") { cls = L"ComboBox"; st |= CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP; h += 240; }
    else if (type == "list") { cls = L"ListBox"; st |= LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | WS_TABSTOP; ex = WS_EX_CLIENTEDGE; }
    else if (type == "slider") { cls = L"ScrollBar"; st |= SBS_HORZ; }
    HWND c = CreateWindowExW(ex, cls, text.c_str(), st, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), w32::module(), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(w32::uiFont(0)), FALSE);
    // keys typed in the control also reach the window (classic subclassing: no comctl32 needed)
    SetPropW(c, L"MonteCalcProc", reinterpret_cast<HANDLE>(GetWindowLongPtrW(c, GWLP_WNDPROC)));
    SetWindowLongPtrW(c, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Win32Window::childProc));
    return c;
}

void fillList(HWND c, const std::string& type, const std::vector<std::string>& items) {
    bool combo = type == "combo";
    SendMessageW(c, combo ? CB_RESETCONTENT : LB_RESETCONTENT, 0, 0);
    for (auto& it : items) {
        std::wstring w = wide(trExact(it));
        SendMessageW(c, combo ? CB_ADDSTRING : LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
    }
}

// the text fits the control: otherwise a smaller font (down to 75 %)
void fitText(HWND c, const std::string& type) {
    if (type != "button" && type != "check" && type != "label") return;
    if (type == "label" && !(GetWindowLongW(c, GWL_STYLE) & SS_LEFTNOWORDWRAP)) return;   // multi-line labels wrap
    RECT rc;
    GetClientRect(c, &rc);
    int avail = rc.right - (type == "check" ? 20 : type == "button" ? 8 : 0);
    std::wstring t = w32::windowText(c);
    HDC dc = GetDC(c);
    for (int shrink = 0; shrink <= 4; ++shrink) {
        HFONT f = w32::uiFont(shrink);
        HGDIOBJ old = SelectObject(dc, f);
        SIZE sz;
        GetTextExtentPoint32W(dc, t.c_str(), int(t.size()), &sz);
        SelectObject(dc, old);
        if (sz.cx <= avail || shrink == 4) {
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
            break;
        }
    }
    ReleaseDC(c, dc);
}
} // namespace

void CanvasWindow::addControl(const std::string& type, const std::string& name, const std::string& text, int x, int y, int w, int h, unsigned style) {
    Ctl c;
    c.name = name;
    c.type = type;
    c.text = text;
    c.style = style;
    c.id = 1000 + int(ctls_.size());
    ctls_.push_back(c);
    rects_.push_back({name, {x, y, w, h}});
    if (shown_) {
        Ctl& n = ctls_.back();
        n.hwnd = createControl(static_cast<HWND>(hwnd_), type, type == "edit" ? w32::editText(text) : wide(text), duX(x), duY(y), duX(w), duY(h), style, n.id);
        if (type == "combo" || type == "list") fillList(static_cast<HWND>(n.hwnd), type, items_[name]);
    }
}

void CanvasWindow::addButton(const std::string& name, const std::string& label, int x, int y, int w, int h) {
    addControl("button", name, tr(label), x, y, w, h, 0);
    buttons_.push_back(name);
}

void CanvasWindow::addLabel(const std::string& name, const std::string& text, int x, int y, int w, int h) {
    addControl("label", name, tr(text), x, y, w, h, h > 12 ? SS_LEFT : SS_LEFTNOWORDWRAP);
}

void CanvasWindow::addEdit(const std::string& name, const std::string& text, int x, int y, int w, int h, bool multi, bool password, bool ro) {
    unsigned st = ES_AUTOHSCROLL;
    if (multi) st = ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN;
    if (password) st |= ES_PASSWORD;
    if (ro) st |= ES_READONLY;
    addControl("edit", name, text, x, y, w, h, st);
}

void CanvasWindow::addList(const std::string& name, const std::vector<std::string>& items, int x, int y, int w, int h, bool dropdown, bool multi) {
    items_[name] = items;
    addControl(dropdown ? "combo" : "list", name, "", x, y, w, h, multi ? LBS_EXTENDEDSEL : 0);
    lists_.push_back(name);
    if (shown_ && !items.empty() && !multi) select(name, 0);
}

void CanvasWindow::addCheck(const std::string& name, const std::string& label, int x, int y, int w, bool on) {
    addControl("check", name, tr(label), x, y, w, 12, 0);
    ctls_.back().checked = on;
    if (shown_) setChecked(name, on);
    checks_.push_back(name);
}

void CanvasWindow::addSlider(const std::string& name, int x, int y, int w, int min, int max, int value) {
    addControl("slider", name, "", x, y, w, 10, 0);
    ctls_.back().smin = min;
    ctls_.back().smax = max;
    ctls_.back().sval = value;
    if (shown_) {
        SetScrollRange(static_cast<HWND>(ctls_.back().hwnd), SB_CTL, min, max, FALSE);
        SetScrollPos(static_cast<HWND>(ctls_.back().hwnd), SB_CTL, value, TRUE);
    }
    sliders_.push_back(name);
}

double& CanvasWindow::defaultZoom() {
    static double z = 1.25;
    return z;
}

std::string CanvasWindow::geometryKey() const {
    std::string k = title_;
    for (const char* cut : {" into ", " - Preview", " - Formula"}) {
        size_t p = k.find(cut);
        if (p != std::string::npos) k = k.substr(0, p);
    }
    return k;
}

void CanvasWindow::rememberGeometry() {
    if (!hwnd_ || relayouting_) return;
    RECT r;
    GetWindowRect(static_cast<HWND>(hwnd_), &r);
    geometries()[geometryKey()] = r;
}

void CanvasWindow::show() {
    Win32Window::registerClass();
    RECT r{0, 0, duX(width_), duY(height_)};
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, ex = 0;
    AdjustWindowRectEx(&r, style, FALSE, ex);
    HWND owner = w32::excelWindow();
    std::wstring t = wide(tr(title_));
    HWND h = CreateWindowExW(ex, kClass, t.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, owner, nullptr,
                             w32::module(), this);
    if (!h) throw std::runtime_error("MonteCalc: the window could not be created");
    hwnd_ = h;
    for (auto& c : ctls_) {
        auto it = std::find_if(rects_.begin(), rects_.end(), [&](const std::pair<std::string, Rect>& p) { return p.first == c.name; });
        Rect rc = it->second;
        c.hwnd = createControl(h, c.type, c.type == "edit" ? w32::editText(c.text) : wide(c.text), duX(rc.x), duY(rc.y), duX(rc.w), duY(rc.h), c.style, c.id);
        HWND ch = static_cast<HWND>(c.hwnd);
        if (c.type == "combo" || c.type == "list") {
            fillList(ch, c.type, items_[c.name]);
            if (!items_[c.name].empty() && !(c.style & LBS_EXTENDEDSEL)) SendMessageW(ch, c.type == "combo" ? CB_SETCURSEL : LB_SETCURSEL, 0, 0);
        } else if (c.type == "check") {
            SendMessageW(ch, BM_SETCHECK, c.checked ? BST_CHECKED : BST_UNCHECKED, 0);
        } else if (c.type == "slider") {
            SetScrollRange(ch, SB_CTL, c.smin, c.smax, FALSE);
            SetScrollPos(ch, SB_CTL, c.sval, TRUE);
        }
        fitText(ch, c.type);
    }
    RECT cr;
    GetClientRect(h, &cr);
    baseWpx_ = cr.right;
    baseHpx_ = cr.bottom;
    shown_ = true;
    auto& geo = geometries();
    auto it = geo.find(geometryKey());
    if (it != geo.end()) {
        relayouting_ = true;
        SetWindowPos(h, nullptr, it->second.left, it->second.top, it->second.right - it->second.left, it->second.bottom - it->second.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        relayouting_ = false;
        relayout();
    } else if (owner) {
        // centred on Excel
        RECT o, me;
        GetWindowRect(owner, &o);
        GetWindowRect(h, &me);
        int x = o.left + ((o.right - o.left) - (me.right - me.left)) / 2, y = o.top + ((o.bottom - o.top) - (me.bottom - me.top)) / 3;
        SetWindowPos(h, nullptr, std::max(0, x), std::max(0, y), 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
}

// Moves / stretches the controls after a resize, following their anchors (as in LibreOffice).
void CanvasWindow::relayout() {
    if (relayouting_ || !hwnd_ || baseWpx_ <= 0) return;
    RECT r;
    GetClientRect(static_cast<HWND>(hwnd_), &r);
    int bx, by;
    w32::baseUnits(bx, by);
    int dW = MulDiv(r.right - baseWpx_, 4, bx), dH = MulDiv(r.bottom - baseHpx_, 8, by);
    relayouting_ = true;
    HDWP dwp = BeginDeferWindowPos(int(ctls_.size()));
    for (auto& [name, rc] : rects_) {
        auto a = anchors_.find(name);
        int flags = a != anchors_.end() ? a->second : (rc.x >= width_ - panelW_ ? AnchorRight : AnchorLeft);
        int x = rc.x + ((flags & AnchorRight) ? dW : 0), y = rc.y + ((flags & AnchorBottom) ? dH : 0);
        int ww = rc.w + ((flags & StretchW) ? dW : 0), hh = rc.h + ((flags & StretchH) ? dH : 0);
        for (auto& c : ctls_)
            if (c.name == name && c.hwnd) {
                int extra = c.type == "combo" ? 240 : 0;
                if (dwp) dwp = DeferWindowPos(dwp, static_cast<HWND>(c.hwnd), nullptr, duX(x), duY(y), duX(std::max(8, ww)), duY(std::max(8, hh)) + extra,
                                              SWP_NOZORDER | SWP_NOACTIVATE);
            }
    }
    if (dwp) EndDeferWindowPos(dwp);
    relayouting_ = false;
    InvalidateRect(static_cast<HWND>(hwnd_), nullptr, FALSE);
}

std::string CanvasWindow::text(const std::string& name) { return w32::fromEdit(w32::windowText(static_cast<HWND>(ctl(name)))); }

void CanvasWindow::setText(const std::string& name, const std::string& t) {
    for (auto& c : ctls_)
        if (c.name == name) {
            c.text = t;
            if (c.hwnd) SetWindowTextW(static_cast<HWND>(c.hwnd), w32::editText(t).c_str());
        }
}

void CanvasWindow::setLabel(const std::string& name, const std::string& t) {
    for (auto& c : ctls_)
        if (c.name == name) {
            c.text = tr(t);
            if (c.hwnd) {
                SetWindowTextW(static_cast<HWND>(c.hwnd), wide(c.text).c_str());
                fitText(static_cast<HWND>(c.hwnd), c.type);
            }
        }
}

int CanvasWindow::selected(const std::string& name) {
    for (auto& c : ctls_)
        if (c.name == name) {
            HWND h = static_cast<HWND>(c.hwnd);
            if (c.type == "combo") return int(SendMessageW(h, CB_GETCURSEL, 0, 0));
            if (c.style & LBS_EXTENDEDSEL) {
                auto all = selectedAll(name);
                return all.empty() ? -1 : all[0];
            }
            return int(SendMessageW(h, LB_GETCURSEL, 0, 0));
        }
    throw std::runtime_error("no list " + name);
}

std::vector<int> CanvasWindow::selectedAll(const std::string& name) {
    std::vector<int> r;
    for (auto& c : ctls_)
        if (c.name == name) {
            HWND h = static_cast<HWND>(c.hwnd);
            if (c.type == "combo") {
                int s = int(SendMessageW(h, CB_GETCURSEL, 0, 0));
                if (s >= 0) r.push_back(s);
            } else if (c.style & LBS_EXTENDEDSEL) {
                int n = int(SendMessageW(h, LB_GETSELCOUNT, 0, 0));
                if (n > 0) {
                    std::vector<int> buf(static_cast<size_t>(n));
                    SendMessageW(h, LB_GETSELITEMS, WPARAM(n), reinterpret_cast<LPARAM>(buf.data()));
                    r = buf;
                }
            } else {
                int s = int(SendMessageW(h, LB_GETCURSEL, 0, 0));
                if (s >= 0) r.push_back(s);
            }
        }
    return r;
}

std::string CanvasWindow::selectedItem(const std::string& name) {
    auto it = items_.find(name);
    int pos = selected(name);
    if (it != items_.end() && pos >= 0 && pos < int(it->second.size())) return it->second[size_t(pos)];
    return {};
}

void CanvasWindow::setItems(const std::string& name, const std::vector<std::string>& items, int sel) {
    items_[name] = items;
    for (auto& c : ctls_)
        if (c.name == name && c.hwnd) {
            fillList(static_cast<HWND>(c.hwnd), c.type, items);
            if (sel >= 0 && sel < int(items.size())) select(name, sel);
        }
}

void CanvasWindow::select(const std::string& name, int index) {
    for (auto& c : ctls_)
        if (c.name == name && c.hwnd) {
            HWND h = static_cast<HWND>(c.hwnd);
            if (c.type == "combo") SendMessageW(h, CB_SETCURSEL, WPARAM(index), 0);
            else if (c.style & LBS_EXTENDEDSEL) SendMessageW(h, LB_SETSEL, TRUE, LPARAM(index));
            else SendMessageW(h, LB_SETCURSEL, WPARAM(index), 0);
        }
}

bool CanvasWindow::checked(const std::string& name) { return SendMessageW(static_cast<HWND>(ctl(name)), BM_GETCHECK, 0, 0) == BST_CHECKED; }

void CanvasWindow::setChecked(const std::string& name, bool on) {
    for (auto& c : ctls_)
        if (c.name == name) {
            c.checked = on;
            if (c.hwnd) SendMessageW(static_cast<HWND>(c.hwnd), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        }
}

void CanvasWindow::setEnabled(const std::string& name, bool on) { EnableWindow(static_cast<HWND>(ctl(name)), on); }
void CanvasWindow::setVisible(const std::string& name, bool on) { ShowWindow(static_cast<HWND>(ctl(name)), on ? SW_SHOW : SW_HIDE); }
int CanvasWindow::sliderValue(const std::string& name) { return GetScrollPos(static_cast<HWND>(ctl(name)), SB_CTL); }
void CanvasWindow::setSliderValue(const std::string& name, int v) { SetScrollPos(static_cast<HWND>(ctl(name)), SB_CTL, v, TRUE); }

void CanvasWindow::setTextColor(const std::string& name, sal_Int32 rgb) {
    textColors_[name] = rgb;
    for (auto& c : ctls_)
        if (c.name == name && c.hwnd) InvalidateRect(static_cast<HWND>(c.hwnd), nullptr, TRUE);
}

void CanvasWindow::canvasSize(int& w, int& h) {
    RECT r{};
    if (hwnd_) GetClientRect(static_cast<HWND>(hwnd_), &r);
    w = std::max(50, int((int(r.right) - panelPixels()) / zoom_));
    h = std::max(50, int(int(r.bottom) / zoom_));
}

void CanvasWindow::setZoom(double z) {
    zoom_ = std::min(3.0, std::max(0.6, z));
    defaultZoom() = zoom_;
    repaint();
}

void CanvasWindow::repaint() {
    if (closed_ || !hwnd_) return;
    RECT r;
    GetClientRect(static_cast<HWND>(hwnd_), &r);
    r.right = std::max(LONG(50), r.right - panelPixels());
    InvalidateRect(static_cast<HWND>(hwnd_), &r, FALSE);
}

void CanvasWindow::close() {
    if (closed_) return;
    rememberGeometry();
    closed_ = true;
    if (hwnd_) {
        HWND h = static_cast<HWND>(hwnd_);
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        DestroyWindow(h);
    }
}

int CanvasWindow::popup(int x, int y, const std::vector<std::pair<int, std::string>>& items) {
    HMENU m = CreatePopupMenu();
    for (auto& [id, text] : items) {
        if (text.empty()) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        else AppendMenuW(m, MF_STRING, UINT_PTR(id), wide(tr(text)).c_str());
    }
    POINT p{LONG(x * zoom_), LONG(y * zoom_)};
    ClientToScreen(static_cast<HWND>(hwnd_), &p);
    int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, static_cast<HWND>(hwnd_), nullptr);
    DestroyMenu(m);
    return r;
}

void CanvasWindow::message(const std::string& text, bool error) {
    std::fprintf(stderr, "[MonteCalc] %s\n", text.c_str());
    w32::messageBox(hwnd_ ? static_cast<HWND>(hwnd_) : w32::excelWindow(), wide(tr(text)), L"MonteCalc", error);
}

std::string CanvasWindow::svg(int w, int h, double scale) {
    SvgPainter sp(w, h, scale);
    ZoomPainter p(sp, 1.0);
    paint(p, w, h);
    return sp.svg();
}

bool CanvasWindow::exportImage(const std::string& url, const std::string& mime, int w, int h, double scale) {
    w32::ensureGdiplus();
    scale *= zoom_;
    int W = std::max(1, int(w * scale)), H = std::max(1, int(h * scale));
    Gdiplus::Bitmap bmp(W, H, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
        g.FillRectangle(&white, 0, 0, W, H);
        GdiPainter gp(g);
        ZoomPainter p(gp, scale);
        paint(p, w, h);
    }
    CLSID enc;
    if (!w32::encoderFor(wide(mime).c_str(), enc)) return false;
    return bmp.Save(wide(w32::pathOfUrl(url)).c_str(), &enc, nullptr) == Gdiplus::Ok;
}

bool CanvasWindow::storeAsComment(const css::uno::Reference<css::frame::XModel>&, int sheet, int col, int row, const std::string& text) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::string path = narrow(tmp) + "montecalc_comment.png";
    int w = 640, h = 400;
    if (!exportImage(path, "image/png", w, h)) return false;
    ModelDoc d;
    com::Disp range = d.cell(css::table::CellAddress(sal_Int16(sheet), col, row)).impl()->range;
    range.call("ClearComments");
    com::Disp comment = range.obj("AddComment", {com::Var(text.empty() ? std::string(" ") : text)});
    com::Disp shape = comment.obj("Shape");
    shape.obj("Fill").call("UserPicture", {com::Var(path)});
    shape.put("Width", com::Var(double(w) * 0.6));
    shape.put("Height", com::Var(double(h) * 0.6));
    DeleteFileW(wide(path).c_str());
    return true;
}

// ================================================================ messages and forms (uno/ui.hpp)

void showMessage(const css::uno::Reference<css::uno::XComponentContext>&, const css::uno::Reference<css::frame::XFrame>&, const std::string& title,
                 const std::string& text, bool error) {
    w32::messageBox(w32::excelWindow(), wide(tr(text)), wide(tr(title)), error);
}

namespace {
struct FormState {
    bool done = false, ok = false;
};
LRESULT CALLBACK formProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<FormState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        static HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
        return reinterpret_cast<LRESULT>(b);
    }
    case WM_COMMAND:
        if (st && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
            st->ok = LOWORD(wp) == IDOK;
            st->done = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        if (st) st->done = true;
        return 0;
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
} // namespace

bool showForm(const css::uno::Reference<css::uno::XComponentContext>&, const std::string& title, std::vector<FormField>& fields, int width) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = formProc;
        wc.hInstance = w32::module();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"MonteCalcForm";
        wc.hIcon = w32::logoIcon(false);
        wc.hIconSm = w32::logoIcon(true);
        RegisterClassExW(&wc);
        registered = true;
    }
    int bx, by;
    w32::baseUnits(bx, by);
    auto X = [&](int du) { return MulDiv(du, bx, 4); };
    auto Y = [&](int du) { return MulDiv(du, by, 8); };
    // the label column fits the longest label (no shrunken fonts); the form widens when needed
    int labelW = width * 45 / 100;
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, w32::uiFont(0));
        for (const auto& f : fields) {
            if (f.isCheck || f.lines > 1 || f.label.empty()) continue;
            std::wstring t = wide(tr(f.label));
            SIZE sz;
            GetTextExtentPoint32W(dc, t.c_str(), int(t.size()), &sz);
            labelW = std::max(labelW, MulDiv(sz.cx, 4, bx) + 12);
        }
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
        width = std::max(width, labelW + 110);
    }
    const int rowH = 14;
    int y = 8;
    std::vector<int> rowY;
    for (const auto& f : fields) {
        rowY.push_back(y);
        y += f.lines > 1 ? 10 + 10 * f.lines : rowH;
    }
    int height = y + 24;
    RECT r{0, 0, X(width), Y(height)};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&r, style, FALSE, 0);   // a plain frame: the title bar shows the MC FLO logo
    HWND owner = GetActiveWindow();
    if (!owner) owner = w32::excelWindow();
    FormState st;
    HWND h = CreateWindowExW(0, L"MonteCalcForm", wide(tr(title)).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                             r.bottom - r.top, owner, nullptr, w32::module(), &st);
    if (!h) return false;
    HFONT font = w32::uiFont(0);
    auto make = [&](const wchar_t* cls, const std::wstring& text, DWORD s, DWORD ex, int x, int yy, int w, int hh, int id) {
        HWND c = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | s, X(x), Y(yy), X(w), Y(hh), h,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), w32::module(), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        return c;
    };
    std::vector<HWND> ctl(fields.size());
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        int yy = rowY[i], id = 2000 + int(i);
        if (f.isCheck) {
            ctl[i] = make(L"Button", wide(tr(f.label)), BS_AUTOCHECKBOX | WS_TABSTOP, 0, 8, yy, width - 16, 12, id);
            SendMessageW(ctl[i], BM_SETCHECK, f.checked ? BST_CHECKED : BST_UNCHECKED, 0);
        } else if (f.lines > 1) {
            if (!f.label.empty()) make(L"Static", wide(tr(f.label)), SS_LEFT, 0, 8, yy, width - 16, 10, -1);
            ctl[i] = make(L"Edit", w32::editText(f.value), ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN | WS_TABSTOP | (f.readOnly ? ES_READONLY : 0),
                          WS_EX_CLIENTEDGE, 8, yy + 10, width - 16, 10 * f.lines - 2, id);
        } else {
            HWND l = make(L"Static", wide(tr(f.label)), SS_LEFTNOWORDWRAP, 0, 8, yy + 2, labelW - 4, 10, -1);
            fitText(l, "label");
            if (!f.options.empty()) {
                ctl[i] = make(L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, labelW + 8, yy, width - labelW - 16, 12 + 120, id);
                int sel = 0;
                for (size_t k = 0; k < f.options.size(); ++k) {
                    SendMessageW(ctl[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(trExact(f.options[k])).c_str()));
                    if (f.options[k] == f.value) sel = int(k);
                }
                SendMessageW(ctl[i], CB_SETCURSEL, WPARAM(sel), 0);
            } else {
                ctl[i] = make(L"Edit", wide(f.value), ES_AUTOHSCROLL | WS_TABSTOP | (f.readOnly ? ES_READONLY : 0), WS_EX_CLIENTEDGE, labelW + 8, yy,
                              width - labelW - 16, 12, id);
            }
        }
    }
    make(L"Button", wide(tr("OK")), BS_DEFPUSHBUTTON | WS_TABSTOP, 0, width - 108, height - 20, 48, 14, IDOK);
    make(L"Button", wide(tr("Cancel")), BS_PUSHBUTTON | WS_TABSTOP, 0, width - 56, height - 20, 48, 14, IDCANCEL);
    // centre on the owner
    RECT o, me;
    GetWindowRect(owner ? owner : GetDesktopWindow(), &o);
    GetWindowRect(h, &me);
    SetWindowPos(h, nullptr, o.left + ((o.right - o.left) - (me.right - me.left)) / 2, o.top + ((o.bottom - o.top) - (me.bottom - me.top)) / 3, 0, 0,
                 SWP_NOZORDER | SWP_NOSIZE);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    if (!ctl.empty() && ctl[0]) SetFocus(ctl[0]);
    MSG m;
    while (!st.done && GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE && (m.hwnd == h || IsChild(h, m.hwnd))) { st.done = true; break; }
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN && (m.hwnd == h || IsChild(h, m.hwnd))) {
            wchar_t cls[16];
            GetClassNameW(m.hwnd, cls, 16);
            if (!(_wcsicmp(cls, L"Edit") == 0 && (GetWindowLongW(m.hwnd, GWL_STYLE) & ES_MULTILINE))) { st.ok = true; st.done = true; break; }
        }
        if (!IsDialogMessageW(h, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (st.ok)
        for (size_t i = 0; i < fields.size(); ++i) {
            if (!ctl[i]) continue;
            if (fields[i].isCheck) fields[i].checked = SendMessageW(ctl[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
            else if (!fields[i].options.empty() && fields[i].lines <= 1) {
                int pos = int(SendMessageW(ctl[i], CB_GETCURSEL, 0, 0));
                if (pos >= 0 && pos < int(fields[i].options.size())) fields[i].value = fields[i].options[size_t(pos)];
            } else {
                fields[i].value = w32::fromEdit(w32::windowText(ctl[i]));
            }
        }
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(h);
    if (owner) SetActiveWindow(owner);
    return st.ok;
}

} // namespace montecalc
#endif
