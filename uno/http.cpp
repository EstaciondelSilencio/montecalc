// MonteCalc - minimal HTTPS POST
#include "http.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

#include <cstdlib>
#include <string>
#include <vector>

namespace montecalc {

namespace {
#ifdef _WIN32
// ---------------------------------------------------------------- WinHTTP (Windows)
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

// POST JSON over HTTPS; returns the HTTP status (0 on transport error, message in `err`)
long httpPostImpl(const std::string& url, const std::vector<std::string>& headers, const std::string& body, std::string& out, std::string& err) {
    // url is https://host/path
    std::string rest = url.substr(url.find("://") + 3);
    std::wstring host = widen(rest.substr(0, rest.find('/'))), path = widen(rest.substr(rest.find('/')));
    HINTERNET ses = WinHttpOpen(L"MonteCalc/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = WinHttpOpen(L"MonteCalc/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { err = "WinHTTP is not available"; return 0; }
    WinHttpSetTimeouts(ses, 30000, 30000, 30000, 600000);
    long status = 0;
    HINTERNET con = WinHttpConnect(ses, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::wstring hdr;
    for (auto& h : headers) hdr += widen(h) + L"\r\n";
    if (req && WinHttpSendRequest(req, hdr.c_str(), DWORD(-1), (LPVOID)body.data(), DWORD(body.size()), DWORD(body.size()), 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD code = 0, len = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        status = long(code);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            std::string buf(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(req, &buf[0], avail, &read) || read == 0) break;
            out.append(buf.data(), read);
        }
    } else {
        err = "network error (WinHTTP error " + std::to_string(GetLastError()) + ")";
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return status;
}
#else
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
long httpPostImpl(const std::string& url, const std::vector<std::string>& headers, const std::string& body, std::string& out, std::string& err) {
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

#endif

} // namespace

long httpPost(const std::string& url, const std::vector<std::string>& headers, const std::string& body, std::string& out,
              std::string& err) {
    return httpPostImpl(url, headers, body, out, err);
}

} // namespace montecalc
