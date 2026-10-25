#include "Http.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace {

std::wstring Widen(const std::string &text) {
    if (text.empty())
        return std::wstring();
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), length);
    return wide;
}

// Closes a WinHTTP handle when it goes out of scope
struct Handle {
    HINTERNET handle = nullptr;
    explicit Handle(HINTERNET h) : handle(h) {}
    ~Handle() {
        if (handle != nullptr)
            WinHttpCloseHandle(handle);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    operator HINTERNET() const { return handle; }
};

HttpResult Failed(DWORD error, const std::string &url) {
    HttpResult result;
    switch (error) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_TIMEOUT:
        result.failure = HttpFailure::Network;
        result.error = "no connection to " + url + " (WinHTTP error " + std::to_string(error) + ")";
        break;
    case ERROR_WINHTTP_SECURE_FAILURE:
        result.failure = HttpFailure::Network;
        result.error = "the secure connection to " + url + " failed (certificate or TLS)";
        break;
    default:
        result.failure = HttpFailure::Other;
        result.error = "the download of " + url + " failed (WinHTTP error " + std::to_string(error) + ")";
        break;
    }
    return result;
}

} // namespace

HttpResult Http_Platform(const std::string &url, const char *accept, const std::string &userAgent, const HttpSink &sink,
                         const HttpProgress &progress) {
    std::wstring wideUrl = Widen(url);
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256], path[2048];
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(sizeof(host) / sizeof(host[0]));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(sizeof(path) / sizeof(path[0]));
    parts.dwExtraInfoLength = 1;   // the query, appended to the path below
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        HttpResult result;
        result.failure = HttpFailure::Other;
        result.error = "not a URL: " + url;
        return result;
    }
    std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo != nullptr)
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY: the system's proxy settings, Windows 8.1 and later
    Handle session(WinHttpOpen(Widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (session == nullptr)
        return Failed(GetLastError(), url);
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);
    // Redirects are followed by default (a renamed repository, a release asset's storage host), but never from
    // https to http
    Handle connection(WinHttpConnect(session, host, parts.nPort, 0));
    if (connection == nullptr)
        return Failed(GetLastError(), url);
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request(WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (request == nullptr)
        return Failed(GetLastError(), url);
    std::wstring headers;
    if (accept != nullptr)
        headers = L"Accept: " + Widen(accept) + L"\r\n";
    if (!WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr))
        return Failed(GetLastError(), url);

    HttpResult result;
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    result.status = int(status);
    wchar_t lengthText[32] = {};
    size = sizeof(lengthText);
    uint64_t total = 0;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &size,
                            WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(lengthText, nullptr, 10);

    std::vector<char> buffer(65536);
    std::string errorBody;
    uint64_t received = 0;
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read))
            return Failed(GetLastError(), url);
        if (read == 0)
            break;
        received += read;
        if (status != 200) {   // kept for the message: GitHub's says why (a rate limit, say)
            if (errorBody.size() < 4096)
                errorBody.append(buffer.data(), read);
            continue;
        }
        if (!sink(buffer.data(), read) || (progress && !progress(received, total))) {
            result.failure = HttpFailure::Other;
            result.error = "cancelled";
            return result;
        }
    }
    if (status != 200) {
        result.failure = HttpFailure::Status;
        result.error = "HTTP " + std::to_string(status) + " from " + url;
        if (!errorBody.empty())
            result.error += ": " + errorBody.substr(0, 300);
    }
    return result;
}
