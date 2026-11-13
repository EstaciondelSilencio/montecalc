// MonteCalc for Excel - late-bound access to Excel's object model
#ifdef _WIN32
#include "com.hpp"

#include <oleacc.h>

#include <algorithm>
#include <cstdio>

namespace montecalc {
namespace com {

// Excel's late-bound members follow the caller's locale: with German or Spanish settings,
// Range.Formula would expect ";" between arguments and "," as decimal mark. MonteCalc always
// speaks English (US) to Excel, as VBA does; the formulas are converted to that syntax.
constexpr LCID kEnglishUS = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);


std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

std::string narrow(const wchar_t* w, int n) {
    if (!w) return {};
    if (n < 0) n = int(wcslen(w));
    if (n == 0) return {};
    int m = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, &s[0], m, nullptr, nullptr);
    return s;
}

Var::Var(const std::string& s) {
    VariantInit(&v_);
    v_.vt = VT_BSTR;
    std::wstring w = wide(s);
    v_.bstrVal = SysAllocStringLen(w.data(), UINT(w.size()));
}

Var::Var(const Disp& d) {
    VariantInit(&v_);
    v_.vt = VT_DISPATCH;
    v_.pdispVal = d.ptr();
    if (v_.pdispVal) v_.pdispVal->AddRef();
}

Var Var::table(const std::vector<std::vector<Var>>& rows) {
    size_t w = 0;
    for (auto& r : rows) w = std::max(w, r.size());
    SAFEARRAYBOUND b[2] = {{ULONG(rows.size()), 1}, {ULONG(w), 1}};
    SAFEARRAY* sa = SafeArrayCreate(VT_VARIANT, 2, b);
    for (size_t i = 0; i < rows.size(); ++i)
        for (size_t j = 0; j < w; ++j) {
            LONG idx[2] = {LONG(i + 1), LONG(j + 1)};
            Var cell = j < rows[i].size() ? rows[i][j] : Var(std::string());
            SafeArrayPutElement(sa, idx, cell.get());
        }
    Var t;
    t.v_.vt = VT_ARRAY | VT_VARIANT;
    t.v_.parray = sa;
    return t;
}

bool Var::isNumber() const {
    switch (v_.vt) {
    case VT_R8: case VT_R4: case VT_I4: case VT_I2: case VT_UI1: case VT_INT: case VT_CY: case VT_DATE: case VT_I8: return true;
    default: return false;
    }
}

double Var::num(double def) const {
    if (v_.vt == VT_BOOL) return v_.boolVal ? 1 : 0;
    if (!isNumber()) return def;
    VARIANT d;
    VariantInit(&d);
    if (FAILED(VariantChangeType(&d, const_cast<VARIANT*>(&v_), 0, VT_R8))) return def;
    return d.dblVal;
}

std::string Var::str() const {
    if (v_.vt == VT_BSTR) return narrow(v_.bstrVal, int(SysStringLen(v_.bstrVal)));
    if (v_.vt == VT_EMPTY || v_.vt == VT_NULL || v_.vt == VT_ERROR) return {};
    VARIANT d;
    VariantInit(&d);
    if (FAILED(VariantChangeTypeEx(&d, const_cast<VARIANT*>(&v_), MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT), 0, VT_BSTR))) return {};
    std::string s = narrow(d.bstrVal, int(SysStringLen(d.bstrVal)));
    VariantClear(&d);
    return s;
}

bool Var::boolean() const {
    if (v_.vt == VT_BOOL) return v_.boolVal != VARIANT_FALSE;
    return num(0) != 0;
}

Disp Var::disp() const {
    if (v_.vt == VT_DISPATCH) return Disp(v_.pdispVal);
    if (v_.vt == VT_UNKNOWN && v_.punkVal) {
        IDispatch* d = nullptr;
        if (SUCCEEDED(v_.punkVal->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&d)))) return Disp(d, false);
    }
    return Disp();
}

std::vector<std::vector<Var>> Var::rows() const {
    std::vector<std::vector<Var>> r;
    if (!(v_.vt & VT_ARRAY)) {
        r.push_back({*this});
        return r;
    }
    SAFEARRAY* sa = v_.parray;
    if (!sa || SafeArrayGetDim(sa) != 2) return r;
    LONG r0, r1, c0, c1;
    SafeArrayGetLBound(sa, 1, &r0);
    SafeArrayGetUBound(sa, 1, &r1);
    SafeArrayGetLBound(sa, 2, &c0);
    SafeArrayGetUBound(sa, 2, &c1);
    for (LONG i = r0; i <= r1; ++i) {
        std::vector<Var> row;
        for (LONG j = c0; j <= c1; ++j) {
            LONG idx[2] = {i, j};
            VARIANT e;
            VariantInit(&e);
            SafeArrayGetElement(sa, idx, &e);
            row.emplace_back(e);
            VariantClear(&e);
        }
        r.push_back(std::move(row));
    }
    return r;
}

