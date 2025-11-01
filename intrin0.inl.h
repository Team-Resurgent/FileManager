// intrin0.inl.h - Local override to prevent conflicts with Xbox SDK
// This file shadows the VS2022 intrin0.inl.h to prevent it from 
// redeclaring interlocked functions that are already declared by the Xbox SDK

#pragma once

// Prevent the actual intrin0.inl.h from being included
#ifndef _VCRUNTIME_INTRIN0_INL_H
#define _VCRUNTIME_INTRIN0_INL_H

#include <math.h>

// The Xbox SDK already declares these functions with extern "C" linkage
// in winbase.h, so we don't need to redeclare them here.
// Just ensure the compiler knows they exist as intrinsics.

#if defined(_M_IX86) && !defined(_M_HYBRID_X86_ARM64)

// Tell the compiler these are intrinsic functions without redeclaring them
#if !defined(_M_CEE_PURE)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedDecrement)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedIncrement)
#endif

// Provide macro definitions for missing intrinsics to map to standard C functions
#define _ReadWriteBarrier() __asm { }
#define _mm_pause() __asm { pause }
#define __iso_volatile_load32(ptr) (*(ptr))
#define __iso_volatile_store32(ptr, value) (*(ptr) = (value))

// Math intrinsics - map to standard C math functions
#define __floorf(x) floorf(x)
#define __ceilf(x) ceilf(x)
#define __truncf(x) truncf(x)
#define __roundf(x) roundf(x)
#define __copysignf(x, y) copysignf(x, y)

#define __floor(x) floor(x)
#define __ceil(x) ceil(x)
#define __trunc(x) trunc(x)
#define __round(x) round(x)
#define __copysign(x, y) copysign(x, y)

#endif // _M_IX86

#endif // _VCRUNTIME_INTRIN0_INL_H

