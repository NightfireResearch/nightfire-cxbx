"""Ed25519 (RFC 8032, PureEdDSA) in plain Python: key generation, signing and verification.

Slow next to a C library (a few milliseconds a signature), which does not matter for one manifest per release, and
it needs nothing installed, so the release job and a person's machine run the same code. The launcher checks the
signatures with Monocypher's crypto_ed25519_check (third_party/monocypher).

Keys are the RFC's: a 32-byte private seed, a 32-byte public key; signatures are 64 bytes.
"""

import hashlib
import os

P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _sha512(data):
    return hashlib.sha512(data).digest()


# Points in extended coordinates (X, Y, Z, T), x = X/Z, y = Y/Z, x*y = T/Z
def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _multiply(s, p):
    q = (0, 1, 1, 0)   # the neutral element
    while s > 0:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _equal(p, q):
    return (p[0] * q[2] - q[0] * p[2]) % P == 0 and (p[1] * q[2] - q[1] * p[2]) % P == 0


def _recover_x(y, sign):
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_GY = 4 * pow(5, P - 2, P) % P
_GX = _recover_x(_GY, 0)
G = (_GX, _GY, 1, _GX * _GY % P)


def _compress(p):
    zinv = pow(p[2], P - 2, P)
    x, y = p[0] * zinv % P, p[1] * zinv % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(data):
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % P)


def _secret_expand(seed):
    if len(seed) != 32:
        raise ValueError("an Ed25519 private key is 32 bytes")
    h = _sha512(seed)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def generate_seed():
    return os.urandom(32)


def public_key(seed):
    a, _ = _secret_expand(seed)
    return _compress(_multiply(a, G))


def sign(seed, message):
    a, prefix = _secret_expand(seed)
    public = _compress(_multiply(a, G))
    r = int.from_bytes(_sha512(prefix + message), "little") % L
    rs = _compress(_multiply(r, G))
    h = int.from_bytes(_sha512(rs + public + message), "little") % L
    s = (r + h * a) % L
    return rs + int.to_bytes(s, 32, "little")


def verify(public, message, signature):
    if len(public) != 32 or len(signature) != 64:
        return False
    a = _decompress(public)
    if a is None:
        return False
    r = _decompress(signature[:32])
    if r is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    h = int.from_bytes(_sha512(signature[:32] + public + message), "little") % L
    return _equal(_multiply(s, G), _add(r, _multiply(h, a)))


if __name__ == "__main__":
    # RFC 8032 section 7.1, test 2
    seed = bytes.fromhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")
    expected_public = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"
    expected_signature = ("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                          "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")
    assert public_key(seed).hex() == expected_public
    assert sign(seed, b"\x72").hex() == expected_signature
    assert verify(bytes.fromhex(expected_public), b"\x72", bytes.fromhex(expected_signature))
    assert not verify(bytes.fromhex(expected_public), b"\x73", bytes.fromhex(expected_signature))
    print("ed25519: RFC 8032 test vector passes")
