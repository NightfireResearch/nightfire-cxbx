#include "DetMath.h"

#include <math.h>
#include <stdint.h>
#include <bit>

// fdlibm's algorithms and coefficients (FreeBSD msun: k_sin.c, k_cos.c, k_tan.c, e_rem_pio2.c, s_atan.c, e_atan2.c,
// e_asin.c, e_acos.c, e_exp.c, e_log.c, e_pow.c), copyright Sun Microsystems: "Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice is preserved." The reduction of arguments above 2^20 * pi/2
// is our own (Payne and Hanek's, in integers).

// Only the C forms of the x87 code use these: an x87 build compiles none of it.
#if !NIGHTFIRE_X87

#if defined(_MSC_VER) && !defined(__clang__)
#pragma fp_contract(off)
#endif
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif
#if NIGHTFIRE_DET_MATH && ((defined(_M_IX86) && !defined(_M_IX86_FP)) || (defined(_M_IX86_FP) && _M_IX86_FP < 2) || \
                           (defined(__i386__) && !defined(__SSE2_MATH__)))
#error "DetMath needs SSE2 double arithmetic (/arch:SSE2, -msse2 -mfpmath=sse)"
#endif

namespace DetMath {
namespace {

uint32_t High(double x) {
    return uint32_t(std::bit_cast<uint64_t>(x) >> 32);
}

uint32_t Low(double x) {
    return uint32_t(std::bit_cast<uint64_t>(x));
}

double WithLow(double x, uint32_t low) {
    return std::bit_cast<double>((std::bit_cast<uint64_t>(x) & 0xffffffff00000000ull) | low);
}

double FromWords(uint32_t high, uint32_t low) {
    return std::bit_cast<double>(uint64_t(high) << 32 | low);
}

double Quiet(double x) {
    return std::bit_cast<double>(std::bit_cast<uint64_t>(x) | 0x0008000000000000ull);
}

// The x87's default NaN, the "real indefinite"
const double kInvalid = std::bit_cast<double>(0xfff8000000000000ull);

// The x87's choice between two NaNs: the larger significand, the positive one when only the signs differ
double PickNaN(double a, double b) {
    if (a == a)
        return Quiet(b);
    if (b == b)
        return Quiet(a);
    uint64_t magnitudeA = std::bit_cast<uint64_t>(a) & 0x7fffffffffffffffull;
    uint64_t magnitudeB = std::bit_cast<uint64_t>(b) & 0x7fffffffffffffffull;
    if (magnitudeA != magnitudeB)
        return Quiet(magnitudeA > magnitudeB ? a : b);
    return Quiet(std::bit_cast<double>(std::bit_cast<uint64_t>(a) & std::bit_cast<uint64_t>(b)));
}

double Abs(double x) {
    return std::bit_cast<double>(std::bit_cast<uint64_t>(x) & 0x7fffffffffffffffull);
}

// ---- sin, cos and tan on [-pi/4, pi/4], of x + y (y the tail of a reduced argument)

const double S1 = -1.66666666666666324348e-01;
const double S2 = 8.33333333332248946124e-03;
const double S3 = -1.98412698298579493134e-04;
const double S4 = 2.75573137070700676789e-06;
const double S5 = -2.50507602534068634195e-08;
const double S6 = 1.58969099521155010221e-10;

double KernelSin(double x, double y, bool hasTail) {
    double z = x * x;
    double w = z * z;
    double r = S2 + z * (S3 + z * S4) + z * w * (S5 + z * S6);
    double v = z * x;
    if (!hasTail)
        return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

const double C1 = 4.16666666666666019037e-02;
const double C2 = -1.38888888888741095749e-03;
const double C3 = 2.48015872894767294178e-05;
const double C4 = -2.75573143513906633035e-07;
const double C5 = 2.08757232129817482790e-09;
const double C6 = -1.13596475577881948265e-11;

double KernelCos(double x, double y) {
    double z = x * x;
    double w = z * z;
    double r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
    double hz = 0.5 * z;
    w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * r - x * y));
}

const double T[] = {
    3.33333333333334091986e-01, 1.33333333333201242699e-01, 5.39682539762260521377e-02, 2.18694882948595424599e-02,
    8.86323982359930005737e-03, 3.59207910759131235356e-03, 1.45620945432529025516e-03, 5.88041240820264096874e-04,
    2.46463134818469906812e-04, 7.81794442939557092300e-05, 7.14072491382608190305e-05, -1.85586374855275456654e-05,
    2.59073051863633712884e-05,
};
const double kPio4 = 7.85398163397448278999e-01;
const double kPio4Lo = 3.06161699786838301793e-17;

// tan(x + y) for parity 1, -1/tan(x + y) for parity -1
double KernelTan(double x, double y, int parity) {
    uint32_t hx = High(x);
    bool big = (hx & 0x7fffffff) >= 0x3fe59428;   // |x| >= 0.6744
    if (big) {
        if (int32_t(hx) < 0) {
            x = -x;
            y = -y;
        }
        double z = kPio4 - x;
        double w = kPio4Lo - y;
        x = z + w;
        y = 0.0;
    }
    double z = x * x;
    double w = z * z;
    double r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
    double v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
    double s = z * x;
    r = y + z * (s * (r + v) + y);
    r += T[0] * s;
    w = x + r;
    if (big) {
        v = parity;
        return (1 - int((hx >> 30) & 2)) * (v - 2.0 * (x - (w * w / (w + v) - r)));
    }
    if (parity == 1)
        return w;
    // -1/(x + r), accurately
    z = WithLow(w, 0);
    v = r - (z - x);
    double a = -1.0 / w;
    double t = WithLow(a, 0);
    s = 1.0 + t * z;
    return t + a * (s + t * v);
}

// ---- argument reduction: x = n * pi/2 + (y[0] + y[1]), |y[0] + y[1]| <= pi/4

const double kInvPio2 = 6.36619772367581382433e-01;
const double kPio2_1 = 1.57079632673412561417e+00;
const double kPio2_1t = 6.07710050650619224932e-11;
const double kPio2_2 = 6.07710050630396597660e-11;
const double kPio2_2t = 2.02226624879595063154e-21;
const double kPio2_3 = 2.02226624871116645580e-21;
const double kPio2_3t = 8.47842766036889956997e-32;
const double kRoundToInteger = 6755399441055744.0;   // 0x1.8p52

// 2/pi's bits after the point, 32 at a time
const uint32_t kTwoOverPi[] = {
    0xa2f9836e, 0x4e441529, 0xfc2757d1, 0xf534ddc0, 0xdb629599, 0x3c439041, 0xfe5163ab, 0xdebbc561, 0xb7246e3a,
    0x424dd2e0, 0x06492eea, 0x09d1921c, 0xfe1deb1c, 0xb129a73e, 0xe88235f5, 0x2ebb4484, 0xe99c7026, 0xb45f7e41,
    0x3991d639, 0x835339f4, 0x9c845f8b, 0xbdf9283b, 0x1ff897ff, 0xde05980f, 0xef2f118b, 0x5a0a6d1f, 0x6d367ecf,
    0x27cb09b7, 0x4f463f66, 0x9e5fea2d, 0x7527bac7, 0xebe5f17b, 0x3d0739f7, 0x8a5292ea, 0x6bfb5fb1, 0x1f8d5d08,
    0x56033046, 0xfc7b6bab, 0xf0cfbc20, 0x9af4361d,
};
const double kPio2Hi = 1.57079632679489655800e+00;   // 0x1.921fb54442d18p+0
const double kPio2Lo = 6.12323399573676603587e-17;   // 0x1.1a62633145c07p-54

// a + b exactly, as hi + lo
void TwoSum(double a, double b, double &hi, double &lo) {
    hi = a + b;
    double bv = hi - a;
    double av = hi - bv;
    lo = (a - av) + (b - bv);
}

// a * b exactly, as hi + lo (Dekker's product, without a fused multiply-add)
void TwoProduct(double a, double b, double &hi, double &lo) {
    const double kSplit = 134217729.0;   // 2^27 + 1
    double ca = kSplit * a;
    double aHi = ca - (ca - a);
    double aLo = a - aHi;
    double cb = kSplit * b;
    double bHi = cb - (cb - b);
    double bLo = b - bHi;
    hi = a * b;
    lo = ((aHi * bHi - hi) + aHi * bLo + aLo * bHi) + aLo * bLo;
}

// 32 bits of a 256-bit number (eight words, least significant first) from bit `at` up
uint32_t BitsAt(const uint32_t number[8], int at) {
    int word = at / 32;
    int shift = at % 32;
    uint32_t bits = number[word] >> shift;
    if (shift != 0 && word + 1 < 8)
        bits |= number[word + 1] << (32 - shift);
    return bits;
}

// |x| at or above 2^20 * pi/2, finite: the fraction of x * 2/pi in integers
int ReduceLarge(double x, double y[2]) {
    uint64_t bits = std::bit_cast<uint64_t>(x);
    int exponent = int((bits >> 52) & 0x7ff) - 1075;                 // |x| = mantissa * 2^exponent
    uint64_t mantissa = (bits & 0x000fffffffffffffull) | 0x0010000000000000ull;

    // 192 bits of 2/pi from bit `first` (1 the first after the point). Earlier bits make multiples of 4 of
    // mantissa * 2^exponent * 2/pi, which leave the quadrant and the fraction alone.
    int first = exponent - 1 > 1 ? exponent - 1 : 1;
    uint32_t window[6];   // least significant first
    for (int i = 0; i < 6; i++) {
        int at = first - 1 + 32 * (5 - i);   // bit offset of this word's top bit, from the first after the point
        int word = at / 32;
        int shift = at % 32;
        uint32_t value = kTwoOverPi[word] << shift;
        if (shift != 0)
            value |= kTwoOverPi[word + 1] >> (32 - shift);
        window[i] = value;
    }

    // mantissa * window, 256 bits
    uint32_t product[8] = {};
    uint32_t m[2] = {uint32_t(mantissa), uint32_t(mantissa >> 32)};
    for (int j = 0; j < 2; j++) {
        uint64_t carry = 0;
        for (int i = 0; i < 6; i++) {
            uint64_t t = uint64_t(m[j]) * window[i] + product[i + j] + carry;
            product[i + j] = uint32_t(t);
            carry = t >> 32;
        }
        for (int k = j + 6; k < 8 && carry != 0; k++) {
            uint64_t t = uint64_t(product[k]) + carry;
            product[k] = uint32_t(t);
            carry = t >> 32;
        }
    }

    // x * 2/pi = product * 2^-point, modulo 4
    int point = first + 191 - exponent;
    int quadrant = int(BitsAt(product, point) & 3);
    uint32_t f[4] = {BitsAt(product, point - 128), BitsAt(product, point - 96), BitsAt(product, point - 64),
                     BitsAt(product, point - 32)};   // the fraction, least significant word first
    bool negative = (f[3] & 0x80000000u) != 0;
    if (negative) {
        // From the next quadrant: the fraction becomes 1 - f
        quadrant = (quadrant + 1) & 3;
        uint64_t borrow = 0;
        for (int i = 0; i < 4; i++) {
            uint64_t t = 0 - uint64_t(f[i]) - borrow;
            f[i] = uint32_t(t);
            borrow = (t >> 32) & 1;
        }
    }

    double hi, lo;
    TwoSum(double(f[3]) * 0x1p-32, double(f[2]) * 0x1p-64, hi, lo);
    lo += double(f[1]) * 0x1p-96 + double(f[0]) * 0x1p-128;
    double sum = hi + lo;
    lo = lo - (sum - hi);
    hi = sum;

    double p, e;
    TwoProduct(hi, kPio2Hi, p, e);
    e += hi * kPio2Lo + lo * kPio2Hi;
    y[0] = p + e;
    y[1] = (p - y[0]) + e;
    if (negative) {
        y[0] = -y[0];
        y[1] = -y[1];
    }
    return quadrant;
}

// The quadrant n (mod 4 is what matters) and the remainder, for |x| > pi/4, finite
int RemPio2(double x, double y[2]) {
    uint32_t hx = High(x);
    uint32_t ix = hx & 0x7fffffff;
    if (ix < 0x413921fb) {   // |x| below about 2^20 * pi/2: Cody and Waite's, in up to three steps
        double fn = (x * kInvPio2 + kRoundToInteger) - kRoundToInteger;
        int n = int(fn);
        double r = x - fn * kPio2_1;
        double w = fn * kPio2_1t;
        int j = int(ix >> 20);
        y[0] = r - w;
        int i = j - int((High(y[0]) >> 20) & 0x7ff);
        if (i > 16) {
            double t = r;
            w = fn * kPio2_2;
            r = t - w;
            w = fn * kPio2_2t - ((t - r) - w);
            y[0] = r - w;
            i = j - int((High(y[0]) >> 20) & 0x7ff);
            if (i > 49) {
                t = r;
                w = fn * kPio2_3;
                r = t - w;
                w = fn * kPio2_3t - ((t - r) - w);
                y[0] = r - w;
            }
        }
        y[1] = (r - y[0]) - w;
        return n;
    }
    double reduced[2];
    int n = ReduceLarge(Abs(x), reduced);
    if (int32_t(hx) < 0) {
        y[0] = -reduced[0];
        y[1] = -reduced[1];
        return -n;
    }
    y[0] = reduced[0];
    y[1] = reduced[1];
    return n;
}

// ---- arc tangent

const double kAtanHi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01,
                          1.57079632679489655800e+00};
