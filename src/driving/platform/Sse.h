#ifndef DRIVING_PLATFORM_SSE_H_
#define DRIVING_PLATFORM_SSE_H_

// The x86 instructions the driving engine's ports use by their intrinsics: SSE's, RDTSC, REP MOVSD/MOVSB and EMMS.
// NIGHTFIRE_SSE (the CMake option of that name, 1 or 0) chooses their forms: 1 the instructions themselves; 0 the C
// forms of SseC.h under the intrinsics' names, with the instructions' results (docs/driving/macos.md 3.1), which a
// host without them needs and an x86 build can be made with to test them. On x86, RDTSC and the string moves stay
// the instructions either way: the system's headers declare them again (winnt.h does).
#if !defined(NIGHTFIRE_SSE)
#error "NIGHTFIRE_SSE must be defined as 1 or 0 (CMakeLists.txt)"
#endif

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)

// First even for the C forms, so that a system header including them later adds nothing the names would clash with
#include <xmmintrin.h>
#include <emmintrin.h>
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <intrin.h>
#else
#include <stddef.h>
#include <x86intrin.h>

static inline void __movsb(unsigned char *destination, const unsigned char *source, size_t count) {
    __asm__ __volatile__("rep movsb" : "+D"(destination), "+S"(source), "+c"(count) : : "memory");
}

static inline void __movsd(unsigned long *destination, const unsigned long *source, size_t count) {
    __asm__ __volatile__("rep movsl" : "+D"(destination), "+S"(source), "+c"(count) : : "memory");
}
#endif

#else

#if NIGHTFIRE_SSE
#error "NIGHTFIRE_SSE=1 needs an x86 host"
#endif
#include "SseC.h"

#define __rdtsc sse::rdtsc
#define __movsd sse::movsd
#define __movsb sse::movsb

#endif

#if !NIGHTFIRE_SSE
#include "SseC.h"

#define __m128 sse::m128
#define __m128i sse::m128i
#define _mm_add_ps sse::add_ps
#define _mm_add_ss sse::add_ss
#define _mm_cvtepi32_ps sse::cvtepi32_ps
#define _mm_cvtsi32_ss sse::cvtsi32_ss
#define _mm_cvtss_f32 sse::cvtss_f32
#define _mm_cvtss_si32 sse::cvtss_si32
#define _mm_cvtt_ss2si sse::cvtt_ss2si
#define _mm_empty sse::empty
#define _mm_load_ps sse::load_ps
#define _mm_load_si128 sse::load_si128
#define _mm_load_ss sse::load_ss
#define _mm_loadu_ps sse::loadu_ps
#define _mm_mul_ps sse::mul_ps
#define _mm_mul_ss sse::mul_ss
#define _mm_rsqrt_ss sse::rsqrt_ss
#define _mm_set1_ps sse::set1_ps
#define _mm_set_ps sse::set_ps
#define _mm_set_ss sse::set_ss
#define _mm_setr_ps sse::setr_ps
#define _mm_setzero_ps sse::setzero_ps
#define _mm_shuffle_ps sse::shuffle_ps
#define _mm_sqrt_ss sse::sqrt_ss
#define _mm_store_ps sse::store_ps
#define _mm_store_ss sse::store_ss
#define _mm_storeu_ps sse::storeu_ps
#define _mm_sub_ps sse::sub_ps
#define _mm_sub_ss sse::sub_ss
#define _mm_xor_ps sse::xor_ps
#endif

#endif // DRIVING_PLATFORM_SSE_H_
