#include <stdint.h>
#include <sys/types.h>

void
memcpy_neon(void *dst, const void *src, size_t len)
{
    asm volatile (
    	"dng_neon_memcpy:               \n"
        "   pld [%[s], #0xc0]           \n"
        "   vldm %[s]!,{d0-d7}          \n"
        "   vstm %[d]!,{d0-d7}          \n"
        "   subs %[count],%[count], #64 \n"
        "   bgt dng_neon_memcpy         \n"
        : [d]"+r"(dst), [s]"+r"(src), [count]"+r"(len) :: "cc" );
}

void
memcpy_bgr2rgb(void *dst, const void *src, size_t len)
{
    asm volatile (
    	"memcpy_bgr2rgb_loop:           \n"
        "   vld3.8 {d0,d1,d2}, [%[s]]!  \n"
        "   vld3.8 {d3,d4,d5}, [%[s]]!  \n"
        "   vswp d0, d2                 \n"
        "   vswp d3, d5                 \n"
        "   vst3.8 {d0,d1,d2}, [%[d]]!  \n"
        "   vst3.8 {d3,d4,d5}, [%[d]]!  \n"
        "   subs %[count],%[count], #48 \n"
        "   bgt memcpy_bgr2rgb_loop     \n"
        : [d]"+r"(dst), [s]"+r"(src), [count]"+r"(len) :: "cc" );
}

void
memcpy_rgb2mono(void *dst, const void *src, size_t len)
{
    asm volatile (
    	"memcpy_rgb2mono_loop:          \n"
        "   vld3.8 {d0,d1,d2}, [%[s]]!  \n"
        "   vld3.8 {d3,d4,d5}, [%[s]]!  \n"
        "   vhadd.u8 d0, d0, d2         \n"
        "   vhadd.u8 d0, d1, d0         \n"
        "   vhadd.u8 d3, d3, d5         \n"
        "   vhadd.u8 d1, d3, d4         \n"
        "   vstm %[d]!,{d0-d1}          \n"
        "   subs %[count],%[count], #48 \n"
        "   bgt memcpy_rgb2mono_loop    \n"
        : [d]"+r"(dst), [s]"+r"(src), [count]"+r"(len) :: "cc" );
}

void
memcpy_sum16(void *dst, const void *src, size_t len)
{
    asm volatile (
    	"memcpy_sum16_loop:             \n"
        "   vldm %[d],{d0-d3}           \n"
        "   vldm %[s]!,{d4-d7}          \n"
        "   vadd.u16 d0, d4, d0         \n"
        "   vadd.u16 d1, d5, d1         \n"
        "   vadd.u16 d2, d6, d2         \n"
        "   vadd.u16 d3, d7, d3         \n"
        "   vstm %[d]!,{d0-d3}          \n"
        "   subs %[count],%[count], #32 \n"
        "   bgt memcpy_sum16_loop       \n"
        : [d]"+r"(dst), [s]"+r"(src), [count]"+r"(len) :: "cc" );
}

void
neon_div16(void *framebuf, size_t len)
{
    asm volatile (
    	"memcpy_div16_loop:             \n"
        "   vldm %[ptr],{d0-d3}         \n"
        "   vrshr.u16 d0, d0, #4        \n"
        "   vrshr.u16 d1, d1, #4        \n"
        "   vrshr.u16 d2, d2, #4        \n"
        "   vrshr.u16 d3, d3, #4        \n"
        "   vstm %[ptr]!,{d0-d3}        \n"
        "   subs %[count],%[count], #32 \n"
        "   bgt memcpy_div16_loop       \n"
        : [ptr]"+r"(framebuf), [count]"+r"(len) :: "cc" );
}

void
neon_be12_unpack(void *dst, void *src)
{
    asm volatile (
        "   vld3.8 {d0,d1,d2}, [%[s]]   \n"
        "   vshll.u8  q2, d0, #4        \n" /* q2 = first pixel 8-lsb. */
        "   vshll.u8  q3, d2, #4        \n" /* q3 = second pixel 8-msb. */
        /* low nibble of split byte to high nibble of first pixel. */
        "   vmovl.u8  q4, d1            \n"
        "   vshl.u16  q4, q4, #12       \n"
        "   vadd.u16  q2, q4, q2        \n" /* q2 = first pixel */
        /* high nibble of split byte to low nibble of second pixel. */
        "   vshr.u8   d1, d1, #4        \n"
        "   vaddw.u8  q3, q3, d1        \n" /* q3 = second pixel/16 */
        "   vshl.u16  q3, q3, #4        \n" /* q3 = second pixel */
        /* write out */
        "   vst2.16 {q2,q3}, [%[d]]     \n"
        : [d]"+r"(dst), [s]"+r"(src) : [pattern]"r"(0xf0) : "cc" );
}

void
memcpy_le12_pack(void *dst, const void *src, size_t len)
{
    asm volatile (
    	"memcpy_pack12bpp_loop:         \n"
        /* Read pairs of first/second pixel pairs */
        "   vld2.16 {q0,q1}, [%[s]]!    \n" /* q0 = first pixel, q1 = second pixel */
        "   vshrn.u16 d4, q0, #4        \n" /* d4 = low 8-lsb of first pixel */
        "   vshrn.u16 d6, q1, #8        \n" /* d6 = high 8-msb of second pixel */
        /* Combine the split byte */
        "   vshl.u16  q1, q1, #8        \n"
        "   vsri.16   q0, q1, #4        \n"
        "   vshrn.u16 d5, q0, #8        \n"
        /* Write two pixels into three bytes */
        "   vst3.8 {d4,d5,d6}, [%[d]]!  \n"
        "   subs %[count],%[count], #32 \n"
        "   bgt memcpy_pack12bpp_loop   \n"
        : [d]"+r"(dst), [s]"+r"(src), [count]"+r"(len) : [pattern]"r"(0x0f00) : "cc" );
}
