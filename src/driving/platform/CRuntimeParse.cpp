#include "CRuntimeParse.h"
#include "CRuntimeInternal.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's parsing: sscanf over the runtime's input engine (_input), atof and strtod over its float parser
// (CRuntimeFloat.cpp, not correctly rounded, which is why it is the game's and not ours), atol (which wraps where
// ours would saturate), and strtol and its unsigned and 64-bit forms, which set the game's errno.
// ---------------------------------------------------------------------------------------------------------------

namespace Crt {

namespace {

const int kEof = -1;

// The FILE sscanf hands _input (_IOREAD | _IOSTRG): the next character and how many are left. Past the end every
// read is EOF; a character read can be put back.
struct InputString {
    const unsigned char *next;
    int left;
};

int Read(InputString *input) {
    return --input->left >= 0 ? *input->next++ : kEof;
}

void Unread(int ch, InputString *input) {
    if (ch != kEof) {
        input->next--;
        input->left++;
    }
}

int HexDigitValue(int ch) {                                                 // _hextodec: as a "digit" above '9'
    if (Ctype(ch) & kDigit)
        return ch;
    return (ch & ~('a' - 'A')) - 'A' + '9' + 1;
}

const int kFloatBufferSize = 349;         // _CVTBUFSIZE

int Input(InputString *input, const unsigned char *format, va_list arguments) {   // _input
    unsigned char table[32];              // %[...]'s set, a bit per character
    int count = 0;                        // fields assigned
    int charCount = 0;                    // characters read, for %n
    char matched = 0;                     // directives matched
    int ch = 0;
    uint64_t number64 = 0;
    void *pointer = NULL;
    va_list args, saved;
    va_copy(args, arguments);
    va_copy(saved, args);

    while (*format != '\0') {
        if (isspace(*format)) {
            int skipped;
            charCount--;
            do {
                charCount++;
                skipped = Read(input);
            } while (isspace(skipped));
            Unread(skipped, input);
            while (isspace(*++format)) {
            }
            continue;
        }

        if (*format == '%') {
            unsigned number = 0;
            unsigned char previous = 0;
            int width = 0;
            bool widthSet = false;
            int started = 0;
            unsigned char reject = 0;
            bool negative = false;
            bool suppress = false;
            bool done = false;
            bool wideArgument = false;
            signed char wide = 0;
            signed char longOne = 1;
            bool int64 = false;

            while (!done) {
                int c = *++format;
                if (Ctype(c) & kDigit) {
                    widthSet = true;
                    width = width * 10 + (c - '0');
                    continue;
                }
                switch (c) {
                case 'F':
                case 'N':
                    break;               // near and far: nothing here
                case 'h':
                    longOne--;
                    wide--;
                    break;
                case 'I':
                    if (format[1] == '6' && format[2] == '4') {
                        format += 2;
                        int64 = true;
                        number64 = 0;
                        break;
                    }
                    if (format[1] == '3' && format[2] == '2') {
                        format += 2;
                        break;
                    }
                    if (format[1] == 'd' || format[1] == 'i' || format[1] == 'o' || format[1] == 'x' ||
                        format[1] == 'X')
                        break;
                    done = true;
                    break;
                case 'L':
                    longOne++;
                    break;
                case 'l':
                    longOne++;
                    wide++;
                    break;
                case 'w':
                    wide++;
                    break;
                case '*':
                    suppress = true;
                    break;
                default:
                    done = true;
                    break;
                }
            }

            if (!suppress) {
                va_end(saved);
                va_copy(saved, args);
                pointer = va_arg(args, void *);
            }
            done = false;
            if (wide == 0)
                wide = (*format == 'S' || *format == 'C') ? 1 : -1;

            int type = *format | ('a' - 'A');
            if (type != 'n') {
                if (type == 'c' || type == '{') {     // '{' is '[' in lower case
                    charCount++;
                    ch = Read(input);
                } else {
                    do {
                        charCount++;
                        ch = Read(input);
                    } while (isspace(ch));
                }
            }

            if (widthSet && width == 0) {
                charCount--;
                Unread(ch, input);
                goto finished;
            }

            switch (type) {
            case 'c':
                if (!widthSet) {
                    widthSet = true;
                    width++;
                }
                [[fallthrough]];
            case 's':
                if (wide > 0)
                    wideArgument = true;
                goto scan;

            case '{': {
                if (wide > 0)
                    wideArgument = true;
                const unsigned char *set = ++format;
                if (*set == '^') {
                    set++;
                    reject = 0xff;
                }
                memset(table, 0, sizeof(table));
                if (*set == ']') {
                    previous = ']';
                    set++;
                    table[']' >> 3] = 1 << (']' & 7);
                }
                while (*set != ']') {
                    unsigned char c = *set;
                    if (c == '-' && previous != 0 && set[1] != ']') {
                        unsigned char last = set[1];
                        unsigned char first = last;
                        if (previous < last) {
                            first = previous;
                        } else {
                            last = previous;
                        }
                        for (int r = first; r <= last; r++)
                            table[r >> 3] |= 1 << (r & 7);
                        set += 2;
                        previous = 0;
                    } else {
                        table[c >> 3] |= 1 << (c & 7);
                        previous = c;
                        set++;
                    }
                }
                format = set;
                goto scan;
            }

            scan: {
                unsigned char *out = (unsigned char *)pointer;
                int taken = 0;
                charCount--;
                Unread(ch, input);
                bool full = false;
                for (;;) {
                    if (widthSet && width-- == 0) {
                        full = true;
                        break;
                    }
                    charCount++;
                    ch = Read(input);
                    bool take = ch != kEof &&
                                (type == 'c' ||
                                 (type == 's' && !(ch >= '\t' && ch <= '\r') && ch != ' ') ||
                                 (type == '{' && ((table[ch >> 3] ^ reject) & (1 << (ch & 7)))));
                    if (!take)
                        break;
                    taken++;
                    if (suppress)
                        continue;            // only a match to note
                    if (wideArgument) {
                        unsigned char bytes[2] = { (unsigned char)ch, 0 };
                        if (Ctype(ch & 0xff) & kLeadByte) {
                            charCount++;
                            bytes[1] = (unsigned char)Read(input);
                        }
                        uint16_t wideChar = bytes[0];   // mbtowc in the C locale
                        memcpy(out, &wideChar, 2);
                        out += 2;
                    } else {
                        *out++ = (unsigned char)ch;
                    }
                }
                if (!full) {
                    charCount--;
                    Unread(ch, input);
                }
                if (taken == 0)
                    goto finished;
                if (!suppress) {
                    count++;
                    if (type != 'c') {
                        if (wideArgument) {
                            out[0] = 0;
                            out[1] = 0;
                        } else {
                            *out = 0;
                        }
                    }
                }
                break;
            }

            case 'i':
                type = 'd';
                [[fallthrough]];
            case 'x':
                if (ch == '-' || ch == '+') {
                    if (ch == '-')
                        negative = true;
                    if (--width == 0 && widthSet) {
                        done = true;
                    } else {
                        charCount++;
                        ch = Read(input);
                    }
                }
                if (ch == '0') {
                    charCount++;
                    ch = Read(input);
                    if (char(ch) == 'x' || char(ch) == 'X') {
                        charCount++;
                        ch = Read(input);
                        if (widthSet && (width -= 2) < 1)
                            done = true;
                        type = 'x';
                    } else {
                        started = 1;
                        if (type != 'x') {
                            if (widthSet && --width == 0)
                                done = true;
                            type = 'o';
                        } else {
                            // a hex number starting with 0: put the character back and take the 0 as a digit
                            charCount--;
                            Unread(ch, input);
                            ch = '0';
                        }
                    }
                }
                goto digits;

            case 'p':
                longOne = 1;
                [[fallthrough]];
            case 'o':
            case 'u':
            case 'd':
                if (ch == '-' || ch == '+') {
                    if (ch == '-')
                        negative = true;
                    if (--width == 0 && widthSet) {
                        done = true;
                    } else {
                        charCount++;
                        ch = Read(input);
                    }
                }
            digits:
                if (int64) {
                    while (!done) {
                        if (type == 'x' || type == 'p') {
                            if (Ctype(ch) & kHexDigit) {
                                number64 <<= 4;
                                ch = HexDigitValue(ch);
                            } else {
                                done = true;
                            }
                        } else if (Ctype(ch) & kDigit) {
                            if (type == 'o') {
                                if (ch < '8')
                                    number64 <<= 3;
                                else
                                    done = true;
                            } else {
                                number64 *= 10;
                            }
                        } else {
                            done = true;
                        }
                        if (!done) {
                            started++;
                            number64 += int64_t(ch - '0');
                            if (widthSet && --width == 0) {
                                done = true;
                            } else {
                                charCount++;
                                ch = Read(input);
                            }
                        } else {
                            charCount--;
                            Unread(ch, input);
                        }
                    }
                    if (negative)
                        number64 = 0 - number64;
                } else {
                    while (!done) {
                        if (type == 'x' || type == 'p') {
                            if (Ctype(ch) & kHexDigit) {
                                number <<= 4;
                                ch = HexDigitValue(ch);
                            } else {
                                done = true;
                            }
                        } else if (Ctype(ch) & kDigit) {
                            if (type == 'o') {
                                if (ch < '8')
                                    number <<= 3;
                                else
                                    done = true;
                            } else {
                                number *= 10;
                            }
                        } else {
                            done = true;
                        }
                        if (!done) {
                            started++;
                            number += ch - '0';
                            if (widthSet && --width == 0) {
                                done = true;
                            } else {
                                charCount++;
                                ch = Read(input);
                            }
                        } else {
                            charCount--;
                            Unread(ch, input);
                        }
                    }
                    if (negative)
                        number = 0u - number;
                }
                if (!started)
                    goto finished;
                if (!suppress) {
                    count++;
                assign:
                    if (int64)
                        memcpy(pointer, &number64, 8);
                    else if (longOne)
                        memcpy(pointer, &number, 4);
                    else {
                        uint16_t low = uint16_t(number);
                        memcpy(pointer, &low, 2);
                    }
                }
                break;

            case 'n':
                number = charCount;
                if (!suppress)
                    goto assign;
                break;

            case 'e':
            case 'f':
            case 'g': {
                char floatString[kFloatBufferSize + 3];
                char *p = floatString;
                if (ch == '-' || ch == '+') {
                    if (ch == '-')
                        *p++ = '-';
                    width--;
                    charCount++;
                    ch = Read(input);
                }
                if (!widthSet || width > kFloatBufferSize)
                    width = kFloatBufferSize;
                while ((Ctype(ch) & kDigit) && width-- != 0) {
                    started++;
                    *p++ = char(ch);
                    charCount++;
                    ch = Read(input);
                }
                if (char(ch) == DecimalPoint && width-- != 0) {
                    charCount++;
                    ch = Read(input);
                    *p++ = DecimalPoint;
                    while ((Ctype(ch) & kDigit) && width-- != 0) {
                        started++;
                        *p++ = char(ch);
                        charCount++;
                        ch = Read(input);
                    }
                }
                if (started && (ch == 'e' || ch == 'E') && width-- != 0) {
                    *p++ = 'e';
                    charCount++;
                    ch = Read(input);
                    if (ch == '-' || ch == '+') {
                        if (ch == '-')
                            *p++ = '-';
                        if (width-- == 0) {
                            width++;
                        } else {
                            charCount++;
                            ch = Read(input);
                        }
                    }
                    while ((Ctype(ch) & kDigit) && width-- != 0) {
                        started++;
                        *p++ = char(ch);
                        charCount++;
                        ch = Read(input);
                    }
                }
                charCount--;
                Unread(ch, input);
                if (!started)
                    goto finished;
                if (!suppress) {
                    count++;
                    *p = '\0';
                    AssignFloat(longOne - 1, pointer, floatString);
                }
                break;
            }

            default:                     // "%%", or a character the format expects
                if (*format != ch) {
                    charCount--;
                    Unread(ch, input);
                    goto finished;
                }
                matched--;
                if (!suppress) {
                    va_end(args);
                    va_copy(args, saved);
                }
                break;
            }
            matched++;
            format++;
        } else {
            charCount++;
            ch = Read(input);
            if (*format++ != ch) {
                charCount--;
                Unread(ch, input);
                goto finished;
            }
            if (Ctype(ch & 0xff) & kLeadByte) {
                charCount++;
                int trail = Read(input);
                if (*format != trail) {
                    Unread(trail, input);
                    charCount--;
                    Unread(ch, input);
                    goto finished;
                }
                charCount--;
                format++;
            }
        }

        if (ch == kEof && (format[0] != '%' || format[1] != 'n'))
            break;
    }

finished:
    va_end(saved);
    va_end(args);
    if (ch == kEof && count == 0 && matched == 0)
        return kEof;
    return count;
}

} // namespace

int isspace(int c) {
    return Ctype(c) & kSpace;
}

} // namespace Crt

