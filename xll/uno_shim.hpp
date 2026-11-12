// MonteCalc - minimal stand-ins for the LibreOffice (UNO) types used by the spreadsheet functions,
// so that uno/addin.cpp and the generated function bodies compile unchanged for the Excel XLL
// (MONTECALC_XLL). Strings are UTF-8; Any holds the value kinds that occur in function arguments.
#pragma once

#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

typedef int32_t sal_Int32;
typedef int16_t sal_Int16;
typedef unsigned char sal_Bool;
#define SAL_CALL

namespace rtl {
class OUString {
public:
    OUString() = default;
    OUString(const char* s) : s_(s ? s : "") {}
    explicit OUString(std::string s) : s_(std::move(s)) {}
    static OUString createFromAscii(const char* s) { return OUString(s); }
    bool isEmpty() const { return s_.empty(); }
    sal_Int32 getLength() const { return sal_Int32(s_.size()); }
    bool equalsIgnoreAsciiCaseAscii(const char* o) const {
        std::string a = s_, b = o ? o : "";
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        return true;
    }
    bool equalsAscii(const char* o) const { return s_ == (o ? o : ""); }
    bool operator==(const OUString& o) const { return s_ == o.s_; }
    bool operator!=(const OUString& o) const { return s_ != o.s_; }
    bool operator<(const OUString& o) const { return s_ < o.s_; }
    OUString operator+(const OUString& o) const { return OUString(s_ + o.s_); }
    const std::string& utf8() const { return s_; }

private:
    std::string s_;
};
} // namespace rtl

namespace com { namespace sun { namespace star {
namespace uno {

template <class T>
class Sequence {
public:
    Sequence() = default;
    explicit Sequence(sal_Int32 n) : v_(size_t(n < 0 ? 0 : n)) {}
    Sequence(std::initializer_list<T> l) : v_(l) {}
    sal_Int32 getLength() const { return sal_Int32(v_.size()); }
    bool hasElements() const { return !v_.empty(); }
    T* getArray() { return v_.data(); }
    const T* getConstArray() const { return v_.data(); }
    const T& operator[](sal_Int32 i) const { return v_[size_t(i)]; }
    T& operator[](sal_Int32 i) { return v_[size_t(i)]; }
    void realloc(sal_Int32 n) { v_.resize(size_t(n)); }
    typename std::vector<T>::const_iterator begin() const { return v_.begin(); }
    typename std::vector<T>::const_iterator end() const { return v_.end(); }
    typename std::vector<T>::iterator begin() { return v_.begin(); }
    typename std::vector<T>::iterator end() { return v_.end(); }

private:
    std::vector<T> v_;
};

enum TypeClass { TypeClass_VOID, TypeClass_DOUBLE, TypeClass_BOOLEAN, TypeClass_LONG, TypeClass_STRING, TypeClass_SEQUENCE };

class Any;
using AnyMatrixT = Sequence<Sequence<Any>>;
using DoubleMatrixT = Sequence<Sequence<double>>;

class Any {
public:
    Any() = default;
    Any(double d) : t_(TypeClass_DOUBLE), d_(d) {}
    Any(bool b) : t_(TypeClass_BOOLEAN), d_(b ? 1 : 0) {}
    Any(sal_Int32 i) : t_(TypeClass_LONG), d_(i) {}
    Any(const rtl::OUString& s) : t_(TypeClass_STRING), s_(s) {}
    Any(const AnyMatrixT& m) : t_(TypeClass_SEQUENCE), am_(std::make_shared<AnyMatrixT>(m)) {}
    Any(const DoubleMatrixT& m) : t_(TypeClass_SEQUENCE), dm_(std::make_shared<DoubleMatrixT>(m)) {}
    bool hasValue() const { return t_ != TypeClass_VOID; }
    TypeClass getValueTypeClass() const { return t_; }

