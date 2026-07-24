#!/bin/sh
# Optional helper: convert cosmocc APE tools to native ELF when binfmt_misc
# cannot run MZ polyglots (unusual hosts). Prefer fixing WSLInterop on WSL:
#   sudo sh -c 'echo -1 > /proc/sys/fs/binfmt_misc/WSLInterop'
set -eu
cd "$(dirname "$0")/.."
APE=.cosmocc/bin/ape-x86_64.elf
ASSIM=.cosmocc/bin/assimilate
if [ ! -x "$APE" ]; then
  echo "missing cosmocc; run: make ape-toolchain" >&2
  exit 1
fi

for f in \
  .cosmocc/bin/x86_64-linux-cosmo-gcc \
  .cosmocc/bin/x86_64-linux-cosmo-cc \
  .cosmocc/bin/x86_64-linux-cosmo-as \
  .cosmocc/bin/x86_64-linux-cosmo-ld \
  .cosmocc/bin/x86_64-linux-cosmo-ld.bfd \
  .cosmocc/bin/x86_64-linux-cosmo-g++ \
  .cosmocc/bin/x86_64-linux-cosmo-c++ \
  .cosmocc/bin/apelink \
  .cosmocc/libexec/gcc/x86_64-linux-cosmo/14.1.0/cc1 \
  .cosmocc/libexec/gcc/x86_64-linux-cosmo/14.1.0/cc1plus \
  .cosmocc/libexec/gcc/x86_64-linux-cosmo/14.1.0/collect2 \
  .cosmocc/libexec/gcc/x86_64-linux-cosmo/14.1.0/lto1 \
  .cosmocc/libexec/gcc/x86_64-linux-cosmo/14.1.0/lto-wrapper
do
  if [ -f "$f" ]; then
    echo "assimilate $f"
    "$APE" "$ASSIM" -e -c "$f" || echo "FAIL $f"
  fi
done

echo done