const double kAtanLo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17,
                          6.12323399573676603587e-17};
const double kAT[] = {
    3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01, -1.11111104054623557880e-01,
    9.09088713343650656196e-02, -7.69187620504482999495e-02, 6.66107313738753120669e-02, -5.83357013379057348645e-02,
    4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02,
};

// ---- arc sine and cosine

const double kPi = 3.14159265358979311600e+00;
const double kPS0 = 1.66666666666666657415e-01;
const double kPS1 = -3.25565818622400915405e-01;
const double kPS2 = 2.01212532134862925881e-01;
const double kPS3 = -4.00555345006794114027e-02;
const double kPS4 = 7.91534994289814532176e-04;
const double kPS5 = 3.47933107596021167570e-05;
const double kQS1 = -2.40339491173441421878e+00;
const double kQS2 = 2.02094576023350569471e+00;
const double kQS3 = -6.88283971605453293030e-01;
const double kQS4 = 7.70381505559019352791e-02;

// asin(sqrt(t))'s rational part, p / q
double AsinRatio(double t) {
    double p = t * (kPS0 + t * (kPS1 + t * (kPS2 + t * (kPS3 + t * (kPS4 + t * kPS5)))));
    double q = 1.0 + t * (kQS1 + t * (kQS2 + t * (kQS3 + t * kQS4)));
    return p / q;
}

