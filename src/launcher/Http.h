#ifndef LAUNCHER_HTTP_H_
#define LAUNCHER_HTTP_H_

// Downloads: https:// and http:// through the system's HTTP stack (HttpWin32.cpp: WinHTTP; NSURLSession on macOS and
// libcurl on Linux are to come, each in a file of its own answering Http_Platform), redirects followed, and file://
// here, for the local tests (tools/release/fake_releases.py).

#include <stdint.h>

#include <filesystem>
#include <functional>
#include <string>

enum class HttpFailure {
    None,
    Network,   // no connection: no network, the name not found, refused, timed out
    Status,    // the server answered, but not 200
    Other,     // a bad URL, a file that cannot be written, cancelled
};

struct HttpResult {
    HttpFailure failure = HttpFailure::None;
    int status = 0;
    std::string error;
    bool Ok() const { return failure == HttpFailure::None; }
};

// Called as data arrives, with the total when the server gave one (else 0); false cancels
using HttpProgress = std::function<bool(uint64_t received, uint64_t total)>;

// accept, when given, is sent as the Accept header (the GitHub API's "application/vnd.github+json")
HttpResult Http_Get(const std::string &url, std::string *body, const char *accept = nullptr,
                    const HttpProgress &progress = nullptr);
HttpResult Http_Download(const std::string &url, const std::filesystem::path &file,
                         const HttpProgress &progress = nullptr);

// The User-Agent every request carries (GitHub's API refuses requests without one)
void Http_SetUserAgent(const std::string &agent);

// ---- each platform's: a GET of an http(s) URL, the body handed to sink as it arrives (false from sink cancels)
using HttpSink = std::function<bool(const void *data, size_t size)>;
HttpResult Http_Platform(const std::string &url, const char *accept, const std::string &userAgent, const HttpSink &sink,
                         const HttpProgress &progress);

#endif // LAUNCHER_HTTP_H_
