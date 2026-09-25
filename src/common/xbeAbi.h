#ifndef COMMON_XBEABI_H_
#define COMMON_XBEABI_H_

#include <stddef.h>
#include <type_traits>

// ---------------------------------------------------------------------------------------------------------------
// What a C++ declaration implies at the machine level, for checking a replacement against the original.
//
// A replacement patched over one of the game's functions is called by the game's code, which passes arguments
// the way the original expected: which registers, and who pops the stack. tools/abi_facts.py measures that
// from the binary for every function (tools/abi_<side>.json), and the generated injection table checks each
// tagged declaration against it at compile time:
//
//     XBE_ABI_CHECK(decltype(&UFileLoader::FileLoadz), 0, false, false, "UFileLoader::FileLoadz at 0x001176d0");
//
// XbeAbi<F> gives, for any function-pointer or member-function-pointer type F:
//
//     pops  bytes the callee pops on return - 0 for __cdecl, every argument for __stdcall and __thiscall,
//           the ones not in registers for __fastcall;
//     ecx   whether ECX carries an argument - `this` for __thiscall, the first argument for __fastcall;
//     edx   whether EDX does - the second __fastcall argument.
//
// Each argument takes its size rounded up to four bytes on the stack, a reference four. __fastcall puts the
// first two arguments of four bytes or less that are integers or pointers into ECX and EDX, in order, and
// everything else on the stack. See docs/driving-injection-framework.md.
// ---------------------------------------------------------------------------------------------------------------

template <class T> constexpr int XbeStackSize() {
    if constexpr (std::is_reference_v<T>)
        return 4;
    else
        return (int)((sizeof(T) + 3) & ~(size_t)3);
}

template <class... A> constexpr int XbeStackSum() {
    return (0 + ... + XbeStackSize<A>());
}

template <class T> constexpr bool XbeFastcallRegister() {
    return std::is_reference_v<T> || ((std::is_integral_v<T> || std::is_pointer_v<T> || std::is_enum_v<T>) &&
                                      sizeof(T) <= 4);
}

// __fastcall: the first two register-eligible arguments go in ECX and EDX; the rest are popped from the stack.
template <int InRegisters, class... A> struct XbeFastcall;
template <int InRegisters> struct XbeFastcall<InRegisters> {
    static constexpr int pops = 0;
    static constexpr int registers = InRegisters;
};
template <int InRegisters, class T, class... Rest> struct XbeFastcall<InRegisters, T, Rest...> {
    static constexpr bool inRegister = InRegisters < 2 && XbeFastcallRegister<T>();
    using Next = XbeFastcall<InRegisters + (inRegister ? 1 : 0), Rest...>;
    static constexpr int pops = (inRegister ? 0 : XbeStackSize<T>()) + Next::pops;
    static constexpr int registers = Next::registers;
};

template <class F> struct XbeAbi;

template <class R, class... A> struct XbeAbi<R (__cdecl *)(A...)> {
    static constexpr int pops = 0;
    static constexpr bool ecx = false, edx = false;
};
template <class R, class... A> struct XbeAbi<R (__cdecl *)(A..., ...)> {
    static constexpr int pops = 0;
    static constexpr bool ecx = false, edx = false;
};
template <class R, class... A> struct XbeAbi<R (__stdcall *)(A...)> {
    static constexpr int pops = XbeStackSum<A...>();
    static constexpr bool ecx = false, edx = false;
};
template <class R, class... A> struct XbeAbi<R (__fastcall *)(A...)> {
    static constexpr int pops = XbeFastcall<0, A...>::pops;
    static constexpr bool ecx = XbeFastcall<0, A...>::registers >= 1;
    static constexpr bool edx = XbeFastcall<0, A...>::registers >= 2;
};

// Member functions: __thiscall unless declared otherwise (a variadic member is __cdecl, with `this` pushed).
template <class R, class C, class... A> struct XbeAbi<R (__thiscall C::*)(A...)> {
    static constexpr int pops = XbeStackSum<A...>();
    static constexpr bool ecx = true, edx = false;
};
template <class R, class C, class... A> struct XbeAbi<R (__thiscall C::*)(A...) const> : XbeAbi<R (__thiscall C::*)(A...)> {};
template <class R, class C, class... A> struct XbeAbi<R (__cdecl C::*)(A...)> {
    static constexpr int pops = 0;
    static constexpr bool ecx = false, edx = false;
};
template <class R, class C, class... A> struct XbeAbi<R (__cdecl C::*)(A..., ...)> {
    static constexpr int pops = 0;
    static constexpr bool ecx = false, edx = false;
};
template <class R, class C, class... A> struct XbeAbi<R (__stdcall C::*)(A...)> {
    static constexpr int pops = 4 + XbeStackSum<A...>();   // `this` is pushed too
    static constexpr bool ecx = false, edx = false;
};

// noexcept is part of a function's type, and changes nothing about how it is called. A class's operator delete
// is noexcept without saying so (Event's, in src/driving/EventManager.hpp).
template <class R, class... A> struct XbeAbi<R (__cdecl *)(A...) noexcept> : XbeAbi<R (__cdecl *)(A...)> {};
template <class R, class... A> struct XbeAbi<R (__stdcall *)(A...) noexcept> : XbeAbi<R (__stdcall *)(A...)> {};
template <class R, class... A> struct XbeAbi<R (__fastcall *)(A...) noexcept> : XbeAbi<R (__fastcall *)(A...)> {};
template <class R, class C, class... A> struct XbeAbi<R (__thiscall C::*)(A...) noexcept> : XbeAbi<R (__thiscall C::*)(A...)> {};

// The check the injection table makes for each patch. `pops` is -1 when the binary did not say. A register the
// original reads before writing must be one the declaration passes an argument in; the converse is not
// checked, because a method that never touches `this` (a GetEventName returning a constant) is common.
#define XBE_ABI_CHECK(F, pops_, readsEcx, readsEdx, what)                                                      \
    static_assert((pops_) < 0 || XbeAbi<F>::pops == (pops_),                                                   \
                  what ": the original pops " #pops_ " bytes on return, and this declaration's calling "      \
                  "convention does not - check __cdecl/__stdcall/__thiscall, static, and the argument types");  \
    static_assert(!(readsEcx) || XbeAbi<F>::ecx,                                                                \
                  what ": the original reads ECX on entry (`this`, or a __fastcall argument), and this "        \
                  "declaration passes nothing there - should it be a non-static member, or __fastcall?");      \
    static_assert(!(readsEdx) || XbeAbi<F>::edx,                                                                \
                  what ": the original reads EDX on entry (a __fastcall argument), and this declaration "        \
                  "passes nothing there")

#endif // COMMON_XBEABI_H_
