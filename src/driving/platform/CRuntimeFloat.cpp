#include "CRuntimeInternal.h"

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's floating-point conversions, between text and doubles, both ways through a 12-byte long double:
// a 16-bit extension below a 64-bit mantissa (top bit explicit), then a 15-bit exponent and the sign.
//
// Parsing (__strgtold12) gathers up to 24 significant digits into that, multiplies by the power of ten from tables
// of 12-byte constants, and rounds the result to a double or float (__ld12cvt). Neither step is correctly rounded -
// and __ld12cvt rounds a tie down, not to even - so a modern strtod can answer one ulp away. Printing ($I10_OUTPUT)
// turns a double into 17 decimal digits the same way round, and the %e/%f/%g layouts round those again. Both are
// ported as the runtime has them, step for step.
// ---------------------------------------------------------------------------------------------------------------

namespace Crt {

namespace {

// The 12-byte long double as six 16-bit words, low first. Word 0 is the extension, words 1-4 the mantissa, word 5
// the exponent; the arithmetic below also takes it as three dwords (bytes 0, 4, 8) and the conversions read the
// mantissa as the dwords at bytes 2 and 6.
struct Ld12 {
    uint16_t word[6];
};

uint32_t Dword(const Ld12 &x, int byte) {
    return x.word[byte / 2] | uint32_t(x.word[byte / 2 + 1]) << 16;
}

void SetDword(Ld12 &x, int byte, uint32_t value) {
    x.word[byte / 2] = uint16_t(value);
    x.word[byte / 2 + 1] = uint16_t(value >> 16);
}

// The mantissa's dwords and the exponent word.
uint32_t ManLo(const Ld12 &x) { return Dword(x, 2); }
uint32_t ManHi(const Ld12 &x) { return Dword(x, 6); }
uint16_t& Exponent(Ld12 &x) { return x.word[5]; }

// An 80-bit long double, as the printing side holds a double.
struct LongDouble {
    uint32_t manLo;
    uint32_t manHi;
    uint16_t exponent;
};

// ---- 96-bit arithmetic

// Whether x + y carried.
int AddCarry(uint32_t x, uint32_t y, uint32_t *sum) {                       // __addl
    uint32_t r = x + y;
    *sum = r;
    return r < x || r < y;
}

void Add12(Ld12 *x, const Ld12 *y) {                                        // __add_12
    uint32_t lo, med = Dword(*x, 4), hi = Dword(*x, 8);
    if (AddCarry(Dword(*x, 0), Dword(*y, 0), &lo)) {
        if (AddCarry(med, 1, &med))
            hi++;
    }
    if (AddCarry(med, Dword(*y, 4), &med))
        hi++;
    AddCarry(hi, Dword(*y, 8), &hi);
    SetDword(*x, 0, lo);
    SetDword(*x, 4, med);
    SetDword(*x, 8, hi);
}

void ShiftLeft12(Ld12 *x) {                                                 // __shl_12
    uint32_t lo = Dword(*x, 0), med = Dword(*x, 4), hi = Dword(*x, 8);
    SetDword(*x, 0, lo << 1);
    SetDword(*x, 4, med << 1 | lo >> 31);
    SetDword(*x, 8, hi << 1 | med >> 31);
}

void ShiftRight12(Ld12 *x) {                                                // __shr_12
    uint32_t lo = Dword(*x, 0), med = Dword(*x, 4), hi = Dword(*x, 8);
    SetDword(*x, 4, med >> 1 | hi << 31);
    SetDword(*x, 0, lo >> 1 | med << 31);
    SetDword(*x, 8, hi >> 1);
}

bool IsZero12(const Ld12 &x) {
    return (Dword(x, 8) & 0x7fffffff) == 0 && Dword(x, 4) == 0 && Dword(x, 0) == 0;
}

void PutInfinity(Ld12 *x, uint16_t sign) {
    SetDword(*x, 0, 0);
    SetDword(*x, 4, 0);
    SetDword(*x, 8, sign ? 0xffff8000 : 0x7fff8000);
}

void PutZero(Ld12 *x) {
    SetDword(*x, 0, 0);
    SetDword(*x, 4, 0);
    SetDword(*x, 8, 0);
}

// The decimal digits (0-9 each, most significant first) as a 12-byte long double, normalised.
void DigitsToLd12(const signed char *digits, unsigned count, Ld12 *result) {   // __mtold12
    uint16_t exponent = 0x404e;
    PutZero(result);
    for (; count > 0; count--, digits++) {
        Ld12 times = *result;            // the value so far times 10, plus the digit
        ShiftLeft12(result);
        ShiftLeft12(result);
        Add12(result, &times);
        ShiftLeft12(result);
        Ld12 digit = {};
        SetDword(digit, 0, uint32_t(int32_t(*digits)));
        Add12(result, &digit);
    }
    // normalise: a word at a time while the top dword is empty, then a bit at a time to bit 79
    if (Dword(*result, 8) == 0) {
        uint32_t hi;
        do {
            uint32_t lo = Dword(*result, 0), med = Dword(*result, 4);
            exponent -= 16;
            hi = med >> 16;
            SetDword(*result, 4, med << 16 | lo >> 16);
            SetDword(*result, 0, lo << 16);
        } while (hi == 0);
        SetDword(*result, 8, hi);
    }
    while ((Dword(*result, 8) & 0x8000) == 0) {
        ShiftLeft12(result);
        exponent -= 1;
    }
    Exponent(*result) = exponent;
}

void Ld12Multiply(Ld12 *x, const Ld12 *y) {                                 // __ld12mul
    uint16_t expX = x->word[5];
    uint16_t expY = y->word[5];
    uint16_t sign = (expX ^ expY) & 0x8000;
    expX &= 0x7fff;
    expY &= 0x7fff;
    uint16_t sum = expX + expY;
    if (expX >= 0x7fff || expY >= 0x7fff || sum > 0x7fff + 0x3ffe) {
        PutInfinity(x, sign);
        return;
    }
    if (sum <= 0x3ffe - 63) {
        PutZero(x);
        return;
    }
    if (expX == 0) {
        sum++;                           // a denormal's mantissa sits one place lower
        if (IsZero12(*x)) {
            x->word[5] = 0;              // a positive zero
            return;
        }
    }
    if (expY == 0) {
        sum++;
        if (IsZero12(*y)) {
            PutZero(x);
            return;
        }
    }

    // the mantissas' 16-bit words multiplied out, from the top down
    Ld12 product = {};
    int at = 0;
    for (int i = 0; i < 5; i++) {
        int p = i * 2;
        int q = 8;
        for (int j = 5 - i; j > 0; j--) {
            uint32_t partial;
            if (AddCarry(Dword(product, at), uint32_t(x->word[p / 2]) * y->word[q / 2], &partial)) {
                SetDword(product, at, partial);
                product.word[at / 2 + 2]++;
            } else {
                SetDword(product, at, partial);
            }
            p += 2;
            q -= 2;
        }
        at += 2;
    }

    sum -= 0x3ffe;
    while (int16_t(sum) > 0 && (Dword(product, 8) & 0x80000000) == 0) {
        ShiftLeft12(&product);
        sum--;
    }
    if (int16_t(sum) <= 0) {
        sum--;
        int sticky = 0;
        while (int16_t(sum) < 0) {
            if (product.word[0] & 1)
                sticky++;
            ShiftRight12(&product);
            sum++;
        }
        if (sticky)
            product.word[0] |= 1;
    }

    if (product.word[0] > 0x8000 || (Dword(product, 0) & 0x1ffff) == 0x18000) {
        if (Dword(product, 2) == 0xffffffff) {
            SetDword(product, 2, 0);
            if (Dword(product, 6) == 0xffffffff) {
                SetDword(product, 6, 0);
                if (product.word[5] == 0xffff) {
                    product.word[5] = 0x8000;
                    sum++;
                } else {
                    product.word[5]++;
                }
            } else {
                SetDword(product, 6, Dword(product, 6) + 1);
            }
        } else {
            SetDword(product, 2, Dword(product, 2) + 1);
        }
    }

    if (sum >= 0x7fff) {
        PutInfinity(x, sign);
        return;
    }
    x->word[0] = product.word[1];
    SetDword(*x, 2, Dword(product, 4));
    SetDword(*x, 6, Dword(product, 8));
    x->word[5] = sum | sign;
}

// 10^1..10^7, 10^8..10^56, 10^64..10^448, 10^512..10^3584 and 10^4096, and their reciprocals: 12-byte constants
// out of the runtime's tables (_pow10pos, _pow10neg).
const Ld12 kPowersOfTen[29] = {
    {{0x0000, 0x0000, 0x0000, 0x0000, 0xa000, 0x4002}}, {{0x0000, 0x0000, 0x0000, 0x0000, 0xc800, 0x4005}},
    {{0x0000, 0x0000, 0x0000, 0x0000, 0xfa00, 0x4008}}, {{0x0000, 0x0000, 0x0000, 0x0000, 0x9c40, 0x400c}},
    {{0x0000, 0x0000, 0x0000, 0x0000, 0xc350, 0x400f}}, {{0x0000, 0x0000, 0x0000, 0x0000, 0xf424, 0x4012}},
    {{0x0000, 0x0000, 0x0000, 0x8000, 0x9896, 0x4016}}, {{0x0000, 0x0000, 0x0000, 0x2000, 0xbebc, 0x4019}},
    {{0x0000, 0x0000, 0x0400, 0xc9bf, 0x8e1b, 0x4034}}, {{0x0000, 0xa100, 0xcced, 0x1bce, 0xd3c2, 0x404e}},
    {{0xf020, 0xb59e, 0x2b70, 0xada8, 0x9dc5, 0x4069}}, {{0x5dd0, 0x25fd, 0x1ae5, 0x4f8e, 0xeb19, 0x4083}},
    {{0x9671, 0x95d7, 0x0e43, 0x8d05, 0xaf29, 0x409e}}, {{0xbff9, 0x44a0, 0x81ed, 0x8f12, 0x8281, 0x40b9}},
    {{0x3cbf, 0xa6d5, 0xffcf, 0x1f49, 0xc278, 0x40d3}}, {{0xc66f, 0x8ce0, 0x80e9, 0x47c9, 0x93ba, 0x41a8}},
    {{0x85bc, 0x556b, 0x3927, 0xf78d, 0xe070, 0x427c}}, {{0xddbc, 0xde8e, 0x9df9, 0xebfb, 0xaa7e, 0x4351}},
    {{0xe6a1, 0xe376, 0xf2cc, 0x2f29, 0x8184, 0x4426}}, {{0x1028, 0xaa17, 0xaef8, 0xe310, 0xc4c5, 0x44fa}},
    {{0xa7eb, 0xf3d4, 0xebf7, 0x4ae1, 0x957a, 0x45cf}}, {{0xcc65, 0x91c7, 0xa60e, 0xa0ae, 0xe319, 0x46a3}},
    {{0x650d, 0x0c17, 0x8175, 0x7586, 0xc976, 0x4d48}}, {{0x4258, 0xa7e4, 0x3993, 0x353b, 0xb2b8, 0x53ed}},
    {{0xa74d, 0x5de5, 0xc53d, 0x3b5d, 0x9e8b, 0x5a92}}, {{0x5dff, 0xf0a6, 0x20a1, 0x54c0, 0x8ca5, 0x6137}},
    {{0xfdd1, 0x5a8b, 0xd88b, 0x5d25, 0xf989, 0x67db}}, {{0x95aa, 0xf3f8, 0xbf27, 0xc8a2, 0xdd5d, 0x6e80}},
    {{0xc94c, 0x979b, 0x8a20, 0x5202, 0xc460, 0x7525}},
};

const Ld12 kNegativePowersOfTen[29] = {
    {{0xcccd, 0xcccd, 0xcccc, 0xcccc, 0xcccc, 0x3ffb}}, {{0x3d71, 0xd70a, 0x70a3, 0x0a3d, 0xa3d7, 0x3ff8}},
    {{0x645a, 0xdf3b, 0x8d4f, 0x6e97, 0x8312, 0x3ff5}}, {{0xd3c3, 0x652c, 0xe219, 0x1758, 0xd1b7, 0x3ff1}},
    {{0x0fd0, 0x8423, 0x1b47, 0xac47, 0xa7c5, 0x3fee}}, {{0xa640, 0x69b6, 0xaf6c, 0xbd05, 0x8637, 0x3feb}},
    {{0x3d33, 0x42bc, 0xe57a, 0x94d5, 0xd6bf, 0x3fe7}}, {{0xfdc2, 0xcefd, 0x8461, 0x7711, 0xabcc, 0x3fe4}},
    {{0x4c2f, 0xe15b, 0xc44d, 0x94be, 0xe695, 0x3fc9}}, {{0xc492, 0x3b53, 0x4475, 0x14cd, 0x9abe, 0x3faf}},
    {{0x67de, 0x94ba, 0x4539, 0x1ead, 0xcfb1, 0x3f94}}, {{0x2324, 0xe2c6, 0xbabc, 0x313b, 0x8b61, 0x3f7a}},
    {{0x5561, 0xc159, 0xb17e, 0x7c53, 0xbb12, 0x3f5f}}, {{0xeed7, 0x8d2f, 0xbe06, 0x8592, 0xfb15, 0x3f44}},
    {{0x3f24, 0xe9a5, 0xa539, 0xea27, 0xa87f, 0x3f2a}}, {{0xac7d, 0xe4a1, 0x64bc, 0x467c, 0xddd0, 0x3e55}},
    {{0x7b63, 0xcc06, 0x5423, 0x8377, 0x91ff, 0x3d81}}, {{0xfa91, 0x193a, 0x637a, 0x4325, 0xc031, 0x3cac}},
    {{0x8921, 0x38d1, 0x4782, 0xb897, 0xfd00, 0x3bd7}}, {{0x88dc, 0x0858, 0xb11b, 0xe3e8, 0xa686, 0x3b03}},
    {{0x84c6, 0x4245, 0xb607, 0x7599, 0xdb37, 0x3a2e}}, {{0x7133, 0xd21c, 0xdb23, 0xee32, 0x9049, 0x395a}},
    {{0x87a6, 0xc0be, 0xda57, 0x82a5, 0xa2a6, 0x32b5}}, {{0x68e2, 0x11b2, 0x52a7, 0x449f, 0xb759, 0x2c10}},
    {{0x4925, 0x2de4, 0x3436, 0x534f, 0xceae, 0x256b}}, {{0x598f, 0xa404, 0xdec0, 0x7dc2, 0xe8fb, 0x1ec6}},
    {{0xe79e, 0x5a88, 0x9157, 0xbf3c, 0x8350, 0x1822}}, {{0x4b4e, 0x6265, 0x83fd, 0xaf8f, 0x9406, 0x117d}},
    {{0x2de4, 0x9fde, 0xd2ce, 0x04c8, 0xa6dd, 0x0ad8}},
};

// x times 10^power, the power taken three bits at a time against the tables. A constant whose extension rounds
// up is used one unit lower in its mantissa. Without mult12 the extension is cleared first.
void MultiplyByPowerOfTen(Ld12 *x, int power, int mult12) {                // __multtenpow12
    if (power == 0)
        return;
    const Ld12 *table = kPowersOfTen;
    if (power < 0) {
        power = -power;
        table = kNegativePowersOfTen;
    }
    if (!mult12)
        x->word[0] = 0;
    for (int group = -1; power != 0; group += 7) {   // seven entries per three bits of the power
        int last3 = power & 7;
        power >>= 3;
        if (last3 == 0)
            continue;
        const Ld12 *factor = &table[group + last3];
        Ld12 unrounded;
        if (factor->word[0] >= 0x8000) {
            unrounded = *factor;
            SetDword(unrounded, 2, Dword(unrounded, 2) - 1);
            factor = &unrounded;
        }
        Ld12Multiply(x, factor);
    }
}

// ---- text to a 12-byte long double

enum ParseResult {
    kParseUnderflow = 1,
    kParseOverflow = 2,
    kParseNoDigits = 4
};

bool IsDigit(char c) {
    return Ctype((unsigned char)c) & kDigit;
}

bool IsNonZeroDigit(char c) {
    return c >= '1' && c <= '9';
}

// Leading blanks, a sign, digits with one decimal point, and an exponent after 'e', 'E', 'd' or 'D' (or after a
// bare sign, with implicitE). *end is left after what was taken - at text itself if no number was there. scale is
// added to the exponent when there was none, decpt taken off when there was no point.
unsigned StringToLd12(Ld12 *result, const char **end, const char *text, int mult12, int scale, int decpt,
                      int implicitE) {                                       // __strgtold12
    enum State {
        kStart, kLeadingZeros, kSign, kWhole, kFraction, kPoint, kExponentMark, kExponentSign, kExponentZeros,
        kExponentDigits, kEnd, kImplicitExponent
    };
    const int kMaxDigits = 24;
    const int kMaxPower = 5200;

    signed char digits[kMaxDigits + 1];
    signed char *next = digits;
    unsigned count = 0;
    uint16_t sign = 0;
    int exponentSign = 1;
    bool foundDigit = false, foundPoint = false, foundExponent = false;
    int power = 0;
    int exponentAdjust = 0;
    unsigned flags = 0;

    const char *p = text;
    const char *saved = text;            // where the number ends if what follows turns out not to belong to it
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    State state = kStart;
    while (state != kEnd) {
        char c = *p++;
        switch (state) {
        case kStart:
            if (IsNonZeroDigit(c)) {
                state = kWhole;
                p--;
            } else if (c == DecimalPoint) {
                state = kPoint;
            } else if (c == '+') {
                sign = 0;
                state = kSign;
            } else if (c == '-') {
                sign = 0x8000;
                state = kSign;
            } else if (c == '0') {
                state = kLeadingZeros;
            } else {
                state = kEnd;
                p--;
            }
            break;

        case kLeadingZeros:
            foundDigit = true;
            if (IsNonZeroDigit(c)) {
                state = kWhole;
                p--;
            } else if (c == DecimalPoint) {
                state = kFraction;
            } else if (c == '+' || c == '-') {
                state = kImplicitExponent;
                p--;
            } else if (c == '0') {
                state = kLeadingZeros;
            } else if (c == 'E' || c == 'e' || c == 'D' || c == 'd') {
                state = kExponentMark;
            } else {
                state = kEnd;
                p--;
            }
            break;

        case kSign:
            if (IsNonZeroDigit(c)) {
                state = kWhole;
                p--;
            } else if (c == DecimalPoint) {
                state = kPoint;
            } else if (c == '0') {
                state = kLeadingZeros;
            } else {
                state = kEnd;
                p = saved;
            }
            break;

        case kWhole:
        case kFraction:
            foundDigit = true;
            if (state == kWhole) {
                for (; IsDigit(c); c = *p++) {
                    if (count < kMaxDigits + 1) {
                        count++;
                        *next++ = static_cast<signed char>(c - '0');
                    } else {
                        exponentAdjust++;
                    }
                }
                if (c == DecimalPoint) {
                    state = kFraction;
                    break;
                }
            } else {
                foundPoint = true;
                if (count == 0) {
                    for (; c == '0'; c = *p++)
                        exponentAdjust--;
                }
                for (; IsDigit(c); c = *p++) {
                    if (count < kMaxDigits + 1) {
                        count++;
                        exponentAdjust--;
                        *next++ = static_cast<signed char>(c - '0');
                    }
                }
            }
            if (c == '+' || c == '-') {
                state = kImplicitExponent;
                p--;
            } else if (c == 'E' || c == 'e' || c == 'D' || c == 'd') {
                state = kExponentMark;
            } else {
                state = kEnd;
                p--;
            }
            break;

        case kPoint:
            foundPoint = true;
            if (IsDigit(c)) {
                state = kFraction;
                p--;
            } else {
                state = kEnd;
                p = saved;
            }
            break;

        case kExponentMark:
            saved = p - 2;               // the 'e'
            if (IsNonZeroDigit(c)) {
                state = kExponentDigits;
                p--;
            } else if (c == '+') {
                state = kExponentSign;
            } else if (c == '-') {
                exponentSign = -1;
                state = kExponentSign;
            } else if (c == '0') {
                state = kExponentZeros;
            } else {
                state = kEnd;
                p = saved;
            }
            break;

        case kImplicitExponent:
            if (!implicitE) {
                state = kEnd;
                p--;
                break;
            }
            saved = p - 1;               // the sign
            if (c == '+') {
                state = kExponentSign;
            } else if (c == '-') {
                exponentSign = -1;
                state = kExponentSign;
            } else {
                state = kEnd;
                p = saved;
            }
            break;

        case kExponentSign:
            if (IsNonZeroDigit(c)) {
                state = kExponentDigits;
                p--;
            } else if (c == '0') {
                state = kExponentZeros;
            } else {
                state = kEnd;
                p = saved;
            }
            break;

        case kExponentZeros:
            foundExponent = true;
            for (; c == '0'; c = *p++) {
            }
            if (IsNonZeroDigit(c)) {
                state = kExponentDigits;
                p--;
            } else {
                state = kEnd;
                p--;
            }
            break;

        case kExponentDigits:
            foundExponent = true;
            power = 0;
            for (; IsDigit(c); c = *p++) {
                power = power * 10 + (c - '0');
                if (power > kMaxPower) {
                    power = kMaxPower + 1;
                    break;
                }
            }
            for (; IsDigit(c); c = *p++) {
            }
            state = kEnd;
            p--;
            break;

        case kEnd:
            break;
        }
    }
    *end = p;

    Ld12 value = {};
    if (!foundDigit) {
        flags = kParseNoDigits;
    } else {
        if (count > kMaxDigits) {
            if (digits[kMaxDigits - 1] >= 5)   // the last digit kept goes up, even past 9
                digits[kMaxDigits - 1]++;
            count = kMaxDigits;
            next--;
            exponentAdjust++;
        }
        if (count > 0) {
            for (next--; *next == 0; next--) {
                count--;
                exponentAdjust++;
            }
            DigitsToLd12(digits, count, &value);
            if (exponentSign < 0)
                power = -power;
            power += exponentAdjust;
            if (!foundExponent)
                power += scale;
            if (!foundPoint)
                power -= decpt;
            if (power > kMaxPower) {
                value = {};
                SetDword(value, 6, 0x80000000);
                value.word[5] = 0x7fff;
                flags = kParseOverflow;
            } else if (power < -kMaxPower) {
                value = {};
                flags = kParseUnderflow;
            } else {
                MultiplyByPowerOfTen(&value, power, mult12);
            }
        }
    }
    result->word[0] = value.word[0];
    SetDword(*result, 2, ManLo(value));
    SetDword(*result, 6, ManHi(value));
    result->word[5] = value.word[5] | sign;
    return flags;
}

// ---- a 12-byte long double to a double or a float

enum ConvertResult {
    kConvertOk = 0,
    kConvertOverflow = 1,
    kConvertUnderflow = 2
};

struct FpFormat {
    int maxExponent;
    int minExponent;
    int precision;
    int exponentWidth;
    int bits;
    int bias;
};

const FpFormat kDoubleFormat = { 0x7ff - 0x3ff, 0 - 0x3ff, 53, 11, 64, 0x3ff };
const FpFormat kFloatFormat = { 0xff - 0x7f, 0 - 0x7f, 24, 8, 32, 0x7f };

// The mantissa as three dwords, most significant first, bit 0 of the first dword's top the explicit one.

// Whether every bit after bit n is clear.
bool TailIsZero(const uint32_t mantissa[3], int n) {                      // _ZeroTail
    int word = n / 32;
    if (mantissa[word] & ~(0xffffffffu << (31 - n % 32)))
        return false;
    for (word++; word < 3; word++) {
        if (mantissa[word] != 0)
            return false;
    }
    return true;
}

// Adds one at bit n; whether it carried out of the top.
int IncrementMantissa(uint32_t mantissa[3], int n) {                       // _IncMan
    int word = n / 32;
    int carry = AddCarry(mantissa[word], 1u << (31 - n % 32), &mantissa[word]);
    for (word--; word >= 0 && carry; word--)
        carry = AddCarry(mantissa[word], 1, &mantissa[word]);
    return carry;
}

// Rounds to precision bits: up when the bit after them is set and something below it is too - a tie stays down.
// The rounding bit itself is kept and everything after it cleared. Whether the rounding carried out of the top.
int RoundMantissa(uint32_t mantissa[3], int precision) {                   // _RoundMan
    int last = precision - 1;
    int roundBit = last + 1;
    int word = roundBit / 32;
    int shift = 31 - roundBit % 32;
    int carried = 0;
    if ((mantissa[word] & (1u << shift)) && !TailIsZero(mantissa, roundBit))
        carried = IncrementMantissa(mantissa, last);
    mantissa[word] &= 0xffffffffu << shift;
    for (word++; word < 3; word++)
        mantissa[word] = 0;
    return carried;
}

bool MantissaIsZero(const uint32_t mantissa[3]) {                          // _IsZeroMan
    return mantissa[0] == 0 && mantissa[1] == 0 && mantissa[2] == 0;
}

void ShiftMantissaRight(uint32_t mantissa[3], int n) {                     // _ShrMan
    int words = n / 32;
    int bits = n % 32;
    uint32_t lowMask = ~(0xffffffffu << bits);
    uint32_t carry = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t low = mantissa[i] & lowMask;
        mantissa[i] = mantissa[i] >> bits | carry;
        carry = bits != 0 ? low << (32 - bits) : 0;
    }
    for (int i = 2; i >= 0; i--)
        mantissa[i] = i >= words ? mantissa[i - words] : 0;
}

int ConvertLd12(const Ld12 *value, void *result, const FpFormat *format) {  // _ld12cvt
    uint16_t exponentWord = value->word[5];
    uint32_t sign = exponentWord & 0x8000;
    int exponent = (exponentWord & 0x7fff) - 0x3fff;
    uint32_t mantissa[3] = { ManHi(*value), ManLo(*value), uint32_t(value->word[0]) << 16 };
    uint32_t saved[3];
    uint32_t biasedExponent;
    int status;

    if (exponent == 0 - 0x3fff) {
        biasedExponent = 0;              // zero, or a denormal flushed to zero
        if (MantissaIsZero(mantissa)) {
            status = kConvertOk;
        } else {
            memset(mantissa, 0, sizeof(mantissa));
            status = kConvertUnderflow;
        }
    } else {
        memcpy(saved, mantissa, sizeof(saved));
        if (RoundMantissa(mantissa, format->precision))
            exponent++;
        if (exponent < format->minExponent - format->precision) {
            memset(mantissa, 0, sizeof(mantissa));
            biasedExponent = 0;
            status = kConvertUnderflow;
        } else if (exponent <= format->minExponent) {
            // a denormal: shifted from the saved mantissa, rounded again, and moved below the exponent field
            memcpy(mantissa, saved, sizeof(mantissa));
            ShiftMantissaRight(mantissa, format->minExponent - exponent);
            RoundMantissa(mantissa, format->precision);
            ShiftMantissaRight(mantissa, format->exponentWidth + 1);
            biasedExponent = 0;
            status = kConvertUnderflow;
        } else if (exponent >= format->maxExponent) {
            memset(mantissa, 0, sizeof(mantissa));
            mantissa[0] |= 0x80000000;
            ShiftMantissaRight(mantissa, format->exponentWidth);
            biasedExponent = format->maxExponent + format->bias;
            status = kConvertOverflow;
        } else {
            biasedExponent = exponent + format->bias;
            mantissa[0] &= 0x7fffffff;   // the implied bit
            ShiftMantissaRight(mantissa, format->exponentWidth);
            status = kConvertOk;
        }
    }

    uint32_t high = mantissa[0] | biasedExponent << (31 - format->exponentWidth) | (sign ? 0x80000000 : 0);
    if (format->bits == 64) {
        uint32_t words[2] = { mantissa[1], high };
        memcpy(result, words, 8);
    } else if (format->bits == 32) {
        memcpy(result, &high, 4);
    }
    return status;
}

int Ld12ToDouble(const Ld12 *value, double *result) {                      // _ld12tod
    return ConvertLd12(value, result, &kDoubleFormat);
}

int Ld12ToFloat(const Ld12 *value, float *result) {                        // _ld12tof
    return ConvertLd12(value, result, &kFloatFormat);
}

int StringToDouble(double *result, const char *text) {                     // _atodbl
    Ld12 value;
    const char *end;
    StringToLd12(&value, &end, text, 0, 0, 0, 0);
    return Ld12ToDouble(&value, result);
}

int StringToFloat(float *result, const char *text) {                       // _atoflt
    Ld12 value;
    const char *end;
    StringToLd12(&value, &end, text, 0, 0, 0, 0);
    return Ld12ToFloat(&value, result);
}

// ---- a double to decimal digits

void DoubleToLongDouble(LongDouble *result, const double *value) {         // __dtold
    uint32_t words[2];
    memcpy(words, value, 8);
    uint32_t low = words[0];
    uint16_t sign = (words[1] >> 16) & 0x8000;
    uint32_t biased = (words[1] >> 20) & 0x7ff;
    uint32_t high = words[1] & 0xfffff;
    uint32_t topBit = 0x80000000;
    uint16_t exponent;
    if (biased == 0) {
        if (high == 0 && low == 0) {
            result->manLo = 0;           // a zero of either sign comes out positive
            result->manHi = 0;
            result->exponent = 0;
            return;
        }
        exponent = 0x3c01;
        topBit = 0;
    } else if (biased == 0x7ff) {
        exponent = 0x7fff;
    } else {
        exponent = uint16_t(biased + 0x3c00);
    }
    result->manHi = high << 11 | low >> 21 | topBit;
    result->manLo = low << 11;
    while ((result->manHi & 0x80000000) == 0) {
        result->manHi = result->manHi << 1 | result->manLo >> 31;
        result->manLo <<= 1;
        exponent--;
    }
    result->exponent = sign | exponent;
}

// The digits $I10_OUTPUT gives: the decimal exponent, ' ' or '-', and up to 21 digits (rounded, trailing zeros
// dropped), or "1#INF" and the like.
struct DecimalDigits {
    int16_t exponent;
    char sign;
    char count;
    char digits[22];
};

const Ld12 kOneTenth = {{0xcccc, 0xcccc, 0xcccc, 0xcccc, 0xcccc, 0x3ffb}};

void PutZeroDigits(DecimalDigits *out) {
    out->digits[0] = '0';
    out->exponent = 0;
    out->digits[1] = '\0';
    out->sign = ' ';
    out->count = 1;
}

// ndigits significant digits, or with kFFormat that many after the point. 0 for an infinity or a NaN, 1 otherwise.
enum { kFFormat = 1 };
int LongDoubleToDigits(LongDouble value, int ndigits, unsigned flags, DecimalDigits *out) {   // $I10_OUTPUT
    uint16_t exponent = value.exponent & 0x7fff;
    out->sign = (value.exponent & 0x8000) ? '-' : ' ';

    if (exponent == 0 && value.manHi == 0 && value.manLo == 0) {
        PutZeroDigits(out);
        return 1;
    }

    if (exponent == 0x7fff) {
        static const char kSignallingNaN[] = "1#SNAN";
        static const char kIndefinite[] = "1#IND";
        static const char kInfinity[] = "1#INF";
        static const char kQuietNaN[] = "1#QNAN";
        const char *text;
        out->exponent = 1;
        if (!(value.manHi == 0x80000000 && value.manLo == 0) && !(value.manHi & 0x40000000))
            text = kSignallingNaN;
        else if ((value.exponent & 0x8000) && value.manHi == 0xc0000000)
            text = value.manLo != 0 ? kQuietNaN : kIndefinite;
        else if (value.manHi == 0x80000000 && value.manLo == 0)
            text = kInfinity;
        else
            text = kQuietNaN;
        size_t length = strlen(text);
        memcpy(out->digits, text, length + 1);
        out->count = char(length);
        return 0;
    }

    // the decimal exponent, estimated from the binary one and the mantissa's top byte (log10(2) in 16.16)
    int estimate = int(exponent) * 0x4d10 + (int(exponent >> 8) + int(value.manHi >> 24) * 2) * 0x4d - 0x134312f4;
    int16_t power = int16_t(estimate >> 16);

    Ld12 x = {};
    SetDword(x, 2, value.manLo);
    SetDword(x, 6, value.manHi);
    x.word[5] = exponent;
    MultiplyByPowerOfTen(&x, -power, 1);
    if (x.word[5] >= 0x3fff) {
        power++;
        Ld12Multiply(&x, &kOneTenth);
    }
    out->exponent = power;

    if ((flags & kFFormat) && (ndigits += power) <= 0) {
        PutZeroDigits(out);
        return 1;
    }
    if (ndigits > 21)
        ndigits = 21;

    // a binary fraction with eight bits of integer above it, from which each digit is taken by multiplying by 10
    int shift = int(x.word[5]) - 0x3ffe;
    x.word[5] = 0;
    for (int i = 0; i < 8; i++)
        ShiftLeft12(&x);
    if (shift < 0) {
        for (shift = -shift & 0xff; shift > 0; shift--)
            ShiftRight12(&x);
    }
    char *p = out->digits;
    for (int count = ndigits + 1; count > 0; count--) {
        Ld12 times = x;
        ShiftLeft12(&x);
        ShiftLeft12(&x);
        Add12(&x, &times);
        ShiftLeft12(&x);
        *p++ = char((x.word[5] >> 8) + '0');
        x.word[5] &= 0x00ff;
    }

    // round on the extra digit
    p--;
    char last = *p--;
    if (last >= '5') {
        for (; p >= out->digits && *p == '9'; p--)
            *p = '0';
        if (p < out->digits) {
            p++;
            out->exponent++;
        }
        (*p)++;
    } else {
        for (; p >= out->digits && *p == '0'; p--) {
        }
        if (p < out->digits) {
            PutZeroDigits(out);
            return 1;
        }
    }
    out->count = char(p - out->digits + 1);
    out->digits[int(out->count)] = '\0';
    return 1;
}

// The digits as the %e/%f/%g layouts take them (_strflt): sign, decimal point position, the status, the digits.
struct FloatDigits {
    int sign;
    int decpt;
    int flag;
    char *mantissa;
};

FloatDigits* DoubleToDigits(double value, FloatDigits *result, char *digits) {   // _fltout2
    LongDouble x;
    DoubleToLongDouble(&x, &value);
    DecimalDigits decimal;
    result->flag = LongDoubleToDigits(x, 17, 0, &decimal);
    result->sign = decimal.sign;
    result->decpt = decimal.exponent;
    memcpy(digits, decimal.digits, strlen(decimal.digits) + 1);
    result->mantissa = digits;
    return result;
}

// "0" and then `count` digits of the mantissa (zeros past its end), rounded on the next one; the leading zero
// stays only if the rounding carried into it, which moves the point.
void CopyDigits(char *buffer, int count, FloatDigits *flt) {               // _fptostr
    char *p = buffer;
    const char *mantissa = flt->mantissa;
    *p++ = '0';
    while (count > 0) {
        *p++ = *mantissa ? *mantissa++ : '0';
        count--;
    }
    *p = '\0';
    if (count >= 0 && *mantissa >= '5') {
        p--;
        while (*p == '9')
            *p-- = '0';
        *p += 1;
    }
    if (*buffer == '1')
        flt->decpt++;
    else
        memmove(buffer, buffer + 1, strlen(buffer + 1) + 1);
}

// Opens a gap of `distance` characters at s.
void Shift(char *s, int distance) {                                         // _shift
    if (distance)
        memmove(s + distance, s, strlen(s) + 1);
}

char* LayOutE(char *buffer, int ndec, int caps, const FloatDigits *flt, bool gFormat) {   // _cftoe2
    if (gFormat)
        Shift(buffer + (flt->sign == '-'), ndec > 0);
    char *p = buffer;
    if (flt->sign == '-')
        *p++ = '-';
    if (ndec > 0) {
        *p = p[1];
        *++p = DecimalPoint;
    }
    p += ndec + !gFormat;
    memcpy(p, "e+000", sizeof("e+000"));
    if (caps)
        *p = 'E';
    p++;
    if (*flt->mantissa != '0') {
        int exponent = flt->decpt - 1;
        if (exponent < 0) {
            exponent = -exponent;
            *p = '-';
        }
        p++;
        if (exponent >= 100) {
            *p += char(exponent / 100);
            exponent %= 100;
        }
        p++;
        if (exponent >= 10) {
            *p += char(exponent / 10);
            exponent %= 10;
        }
        *++p += char(exponent);
    }
    return buffer;
}

char* LayOutF(char *buffer, int ndec, const FloatDigits *flt, bool gFormat) {   // _cftof2
    int decpt = flt->decpt - 1;
    if (gFormat && decpt == ndec) {
        char *p = buffer + (flt->sign == '-') + decpt;
        p[0] = '0';
        p[1] = '\0';
    }
    char *p = buffer;
    if (flt->sign == '-')
        *p++ = '-';
    if (flt->decpt <= 0) {
        Shift(p, 1);
        *p++ = '0';
    } else {
        p += flt->decpt;
    }
    if (ndec > 0) {
        Shift(p, 1);
        *p++ = DecimalPoint;
        if (flt->decpt < 0) {
            if (gFormat || ndec >= -flt->decpt)
                ndec = -flt->decpt;
            Shift(p, ndec);
            memset(p, '0', ndec);
        }
    }
    return buffer;
}

char* FormatE(const double *value, char *buffer, int ndec, int caps) {     // _cftoe
    FloatDigits flt;
    char digits[24];
    DoubleToDigits(*value, &flt, digits);
    CopyDigits(buffer + (flt.sign == '-') + (ndec > 0), ndec + 1, &flt);
    LayOutE(buffer, ndec, caps, &flt, false);
    return buffer;
}

char* FormatF(const double *value, char *buffer, int ndec) {                // _cftof
    FloatDigits flt;
    char digits[24];
    DoubleToDigits(*value, &flt, digits);
    CopyDigits(buffer + (flt.sign == '-'), flt.decpt + ndec, &flt);
    LayOutF(buffer, ndec, &flt, false);
    return buffer;
}

char* FormatG(const double *value, char *buffer, int ndec, int caps) {      // _cftog
    FloatDigits flt;
    char digits[24];
    DoubleToDigits(*value, &flt, digits);
    int magnitude = flt.decpt - 1;
    char *p = buffer + (flt.sign == '-');
    CopyDigits(p, ndec, &flt);
    bool roundExpanded = magnitude < flt.decpt - 1;
    magnitude = flt.decpt - 1;
    if (magnitude < -4 || magnitude >= ndec)
        return LayOutE(buffer, ndec, caps, &flt, true);
    if (roundExpanded) {                 // the rounding added a digit: drop the last
        while (*p++) {
        }
        p[-2] = '\0';
    }
    return LayOutF(buffer, ndec, &flt, true);
}

} // namespace

