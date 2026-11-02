// MonteCalc - AI mentor: ask Claude about the model. The mentor receives the model's
// structure and simulated statistics (not the worksheet cells) and interprets them.
//
// The request is a plain HTTPS POST to the Anthropic Messages API (there is no official
// C++ SDK). libcurl is loaded at run time, so the extension works without it until the
// mentor is used.
#include "canvaswin.hpp"
#include "ui.hpp"
#include "util.hpp"
#include "windows.hpp"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/datatransfer/DataFlavor.hpp>
#include <com/sun/star/datatransfer/XTransferable.hpp>
#include <com/sun/star/datatransfer/clipboard/SystemClipboard.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/util/XCloseable.hpp>
#include <cppuhelper/implbase1.hxx>

#include <dlfcn.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

namespace {

// ---------------------------------------------------------------- minimal JSON
struct Json {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;
    std::string raw;  // exact source text (re-sent unchanged)
    const Json* get(const std::string& k) const {
        for (auto& [key, v] : o) if (key == k) return &v;
        return nullptr;
    }
    std::string str(const std::string& k) const {
        auto* v = get(k);
        return v && v->t == Str ? v->s : std::string();
    }
};

struct JsonParser {
    const std::string& t;
    size_t p = 0;
    explicit JsonParser(const std::string& s) : t(s) {}
    void ws() { while (p < t.size() && std::isspace((unsigned char)t[p])) ++p; }
    Json parse() {
        ws();
        size_t start = p;
        Json j = value();
        j.raw = t.substr(start, p - start);
        return j;
    }
    Json value() {
        ws();
        if (p >= t.size()) throw std::runtime_error("unexpected end of JSON");
        char c = t[p];
        Json j;
        if (c == '{') {
            j.t = Json::Obj;
            ++p;
            ws();
            if (t[p] == '}') { ++p; return j; }
            while (true) {
                ws();
                std::string k = string();
                ws();
                if (t[p++] != ':') throw std::runtime_error("bad JSON object");
                j.o.push_back({k, parse()});
                ws();
                if (t[p] == ',') { ++p; continue; }
                if (t[p++] == '}') break;
                throw std::runtime_error("bad JSON object");
            }
        } else if (c == '[') {
            j.t = Json::Arr;
            ++p;
            ws();
            if (t[p] == ']') { ++p; return j; }
            while (true) {
                j.a.push_back(parse());
                ws();
                if (t[p] == ',') { ++p; continue; }
                if (t[p++] == ']') break;
                throw std::runtime_error("bad JSON array");
            }
        } else if (c == '"') {
            j.t = Json::Str;
            j.s = string();
        } else if (t.compare(p, 4, "true") == 0) { j.t = Json::Bool; j.b = true; p += 4; }
        else if (t.compare(p, 5, "false") == 0) { j.t = Json::Bool; p += 5; }
        else if (t.compare(p, 4, "null") == 0) { p += 4; }
        else {
            size_t e = p;
            while (e < t.size() && std::string("+-0123456789.eE").find(t[e]) != std::string::npos) ++e;
            j.t = Json::Num;
            j.n = std::strtod(t.substr(p, e - p).c_str(), nullptr);
            p = e;
        }
        return j;
    }
    std::string string() {
        if (t[p] != '"') throw std::runtime_error("expected a JSON string");
        ++p;
        std::string r;
        while (p < t.size() && t[p] != '"') {
            char c = t[p++];
            if (c != '\\') { r += c; continue; }
            char e = t[p++];
            switch (e) {
            case 'n': r += '\n'; break;
            case 't': r += '\t'; break;
            case 'r': r += '\r'; break;
            case 'b': r += '\b'; break;
            case 'f': r += '\f'; break;
            case 'u': {
                unsigned cp = std::stoul(t.substr(p, 4), nullptr, 16);
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && t.compare(p, 2, "\\u") == 0) {
                    unsigned lo = std::stoul(t.substr(p + 2, 4), nullptr, 16);
                    p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (cp < 0x80) r += char(cp);
                else if (cp < 0x800) { r += char(0xC0 | (cp >> 6)); r += char(0x80 | (cp & 63)); }
                else if (cp < 0x10000) { r += char(0xE0 | (cp >> 12)); r += char(0x80 | ((cp >> 6) & 63)); r += char(0x80 | (cp & 63)); }
                else { r += char(0xF0 | (cp >> 18)); r += char(0x80 | ((cp >> 12) & 63)); r += char(0x80 | ((cp >> 6) & 63)); r += char(0x80 | (cp & 63)); }
                break;
            }
            default: r += e;
            }
        }
        ++p;
        return r;
    }
};

std::string jsonEscape(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); r += b; }
            else r += char(c);
        }
    }
    return r + "\"";
}

