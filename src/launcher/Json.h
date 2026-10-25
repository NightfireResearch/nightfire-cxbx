#ifndef LAUNCHER_JSON_H_
#define LAUNCHER_JSON_H_

// A small JSON reader and writer for what the launcher handles: launcher.json, a release's manifest.json and the
// GitHub releases API's answers. Numbers are doubles; objects keep their keys in order.

#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool value) : m_type(Bool), m_bool(value) {}
    Json(double value) : m_type(Number), m_number(value) {}
    Json(int value) : m_type(Number), m_number(value) {}
    Json(long long value) : m_type(Number), m_number(double(value)) {}
    Json(const char *value) : m_type(String), m_string(value) {}
    Json(std::string value) : m_type(String), m_string(std::move(value)) {}
    static Json MakeArray() { Json j; j.m_type = Array; return j; }
    static Json MakeObject() { Json j; j.m_type = Object; return j; }

    // False, with where and why in error, for text that is not one JSON value
    static bool Parse(const std::string &text, Json *out, std::string *error);
    std::string Dump(int indent = 2) const;

    Type GetType() const { return m_type; }
    bool IsNull() const { return m_type == Null; }
    bool IsObject() const { return m_type == Object; }
    bool IsArray() const { return m_type == Array; }
    bool IsString() const { return m_type == String; }
    bool IsNumber() const { return m_type == Number; }

    // Values of another type, and missing members, read as the fallback
    bool AsBool(bool fallback = false) const { return m_type == Bool ? m_bool : fallback; }
    double AsNumber(double fallback = 0) const { return m_type == Number ? m_number : fallback; }
    std::string AsString(const std::string &fallback = "") const { return m_type == String ? m_string : fallback; }

    // Objects: a missing key reads as null; Set adds or replaces
    const Json &operator[](const std::string &key) const;
    void Set(const std::string &key, Json value);
    bool Has(const std::string &key) const;
    const std::vector<std::pair<std::string, Json>> &Members() const { return m_members; }

    // Arrays
    size_t Size() const { return m_type == Array ? m_items.size() : 0; }
    const Json &operator[](size_t index) const;
    void Push(Json value) { m_items.push_back(std::move(value)); }
    const std::vector<Json> &Items() const { return m_items; }

private:
    void DumpTo(std::string &out, int indent, int depth) const;

    Type m_type = Null;
    bool m_bool = false;
    double m_number = 0;
    std::string m_string;
    std::vector<Json> m_items;
    std::vector<std::pair<std::string, Json>> m_members;
};

#endif // LAUNCHER_JSON_H_
