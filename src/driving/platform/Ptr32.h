#ifndef DRIVING_PLATFORM_PTR32_H_
#define DRIVING_PLATFORM_PTR32_H_

// Pointers inside data read in place from the game's files (docs/driving/64-bit.md 7.1). The files hold 32-bit
// words where the game keeps addresses, and the loaders write 32-bit addresses into them, so the structures laid
// over that data keep the file's layout on every host. On a 32-bit host a Ptr32<T> is a T *. On a 64-bit host it is
// the 32-bit word: an offset from the base of the game window, the 4 GB of address space that holds every game
// allocation, the game's thread stacks and the executable's statics (GameWindow.cpp). The base is
// NIGHTFIRE_WINDOW_BASE (CMakeLists.txt): 0 where the host can place that memory low, 4 GB on macOS. Null is the word
// 0 wherever the window lies; nothing is placed in its first 64 KB.
//
// PtrFrom32 and PtrTo32 decode and encode the same words where they have no type: the word streams, CARP's data
// words, the resource and push-buffer words. Encoding an address outside the window stops the game with the address.
// AnswerTo32 encodes a word that may be an address or a number.

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

inline uint32_t AnswerTo32(const void *answer) {
    return reinterpret_cast<uint32_t>(answer);
}

#else

#include <type_traits>

#ifndef NIGHTFIRE_WINDOW_BASE
#define NIGHTFIRE_WINDOW_BASE 0
#endif
constexpr uintptr_t kGameWindowBase = NIGHTFIRE_WINDOW_BASE;
constexpr uintptr_t kGameWindowEnd = kGameWindowBase + (uintptr_t(1) << 32);
static_assert(kGameWindowBase % 0x10000 == 0, "the window starts on a 64 KB boundary");

[[noreturn]] void GameWindowOutOfRange(const void *pointer);   // GameWindow.cpp

template <class T> inline T *PtrFrom32(uint32_t word) {
    if (kGameWindowBase != 0 && word == 0)
        return nullptr;
    return reinterpret_cast<T *>(kGameWindowBase + word);
}

inline uint32_t PtrTo32(const void *pointer) {
    if (kGameWindowBase != 0 && pointer == nullptr)
        return 0;
    uintptr_t offset = reinterpret_cast<uintptr_t>(pointer) - kGameWindowBase;
    if (offset > 0xffffffffu)
        GameWindowOutOfRange(pointer);
    return uint32_t(offset);
}

// A symbol resolver's answer (EAGL's loader): an address, or a number the data keeps as it is (an event's id). With
// the window above 4 GB, nothing below 4 GB is an address.
inline uint32_t AnswerTo32(const void *answer) {
    if (kGameWindowBase != 0 && reinterpret_cast<uintptr_t>(answer) <= 0xffffffffu)
        return uint32_t(reinterpret_cast<uintptr_t>(answer));
    return PtrTo32(answer);
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