// ---------------------------------------------------------------- libcurl (loaded at run time)
struct Curl {
    void* lib = nullptr;
    void* (*init)() = nullptr;
    int (*setopt)(void*, int, ...) = nullptr;
    int (*perform)(void*) = nullptr;
    void (*cleanup)(void*) = nullptr;
    void* (*slistAppend)(void*, const char*) = nullptr;
    void (*slistFree)(void*) = nullptr;
    int (*getinfo)(void*, int, ...) = nullptr;
    const char* (*strerror)(int) = nullptr;
    bool load() {
        if (lib) return true;
        for (const char* n : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so", "libcurl.4.dylib", "libcurl.dylib"})
            if ((lib = dlopen(n, RTLD_NOW))) break;
        if (!lib) return false;
        init = reinterpret_cast<void* (*)()>(dlsym(lib, "curl_easy_init"));
        setopt = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "curl_easy_setopt"));
        perform = reinterpret_cast<int (*)(void*)>(dlsym(lib, "curl_easy_perform"));
        cleanup = reinterpret_cast<void (*)(void*)>(dlsym(lib, "curl_easy_cleanup"));
        slistAppend = reinterpret_cast<void* (*)(void*, const char*)>(dlsym(lib, "curl_slist_append"));
        slistFree = reinterpret_cast<void (*)(void*)>(dlsym(lib, "curl_slist_free_all"));
        getinfo = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "curl_easy_getinfo"));
        strerror = reinterpret_cast<const char* (*)(int)>(dlsym(lib, "curl_easy_strerror"));
        return init && setopt && perform && cleanup && slistAppend && slistFree && getinfo;
    }
};

size_t writeCb(char* data, size_t size, size_t n, void* user) {
    static_cast<std::string*>(user)->append(data, size * n);
    return size * n;
}

// POST JSON; returns the HTTP status (0 on transport error, message in `err`)
long httpPost(const std::string& url, const std::vector<std::string>& headers, const std::string& body, std::string& out, std::string& err) {
    static Curl curl;
    if (!curl.load()) { err = "libcurl was not found on this system; the AI mentor needs it to reach the API"; return 0; }
    void* h = curl.init();
    if (!h) { err = "could not initialise libcurl"; return 0; }
    void* list = nullptr;
    for (auto& hd : headers) list = curl.slistAppend(list, hd.c_str());
    const int URL = 10002, POSTFIELDS = 10015, HTTPHEADER = 10023, WRITEFUNCTION = 20011, WRITEDATA = 10001, TIMEOUT = 13,
              POSTFIELDSIZE = 60, CAINFO = 10065;
    curl.setopt(h, URL, url.c_str());
    curl.setopt(h, HTTPHEADER, list);
    curl.setopt(h, POSTFIELDS, body.c_str());
    curl.setopt(h, POSTFIELDSIZE, long(body.size()));
    curl.setopt(h, WRITEFUNCTION, &writeCb);
    curl.setopt(h, WRITEDATA, &out);
    curl.setopt(h, TIMEOUT, 600L);
    if (const char* ca = std::getenv("SSL_CERT_FILE")) curl.setopt(h, CAINFO, ca);
    int rc = curl.perform(h);
    long status = 0;
    curl.getinfo(h, 0x200002 /* CURLINFO_RESPONSE_CODE */, &status);
    if (rc != 0) err = curl.strerror ? curl.strerror(rc) : "network error";
    curl.slistFree(list);
    curl.cleanup(h);
    return rc == 0 ? status : 0;
}

