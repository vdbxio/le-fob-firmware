/*
 * A strong memset, to work around an LTO interaction in the ch32fun build.
 *
 * ch32fun.c already carries a mini-libc, but its memset is declared WEAK.
 * Nothing in this firmware calls memset by name, so at link-time-optimisation
 * stage the symbol looks unreferenced and gets discarded -- and only *after*
 * that does code generation turn an array initialiser inside iSLER.h
 * (`uint8_t txctune_co[...] = {0}` in iSLERTXTune) into a memset call. The
 * result is an undefined reference to a function that was present in the
 * source all along.
 *
 * A strong definition here is kept regardless, and overrides the weak one.
 * The alternative is building with -fno-lto, which also works but costs
 * ~780 bytes and disables LTO for everything else.
 *
 * If a future change introduces the same failure for memcpy or memmove, add
 * them here the same way.
 */

#include <stddef.h>

__attribute__((used))
void *memset(void *dest, int c, size_t n)
{
	unsigned char *s = dest;

	while (n--)
		*s++ = (unsigned char)c;

	return dest;
}
