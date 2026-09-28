#pragma once

#include <string.h>

// The line reader the input-configuration parsers share (src/driving/engine/InputMapping.cpp and
// InputConfigManager.cpp). The originals copy a line - up to its CR - into a 256-byte buffer, split it with
// strtok on TAB and SPACE, compare keywords with the C locale's _stricmp and read numbers with atol. This does the
// same without strtok's hidden state.
//
// On the files the game ships (every line CR LF, no blank line before END, no line over 255 characters) it gives
// the originals' results exactly. Where the originals would crash or overrun - a line with no CR before the end of
// the text, a blank line, an overlong line - this stops or truncates instead; see the callers.

class DefLine {
public:
    // Copies the line at `text` (up to its CR, or the end of the text) and returns its first token, or null for a
    // blank line.
    const char *First(const char *text) {
        const char *end = strchr(text, '\r');
        size_t length = end ? (size_t)(end - text) : strlen(text);
        if (length > sizeof(buffer_) - 1)
            length = sizeof(buffer_) - 1;
        memcpy(buffer_, text, length);
        buffer_[length] = '\0';
        next_ = buffer_;
        return Next();
    }

    // The line's next token, or null when there are no more (strtok(NULL, "\t ")).
    const char *Next() {
        char *p = next_;
        while (*p == '\t' || *p == ' ')
            p++;
        if (*p == '\0') {
            next_ = p;
            return nullptr;
        }
        char *token = p;
        while (*p != '\0' && *p != '\t' && *p != ' ')
            p++;
        if (*p != '\0')
            *p++ = '\0';
        next_ = p;
        return token;
    }

private:
    char buffer_[256];
    char *next_ = buffer_;
};

// The text after the next LF, or null (AdvanceFilePtr's step).
inline const char *DefNextLine(const char *text) {
    const char *n = strchr(text, '\n');
    return n ? n + 1 : nullptr;
}

// MSVC's _stricmp in the C locale: only A-Z fold.
inline int DefStricmp(const char *a, const char *b) {
    for (;;) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z')
            ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z')
            cb += 'a' - 'A';
        if (ca != cb || ca == 0)
            return (int)ca - (int)cb;
    }
}

// MSVC's atol: leading white space, an optional sign, decimal digits up to the first non-digit.
inline long DefAtol(const char *s) {
    while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
        s++;
    bool negative = *s == '-';
    if (*s == '-' || *s == '+')
        s++;
    unsigned long value = 0;
    while (*s >= '0' && *s <= '9')
        value = value * 10 + (unsigned long)(*s++ - '0');
    return negative ? -(long)value : (long)value;
}