DISPID Disp::id(const char* name) const {
    if (!p_) throw std::runtime_error(std::string("Excel object missing for ") + name);
    std::wstring w = wide(name);
    LPOLESTR n = &w[0];
    DISPID d;
    if (FAILED(p_->GetIDsOfNames(IID_NULL, &n, 1, kEnglishUS, &d))) throw std::runtime_error(std::string("Excel: unknown member ") + name);
    return d;
}

namespace {
std::string invokeError(const char* name, HRESULT hr, EXCEPINFO& ei) {
    // Excel fills the exception details in only on request
    if (hr == DISP_E_EXCEPTION && ei.pfnDeferredFillIn) ei.pfnDeferredFillIn(&ei);
    std::string m = std::string("Excel: ") + name + " failed";
    if (ei.bstrDescription && *ei.bstrDescription) m += ": " + narrow(ei.bstrDescription);
    char b[48];
    HRESULT code = hr == DISP_E_EXCEPTION && ei.scode ? ei.scode : hr;
    std::snprintf(b, sizeof b, " (0x%08lX)", static_cast<unsigned long>(code));
    m += b;
    return m;
}
} // namespace

Var Disp::get(const char* name, const std::vector<Var>& args) const {
    DISPID d = id(name);
    std::vector<VARIANT> a(args.size());
    for (size_t i = 0; i < args.size(); ++i) a[args.size() - 1 - i] = args[i].raw();   // reverse order
    DISPPARAMS dp{a.empty() ? nullptr : a.data(), nullptr, UINT(a.size()), 0};
    Var r;
    EXCEPINFO ei{};
    HRESULT hr = p_->Invoke(d, IID_NULL, kEnglishUS, DISPATCH_METHOD | DISPATCH_PROPERTYGET, &dp, r.get(), &ei, nullptr);
    if (FAILED(hr)) {
        std::string m = invokeError(name, hr, ei);
        SysFreeString(ei.bstrSource);
        SysFreeString(ei.bstrDescription);
        SysFreeString(ei.bstrHelpFile);
        throw std::runtime_error(m);
    }
    return r;
}

void Disp::put(const char* name, const Var& value, const std::vector<Var>& args) const {
    DISPID d = id(name);
    std::vector<VARIANT> a(args.size() + 1);
    a[0] = value.raw();
    for (size_t i = 0; i < args.size(); ++i) a[args.size() - i] = args[i].raw();
    DISPID named = DISPID_PROPERTYPUT;
    DISPPARAMS dp{a.data(), &named, UINT(a.size()), 1};
    EXCEPINFO ei{};
    HRESULT hr = p_->Invoke(d, IID_NULL, kEnglishUS, DISPATCH_PROPERTYPUT, &dp, nullptr, &ei, nullptr);
    if (FAILED(hr)) {
        std::string m = invokeError(name, hr, ei);
        SysFreeString(ei.bstrSource);
        SysFreeString(ei.bstrDescription);
        SysFreeString(ei.bstrHelpFile);
        throw std::runtime_error(m);
    }
}

namespace {
Disp g_app;

BOOL CALLBACK findExcel7(HWND h, LPARAM out) {
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (wcscmp(cls, L"EXCEL7") == 0) {
        *reinterpret_cast<HWND*>(out) = h;
        return FALSE;
    }
    return TRUE;
}
} // namespace

Disp& application() {
    if (!g_app.valid()) throw std::runtime_error("MonteCalc: Excel's Application object is not available yet (open a workbook).");
    return g_app;
}
bool haveApplication() { return g_app.valid(); }
void setApplication(IDispatch* app) { g_app = Disp(app); }

bool findApplication(HWND excelMain) {
    if (g_app.valid()) return true;
    // a workbook window gives the Window object, and that the Application
    HWND w = nullptr;
    if (excelMain) EnumChildWindows(excelMain, findExcel7, reinterpret_cast<LPARAM>(&w));
    if (w) {
        IDispatch* win = nullptr;
        if (SUCCEEDED(AccessibleObjectFromWindow(w, DWORD(OBJID_NATIVEOM), IID_IDispatch, reinterpret_cast<void**>(&win))) && win) {
            try {
                Disp window(win, false);
                Disp app = window.obj("Application");
                if (app.valid()) { g_app = app; return true; }
            } catch (...) {
            }
        }
    }
    // the running object table (the first Excel started registers itself there)
    CLSID cls;
    if (SUCCEEDED(CLSIDFromProgID(L"Excel.Application", &cls))) {
        IUnknown* unk = nullptr;
        if (SUCCEEDED(GetActiveObject(cls, nullptr, &unk)) && unk) {
            IDispatch* app = nullptr;
            unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&app));
            unk->Release();
            if (app) { g_app = Disp(app, false); return true; }
        }
    }
    return false;
}

void runMacro(const std::string& name) { application().call("Run", {Var(name)}); }

} // namespace com
} // namespace montecalc
#endif
