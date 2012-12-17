/* Oren - a streaming media software development kit.
 *
 * Copyright © 2012 TinySoft, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __OREN_ENDIAN_H__
#define __OREN_ENDIAN_H__

#include <glib.h>

G_BEGIN_DECLS

/* These stuff was lifted from SDL-1.2.14. */
#define OREN_LIL_ENDIAN  1234
#define OREN_BIG_ENDIAN  4321

#if defined(__i386__) || defined(__ia64__) || defined(_M_IX86) || \
    defined(_M_IA64) || (defined(__alpha__) || defined(__alpha)) || \
    defined(__arm__) || defined(ARM) || \
    (defined(__mips__) && defined(__MIPSEL__)) || defined(__SYMBIAN32__) \
    || defined(__x86_64__) || defined(__LITTLE_ENDIAN__)
# define OREN_BYTEORDER OREN_LIL_ENDIAN
#else
# define OREN_BYTEORDER OREN_BIG_ENDIAN
#endif

#ifdef __cplusplus
#define oren_reinterpret_cast(type, expression) reinterpret_cast<type>(expres
#define oren_static_cast(type, expression) static_cast<type>(expression)
#else
#define oren_reinterpret_cast(type, expression) ((type)(expression))
#define oren_static_cast(type, expression) ((type)(expression))
#endif

#if defined(__GNUC__) && defined(__i386__) && \
    !(__GNUC__ == 2 && __GNUC_MINOR__ <= 95 /* broken gcc version */)
G_INLINE_FUNC guint16 oren_swap16 (guint16 x)
{
    __asm__("xchgb %b0,%h0" : "=q" (x) :  "0" (x));
    return x;
}
#elif defined(__GNUC__) && defined(__x86_64__)
G_INLINE_FUNC guint16 oren_swap16 (guint16 x)
{
    __asm__("xchgb %b0,%h0" : "=Q" (x) :  "0" (x));
    return x;
}
#elif defined(__GNUC__) && (defined(__powerpc__) || defined(__ppc__))
G_INLINE_FUNC guint16 oren_swap16 (guint16 x)
{
    Uint16 result;

    __asm__("rlwimi %0,%2,8,16,23" : "=&r" (result) : "0" (x >> 8), "r" (x));
    return result;
}
#elif defined(__GNUC__) && (defined(__M68000__) || defined(__M68020__))
G_INLINE_FUNC guint16 oren_swap16 (guint16 x)
{
    __asm__("rorw #8,%0" : "=d" (x) :  "0" (x) : "cc");
    return x;
}
#else
G_INLINE_FUNC guint16 oren_swap16 (guint16 x) {
    return ((x<<8)|(x>>8));
}
#endif

#if defined(__GNUC__) && defined(__i386__) && \
    !(__GNUC__ == 2 && __GNUC_MINOR__ <= 95 /* broken gcc version */)
G_INLINE_FUNC guint32 oren_swap32 (guint32 x)
{
    __asm__("bswap %0" : "=r" (x) : "0" (x));
    return x;
}
#elif defined(__GNUC__) && defined(__x86_64__)
G_INLINE_FUNC guint32 oren_swap32 (guint32 x)
{
    __asm__("bswapl %0" : "=r" (x) : "0" (x));
    return x;
}
#elif defined(__GNUC__) && (defined(__powerpc__) || defined(__ppc__))
G_INLINE_FUNC guint32 oren_swap32 (guint32 x)
{
    guint32 result;

    __asm__("rlwimi %0,%2,24,16,23" : "=&r" (result) : "0" (x>>24), "r" (x));
    __asm__("rlwimi %0,%2,8,8,15"   : "=&r" (result) : "0" (result),    "r" (x));
    __asm__("rlwimi %0,%2,24,0,7"   : "=&r" (result) : "0" (result),    "r" (x));
    return result;
}
#elif defined(__GNUC__) && (defined(__M68000__) || defined(__M68020__))
G_INLINE_FUNC guint32 oren_swap32 (guint32 x)
{
    __asm__("rorw #8,%0\n\tswap %0\n\trorw #8,%0" : "=d" (x) :  "0" (x) : "cc");
    return x;
}
#else
G_INLINE_FUNC guint32 oren_swap32 (guint32 x) {
    return ((x<<24)|((x<<8)&0x00FF0000)|((x>>8)&0x0000FF00)|(x>>24));
}
#endif

#if defined(__GNUC__) && defined(__i386__) && \
    !(__GNUC__ == 2 && __GNUC_MINOR__ <= 95 /* broken gcc version */)
G_INLINE_FUNC guint64 oren_swap64 (guint64 x)
{
    union {
        struct { guint32 a,b; } s;
        guint64 u;
    } v;
    v.u = x;
    __asm__("bswapl %0 ; bswapl %1 ; xchgl %0,%1"
            : "=r" (v.s.a), "=r" (v.s.b)
            : "0" (v.s.a), "1" (v.s.b));
    return v.u;
}
#elif defined(__GNUC__) && defined(__x86_64__)
G_INLINE_FUNC guint64 oren_swap64 (guint64 x)
{
    __asm__("bswapq %0" : "=r" (x) : "0" (x));
    return x;
}
#else
G_INLINE_FUNC guint64 oren_swap64 (guint64 x)
{
    guint32 hi, lo;

    /* Separate into high and low 32-bit values and swap them */
    lo = oren_static_cast (guint32, x & 0xFFFFFFFF);
    x >>= 32;
    hi = oren_static_cast (guint32, x & 0xFFFFFFFF);
    x = oren_swap32 (lo);
    x <<= 32;
    x |= oren_swap32 (hi);
    return(x);
}
#endif

#if OREN_BYTEORDER == OREN_LIL_ENDIAN
#define oren_swap_le16(X) (X)
#define oren_swap_le32(X) (X)
#define oren_swap_le64(X) (X)
#define oren_swap_be16(X) oren_swap16(X)
#define oren_swap_be32(X) oren_swap32(X)
#define oren_swap_be64(X) oren_swap64(X)
#else
#define oren_swap_le16(X) oren_swap16(X)
#define oren_swap_le32(X) oren_swap32(X)
#define oren_swap_le64(X) oren_swap64(X)
#define oren_swap_be16(X) (X)
#define oren_swap_be32(X) (X)
#define oren_swap_be64(X) (X)
#endif

G_END_DECLS

#endif /* __OREN_ENDIAN_H__ */
