#include "Http.h"

#include <fstream>

#include "Platform.h"

namespace {

std::string g_userAgent = "NightfireLauncher";

int HexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// file:///C:/x/y or file:///home/x: percent-escapes decoded, the query dropped (a releases list asked for with
// "?per_page=" reads the same file), and a folder read as its index.html, as a web server would serve it
fs::path FilePath(const std::string &url) {
    std::string path = url.substr(7);   // after "file://"
    size_t query = path.find('?');
    if (query != std::string::npos)
        path.resize(query);
    std::string decoded;
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '%' && i + 2 < path.size() && HexValue(path[i + 1]) >= 0 && HexValue(path[i + 2]) >= 0) {
            decoded += char(HexValue(path[i + 1]) * 16 + HexValue(path[i + 2]));
            i += 2;
        } else {
            decoded += path[i];
        }
    }
#ifdef _WIN32
    if (decoded.size() >= 3 && decoded[0] == '/' && decoded[2] == ':')
        decoded.erase(0, 1);   // "/C:/..." to "C:/..."
#endif
    fs::path file = FromUtf8(decoded);
    std::error_code error;
    if (fs::is_directory(file, error))
        file /= "index.html";
    return file;
}

HttpResult Fetch(const std::string &url, const char *accept, const HttpSink &sink, const HttpProgress &progress) {
    HttpResult result;
    if (url.compare(0, 7, "file://") == 0) {
        fs::path file = FilePath(url);
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            result.failure = HttpFailure::Status;
            result.status = 404;
            result.error = "not found: " + url;
            return result;
        }
        std::error_code sizeError;
        uint64_t total = fs::file_size(file, sizeError);
        uint64_t received = 0;
        char buffer[65536];
        while (in) {
            in.read(buffer, sizeof(buffer));
            std::streamsize got = in.gcount();
            if (got <= 0)
                break;
            received += uint64_t(got);
            if (!sink(buffer, size_t(got)) || (progress && !progress(received, total))) {
                result.failure = HttpFailure::Other;
                result.error = "cancelled";
                return result;
            }
        }
        result.status = 200;
        return result;
    }
    if (url.compare(0, 8, "https://") != 0 && url.compare(0, 7, "http://") != 0) {
        result.failure = HttpFailure::Other;
        result.error = "not a URL the launcher reads: " + url;
        return result;
    }
    return Http_Platform(url, accept, g_userAgent, sink, progress);
}

} // namespace

void Http_SetUserAgent(const std::string &agent) {
    g_userAgent = agent;
}

HttpResult Http_Get(const std::string &url, std::string *body, const char *accept, const HttpProgress &progress) {
    body->clear();
    return Fetch(url, accept,
                 [body](const void *data, size_t size) {
                     body->append(static_cast<const char *>(data), size);
                     return true;
                 },
                 progress);
}

HttpResult Http_Download(const std::string &url, const fs::path &file, const HttpProgress &progress) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        HttpResult result;
        result.failure = HttpFailure::Other;
        result.error = "cannot write " + ToUtf8(file);
        return result;
    }
    HttpResult result = Fetch(url, nullptr,
                              [&out](const void *data, size_t size) {
                                  out.write(static_cast<const char *>(data), std::streamsize(size));
                                  return bool(out);
                              },
                              progress);
    out.close();
    if (result.Ok() && !out) {
        result.failure = HttpFailure::Other;
        result.error = "cannot write " + ToUtf8(file) + " (is the disk full?)";
    }
    return result;
}
