#include "CRuntimeFormat.h"
#include "CRuntimeInternal.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's printf family: sprintf, vsprintf and _vsnprintf, all three a string stream round the runtime's
// output engine (_output). Floats go through CRuntimeFloat.cpp, so %e, %f and %g round as the game's do; %e
// prints three exponent digits; %p is eight upper-case hex digits; 'L' is accepted and ignored (a %Lf takes a
// double, a %Ld an int); a precision of more than 163 digits on a float takes a buffer from the game's heap.
// ---------------------------------------------------------------------------------------------------------------

namespace Crt {

namespace {

// The FILE the string functions hand _output (_IOWRT | _IOSTRG): the next character's place, the room left, and
// the buffer. With no buffer it only counts.
struct StringStream {
    char *next;
    int room;
    char *base;
};

// Each write adds to *written, or sets it to -1 once the stream is full.
void WriteChar(char c, StringStream *stream, int *written) {
    if (stream->base == NULL) {
        ++*written;
        return;
    }
    if (--stream->room >= 0) {
        *stream->next++ = c;
        ++*written;
    } else {
        *written = -1;
    }
}

void WriteMultiChar(char c, int count, StringStream *stream, int *written) {
    while (count-- > 0) {
        WriteChar(c, stream, written);
        if (*written == -1)
            break;
    }
}

void WriteString(const char *text, int length, StringStream *stream, int *written) {
    if (stream->base == NULL) {
        *written += length;
        return;
    }
    while (length-- > 0) {
        WriteChar(*text++, stream, written);
        if (*written == -1)
            break;
    }
}

// The terminating null, if there is room for it.
void Terminate(StringStream *stream) {
    if (stream->base != NULL && --stream->room >= 0)
        *stream->next = '\0';
}

// A wide character as a C-locale multibyte one: -1 (errno EILSEQ) above 255.
int WideToChar(char *out, uint16_t wide) {                                  // wctomb
    if (out == NULL)
        return 0;
    if (wide > 0xff) {
        *_errno() = kIllegalSequence;
        return -1;
    }
    *out = char(wide);
    return 1;
}

// The format parser's state machine (__lookuptable): for each character from ' ' to 'x' its class in the low four
// bits, and in the high four bits of entry class * 8 + state, the state that class leads to.
enum CharClass {
    kOther, kPercent, kDot, kStar, kZero, kDigitClass, kFlag, kSize, kType
};
enum State {
    kNormal, kPercentState, kFlagState, kWidth, kDotState, kPrecision, kSizeState, kTypeState
};
const unsigned char kLookup[0x59] = {
    0x06, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x03, 0x06, 0x00, 0x06, 0x02, 0x10,
    0x04, 0x45, 0x45, 0x45, 0x05, 0x05, 0x05, 0x05, 0x05, 0x35, 0x30, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x28, 0x38, 0x50, 0x58, 0x07, 0x08, 0x00, 0x37, 0x30, 0x30, 0x57, 0x50, 0x07, 0x00,
    0x00, 0x20, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08, 0x60, 0x68, 0x60, 0x60, 0x60, 0x60, 0x00,
    0x00, 0x70, 0x70, 0x78, 0x78, 0x78, 0x78, 0x08, 0x07, 0x08, 0x00, 0x00, 0x07, 0x00, 0x08, 0x08,
    0x08, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x07, 0x08,
};

enum Flags {
    kSign = 0x1,          // '+'
    kSignSpace = 0x2,     // ' '
    kLeft = 0x4,          // '-'
    kLeadZero = 0x8,      // '0'
    kLong = 0x10,         // 'l'
    kShort = 0x20,        // 'h'
    kSigned = 0x40,
    kAlternate = 0x80,    // '#'
    kNegative = 0x100,
    kForceOctal = 0x200,
    kWideChar = 0x800,    // 'w', and 'C' and 'S'
    kInt64 = 0x8000       // "I64"
};

const int kBufferSize = 512;
const int kMaxPrecision = 512;
const int kFloatBufferSize = 349;         // _CVTBUFSIZE: the longest %f of a double, and room
const char kNullString[] = "(null)";
const uint16_t kWideNullString[] = { '(', 'n', 'u', 'l', 'l', ')', 0 };

// NT's counted string, for %Z.
struct CountedString {
    short length;
    short maximumLength;
    char *buffer;
};

int Output(StringStream *stream, const char *format, va_list arguments) {   // _output
    int written = 0;
    State state = kNormal;
    int flags = 0, width = 0, precision = -1, prefixLength = 0, textLength = 0;
    int hexAdjust = 0, radix = 10;
    bool caps = false, noOutput = false, textIsWide = false;
    char prefix[2] = {};
    char buffer[kBufferSize];
    char *heapBuffer = NULL;
    union {
        char *narrow;
        uint16_t *wide;
    } text;
    text.narrow = buffer;

    char ch;
    while ((ch = *format++) != '\0' && written >= 0) {
        CharClass charClass = ch < ' ' || ch > 'x' ? kOther : CharClass(kLookup[ch - ' '] & 0xf);
        state = State(kLookup[charClass * 8 + state] >> 4);

        switch (state) {
        case kNormal:
        normal:
            textIsWide = false;
            if (Ctype((unsigned char)ch) & kLeadByte) {
                WriteChar(ch, stream, &written);
                ch = *format++;
            }
            WriteChar(ch, stream, &written);
            break;

        case kPercentState:
            width = prefixLength = 0;
            caps = noOutput = false;
            flags = 0;
            precision = -1;
            textIsWide = false;
            break;

        case kFlagState:
            switch (ch) {
            case '-': flags |= kLeft; break;
            case '+': flags |= kSign; break;
            case ' ': flags |= kSignSpace; break;
            case '#': flags |= kAlternate; break;
            case '0': flags |= kLeadZero; break;
            }
            break;

        case kWidth:
            if (ch == '*') {
                width = va_arg(arguments, int);
                if (width < 0) {
                    flags |= kLeft;
                    width = -width;
                }
            } else {
                width = width * 10 + (ch - '0');
            }
            break;

        case kDotState:
            precision = 0;
            break;

        case kPrecision:
            if (ch == '*') {
                precision = va_arg(arguments, int);
                if (precision < 0)
                    precision = -1;
            } else {
                precision = precision * 10 + (ch - '0');
            }
            break;

        case kSizeState:
            switch (ch) {
            case 'l':
                flags |= kLong;
                break;
            case 'I':
                if (format[0] == '6' && format[1] == '4') {
                    format += 2;
                    flags |= kInt64;
                } else if (format[0] == '3' && format[1] == '2') {
                    format += 2;
                    flags &= ~kInt64;
                } else if (format[0] != 'd' && format[0] != 'i' && format[0] != 'o' && format[0] != 'u' &&
                           format[0] != 'x' && format[0] != 'X') {
                    state = kNormal;     // not a size after all: the 'I' is printed
                    goto normal;
                }
                break;
            case 'h':
                flags |= kShort;
                break;
            case 'w':
                flags |= kWideChar;
                break;
            }
            break;

        case kTypeState:
            switch (ch) {
            case 'C':
                if (!(flags & (kShort | kLong | kWideChar)))
                    flags |= kWideChar;
                [[fallthrough]];
            case 'c':
                if (flags & (kLong | kWideChar)) {
                    textLength = WideToChar(buffer, uint16_t(va_arg(arguments, int)));
                    if (textLength < 0)
                        noOutput = true;
                } else {
                    buffer[0] = char(va_arg(arguments, int));
                    textLength = 1;
                }
                text.narrow = buffer;
                break;

            case 'Z': {
                CountedString *counted = va_arg(arguments, CountedString *);
                if (counted == NULL || counted->buffer == NULL) {
                    text.narrow = const_cast<char *>(kNullString);
                    textLength = int(strlen(text.narrow));
                } else if (flags & kWideChar) {
                    text.narrow = counted->buffer;
                    textLength = counted->length / 2;
                    textIsWide = true;
                } else {
                    text.narrow = counted->buffer;
                    textLength = counted->length;
                    textIsWide = false;
                }
                break;
            }

            case 'S':
                if (!(flags & (kShort | kLong | kWideChar)))
                    flags |= kWideChar;
                [[fallthrough]];
            case 's': {
                int limit = precision == -1 ? INT_MAX : precision;
                text.narrow = va_arg(arguments, char *);
                if (flags & (kLong | kWideChar)) {
                    if (text.wide == NULL)
                        text.wide = const_cast<uint16_t *>(kWideNullString);
                    textIsWide = true;
                    const uint16_t *p = text.wide;
                    while (limit-- && *p)
                        p++;
                    textLength = int(p - text.wide);
                } else {
                    if (text.narrow == NULL)
                        text.narrow = const_cast<char *>(kNullString);
                    const char *p = text.narrow;
                    while (limit-- && *p)
                        p++;
                    textLength = int(p - text.narrow);
                }
                break;
            }

            case 'n': {
                void *count = va_arg(arguments, void *);
                if (flags & kShort)
                    *(short *)count = short(written);
                else
                    *(int *)count = written;
                noOutput = true;
                break;
            }

            case 'E':
            case 'G':
                caps = true;
                ch += 'a' - 'A';
                [[fallthrough]];
            case 'e':
            case 'f':
            case 'g': {
                flags |= kSigned;
                text.narrow = buffer;
                if (precision < 0) {
                    precision = 6;
                } else if (precision == 0) {
                    if (ch == 'g')
                        precision = 1;
                } else {
                    if (precision > kMaxPrecision)
                        precision = kMaxPrecision;
                    if (precision > kBufferSize - kFloatBufferSize) {
                        heapBuffer = (char *)malloc(precision + kFloatBufferSize);
                        if (heapBuffer != NULL)
                            text.narrow = heapBuffer;
                        else
                            precision = kBufferSize - kFloatBufferSize;
                    }
                }
                double value = va_arg(arguments, double);
                FormatFloat(&value, text.narrow, ch, precision, caps);
                if ((flags & kAlternate) && precision == 0)
                    ForceDecimalPoint(text.narrow);
                if (ch == 'g' && !(flags & kAlternate))
                    CropZeros(text.narrow);
                if (*text.narrow == '-') {
                    flags |= kNegative;
                    text.narrow++;
                }
                textLength = int(strlen(text.narrow));
                break;
            }

            case 'd':
            case 'i':
                flags |= kSigned;
                radix = 10;
                goto integer;

            case 'u':
                radix = 10;
                goto integer;

            case 'p':
                precision = 2 * sizeof(void *);
                [[fallthrough]];
            case 'X':
                hexAdjust = 'A' - '9' - 1;
                goto hex;

            case 'x':
                hexAdjust = 'a' - '9' - 1;
            hex:
                radix = 16;
                if (flags & kAlternate) {
                    prefix[0] = '0';
                    prefix[1] = char('x' - 'a' + '9' + 1 + hexAdjust);
                    prefixLength = 2;
                }
                goto integer;

            case 'o':
                radix = 8;
                if (flags & kAlternate)
                    flags |= kForceOctal;
            integer: {
                int64_t value;
                if (flags & kInt64)
                    value = va_arg(arguments, int64_t);
                else if (flags & kShort)
                    value = (flags & kSigned) ? int64_t(short(va_arg(arguments, int)))
                                              : int64_t(uint16_t(va_arg(arguments, int)));
                else
                    value = (flags & kSigned) ? int64_t(va_arg(arguments, int))
                                              : int64_t(va_arg(arguments, unsigned));
                uint64_t number;
                if ((flags & kSigned) && value < 0) {
                    number = 0 - uint64_t(value);
                    flags |= kNegative;
                } else {
                    number = uint64_t(value);
                }
                if (!(flags & kInt64))
                    number &= 0xffffffff;

                if (precision < 0) {
                    precision = 1;
                } else {
                    flags &= ~kLeadZero;
                    if (precision > kMaxPrecision)
                        precision = kMaxPrecision;
                }
                if (number == 0)
                    prefixLength = 0;

                char *last = &buffer[kBufferSize - 1];
                text.narrow = last;
                while (precision-- > 0 || number != 0) {
                    int digit = int(number % radix) + '0';
                    number /= radix;
                    if (digit > '9')
                        digit += hexAdjust;
                    *text.narrow-- = char(digit);
                }
                textLength = int(last - text.narrow);
                text.narrow++;

                if ((flags & kForceOctal) && (*text.narrow != '0' || textLength == 0)) {
                    *--text.narrow = '0';
                    textLength++;
                }
                break;
            }
            }

            if (!noOutput) {
                if (flags & kSigned) {
                    if (flags & kNegative) {
                        prefix[0] = '-';
                        prefixLength = 1;
                    } else if (flags & kSign) {
                        prefix[0] = '+';
                        prefixLength = 1;
                    } else if (flags & kSignSpace) {
                        prefix[0] = ' ';
                        prefixLength = 1;
                    }
                }
                int padding = width - textLength - prefixLength;
                if (!(flags & (kLeft | kLeadZero)))
                    WriteMultiChar(' ', padding, stream, &written);
                WriteString(prefix, prefixLength, stream, &written);
                if ((flags & kLeadZero) && !(flags & kLeft))
                    WriteMultiChar('0', padding, stream, &written);
                if (textIsWide && textLength > 0) {
                    const uint16_t *p = text.wide;
                    for (int count = textLength; count > 0; count--) {
                        char converted[2];
                        int length = WideToChar(converted, *p++);
                        if (length <= 0)
                            break;
                        WriteString(converted, length, stream, &written);
                    }
                } else {
                    WriteString(text.narrow, textLength, stream, &written);
                }
                if (flags & kLeft)
                    WriteMultiChar(' ', padding, stream, &written);
            }
            if (heapBuffer != NULL) {
                free(heapBuffer);
                heapBuffer = NULL;
            }
            break;
        }
    }
    return written;
}

} // namespace

} // namespace Crt

// FUNC_AT(0x00132767)
int Crt::sprintf(char *buffer, const char *format, ...) {
    StringStream stream = { buffer, INT_MAX, buffer };
    va_list arguments;
    va_start(arguments, format);
    int written = Output(&stream, format, arguments);
    va_end(arguments);
    Terminate(&stream);
    return written;
}

// FUNC_AT(0x00133aee)
int Crt::vsprintf(char *buffer, const char *format, va_list arguments) {
    StringStream stream = { buffer, INT_MAX, buffer };
    int written = Output(&stream, format, arguments);
    Terminate(&stream);
    return written;
}

// -1 if the output did not fit in count characters; no null when it filled them exactly.
// FUNC_AT(0x001340f5)
int Crt::_vsnprintf(char *buffer, size_t count, const char *format, va_list arguments) {
    StringStream stream = { buffer, int(count), buffer };
    int written = Output(&stream, format, arguments);
    Terminate(&stream);
    return written;
}