// ---- exponential

const double kOverflow = 7.09782712893383973096e+02;
const double kUnderflow = -7.45133219101941108420e+02;
const double kLn2Hi = 6.93147180369123816490e-01;
const double kLn2Lo = 1.90821492927058770002e-10;
const double kInvLn2 = 1.44269504088896338700e+00;
const double kP1 = 1.66666666666666019037e-01;
const double kP2 = -2.77777777770155933842e-03;
const double kP3 = 6.61375632143793436117e-05;
const double kP4 = -1.65339022054652515390e-06;
const double kP5 = 4.13813679705723846039e-08;

} // namespace

double Sin(double x) {
    uint32_t ix = High(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {   // |x| <= pi/4
        if (ix < 0x3e500000)   // |x| < 2^-26
            return x;
        return KernelSin(x, 0.0, false);
    }
    if (ix >= 0x7ff00000)
        return x != x ? Quiet(x) : kInvalid;
    double y[2];
    switch (RemPio2(x, y) & 3) {
    case 0:
        return KernelSin(y[0], y[1], true);
    case 1:
        return KernelCos(y[0], y[1]);
    case 2:
        return -KernelSin(y[0], y[1], true);
    default:
        return -KernelCos(y[0], y[1]);
    }
}

double Cos(double x) {
    uint32_t ix = High(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {
        if (ix < 0x3e46a09e)   // |x| < 2^-27 * sqrt(2)
            return 1.0;
        return KernelCos(x, 0.0);
    }
    if (ix >= 0x7ff00000)
        return x != x ? Quiet(x) : kInvalid;
    double y[2];
    switch (RemPio2(x, y) & 3) {
    case 0:
        return KernelCos(y[0], y[1]);
    case 1:
        return -KernelSin(y[0], y[1], true);
    case 2:
        return -KernelCos(y[0], y[1]);
    default:
        return KernelSin(y[0], y[1], true);
    }
}

double Tan(double x) {
    uint32_t ix = High(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) {
        if (ix < 0x3e400000)   // |x| < 2^-27
            return x;
        return KernelTan(x, 0.0, 1);
    }
    if (ix >= 0x7ff00000)
        return x != x ? Quiet(x) : kInvalid;
    double y[2];
    int n = RemPio2(x, y);
    return KernelTan(y[0], y[1], 1 - ((n & 1) << 1));
}

double Atan(double x) {
    uint32_t hx = High(x);
    uint32_t ix = hx & 0x7fffffff;
    if (ix >= 0x44100000) {   // |x| >= 2^66
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && Low(x) != 0))
            return Quiet(x);
        return int32_t(hx) > 0 ? kAtanHi[3] + kAtanLo[3] : -kAtanHi[3] - kAtanLo[3];
    }
    int id;
    if (ix < 0x3fdc0000) {   // |x| < 0.4375
        if (ix < 0x3e400000)   // |x| < 2^-27
            return x;
        id = -1;
    } else {
        x = Abs(x);
        if (ix < 0x3ff30000) {   // |x| < 1.1875
            if (ix < 0x3fe60000) {   // 7/16 <= |x| < 11/16
                id = 0;
                x = (2.0 * x - 1.0) / (2.0 + x);
            } else {   // 11/16 <= |x| < 19/16
                id = 1;
                x = (x - 1.0) / (x + 1.0);
            }
        } else if (ix < 0x40038000) {   // |x| < 2.4375
            id = 2;
            x = (x - 1.5) / (1.0 + 1.5 * x);
        } else {
            id = 3;
            x = -1.0 / x;
        }
    }
    double z = x * x;
    double w = z * z;
    double s1 = z * (kAT[0] + w * (kAT[2] + w * (kAT[4] + w * (kAT[6] + w * (kAT[8] + w * kAT[10])))));
    double s2 = w * (kAT[1] + w * (kAT[3] + w * (kAT[5] + w * (kAT[7] + w * kAT[9]))));
    if (id < 0)
        return x - x * (s1 + s2);
    z = kAtanHi[id] - ((x * (s1 + s2) - kAtanLo[id]) - x);
    return int32_t(hx) < 0 ? -z : z;
}

