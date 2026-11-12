// MonteCalc for Excel - formulas between LibreOffice syntax (shared code) and Excel syntax
#include "xlformula.hpp"

#include <algorithm>
#include <cctype>

namespace montecalc {
namespace xlf {


bool isIdStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || (static_cast<unsigned char>(c) & 0x80); }
bool isIdChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || (static_cast<unsigned char>(c) & 0x80); }

// is s[i..] a cell reference ($A$1, A1, AB12)? returns its length
size_t cellRefLength(const std::string& s, size_t i) {
    size_t k = i;
    if (k < s.size() && s[k] == '$') ++k;
    size_t c0 = k;
    while (k < s.size() && std::isalpha(static_cast<unsigned char>(s[k]))) ++k;
    if (k == c0 || k - c0 > 3) return 0;
    if (k < s.size() && s[k] == '$') ++k;
    size_t r0 = k;
    while (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) ++k;
    if (k == r0) return 0;
    if (k < s.size() && (isIdChar(s[k]) && s[k] != '.')) return 0;
    return k - i;
}

// LibreOffice -> Excel: ";" -> "," (in arrays: ";" -> "," and "|" -> ";"), $'Sheet'.A1 / Sheet.A1 -> 'Sheet'!A1
std::string toExcel(const std::string& f) {
    std::string out;
    bool inArray = false;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if (c == '"') {   // string literal (doubled quotes inside)
            size_t j = i + 1;
            while (j < f.size()) {
                if (f[j] == '"') {
                    if (j + 1 < f.size() && f[j + 1] == '"') { j += 2; continue; }
                    break;
                }
                ++j;
            }
            out.append(f, i, std::min(f.size(), j + 1) - i);
            i = j + 1;
            continue;
        }
        if (c == '{') inArray = true;
        if (c == '}') inArray = false;
        if (c == ';') { out += ','; ++i; continue; }
        if (c == '|' && inArray) { out += ';'; ++i; continue; }
        // sheet references
        size_t k = i;
        if (f[k] == '$' && k + 1 < f.size() && (f[k + 1] == '\'' || isIdStart(f[k + 1]))) ++k;
        std::string sheet;
        size_t afterSheet = 0;
        if (k < f.size() && f[k] == '\'') {
            size_t j = k + 1;
            std::string name;
            while (j < f.size()) {
                if (f[j] == '\'') {
                    if (j + 1 < f.size() && f[j + 1] == '\'') { name += '\''; j += 2; continue; }
                    break;
                }
                name += f[j++];
            }
            if (j + 1 < f.size() && f[j + 1] == '.') { sheet = name; afterSheet = j + 2; }
        } else if (k < f.size() && isIdStart(f[k]) && (i == 0 || !isIdChar(f[i - 1]))) {
            size_t j = k;
            while (j < f.size() && (std::isalnum(static_cast<unsigned char>(f[j])) || f[j] == '_' || (static_cast<unsigned char>(f[j]) & 0x80))) ++j;
            if (j < f.size() && f[j] == '.' && cellRefLength(f, j + 1)) { sheet = f.substr(k, j - k); afterSheet = j + 1; }
        }
        if (!sheet.empty()) {
            std::string q = "'";
            for (char ch : sheet) q += ch == '\'' ? std::string("''") : std::string(1, ch);
            q += "'!";
            out += q;
            i = afterSheet;
            size_t n = cellRefLength(f, i);
            out.append(f, i, n);
            i += n;
            // range end "Sheet.B2" of the same sheet: Excel names the sheet once
            if (i < f.size() && f[i] == ':') {
                out += ':';
                ++i;
                size_t m = i;
                if (m < f.size() && f[m] == '$') ++m;
                if (m < f.size() && f[m] == '\'') {
                    size_t j = m + 1;
                    while (j < f.size() && !(f[j] == '\'' && (j + 1 >= f.size() || f[j + 1] != '\''))) j += f[j] == '\'' ? 2 : 1;
                    if (j + 1 < f.size() && f[j + 1] == '.') i = j + 2;
                } else {
                    size_t j = m;
                    while (j < f.size() && (std::isalnum(static_cast<unsigned char>(f[j])) || f[j] == '_')) ++j;
                    if (j < f.size() && f[j] == '.' && cellRefLength(f, j + 1)) i = j + 1;
                }
            }
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// Excel -> LibreOffice
std::string fromExcel(const std::string& f) {
    std::string out;
    bool inArray = false;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if (c == '"') {
            size_t j = i + 1;
            while (j < f.size()) {
                if (f[j] == '"') {
                    if (j + 1 < f.size() && f[j + 1] == '"') { j += 2; continue; }
                    break;
                }
                ++j;
            }
            out.append(f, i, std::min(f.size(), j + 1) - i);
            i = j + 1;
            continue;
        }
        if (c == '{') inArray = true;
        if (c == '}') inArray = false;
        if (c == ',') { out += ';'; ++i; continue; }
        if (c == ';' && inArray) { out += '|'; ++i; continue; }
        // 'Sheet'!A1 or Sheet!A1
        if (c == '\'') {
            size_t j = i + 1;
            std::string name;
            while (j < f.size()) {
                if (f[j] == '\'') {
                    if (j + 1 < f.size() && f[j + 1] == '\'') { name += '\''; j += 2; continue; }
                    break;
                }
                name += f[j++];
            }
            if (j + 1 < f.size() && f[j + 1] == '!') {
                std::string q = "$'";
                for (char ch : name) q += ch == '\'' ? std::string("''") : std::string(1, ch);
                out += q + "'.";
                i = j + 2;
                continue;
            }
        }
        if (isIdStart(c) && (i == 0 || !isIdChar(f[i - 1]))) {
            size_t j = i;
            while (j < f.size() && (std::isalnum(static_cast<unsigned char>(f[j])) || f[j] == '_' || f[j] == '.' || (static_cast<unsigned char>(f[j]) & 0x80))) ++j;
            if (j < f.size() && f[j] == '!') {
                out += "$'" + f.substr(i, j - i) + "'.";
                i = j + 1;
                continue;
            }
            out.append(f, i, j - i);
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

} // namespace xlf
} // namespace montecalc