    // extraction as in UNO: numbers widen to double, but a boolean is not a number
    bool operator>>=(double& d) const {
        if (t_ != TypeClass_DOUBLE && t_ != TypeClass_LONG) return false;
        d = d_;
        return true;
    }
    bool operator>>=(sal_Int32& i) const {
        if (t_ != TypeClass_LONG) return false;
        i = sal_Int32(d_);
        return true;
    }
    bool operator>>=(bool& b) const {
        if (t_ != TypeClass_BOOLEAN) return false;
        b = d_ != 0;
        return true;
    }
    bool operator>>=(rtl::OUString& s) const {
        if (t_ != TypeClass_STRING) return false;
        s = s_;
        return true;
    }
    bool operator>>=(AnyMatrixT& m) const {
        if (!am_) return false;
        m = *am_;
        return true;
    }
    bool operator>>=(DoubleMatrixT& m) const {
        if (!dm_) return false;
        m = *dm_;
        return true;
    }

private:
    TypeClass t_ = TypeClass_VOID;
    double d_ = 0;
    rtl::OUString s_;
    std::shared_ptr<AnyMatrixT> am_;
    std::shared_ptr<DoubleMatrixT> dm_;
};

struct XInterface {};
struct XComponentContext {};
struct Exception {   // never thrown in Excel; keeps catch clauses of the shared code valid
    rtl::OUString Message;
};
// LibreOffice object references: in Excel the code never holds such objects; an empty
// Reference keeps signatures (context, frame, model) unchanged
template <class T>
class Reference {
public:
    Reference() = default;
    bool is() const { return false; }
    T* get() const { return nullptr; }
    // only the stateless placeholders below (frame -> controller -> model) are reached this way
    T* operator->() const {
        static T placeholder;
        return &placeholder;
    }
};

} // namespace uno

namespace table {
struct CellAddress {
    sal_Int16 Sheet = 0;
    sal_Int32 Column = 0, Row = 0;
    CellAddress() = default;
    CellAddress(sal_Int16 s, sal_Int32 c, sal_Int32 r) : Sheet(s), Column(c), Row(r) {}
};
struct CellRangeAddress {
    sal_Int16 Sheet = 0;
    sal_Int32 StartColumn = 0, StartRow = 0, EndColumn = 0, EndRow = 0;
    CellRangeAddress() = default;
    CellRangeAddress(sal_Int16 s, sal_Int32 c0, sal_Int32 r0, sal_Int32 c1, sal_Int32 r1)
        : Sheet(s), StartColumn(c0), StartRow(r0), EndColumn(c1), EndRow(r1) {}
};
struct XCellRange {};
} // namespace table

namespace frame {
// in Excel the document is the active workbook: ModelDoc(model) ignores the placeholder
struct XModel {};
struct XController {
    uno::Reference<XModel> getModel() const { return {}; }
};
struct XFrame {
    uno::Reference<XController> getController() const { return {}; }
};
} // namespace frame

namespace beans {
struct PropertyValue {
    rtl::OUString Name;
    uno::Any Value;
};
} // namespace beans

namespace awt {
// key codes as LibreOffice's awt::Key (the Win32 window maps virtual keys to them)
namespace Key {
constexpr int NUM0 = 256, NUM1 = 257, NUM9 = 265, A = 512, B = 513, C = 514, D = 515, E = 516, F = 517, G = 518, H = 519, I = 520, J = 521, K = 522, L = 523, M = 524, N = 525, O = 526, P = 527, Q = 528, R = 529, S = 530, T = 531, U = 532, V = 533, W = 534, X = 535, Y = 536, Z = 537, DOWN = 1024, UP = 1025, LEFT = 1026, RIGHT = 1027, HOME = 1028, END = 1029,
              PAGEUP = 1030, PAGEDOWN = 1031, RETURN = 1280, ESCAPE = 1281, TAB = 1282, BACKSPACE = 1283, SPACE = 1284, INSERT = 1285,
              KEY_DELETE = 1286, ADD = 1287, SUBTRACT = 1288, MULTIPLY = 1289, DIVIDE = 1290, F1 = 768, F2 = 769, F3 = 770, F4 = 771, F5 = 772, F6 = 773, F7 = 774, F8 = 775, F9 = 776, F10 = 777, F11 = 778, F12 = 779;
}
} // namespace awt

namespace lang {
struct IllegalArgumentException {
    IllegalArgumentException(const rtl::OUString& m, const uno::Reference<uno::XInterface>&, sal_Int16) : Message(m) {}
    rtl::OUString Message;
};
struct Locale {};
struct XComponent {};
} // namespace lang
}}} // namespace com::sun::star
