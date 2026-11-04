// MonteCalc - user interface translation (English, German, Spanish)
#include "i18n.hpp"
#include "util.hpp"

#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/configuration/theDefaultProvider.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {
namespace {

struct Entry { const char* t[3]; };
const Entry kCatalog[] = {
#include "i18n_catalog.inc"
};

std::mutex g_mtx;
Reference<uno::XComponentContext> g_ctx;
bool g_resolved = false;
Lang g_lang = Lang::EN;

struct Pattern {
    std::vector<std::string> parts;   // literal pieces around the {} holes (holes = parts.size() - 1)
    const Entry* e;
};
struct Tables {
    std::unordered_map<std::string, const Entry*> exact;
    std::vector<Pattern> patterns;
    std::vector<std::pair<std::string, const Entry*>> phrases;   // longest first
};

std::vector<std::string> splitHoles(const std::string& s) {
    std::vector<std::string> parts;
    size_t pos = 0;
    for (;;) {
        size_t h = s.find("{}", pos);
        parts.push_back(s.substr(pos, h == std::string::npos ? std::string::npos : h - pos));
        if (h == std::string::npos) break;
        pos = h + 2;
    }
    return parts;
}

const Tables& tables() {
    static Tables t = [] {
        Tables r;
        for (const auto& e : kCatalog) {
            std::string en = e.t[0];
            if (en.find("{}") != std::string::npos) r.patterns.push_back({splitHoles(en), &e});
            else {
                r.exact.emplace(en, &e);
                if (en.find(' ') != std::string::npos && en.size() >= 8) r.phrases.emplace_back(en, &e);
            }
        }
        std::stable_sort(r.phrases.begin(), r.phrases.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
        return r;
    }();
    return t;
}

// matches s against the literal pieces; fills the hole contents (non-empty, shortest first)
bool matchParts(const std::string& s, const std::vector<std::string>& parts, size_t k, size_t pos, std::vector<std::string>& holes) {
    if (k == parts.size()) return pos == s.size();
    const std::string& lit = parts[k];
    if (k == 0) return s.compare(0, lit.size(), lit) == 0 && matchParts(s, parts, 1, lit.size(), holes);
    // hole k-1 starts at pos, then lit follows
    if (k == parts.size() - 1 && lit.empty()) {   // trailing hole
        if (pos >= s.size()) return false;
        holes.push_back(s.substr(pos));
        return true;
    }
    for (size_t at = s.find(lit, pos + 1); at != std::string::npos; at = s.find(lit, at + 1)) {
        holes.push_back(s.substr(pos, at - pos));
        if (matchParts(s, parts, k + 1, at + lit.size(), holes)) return true;
        holes.pop_back();
    }
    return false;
}

bool wordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || (static_cast<unsigned char>(c) & 0x80); }

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Lang fromName(const std::string& name) {
    std::string n = lower(name);
    if (n.rfind("de", 0) == 0) return Lang::DE;
    if (n.rfind("es", 0) == 0) return Lang::ES;
    return Lang::EN;
}

std::string settingFile() { return documentsDir() + "/MC_FLO_language.txt"; }

std::string configValue(const char* path, const char* prop) {
    if (!g_ctx.is()) return {};
    try {
        auto provider = configuration::theDefaultProvider::get(g_ctx);
        beans::NamedValue nv;
        nv.Name = "nodepath";
        nv.Value <<= OUString::createFromAscii(path);
        Sequence<Any> args(1);
        args.getArray()[0] <<= nv;
        Reference<container::XNameAccess> na(
            provider->createInstanceWithArguments("com.sun.star.configuration.ConfigurationAccess", args), UNO_QUERY_THROW);
        OUString v;
        na->getByName(OUString::createFromAscii(prop)) >>= v;
        return u8(v);
    } catch (...) {
        return {};
    }
}

Lang resolve() {
    std::string choice = languageSetting();
    if (lower(choice) != "auto") return fromName(choice == "Español" ? "es" : choice);
    std::string loc = configValue("/org.openoffice.Setup/L10N", "ooLocale");
    if (loc.empty()) loc = configValue("/org.openoffice.System/L10N", "UILocale");
    if (loc.empty()) loc = envOr("LANG", "en");
    return fromName(loc);
}

std::unordered_map<std::string, std::string>& cache() {
    static std::unordered_map<std::string, std::string> c;
    return c;
}

std::string translate(const std::string& s, Lang l, bool exactOnly) {
    const Tables& t = tables();
    int li = int(l);
    auto it = t.exact.find(s);
    if (it != t.exact.end()) return it->second->t[li];
    if (exactOnly) return s;
    for (const auto& p : t.patterns) {
        std::vector<std::string> holes;
        if (!matchParts(s, p.parts, 0, 0, holes)) continue;
        // holes may themselves be texts ("{} - {}" style compositions)
        std::vector<std::string> parts = splitHoles(p.e->t[li]);
        std::string out = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) out += (i - 1 < holes.size() ? translate(holes[i - 1], l, false) : "") + parts[i];
        return out;
    }
    // phrases inside a longer text (whole words only)
    std::string out;
    bool changed = false;
    for (size_t i = 0; i < s.size();) {
        bool hit = false;
        if (i == 0 || !wordChar(s[i - 1])) {
            for (const auto& [en, e] : t.phrases) {
                if (en.size() > s.size() - i || s.compare(i, en.size(), en) != 0) continue;
                size_t end = i + en.size();
                if (end < s.size() && wordChar(s[end])) continue;
                out += e->t[li];
                i = end;
                hit = changed = true;
                break;
            }
        }
        if (!hit) out += s[i++];
    }
    return changed ? out : s;
}