// FUNC_AT(0x00133234)
int Crt::sscanf(const char *text, const char *format, ...) {
    InputString input = { (const unsigned char *)text, int(strlen(text)) };
    va_list arguments;
    va_start(arguments, format);
    int count = Input(&input, (const unsigned char *)format, arguments);
    va_end(arguments);
    return count;
}

// FUNC_AT(0x00133e84)
double Crt::atof(const char *text) {
    while (Ctype((unsigned char)*text) & kSpace)
        text++;
    FloatIn result;
    return ParseFloat(&result, text)->value;
}

// Accumulates in 32 bits, wrapping on overflow.
// FUNC_AT(0x00133d51)
long Crt::atol(const char *text) {
    while (Ctype((unsigned char)*text) & kSpace)
        text++;
    int c = (unsigned char)*text++;
    int sign = c;
    if (c == '-' || c == '+')
        c = (unsigned char)*text++;
    uint32_t total = 0;
    while (c >= '0' && c <= '9') {
        total = total * 10 + (c - '0');
        c = (unsigned char)*text++;
    }
    if (sign == '-')
        total = 0u - total;
    return long(total);
}

double Crt::strtod(const char *text, char **end) {
    const char *start = text;
    while (Ctype((unsigned char)*text) & kSpace)
        text++;
    FloatIn result;
    ParseFloat(&result, text);
    if (end != NULL)
        *end = const_cast<char *>(text) + result.length;
    if (result.flags & kFloatNoDigits) {
        if (end != NULL)
            *end = const_cast<char *>(start);
        return 0.0;
    }
    if (result.flags & kFloatOverflow) {
        double huge = *text == '-' ? -HUGE_VAL : HUGE_VAL;
        *_errno() = kRangeError;
        return huge;
    }
    if (result.flags & kFloatUnderflow) {
        *_errno() = kRangeError;
        return 0.0;
    }
    return result.value;
}

