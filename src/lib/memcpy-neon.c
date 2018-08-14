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