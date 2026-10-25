#include "Json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const Json kNull;

class Parser {
public:
    explicit Parser(const std::string &text) : m_text(text) {}

    bool ParseDocument(Json *out, std::string *error) {
        bool ok = ParseValue(out, 0);
        if (ok) {
            SkipSpace();
            if (m_at != m_text.size())
                ok = Fail("text after the value");
        }
        if (!ok && error != nullptr)
            *error = m_error;
        return ok;
    }

private:
    bool Fail(const char *why) {
        if (m_error.empty()) {
            char where[32];
            snprintf(where, sizeof(where), " at offset %zu", m_at);
            m_error = std::string(why) + where;
        }
        return false;
    }

    void SkipSpace() {
        while (m_at < m_text.size() && (m_text[m_at] == ' ' || m_text[m_at] == '\t' || m_text[m_at] == '\n' ||
                                        m_text[m_at] == '\r'))
            m_at++;
    }

    bool Literal(const char *word) {
        size_t length = strlen(word);
        if (m_text.compare(m_at, length, word) != 0)
            return false;
        m_at += length;
        return true;
    }

    bool ParseValue(Json *out, int depth) {
        if (depth > 64)
            return Fail("nested too deeply");
        SkipSpace();
        if (m_at >= m_text.size())
            return Fail("unexpected end");
        char c = m_text[m_at];
        if (c == '{')
            return ParseObject(out, depth);
        if (c == '[')
            return ParseArray(out, depth);
        if (c == '"') {
            std::string text;
            if (!ParseString(&text))
                return false;
            *out = Json(std::move(text));
            return true;
        }
        if (Literal("true")) {
            *out = Json(true);
            return true;
        }
        if (Literal("false")) {
            *out = Json(false);
            return true;
        }
        if (Literal("null")) {
            *out = Json();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
            return ParseNumber(out);
        return Fail("unexpected character");
    }

    bool ParseNumber(Json *out) {
        const char *start = m_text.c_str() + m_at;
        char *end = nullptr;
        double value = strtod(start, &end);
        if (end == start)
            return Fail("bad number");
        m_at += size_t(end - start);
        *out = Json(value);
        return true;
    }

    static void AppendUtf8(std::string &out, unsigned code) {
        if (code < 0x80) {
            out += char(code);
        } else if (code < 0x800) {
            out += char(0xC0 | (code >> 6));
            out += char(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += char(0xE0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        } else {
            out += char(0xF0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 0x3F));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        }
    }

    bool ParseHex4(unsigned *code) {
        if (m_at + 4 > m_text.size())
            return Fail("short \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; i++) {
            char c = m_text[m_at++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= unsigned(c - 'A' + 10);
            else
                return Fail("bad \\u escape");
        }
        *code = value;
        return true;
    }

    bool ParseString(std::string *out) {
        m_at++;   // the opening quote
        while (m_at < m_text.size()) {
            char c = m_text[m_at++];
            if (c == '"')
                return true;
            if (c != '\\') {
                *out += c;
                continue;
            }
            if (m_at >= m_text.size())
                break;
            char escape = m_text[m_at++];
            switch (escape) {
            case '"': *out += '"'; break;
            case '\\': *out += '\\'; break;
            case '/': *out += '/'; break;
            case 'b': *out += '\b'; break;
            case 'f': *out += '\f'; break;
            case 'n': *out += '\n'; break;
            case 'r': *out += '\r'; break;
            case 't': *out += '\t'; break;
            case 'u': {
                unsigned code;
                if (!ParseHex4(&code))
                    return false;
                if (code >= 0xD800 && code < 0xDC00 && Literal("\\u")) {   // a surrogate pair
                    unsigned low;
                    if (!ParseHex4(&low))
                        return false;
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                AppendUtf8(*out, code);
                break;
            }
            default:
                return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseArray(Json *out, int depth) {
        m_at++;
        *out = Json::MakeArray();
        SkipSpace();
        if (m_at < m_text.size() && m_text[m_at] == ']') {
            m_at++;
            return true;
        }
        for (;;) {
            Json item;
            if (!ParseValue(&item, depth + 1))
                return false;
            out->Push(std::move(item));
            SkipSpace();
            if (m_at < m_text.size() && m_text[m_at] == ',') {
                m_at++;
                continue;
            }
            if (m_at < m_text.size() && m_text[m_at] == ']') {
                m_at++;
                return true;
            }
            return Fail("expected , or ]");
        }
    }

    bool ParseObject(Json *out, int depth) {
        m_at++;
        *out = Json::MakeObject();
        SkipSpace();
        if (m_at < m_text.size() && m_text[m_at] == '}') {
            m_at++;
            return true;
        }
        for (;;) {
            SkipSpace();
            if (m_at >= m_text.size() || m_text[m_at] != '"')
                return Fail("expected a key");
            std::string key;
            if (!ParseString(&key))
                return false;
            SkipSpace();
            if (m_at >= m_text.size() || m_text[m_at] != ':')
                return Fail("expected :");
            m_at++;
            Json value;
            if (!ParseValue(&value, depth + 1))
                return false;
            out->Set(key, std::move(value));
            SkipSpace();
            if (m_at < m_text.size() && m_text[m_at] == ',') {
                m_at++;
                continue;
            }
            if (m_at < m_text.size() && m_text[m_at] == '}') {
                m_at++;
                return true;
            }
            return Fail("expected , or }");
        }
    }

    const std::string &m_text;
    size_t m_at = 0;
    std::string m_error;
};

void DumpString(std::string &out, const std::string &text) {
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escape[8];
                snprintf(escape, sizeof(escape), "\\u%04x", c);
                out += escape;
            } else {
                out += char(c);
            }
        }
    }
    out += '"';
}

} // namespace

bool Json::Parse(const std::string &text, Json *out, std::string *error) {
    Parser parser(text);
    return parser.ParseDocument(out, error);
}

const Json &Json::operator[](const std::string &key) const {
    for (const auto &member : m_members)
        if (member.first == key)
            return member.second;
    return kNull;
}

bool Json::Has(const std::string &key) const {
    for (const auto &member : m_members)
        if (member.first == key)
            return true;
    return false;
}

void Json::Set(const std::string &key, Json value) {
    m_type = Object;
    for (auto &member : m_members) {
        if (member.first == key) {
            member.second = std::move(value);
            return;
        }
    }
    m_members.emplace_back(key, std::move(value));
}

const Json &Json::operator[](size_t index) const {
    return m_type == Array && index < m_items.size() ? m_items[index] : kNull;
}

std::string Json::Dump(int indent) const {
    std::string out;
    DumpTo(out, indent, 0);
    out += '\n';
    return out;
}

void Json::DumpTo(std::string &out, int indent, int depth) const {
    auto newline = [&](int level) {
        out += '\n';
        out.append(size_t(indent * level), ' ');
    };
    switch (m_type) {
    case Null:
        out += "null";
        break;
    case Bool:
        out += m_bool ? "true" : "false";
        break;
    case Number: {
        char text[32];
        if (m_number == floor(m_number) && fabs(m_number) < 1e15)
            snprintf(text, sizeof(text), "%.0f", m_number);
        else
            snprintf(text, sizeof(text), "%.17g", m_number);
        out += text;
        break;
    }
    case String:
        DumpString(out, m_string);
        break;
    case Array:
        out += '[';
        for (size_t i = 0; i < m_items.size(); i++) {
            if (i > 0)
                out += ',';
            newline(depth + 1);
            m_items[i].DumpTo(out, indent, depth + 1);
        }
        if (!m_items.empty())
            newline(depth);
        out += ']';
        break;
    case Object:
        out += '{';
        for (size_t i = 0; i < m_members.size(); i++) {
            if (i > 0)
                out += ',';
            newline(depth + 1);
            DumpString(out, m_members[i].first);
            out += ": ";
            m_members[i].second.DumpTo(out, indent, depth + 1);
        }
        if (!m_members.empty())
            newline(depth);
        out += '}';
        break;
    }
}
