# Freestanding libc headers

Homebrew's `riscv64-elf-gcc` is a compiler only — it ships no C library, so
there is no `stdio.h` or `string.h` anywhere on the include path and the build
fails before it reaches any of our code.

That turns out to be almost harmless here, because ch32fun does not use a C
library either: it builds with `-nostdlib` and carries its own `printf`,
`memcpy`, `strlen` and friends in `ch32fun.c`. Only the *declarations* were
missing.

So these two headers declare exactly the functions ch32fun already
implements, and the build adds `-ffreestanding`, which makes GCC's own
`stdint.h` fall back to the bundled `stdint-gcc.h` instead of chaining to a
libc one that does not exist. `stddef.h`, `stdarg.h`, `limits.h` and
`stdbool.h` all come from GCC itself and need no help.

The alternative is installing the xPack `riscv-none-elf-gcc`, which bundles
newlib. If you go that route, drop `-ffreestanding -Iinclude` from
`firmware/Makefile` and delete this directory.
