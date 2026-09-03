# Vendored copy of ch32fun

This directory is a verbatim snapshot of upstream ch32fun, committed into
this repository rather than referenced as a submodule, so that a clone of
Le'Fob builds with no further fetching.

    upstream  https://github.com/cnlohr/ch32fun.git
    commit    618bba58c615ed29dc99e6ea92d869c914b6a8c0
    dated     2026-08-28 08:46:11 +0000
    vendored  2026-09-03

Nothing here is modified. The only added file is this one.

## Why pinned rather than tracked

Le'Fob leans on parts of ch32fun that are still moving: the iSLER raw-radio
layer (whose header records a function rename in December 2025), the CH59x
register definitions, and minichlink's CH5xx ISP support. An unpinned
dependency would let a routine `git pull` change radio behaviour or GPIO
register names underneath working firmware.

## Updating

    git rm -r --cached ch32fun
    rm -rf ch32fun
    git clone https://github.com/cnlohr/ch32fun.git ch32fun
    rm -rf ch32fun/.git
    # restore this file, update the commit above
    make -C firmware clean && make -C firmware && make -C tests

Re-read `extralibs/iSLER.h` after any update — Le'Fob calls into it
directly (`iSLERInit`, `iSLERTX`, `ISLER_BUF_ATTR`) and those names have
changed before.

## Build outputs

`minichlink/minichlink`, `minichlink/minichlink.so` and
`ch32fun/generated_*.ld` are produced by the build and are excluded by
upstream's own `.gitignore`, which still applies here.