double Atan2(double y, double x) {
    if (x != x || y != y)
        return PickNaN(y, x);
    uint32_t hx = High(x), lx = Low(x);
    uint32_t hy = High(y), ly = Low(y);
    uint32_t ix = hx & 0x7fffffff;
    uint32_t iy = hy & 0x7fffffff;
    if (hx == 0x3ff00000 && lx == 0)   // x is 1
        return Atan(y);
    int m = int((hy >> 31) & 1) | int((hx >> 30) & 2);   // 2 * sign(x) + sign(y)

    if ((iy | ly) == 0) {   // y is 0
        switch (m) {
        case 0:
        case 1:
            return y;
        case 2:
            return kPi;
        default:
            return -kPi;
        }
    }
    if ((ix | lx) == 0)   // x is 0
        return int32_t(hy) < 0 ? -kPio2Hi : kPio2Hi;
    if (ix == 0x7ff00000) {   // x is infinite
        if (iy == 0x7ff00000) {
            switch (m) {
            case 0:
                return kPio4;
            case 1:
                return -kPio4;
            case 2:
                return 3.0 * kPio4;
            default:
                return -3.0 * kPio4;
            }
        }
        switch (m) {
        case 0:
            return 0.0;
        case 1:
            return -0.0;
        case 2:
            return kPi;
        default:
            return -kPi;
        }
    }
    if (iy == 0x7ff00000)
        return int32_t(hy) < 0 ? -kPio2Hi : kPio2Hi;

    int k = (int(iy) - int(ix)) >> 20;
    double z;
    if (k > 60) {   // |y / x| > 2^60
        z = kPio2Hi + 0.5 * kPio2Lo;
        m &= 1;
    } else if (int32_t(hx) < 0 && k < -60) {   // |y| / x < -2^-60
        z = 0.0;
    } else {
        z = Atan(Abs(y / x));
    }
    const double kPiLo = 1.2246467991473531772e-16;
    switch (m) {
    case 0:
        return z;
    case 1:
        return -z;
    case 2:
        return kPi - (z - kPiLo);
    default:
        return (z - kPiLo) - kPi;
    }
}

