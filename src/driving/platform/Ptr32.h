#ifndef DRIVING_PLATFORM_PTR32_H_
#define DRIVING_PLATFORM_PTR32_H_

// Pointers inside data read in place from the game's files (docs/driving/64-bit.md 7.1). The files hold 32-bit
// words where the game keeps addresses, and the loaders write 32-bit addresses into them, so the structures laid
// over that data keep the file's layout on every host. On a 32-bit host a Ptr32<T> is a T *. On a 64-bit host it is
// the 32-bit word: an offset from the base of the game window, the 4 GB of address space that holds every game
// allocation and the executable's statics (GameWindow.h). The base is 0 where the host places that memory low.
//
// PtrFrom32 and PtrTo32 decode and encode the same words where they have no type: the word streams, CARP's data
// words, the resource and push-buffer words. Encoding an address outside the window stops the game with the address.

#include <stddef.h>
#include <stdint.h>

#if UINTPTR_MAX == 0xffffffffu

template <class T> using Ptr32 = T *;

template <class T> inline T *PtrFrom32(uint32_t word) {
    return reinterpret_cast<T *>(word);
}

inline uint32_t PtrTo32(const void *pointer) {
    return reinterpret_cast<uint32_t>(pointer);
}

#else

#include <type_traits>

constexpr uintptr_t kGameWindowBase = 0;

[[noreturn]] void GameWindowOutOfRange(const void *pointer);   // GameWindow.cpp

template <class T> inline T *PtrFrom32(uint32_t word) {
    return reinterpret_cast<T *>(kGameWindowBase + word);
}

inline uint32_t PtrTo32(const void *pointer) {
    uintptr_t offset = reinterpret_cast<uintptr_t>(pointer) - kGameWindowBase;
    if (offset > 0xffffffffu)
        GameWindowOutOfRange(pointer);
    return uint32_t(offset);
}

// Behaves as a T * would: converts to and from one, -> and the increments; *, [], + and the comparisons come
// through the conversion.
template <class T> class Ptr32 {
public:
    Ptr32() = default;
    Ptr32(T *pointer) : word(PtrTo32(pointer)) {}
    template <class U, class = std::enable_if_t<std::is_convertible_v<U *, T *>>>
    Ptr32(Ptr32<U> other) : word(PtrTo32(static_cast<U *>(other))) {}

    Ptr32 &operator=(T *pointer) {
        word = PtrTo32(pointer);
        return *this;
    }
    operator T *() const { return PtrFrom32<T>(word); }
    T *operator->() const { return PtrFrom32<T>(word); }
    // A cast to a derived class's pointer, as static_cast or a C-style cast makes one from a T *
    template <class U, class = decltype(static_cast<U *>(static_cast<T *>(nullptr)))>
    explicit operator U *() const { return static_cast<U *>(PtrFrom32<T>(word)); }

    Ptr32 &operator++() { return *this += 1; }
    Ptr32 &operator--() { return *this -= 1; }
    T *operator++(int) {
        T *old = *this;
        *this += 1;
        return old;
    }
    T *operator--(int) {
        T *old = *this;
        *this -= 1;
        return old;
    }
    Ptr32 &operator+=(ptrdiff_t count) { return *this = static_cast<T *>(*this) + count; }
    Ptr32 &operator-=(ptrdiff_t count) { return *this = static_cast<T *>(*this) - count; }

private:
    uint32_t word;
};

#endif

#endif // DRIVING_PLATFORM_PTR32_H_