std::string documentsDir() {
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/tmp";
    std::ifstream probe(home + "/Documents/.");
    return home + "/Documents";
}

std::string readApiKey() {
    for (const auto& path : {documentsDir() + "/MC_FLO_API.txt", std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/MC_FLO_API.txt"}) {
        std::ifstream f(path);
        std::string k;
        if (f && std::getline(f, k)) {
            while (!k.empty() && std::isspace((unsigned char)k.back())) k.pop_back();
            if (!k.empty()) return k;
        }
    }
    const char* env = std::getenv("ANTHROPIC_API_KEY");
    return env ? env : "";
}

const char* kSystemPrompt =
    "You are the AI mentor of MonteCalc, a Monte Carlo simulation add-in for LibreOffice Calc that follows the MC FLO "
    "conventions (FLOsimula_ model functions, FLO_i_ inputs, FLO_c_ correlations, FLO_o_ outputs, fMC_ result functions). "
    "You receive the structure of the user's model - inputs with their distributions, correlations, dependencies and "
    "outputs - together with the statistics of the last simulation. MonteCalc computes the simulation; you interpret it. "
    "Explain drivers of outputs, where the downside concentrates (P5/P95 tails), the shape of distributions and which "
    "inputs move which outputs. Only use figures that appear in the model description; if a figure is not there, say so "
    "instead of estimating it. Suggest concrete next steps in MonteCalc terms (for example a sensitivity check, a "
    "correlation to add, or an fMC_ function) when they help. Answer in plain text with short paragraphs and lists.";

class ClipboardText : public cppu::WeakImplHelper1<datatransfer::XTransferable> {
public:
    explicit ClipboardText(const OUString& t) : t_(t) {}
    Any SAL_CALL getTransferData(const datatransfer::DataFlavor&) override { return Any(t_); }
    Sequence<datatransfer::DataFlavor> SAL_CALL getTransferDataFlavors() override {
        datatransfer::DataFlavor f;
        f.MimeType = "text/plain;charset=utf-16";
        f.HumanPresentableName = "Unicode text";
        f.DataType = cppu::UnoType<OUString>::get();
        return seq<datatransfer::DataFlavor>({f});
    }
    sal_Bool SAL_CALL isDataFlavorSupported(const datatransfer::DataFlavor& f) override { return f.MimeType.startsWith("text/plain"); }

private:
    OUString t_;
};

} // namespace

class MentorWindow : public CanvasWindow, public std::enable_shared_from_this<MentorWindow> {
public:
    MentorWindow(const Ctx& ctx, const Frame& frame) : CanvasWindow(ctx, frame, "MonteCalc - AI mentor", 420, 290, 110) {
        int x = panelX(), w = panelW(), y = 6;
        addLabel("lkey", "Anthropic API key", x, y, w); y += 10;
        addEdit("key", readApiKey(), x, y, w, 12, false, true); y += 16;
        addButton("send", "Send (Ctrl+Enter)", x, y, w); y += 16;
        addButton("refresh", "Refresh model context", x, y, w); y += 16;
        addButton("copy", "Copy conversation", x, y, w); y += 16;
        addButton("html", "Export HTML", x, y, w); y += 16;
        addButton("word", "Export Word", x, y, w / 2 - 1); addButton("pdf", "Export PDF", x + w / 2 + 1, y, w / 2 - 1); y += 16;
        addButton("clear", "New conversation", x, y, w); y += 16;
        addButton("close", "Close", x, y, w);
        int cw = 420 - 110 - 8;
        addEdit("history", "", 4, 4, cw, 210, true, false, true);
        addLabel("lq", "Your question", 4, 218, cw);
        addEdit("question", "Which inputs drive the outputs most, and where does the downside concentrate?", 4, 228, cw, 40, true);
        addLabel("status", "", 4, 272, cw);
    }