double Asin(double x) {
    uint32_t hx = High(x);
    uint32_t ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) {   // |x| >= 1
        if (((ix - 0x3ff00000) | Low(x)) == 0)
            return x * kPio2Hi + x * kPio2Lo;
        return x != x ? Quiet(x) : kInvalid;
    }
    if (ix < 0x3fe00000) {   // |x| < 0.5
        if (ix < 0x3e500000)   // |x| < 2^-26
            return x;
        return x + x * AsinRatio(x * x);
    }
    double w = 1.0 - Abs(x);
    double t = w * 0.5;
    double r = AsinRatio(t);
    double s = sqrt(t);
    if (ix >= 0x3fef3333) {   // |x| > 0.975
        t = kPio2Hi - (2.0 * (s + s * r) - kPio2Lo);
    } else {
        w = WithLow(s, 0);
        double c = (t - w * w) / (s + w);
        double p = 2.0 * s * r - (kPio2Lo - 2.0 * c);
        double q = kPio4 - 2.0 * w;
        t = kPio4 - (p - q);
    }
    return int32_t(hx) > 0 ? t : -t;
}

double Acos(double x) {
    uint32_t hx = High(x);
    uint32_t ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) {   // |x| >= 1
        if (((ix - 0x3ff00000) | Low(x)) == 0)
            return int32_t(hx) > 0 ? 0.0 : kPi + 2.0 * kPio2Lo;
        return x != x ? Quiet(x) : kInvalid;
    }
    if (ix < 0x3fe00000) {   // |x| < 0.5
        if (ix <= 0x3c600000)   // |x| <= 2^-57
            return kPio2Hi + kPio2Lo;
        return kPio2Hi - (x - (kPio2Lo - x * AsinRatio(x * x)));
    }
    if (int32_t(hx) < 0) {   // x <= -0.5
        double z = (1.0 + x) * 0.5;
        double s = sqrt(z);
        double w = AsinRatio(z) * s - kPio2Lo;
        return kPi - 2.0 * (s + w);
    }
    double z = (1.0 - x) * 0.5;   // x >= 0.5
    double s = sqrt(z);
    double df = WithLow(s, 0);
    double c = (z - df * df) / (s + df);
    double w = AsinRatio(z) * s + c;
    return 2.0 * (df + w);
}

