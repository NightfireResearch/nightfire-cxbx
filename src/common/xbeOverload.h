#ifndef COMMON_XBEOVERLOAD_H_
#define COMMON_XBEOVERLOAD_H_

#include <stddef.h>
#include <string.h>
#include <type_traits>

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
// WriteJmpTo wants, which for a member-function pointer means reading it as a number. Under MSVC's ABI that is
// valid only for a 4-byte member pointer, a class with single inheritance and no virtual bases, which the assert
// enforces. The cross builds (clang for i686-w64-mingw32) use the Itanium C++ ABI instead, where every
// member-function pointer is two words, {function, this-adjustment}; for a non-virtual method - and overlay
// classes have no virtual methods - the first word is the function's address and the adjustment is 0. See
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

// Whether F is a function pointer, or a member-function pointer this file knows how to read and write.
template <class F> constexpr bool XbeIsSimplePointer() {
#ifdef _MSC_VER
    return sizeof(F) == sizeof(size_t);
#else
    return sizeof(F) == sizeof(size_t) ||
           (std::is_member_function_pointer<F>::value && sizeof(F) == 2 * sizeof(size_t));
#endif
}

template <class F> inline size_t XbeAddress(F f) {
    static_assert(XbeIsSimplePointer<F>(), "not a plain function or single-inheritance member function pointer");
    size_t address;
    memcpy(&address, &f, sizeof(address));   // the first word, in both ABIs
    return address;
}

// The other direction: the original at `address`, as a pointer of type F - which may be a member-function
// pointer, so that the generated AUTOGEN body for a method can call it as (this->*original)(...), with the
// declaration's own calling convention. The type comes from the declaration (decltype of XbeOverload::Of), so
// the compiler, not a hand-written cast, decides how the call is made.
template <class F> inline F XbeOriginal(size_t address) {
    static_assert(XbeIsSimplePointer<F>(), "not a plain function or single-inheritance member function pointer - "
                                           "is the class complete where this is used?");
    size_t words[2] = { address, 0 };   // the address, and (Itanium only) a this-adjustment of 0
    F f;
    memcpy(&f, words, sizeof(f));
    return f;
}

// A virtual method of the game's, as a pointer of type F: slot `slot` of the vtable `object` points at. Overlay
// classes declare no `virtual` - their vtable is the game's, held in their first word - so a call to an override
// has to read that vtable as the compiler would have. The generated body of a // VIRTUAL(n) declaration is
// (this->*XbeVirtual<F>(this, n))(args...), with F the declaration's own member-function-pointer type.
template <class F, class T> inline F XbeVirtual(const T *object, int slot) {
    const size_t *vtable = *(const size_t *const *)object;
    return XbeOriginal<F>(vtable[slot]);
}

#endif // COMMON_XBEOVERLOAD_H_
