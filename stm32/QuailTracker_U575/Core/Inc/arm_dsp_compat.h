/* ARM DSP intrinsics for GCC < 11 (which lacks ACLE builtins)
 * These are needed by CMSIS-NN kernels on Cortex-M33 with DSP extension.
 * GCC 11+ provides these as __builtin_arm_* — this shim covers GCC 8.x. */

#ifndef ARM_DSP_COMPAT_H
#define ARM_DSP_COMPAT_H

#include <stdint.h>

#if defined(__GNUC__) && !defined(__ARMCC_VERSION)

#ifndef __smlabb
__attribute__((always_inline)) static inline int32_t __smlabb(int32_t a, int32_t b, int32_t acc)
{
    int32_t result;
    __asm volatile("smlabb %0, %1, %2, %3" : "=r"(result) : "r"(a), "r"(b), "r"(acc));
    return result;
}
#endif

#ifndef __smlatt
__attribute__((always_inline)) static inline int32_t __smlatt(int32_t a, int32_t b, int32_t acc)
{
    int32_t result;
    __asm volatile("smlatt %0, %1, %2, %3" : "=r"(result) : "r"(a), "r"(b), "r"(acc));
    return result;
}
#endif

#ifndef __smulbb
__attribute__((always_inline)) static inline int32_t __smulbb(int32_t a, int32_t b)
{
    int32_t result;
    __asm volatile("smulbb %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}
#endif

#ifndef __smultt
__attribute__((always_inline)) static inline int32_t __smultt(int32_t a, int32_t b)
{
    int32_t result;
    __asm volatile("smultt %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}
#endif

#ifndef __qadd
__attribute__((always_inline)) static inline int32_t __qadd(int32_t a, int32_t b)
{
    int32_t result;
    __asm volatile("qadd %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}
#endif

#ifndef __sxtb16
__attribute__((always_inline)) static inline int32_t __sxtb16(int32_t a)
{
    int32_t result;
    __asm volatile("sxtb16 %0, %1" : "=r"(result) : "r"(a));
    return result;
}
#endif

#ifndef __sxtab16
__attribute__((always_inline)) static inline int32_t __sxtab16(int32_t a, int32_t b)
{
    int32_t result;
    __asm volatile("sxtab16 %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}
#endif

#ifndef __smlad
__attribute__((always_inline)) static inline int32_t __smlad(int32_t a, int32_t b, int32_t acc)
{
    int32_t result;
    __asm volatile("smlad %0, %1, %2, %3" : "=r"(result) : "r"(a), "r"(b), "r"(acc));
    return result;
}
#endif

#ifndef __smlald
__attribute__((always_inline)) static inline int64_t __smlald(int32_t a, int32_t b, int64_t acc)
{
    union { struct { uint32_t lo; int32_t hi; } s; int64_t val; } u;
    u.val = acc;
    __asm volatile("smlald %0, %1, %2, %3" : "+r"(u.s.lo), "+r"(u.s.hi) : "r"(a), "r"(b));
    return u.val;
}
#endif

#ifndef __ror
__attribute__((always_inline)) static inline uint32_t __ror(uint32_t val, uint32_t shift)
{
    return (val >> shift) | (val << (32 - shift));
}
#endif

#ifndef __qsub8
__attribute__((always_inline)) static inline int32_t __qsub8(int32_t a, int32_t b)
{
    int32_t result;
    __asm volatile("qsub8 %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}
#endif

#ifndef __qsub16
__attribute__((always_inline)) static inline int32_t __qsub16(int32_t a, int32_t b)
{
    int32_t result;
    __asm volatile("qsub16 %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}
#endif

#ifndef __sadd16
__attribute__((always_inline)) static inline int32_t __sadd16(int32_t a, int32_t b)
{
    int32_t result;
    __asm volatile("sadd16 %0, %1, %2" : "=r"(result) : "r"(a), "r"(b));
    return result;
}
#endif

#endif /* __GNUC__ && !__ARMCC_VERSION */

#endif /* ARM_DSP_COMPAT_H */