double Exp(double x) {
    uint32_t hx = High(x);
    int sign = int(hx >> 31);
    hx &= 0x7fffffff;
    if (hx >= 0x40862e42) {   // |x| >= 709.78
        if (hx >= 0x7ff00000) {
            if (((hx & 0xfffff) | Low(x)) != 0)
                return Quiet(x);
            return sign == 0 ? x : 0.0;
        }
        if (x > kOverflow)
            return std::bit_cast<double>(0x7ff0000000000000ull);
        if (x < kUnderflow)
            return 0.0;
    }

    double hi = 0.0, lo = 0.0;
    int k = 0;
    if (hx > 0x3fd62e42) {   // |x| > ln2 / 2
        if (hx < 0x3ff0a2b2) {   // |x| < 3 ln2 / 2
            hi = sign == 0 ? x - kLn2Hi : x + kLn2Hi;
            lo = sign == 0 ? kLn2Lo : -kLn2Lo;
            k = 1 - sign - sign;
        } else {
            k = int(kInvLn2 * x + (sign == 0 ? 0.5 : -0.5));
            double t = k;
            hi = x - t * kLn2Hi;
            lo = t * kLn2Lo;
        }
        x = hi - lo;
    } else if (hx < 0x3e300000) {   // |x| < 2^-28
        return 1.0 + x;
    }

    double t = x * x;
    double c = x - t * (kP1 + t * (kP2 + t * (kP3 + t * (kP4 + t * kP5))));
    if (k == 0)
        return 1.0 - ((x * c) / (c - 2.0) - x);
    double y = 1.0 - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k >= -1021) {
        if (k == 1024)
            return y * 2.0 * 0x1p1023;
        return y * FromWords(uint32_t(0x3ff + k) << 20, 0);
    }
    return y * FromWords(uint32_t(0x3ff + k + 1000) << 20, 0) * 0x1p-1000;
}

namespace {

// ---- logarithm

const double kLg1 = 6.666666666666735130e-01;
const double kLg2 = 3.999999999940941908e-01;
const double kLg3 = 2.857142874366239149e-01;
const double kLg4 = 2.222219843214978396e-01;
const double kLg5 = 1.818357216161805012e-01;
const double kLg6 = 1.531383769920937332e-01;
const double kLg7 = 1.479819860511658591e-01;
const double kTwo54 = 1.80143985094819840000e+16;

double WithHigh(double x, uint32_t high) {
    return std::bit_cast<double>((std::bit_cast<uint64_t>(x) & 0x00000000ffffffffull) | uint64_t(high) << 32);
}

// ---- power

const double kBp[] = {1.0, 1.5};
const double kDpH[] = {0.0, 5.84962487220764160156e-01};
const double kDpL[] = {0.0, 1.35003920212974897128e-08};
const double kTwo53 = 9007199254740992.0;
const double kL1 = 5.99999999999994648725e-01;
const double kL2 = 4.28571428578550184252e-01;
const double kL3 = 3.33333329818377432918e-01;
const double kL4 = 2.72728123808534006489e-01;
const double kL5 = 2.30660745775561754067e-01;
const double kL6 = 2.06975017800338417784e-01;
const double kLg2Full = 6.93147180559945286227e-01;
const double kLg2H = 6.93147182464599609375e-01;
const double kLg2L = -1.90465429995776804525e-09;
const double kOvt = 8.0085662595372944372e-17;   // -(1024 - log2(the largest double + half an ulp))
const double kCp = 9.61796693925975554329e-01;    // 2 / (3 ln2)
const double kCpH = 9.61796700954437255859e-01;
const double kCpL = -7.02846165095275826516e-09;
const double kIvLn2 = 1.44269504088896338700e+00;
const double kIvLn2H = 1.44269502162933349609e+00;
const double kIvLn2L = 1.92596299112661746887e-08;

const double kInfinity = std::bit_cast<double>(0x7ff0000000000000ull);

// z * 2^n for z near 1, rounded once where the result is subnormal
double ScaleByPowerOfTwo(double z, int n) {
    if (n > 1023)
        return z * 0x1p1023 * FromWords(uint32_t(0x3ff + n - 1023) << 20, 0);
    if (n >= -1022)
        return z * FromWords(uint32_t(0x3ff + n) << 20, 0);
    return z * FromWords(uint32_t(0x3ff + n + 54) << 20, 0) * 0x1p-54;
}

} // namespace