    void afterShow() {
        context_ = modelContextText(frame_);
        setLabel("status", "Model context: " + std::to_string(std::count(context_.begin(), context_.end(), '\n')) + " lines. Model claude-opus-5-5.");
    }

    void paint(Painter&, int, int) override {}

    std::string transcript() const {
        std::string t;
        for (auto& [role, text] : shown_) t += (role == "user" ? "You:\n" : "Mentor:\n") + text + "\n\n";
        return t;
    }

    void ask() {
        if (busy_) return;
        std::string q = text("question"), key = text("key");
        if (q.empty()) return;
        if (key.empty()) { message("Enter your Anthropic API key (or store it in Documents/MC_FLO_API.txt).", true); return; }
        // the first user turn carries the model description (stable prefix for caching)
        std::string content = messages_.empty() ? "Model description:\n" + context_ + "\nQuestion: " + q : q;
        messages_.push_back("{\"role\":\"user\",\"content\":" + jsonEscape(content) + "}");
        shown_.push_back({"user", q});
        setText("history", transcript() + "Mentor is thinking...");
        setText("question", "");
        busy_ = true;
        setLabel("status", "Waiting for the answer...");
        std::string body = "{\"model\":\"claude-opus-5-5\",\"max_tokens\":16000,\"fallbacks\":\"default\",\"system\":" + jsonEscape(kSystemPrompt) +
                           ",\"messages\":[";
        for (size_t i = 0; i < messages_.size(); ++i) body += (i ? "," : "") + messages_[i];
        body += "]}";
        std::vector<std::string> headers = {"content-type: application/json", "x-api-key: " + key, "anthropic-version: 2026-10-21",
                                            "anthropic-beta: server-side-fallback-2026-07-01"};
        auto self = shared_from_this();
        std::thread([self, body, headers] {
            std::string out, err;
            long status = httpPost("https://api.anthropic.com/v1/messages", headers, body, out, err);
            self->onAnswer(status, out, err);
        }).detach();
    }

    void onAnswer(long status, const std::string& out, const std::string& err) {
        busy_ = false;
        if (closed()) return;
        std::string answer, problem;
        try {
            if (status == 0) throw std::runtime_error(err);
            Json j = JsonParser(out).parse();
            if (status != 200) {
                const Json* e = j.get("error");
                throw std::runtime_error("HTTP " + std::to_string(status) + ": " + (e ? e->str("message") : out.substr(0, 300)));
            }
            std::string stop = j.str("stop_reason");
            const Json* content = j.get("content");
            if (content && content->t == Json::Arr)
                for (auto& block : content->a)
                    if (block.str("type") == "text") answer += block.str("text");
            if (stop == "refusal") {
                problem = "The request was declined by the model's safeguards.";
                const Json* d = j.get("stop_details");
                if (d && d->t == Json::Obj && !d->str("explanation").empty()) problem += " " + d->str("explanation");
            } else if (stop == "max_tokens") {
                answer += "\n[answer truncated at the token limit]";
            }
            // keep the assistant turn exactly as returned (thinking blocks must be passed back unchanged)
            if (content && stop != "refusal") messages_.push_back("{\"role\":\"assistant\",\"content\":" + content->raw + "}");
            else messages_.pop_back();
        } catch (const std::exception& e) {
            problem = e.what();
            if (!messages_.empty()) messages_.pop_back();  // let the user retry the question
        }
        if (!problem.empty()) {
            if (!shown_.empty()) { setText("question", shown_.back().second); shown_.pop_back(); }
            setText("history", transcript() + "[" + problem + "]");
            setLabel("status", "Not answered.");
            return;
        }
        shown_.push_back({"assistant", answer});
        setText("history", transcript());
        setLabel("status", "Answered. Check figures against the result window before relying on them.");
    }

