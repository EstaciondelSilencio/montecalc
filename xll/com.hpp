// MonteCalc for Excel - late-bound access to Excel's object model (COM / IDispatch).
//
// Windows and ribbon callbacks may not use the C API (only functions and commands that Excel is
// running may), so they read and change the workbook through Excel's Application object.
#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oaidl.h>
#include <ole2.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace montecalc {
namespace com {

std::wstring wide(const std::string& utf8);
std::string narrow(const wchar_t* w, int n = -1);

class Disp;

// a VARIANT that owns its value
class Var {
public:
    Var() { VariantInit(&v_); }
    Var(double d) { VariantInit(&v_); v_.vt = VT_R8; v_.dblVal = d; }
    Var(int i) { VariantInit(&v_); v_.vt = VT_I4; v_.lVal = i; }
    Var(bool b) { VariantInit(&v_); v_.vt = VT_BOOL; v_.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; }
    Var(const std::string& s);
    Var(const char* s) : Var(std::string(s)) {}
    Var(const Disp& d);
    Var(const VARIANT& v) { VariantInit(&v_); VariantCopy(&v_, const_cast<VARIANT*>(&v)); }
    Var(const Var& o) { VariantInit(&v_); VariantCopy(&v_, const_cast<VARIANT*>(&o.v_)); }
    Var& operator=(const Var& o) {
        if (this != &o) { VariantClear(&v_); VariantCopy(&v_, const_cast<VARIANT*>(&o.v_)); }
        return *this;
    }
    ~Var() { VariantClear(&v_); }
    static Var missing() { Var m; m.v_.vt = VT_ERROR; m.v_.scode = DISP_E_PARAMNOTFOUND; return m; }
    // a 2-D array (rows x columns) of values, e.g. for Range.Value
    static Var table(const std::vector<std::vector<Var>>& rows);

    VARIANT* get() { return &v_; }
    const VARIANT& raw() const { return v_; }
    VARTYPE type() const { return v_.vt; }
    bool isEmpty() const { return v_.vt == VT_EMPTY || v_.vt == VT_NULL; }
    bool isError() const { return v_.vt == VT_ERROR; }
    bool isNumber() const;
    double num(double def = 0) const;
    std::string str() const;
    bool boolean() const;
    Disp disp() const;
    // Range.Value / .Formula of several cells: rows x columns (a single cell gives 1 x 1)
    std::vector<std::vector<Var>> rows() const;

private:
    VARIANT v_;
};

// an IDispatch pointer with name-based calls
class Disp {
public:
    Disp() = default;
    explicit Disp(IDispatch* p, bool addRef = true) : p_(p) { if (p_ && addRef) p_->AddRef(); }
    Disp(const Disp& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Disp& operator=(const Disp& o) {
        if (o.p_) o.p_->AddRef();
        if (p_) p_->Release();
        p_ = o.p_;
        return *this;
    }
    ~Disp() { if (p_) p_->Release(); }
    bool valid() const { return p_ != nullptr; }
    IDispatch* ptr() const { return p_; }

    Var get(const char* name, const std::vector<Var>& args = {}) const;    // property get or method call
    Disp obj(const char* name, const std::vector<Var>& args = {}) const { return get(name, args).disp(); }
    void put(const char* name, const Var& value, const std::vector<Var>& args = {}) const;
    Var call(const char* name, const std::vector<Var>& args = {}) const { return get(name, args); }

private:
    DISPID id(const char* name) const;
    IDispatch* p_ = nullptr;
};

// Excel's Application object (set by the ribbon add-in, or found in the running Excel)
Disp& application();
bool haveApplication();
void setApplication(IDispatch* app);
bool findApplication(HWND excelMain);   // through the workbook window (accessibility) or the ROT

// runs an XLL command (a registered macro, e.g. "MCFLO.Start") through Application.Run
void runMacro(const std::string& name);

} // namespace com
} // namespace montecalc
#endif