double Log(double x) {
    int32_t hx = int32_t(High(x));
    uint32_t lx = Low(x);
    int k = 0;
    if (hx < 0x00100000) {   // x < 2^-1022
        if (((hx & 0x7fffffff) | lx) == 0)
            return -kInfinity;
        if (hx < 0)
            return x != x ? Quiet(x) : kInvalid;
        k -= 54;   // subnormal: scaled up
        x *= kTwo54;
        hx = int32_t(High(x));
    }
    if (hx >= 0x7ff00000)
        return x != x ? Quiet(x) : x;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    int32_t i = (hx + 0x95f64) & 0x100000;
    x = WithHigh(x, uint32_t(hx | (i ^ 0x3ff00000)));   // x or x/2 in [sqrt(2)/2, sqrt(2)]
    k += i >> 20;
    double f = x - 1.0;
    double dk = k;
    if ((0x000fffff & (2 + hx)) < 3) {   // -2^-20 <= f < 2^-20
        if (f == 0.0)
            return k == 0 ? 0.0 : dk * kLn2Hi + dk * kLn2Lo;
        double r = f * f * (0.5 - 0.33333333333333333 * f);
        if (k == 0)
            return f - r;
        return dk * kLn2Hi - ((r - dk * kLn2Lo) - f);
    }
    double s = f / (2.0 + f);
    double z = s * s;
    i = hx - 0x6147a;
    double w = z * z;
    int32_t j = 0x6b851 - hx;
    double t1 = w * (kLg2 + w * (kLg4 + w * kLg6));
    double t2 = z * (kLg1 + w * (kLg3 + w * (kLg5 + w * kLg7)));
    i |= j;
    double r = t2 + t1;
    if (i > 0) {
        double hfsq = 0.5 * f * f;
        if (k == 0)
            return f - (hfsq - s * (hfsq + r));
        return dk * kLn2Hi - ((hfsq - (s * (hfsq + r) + dk * kLn2Lo)) - f);
    }
    if (k == 0)
        return f - s * (f - r);
    return dk * kLn2Hi - ((s * (f - r) - dk * kLn2Lo) - f);
}