namespace Crt {

namespace {

enum {
    kUnsigned = 0x1,
    kNegative = 0x2,
    kOverflow = 0x4,
    kReadDigit = 0x8
};

// What both widths of strtol share: blanks, a sign, the base (0 for C's prefixes; 16 takes "0x"), then digits
// and letters below the base. *end is the text itself if no digit came.
template <class Number>
Number StringToNumber(const char *text, char **end, int base, int flags, Number maxSigned) {   // strtoxl, strtoxq
    const unsigned char *p = (const unsigned char *)text;
    unsigned char c = *p++;
    while (Ctype(c) & kSpace)
        c = *p++;
    if (c == '-') {
        flags |= kNegative;
        c = *p++;
    } else if (c == '+') {
        c = *p++;
    }
    if (base < 0 || base == 1 || base > 36) {
        if (end != NULL)
            *end = const_cast<char *>(text);
        return 0;
    }
    if (base == 0) {
        if (c != '0')
            base = 10;
        else if (*p == 'x' || *p == 'X')
            base = 16;
        else
            base = 8;
    }
    if (base == 16 && c == '0' && (*p == 'x' || *p == 'X')) {
        c = p[1];
        p += 2;
    }

    const Number maxValue = Number(~Number(0));
    Number limit = maxValue / Number(base);
    Number lastDigit = maxValue % Number(base);
    Number number = 0;
    for (;;) {
        unsigned digit;
        if (Ctype(c) & kDigit)
            digit = (signed char)c - '0';
        else if (Ctype(c) & kAlpha)
            digit = (c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c) - 'A' + 10;
        else
            break;
        if (digit >= unsigned(base))
            break;
        if (number < limit || (number == limit && digit <= lastDigit)) {
            number = number * Number(base) + digit;
            flags |= kReadDigit;
        } else {
            flags |= kOverflow | kReadDigit;
        }
        c = *p++;
    }

    p--;
    if (!(flags & kReadDigit)) {
        if (end != NULL)
            p = (const unsigned char *)text;
        number = 0;
    } else if ((flags & kOverflow) ||
               (!(flags & kUnsigned) &&
                (((flags & kNegative) && number > Number(maxSigned) + 1) ||
                 (!(flags & kNegative) && number > Number(maxSigned))))) {
        *_errno() = kRangeError;
        if (flags & kUnsigned)
            number = maxValue;
        else if (flags & kNegative)
            number = Number(maxSigned) + 1;
        else
            number = Number(maxSigned);
    }
    if (end != NULL)
        *end = (char *)p;
    if (flags & kNegative)
        number = Number(0) - number;
    return number;
}

} // namespace

} // namespace Crt

// FUNC_AT(0x00133636)
long Crt::strtol(const char *text, char **end, int base) {
    return long(StringToNumber<uint32_t>(text, end, base, 0, 0x7fffffff));
}

// FUNC_AT(0x0013364d)
unsigned long Crt::strtoul(const char *text, char **end, int base) {
    return StringToNumber<uint32_t>(text, end, base, kUnsigned, 0x7fffffff);
}

// FUNC_AT(0x0013396f)
int64_t Crt::_strtoi64(const char *text, char **end, int base) {
    return int64_t(StringToNumber<uint64_t>(text, end, base, 0, 0x7fffffffffffffffull));
}

// FUNC_AT(0x00133986)
uint64_t Crt::_strtoui64(const char *text, char **end, int base) {
    return StringToNumber<uint64_t>(text, end, base, kUnsigned, 0x7fffffffffffffffull);
}
