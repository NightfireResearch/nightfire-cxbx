#ifndef COMMON_XBEOVERLOAD_H_
#define COMMON_XBEOVERLOAD_H_

#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// Picking one overload by its type, and taking any function's address as a number.
//
// The generated injection table (tools/preprocess.py) writes "&Name" for a replacement - which stops compiling,
// or picks the wrong one, the moment our code defines two functions of that name. The game has overloads
// (UFileLoader::FileLoad takes three arguments at 0x00117610 and two at 0x001176b0), so replacing both means
// two definitions of one name, and the table selects each by the signature written on its tagged line:
//
//     WriteJmpTo(0x001176b0, XbeAddress(XbeOverload<void *(char *, int)>::Of(&UFileLoader::FileLoad)));
//
// Of accepts a free function or static member of that signature in any of the three conventions, or a member
// function of any class; overload resolution does the selecting. XbeAddress turns the result into the address
// WriteJmpTo wants, which for a member-function pointer means reading it as a number - valid only for a 4-byte
// member pointer, a class with single inheritance and no virtual bases, which the assert enforces. See
// docs/driving-injection-framework.md.
// ---------------------------------------------------------------------------------------------------------------

template <class Signature> struct XbeOverload;

template <class R, class... A> struct XbeOverload<R(A...)> {
    typedef R (__cdecl *Cdecl)(A...);
    typedef R (__stdcall *Stdcall)(A...);
    typedef R (__fastcall *Fastcall)(A...);

    static Cdecl Of(Cdecl f) { return f; }
    static Stdcall Of(Stdcall f) { return f; }
    static Fastcall Of(Fastcall f) { return f; }
    template <class C> static auto Of(R (C::*f)(A...)) -> R (C::*)(A...) { return f; }
};

template <class F> inline size_t XbeAddress(F f) {
    static_assert(sizeof(F) == sizeof(size_t), "not a plain function or single-inheritance member function pointer");
    size_t address;
    memcpy(&address, &f, sizeof(address));
    return address;
}

#endif // COMMON_XBEOVERLOAD_H_