double Pow(double x, double y) {
    int32_t hx = int32_t(High(x)), hy = int32_t(High(y));
    uint32_t lx = Low(x), ly = Low(y);
    int32_t ix = hx & 0x7fffffff, iy = hy & 0x7fffffff;

    if ((iy | ly) == 0)   // x^0 is 1
        return 1.0;
    if (hx == 0x3ff00000 && lx == 0)   // 1^y is 1, a NaN y too
        return 1.0;
    if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lx != 0) || iy > 0x7ff00000 || (iy == 0x7ff00000 && ly != 0))
        return PickNaN(x, y);

    // For negative x: 0 if y is not an integer, 1 if it is odd, 2 if even
    int yIsInteger = 0;
    if (hx < 0) {
        if (iy >= 0x43400000) {   // |y| >= 2^53
            yIsInteger = 2;
        } else if (iy >= 0x3ff00000) {
            int k = (iy >> 20) - 0x3ff;
            if (k > 20) {
                uint32_t j = ly >> (52 - k);
                if ((j << (52 - k)) == ly)
                    yIsInteger = 2 - int(j & 1);
            } else if (ly == 0) {
                int32_t j = iy >> (20 - k);
                if ((j << (20 - k)) == iy)
                    yIsInteger = 2 - int(j & 1);
            }
        }
    }

    if (ly == 0) {
        if (iy == 0x7ff00000) {   // y is infinite
            if (((ix - 0x3ff00000) | int32_t(lx)) == 0)
                return 1.0;   // (-1)^infinity
            if (ix >= 0x3ff00000)
                return hy >= 0 ? y : 0.0;
            return hy < 0 ? -y : 0.0;
        }
        if (iy == 0x3ff00000)   // y is 1 or -1
            return hy < 0 ? 1.0 / x : x;
        if (hy == 0x40000000)   // y is 2
            return x * x;
        if (hy == 0x3fe00000 && hx >= 0)   // y is 0.5
            return sqrt(x);
    }

    double ax = Abs(x);
    if (lx == 0 && (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000)) {   // x is 0, 1 or infinite, either sign
        double z = ax;
        if (hy < 0)
            z = 1.0 / z;
        if (hx < 0) {
            if (((ix - 0x3ff00000) | yIsInteger) == 0)
                z = kInvalid;   // (-1)^(not an integer)
            else if (yIsInteger == 1)
                z = -z;
        }
        return z;
    }

    int32_t n = int32_t(uint32_t(hx) >> 31) - 1;
    if ((n | yIsInteger) == 0)   // negative x to a power that is not an integer
        return kInvalid;
    double sign = (n | (yIsInteger - 1)) == 0 ? -1.0 : 1.0;   // negative x to an odd power

    double t1, t2;
    if (iy > 0x41e00000) {   // |y| > 2^31
        if (iy > 0x43f00000) {   // |y| > 2^64: overflows or underflows
            if (ix <= 0x3fefffff)
                return hy < 0 ? kInfinity : 0.0;
            if (ix >= 0x3ff00000)
                return hy > 0 ? kInfinity : 0.0;
        }
        if (ix < 0x3fefffff)
            return hy < 0 ? sign * kInfinity : sign * 0.0;
        if (ix > 0x3ff00000)
            return hy > 0 ? sign * kInfinity : sign * 0.0;
        // |1 - x| <= 2^-20: log(x) from its series
        double t = ax - 1.0;
        double w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
        double u = kIvLn2H * t;
        double v = t * kIvLn2L - w * kIvLn2;
        t1 = WithLow(u + v, 0);
        t2 = v - (t1 - u);
    } else {
        n = 0;
        if (ix < 0x00100000) {   // subnormal x
            ax *= kTwo53;
            n -= 53;
            ix = int32_t(High(ax));
        }
        n += (ix >> 20) - 0x3ff;
        int32_t j = ix & 0x000fffff;
        ix = j | 0x3ff00000;
        int k;
        if (j <= 0x3988e) {   // |x| < sqrt(3/2)
            k = 0;
        } else if (j < 0xbb67a) {   // |x| < sqrt(3)
            k = 1;
        } else {
            k = 0;
            n += 1;
            ix -= 0x00100000;
        }
        ax = WithHigh(ax, uint32_t(ix));

        // ss = sH + sL = (x - 1) / (x + 1) or (x - 1.5) / (x + 1.5)
        double u = ax - kBp[k];
        double v = 1.0 / (ax + kBp[k]);
        double ss = u * v;
        double sH = WithLow(ss, 0);
        double tH = FromWords(uint32_t(((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18)), 0);   // ax + bp[k], high
        double tL = ax - (tH - kBp[k]);
        double sL = v * ((u - sH * tH) - sH * tL);
        // log(ax)
        double s2 = ss * ss;
        double r = s2 * s2 * (kL1 + s2 * (kL2 + s2 * (kL3 + s2 * (kL4 + s2 * (kL5 + s2 * kL6)))));
        r += sL * (sH + ss);
        s2 = sH * sH;
        tH = WithLow(3.0 + s2 + r, 0);
        tL = r - ((tH - 3.0) - s2);
        u = sH * tH;
        v = sL * tH + tL * ss;
        // 2 / (3 log2) * (ss + ...)
        double pH = WithLow(u + v, 0);
        double pL = v - (pH - u);
        double zH = kCpH * pH;
        double zL = kCpL * pH + pL * kCp + kDpL[k];
        // log2(ax) = n + dp_h + z_h + z_l
        double t = n;
        t1 = WithLow(((zH + zL) + kDpH[k]) + t, 0);
        t2 = zL - (((t1 - t) - kDpH[k]) - zH);
    }

    // (y1 + y2) * (t1 + t2)
    double y1 = WithLow(y, 0);
    double pL = (y - y1) * t1 + y * t2;
    double pH = y1 * t1;
    double z = pL + pH;
    int32_t j = int32_t(High(z));
    int32_t i = int32_t(Low(z));
    if (j >= 0x40900000) {   // z >= 1024
        if (((j - 0x40900000) | i) != 0 || pL + kOvt > z - pH)
            return sign * kInfinity;
    } else if ((j & 0x7fffffff) >= 0x4090cc00) {   // z <= -1075
        if (((j - int32_t(0xc090cc00)) | i) != 0 || pL <= z - pH)
            return sign * 0.0;
    }

    // 2^(pH + pL)
    i = j & 0x7fffffff;
    int k = (i >> 20) - 0x3ff;
    n = 0;
    if (i > 0x3fe00000) {   // |z| > 0.5: n = [z + 0.5]
        n = j + (0x00100000 >> (k + 1));
        k = ((n & 0x7fffffff) >> 20) - 0x3ff;
        double t = FromWords(uint32_t(n & ~(0x000fffff >> k)), 0);
        n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
        if (j < 0)
            n = -n;
        pH -= t;
    }
    double t = WithLow(pL + pH, 0);
    double u = t * kLg2H;
    double v = (pL - (t - pH)) * kLg2Full + t * kLg2L;
    z = u + v;
    double w = v - (z - u);
    t = z * z;
    t1 = z - t * (kP1 + t * (kP2 + t * (kP3 + t * (kP4 + t * kP5))));
    double r = (z * t1) / (t1 - 2.0) - (w + z * w);
    z = 1.0 - (r - z);
    j = int32_t(High(z)) + (n << 20);
    if ((j >> 20) <= 0)   // subnormal
        z = ScaleByPowerOfTwo(z, n);
    else
        z = WithHigh(z, uint32_t(j));
    return sign * z;
}

} // namespace DetMath

#endif // !NIGHTFIRE_X87