std::string lookup(const std::string& s, bool exactOnly) {
    Lang l = uiLanguage();
    if (l == Lang::EN || s.empty()) return s;
    std::lock_guard<std::mutex> g(g_mtx);
    std::string key = (exactOnly ? "1" : "0") + s;
    auto& c = cache();
    auto it = c.find(key);
    if (it != c.end()) return it->second;
    std::string r = translate(s, l, exactOnly);
    // development aid: MONTECALC_I18N_LOG=<file> collects the texts that have no translation
    static const char* logFile = std::getenv("MONTECALC_I18N_LOG");
    if (logFile && r == s) {
        bool letters = false;
        for (char ch : s) letters = letters || std::isalpha(static_cast<unsigned char>(ch));
        if (letters) {
            std::ofstream f(logFile, std::ios::app);
            f << (exactOnly ? "[list] " : "") << s << "\n----\n";
        }
    }
    if (c.size() > 20000) c.clear();
    c.emplace(key, r);
    return r;
}

} // namespace

void i18nSetContext(const Reference<uno::XComponentContext>& ctx) {
    std::lock_guard<std::mutex> g(g_mtx);
    if (!g_ctx.is() && ctx.is()) {
        g_ctx = ctx;
        g_resolved = false;   // the LibreOffice language can be read now
    }
}

Lang uiLanguage() {
    std::lock_guard<std::mutex> g(g_mtx);
    if (!g_resolved) {
        g_lang = resolve();
        g_resolved = g_ctx.is();   // without the context, try again later
    }
    return g_lang;
}

std::string languageSetting() {
    std::ifstream f(settingFile());
    std::string line;
    if (f && std::getline(f, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        if (line == "English" || line == "Deutsch" || line == "Español" || line == "Auto") return line;
    }
    return "Auto";
}

void setLanguageSetting(const std::string& choice) {
    {
        std::ofstream f(settingFile());
        if (f) f << choice << "\n";
    }
    std::lock_guard<std::mutex> g(g_mtx);
    g_resolved = false;
    cache().clear();
}

std::string tr(const std::string& english) { return lookup(english, false); }
std::string trExact(const std::string& english) { return lookup(english, true); }

} // namespace montecalc
