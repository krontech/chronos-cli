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