    std::string html() const {
        auto esc = [](const std::string& s) {
            std::string r;
            for (char c : s) {
                if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;"; else if (c == '&') r += "&amp;";
                else if (c == '\n') r += "<br/>"; else r += c;
            }
            return r;
        };
        std::string h = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>MonteCalc AI mentor</title>"
                        "<style>body{font-family:sans-serif;max-width:46em;margin:2em auto;line-height:1.45}"
                        ".user{background:#eef4fb;padding:.6em 1em;border-radius:8px}.assistant{padding:.6em 1em}</style></head><body>"
                        "<h1>MonteCalc AI mentor</h1>";
        for (auto& [role, text] : shown_)
            h += "<div class=\"" + role + "\"><b>" + (role == "user" ? "You" : "Mentor") + "</b><br/>" + esc(text) + "</div>";
        return h + "</body></html>";
    }

    std::string exportTo(const std::string& kind) {
        std::time_t t = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&t));
        std::string base = documentsDir() + "/MonteCalc_mentor_" + stamp;
        std::ofstream(base + ".html") << html();
        if (kind == "html") return base + ".html";
        // Word / PDF: open the HTML in Writer and store it with the matching filter
        Reference<frame::XComponentLoader> loader(frame::Desktop::create(ctx_), UNO_QUERY_THROW);
        beans::PropertyValue hidden, filter;
        hidden.Name = "Hidden";
        hidden.Value <<= true;
        filter.Name = "FilterName";
        filter.Value <<= OUString("HTML (StarWriter)");
        auto doc = loader->loadComponentFromURL(ou("file://" + base + ".html"), "_blank", 0, seq<beans::PropertyValue>({hidden, filter}));
        beans::PropertyValue out;
        out.Name = "FilterName";
        out.Value <<= OUString::createFromAscii(kind == "pdf" ? "writer_pdf_Export" : "MS Word 2007 XML");
        std::string file = base + (kind == "pdf" ? ".pdf" : ".docx");
        Reference<frame::XStorable>(doc, UNO_QUERY_THROW)->storeToURL(ou("file://" + file), seq<beans::PropertyValue>({out}));
        Reference<util::XCloseable>(doc, UNO_QUERY_THROW)->close(true);
        std::remove((base + ".html").c_str());
        return file;
    }

    bool key(int code, int, bool ctrl, bool) override {
        if (ctrl && code == 1280 /* awt::Key::RETURN */) { ask(); return true; }
        return false;
    }

    void button(const std::string& b) override {
        if (b == "close") { close(); return; }
        if (b == "send") { ask(); return; }
        if (b == "refresh") { afterShow(); return; }
        if (b == "clear") { messages_.clear(); shown_.clear(); setText("history", ""); afterShow(); return; }
        if (b == "copy") {
            auto cb = datatransfer::clipboard::SystemClipboard::create(ctx_);
            cb->setContents(new ClipboardText(ou(transcript())), nullptr);
            setLabel("status", "Conversation copied to the clipboard.");
            return;
        }
        if (b == "html" || b == "word" || b == "pdf") {
            if (shown_.empty()) { message("There is no conversation to export yet.", true); return; }
            setLabel("status", "Saved " + exportTo(b));
        }
    }

private:
    std::string context_;
    std::vector<std::string> messages_;  // JSON of every turn sent to the API
    std::vector<std::pair<std::string, std::string>> shown_;
    std::atomic<bool> busy_{false};
};

void openMentor(const Ctx& ctx, const Frame& frame) {
    auto w = std::make_shared<MentorWindow>(ctx, frame);
    w->show();
    w->afterShow();
    CanvasWindow::keep(w);
}

} // namespace montecalc