void FormatFloat(const double *value, char *buffer, int format, int precision, int caps) {
    if (format == 'e' || format == 'E')
        FormatE(value, buffer, precision, caps);
    else if (format == 'f')
        FormatF(value, buffer, precision);
    else
        FormatG(value, buffer, precision, caps);
}

void CropZeros(char *buffer) {
    while (*buffer && *buffer != DecimalPoint)
        buffer++;
    if (*buffer++) {
        while (*buffer && *buffer != 'e' && *buffer != 'E')
            buffer++;
        char *stop = buffer--;
        while (*buffer == '0')
            buffer--;
        if (*buffer == DecimalPoint)
            buffer--;
        while ((*++buffer = *stop++) != '\0') {
        }
    }
}

void ForceDecimalPoint(char *buffer) {
    if (tolower((signed char)*buffer) != 'e') {
        do {
            buffer++;
        } while (Ctype((signed char)*buffer) & kDigit);
    }
    char hold = *buffer;
    *buffer++ = DecimalPoint;
    do {
        char next = *buffer;
        *buffer = hold;
        hold = next;
    } while (*buffer++);
}

void AssignFloat(int asDouble, void *argument, const char *number) {
    if (asDouble) {
        double value;
        StringToDouble(&value, number);
        memcpy(argument, &value, sizeof(value));
    } else {
        float value;
        StringToFloat(&value, number);
        memcpy(argument, &value, sizeof(value));
    }
}

FloatIn* ParseFloat(FloatIn *result, const char *text) {
    Ld12 value;
    const char *end;
    int flags = 0;
    unsigned parsed = StringToLd12(&value, &end, text, 0, 0, 0, 0);
    if (parsed & kParseNoDigits) {
        flags = kFloatNoDigits;
        result->value = 0.0;
    } else {
        int status = Ld12ToDouble(&value, &result->value);
        if ((parsed & kParseOverflow) || status == kConvertOverflow)
            flags = kFloatOverflow;
        if ((parsed & kParseUnderflow) || status == kConvertUnderflow)
            flags |= kFloatUnderflow;
    }
    result->flags = flags;
    result->length = int(end - text);
    return result;
}

} // namespace Crt
