// MonteCalc - minimal HTTPS POST (WinHTTP on Windows, libcurl loaded at run time elsewhere).
// No LibreOffice dependency, so it can be compiled and tested on its own.
#pragma once

#include <string>
#include <vector>

namespace montecalc {

// POST `body` to an https:// URL with the given "Name: value" headers. Returns the HTTP
// status (0 on a transport error, with a message in `err`); the response body goes to `out`.
long httpPost(const std::string& url, const std::vector<std::string>& headers, const std::string& body, std::string& out,
              std::string& err);
// GET an https:// URL (redirects are followed). Same return convention as httpPost.
long httpGet(const std::string& url, std::string& out, std::string& err);

} // namespace montecalc
