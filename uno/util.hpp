// MonteCalc - small string helpers shared by the UNO components
#pragma once

#include <com/sun/star/uno/Sequence.hxx>
#include <rtl/ustring.hxx>

#include <initializer_list>

#include <cstdlib>
#include <string>

namespace montecalc {

using rtl::OUString;

// Build a UNO sequence from a list (the public SDK headers lack the
// initializer_list constructor).
template <class T>
com::sun::star::uno::Sequence<T> seq(std::initializer_list<T> l) {
    com::sun::star::uno::Sequence<T> s(static_cast<sal_Int32>(l.size()));
    T* p = s.getArray();
    for (const T& v : l) *p++ = v;
    return s;
}

inline std::string u8(const rtl::OUString& s) {
    return rtl::OUStringToOString(s, RTL_TEXTENCODING_UTF8).getStr();
}

inline rtl::OUString ou(const std::string& s) {
    return rtl::OStringToOUString(rtl::OString(s.c_str(), s.size()), RTL_TEXTENCODING_UTF8);
}

inline double parseNumber(const std::string& s, bool& ok) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    ok = end && end != s.c_str() && *end == '\0';
    return v;
}

} // namespace montecalc
