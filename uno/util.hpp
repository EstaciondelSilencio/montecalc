// MonteCalc - small string helpers shared by the UNO components
#pragma once

#ifdef MONTECALC_XLL
#include "../xll/uno_shim.hpp"
#else
#include <com/sun/star/uno/Sequence.hxx>
#include <rtl/ustring.hxx>
#endif

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

#ifdef MONTECALC_XLL
inline std::string u8(const rtl::OUString& s) { return s.utf8(); }
inline rtl::OUString ou(const std::string& s) { return rtl::OUString(s); }
#else
inline std::string u8(const rtl::OUString& s) {
    return rtl::OUStringToOString(s, RTL_TEXTENCODING_UTF8).getStr();
}

inline rtl::OUString ou(const std::string& s) {
    return rtl::OStringToOUString(rtl::OString(s.c_str(), s.size()), RTL_TEXTENCODING_UTF8);
}
#endif

// ---- platform helpers (Windows and POSIX) ----
inline std::string envOr(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : def;
}
inline std::string homeDir() {
#ifdef _WIN32
    return envOr("USERPROFILE", envOr("HOMEDRIVE", "C:") + envOr("HOMEPATH", "\\"));
#else
    return envOr("HOME", "/tmp");
#endif
}
inline std::string tempDir() {
#ifdef _WIN32
    return envOr("TEMP", envOr("TMP", homeDir()));
#else
    return envOr("TMPDIR", "/tmp");
#endif
}
inline std::string documentsDir() { return homeDir() + "/Documents"; }
// file URL for a local path ("C:\a\b.png" -> "file:///C:/a/b.png"); URLs pass through
inline std::string fileUrl(std::string path) {
    if (path.rfind("file:", 0) == 0) return path;
    for (auto& c : path) if (c == '\\') c = '/';
    std::string enc;
    for (unsigned char c : path) {
        if (c == ' ') enc += "%20";
        else if (c == '#') enc += "%23";
        else if (c == '%') enc += "%25";
        else enc += char(c);
    }
    return (enc.size() > 1 && enc[1] == ':') ? "file:///" + enc : "file://" + enc;
}

inline double parseNumber(const std::string& s, bool& ok) {
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    ok = end && end != s.c_str() && *end == '\0';
    return v;
}

} // namespace montecalc
