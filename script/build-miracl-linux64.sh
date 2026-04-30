#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <miracl-root>" >&2
    exit 1
fi

MIRACL_ROOT="$1"
MIRACL_SOURCE_DIR="${MIRACL_ROOT}/source"

cd "${MIRACL_SOURCE_DIR}"

cp ../include/*.h .
if [[ -f ../include/mirdef.h64 ]]; then
    cp ../include/mirdef.h64 mirdef.h
elif [[ -f ../include/mirdef.h ]]; then
    cp ../include/mirdef.h mirdef.h
else
    echo "failed to find a usable mirdef header under ${MIRACL_ROOT}/include" >&2
    exit 1
fi
grep -q '^#define MR_GENERIC_MT' mirdef.h || printf '\n#define MR_GENERIC_MT\n' >> mirdef.h
cp mirdef.h ../include/mirdef.h
cp ../include/miracl.h .
generated_mrmuldv=0
if [[ -f mrmuldv.g64 ]]; then
    cp mrmuldv.g64 mrmuldv.c
    generated_mrmuldv=1
fi

for src in \
    mrcore.c mrarth0.c mrarth1.c mrarth2.c mralloc.c mrsmall.c mrio1.c mrio2.c \
    mrgcd.c mrjack.c mrxgcd.c mrarth3.c mrbits.c mrrand.c mrprime.c mrcrt.c \
    mrscrt.c mrmonty.c mrpower.c mrsroot.c mrcurve.c mrfast.c mrshs.c \
    mrshs256.c mrshs512.c mrsha3.c mrfpe.c mraes.c mrgcm.c mrlucas.c \
    mrzzn2.c mrzzn2b.c mrzzn3.c mrzzn4.c mrecn2.c mrstrong.c mrbrick.c \
    mrebrick.c mrec2m.c mrgf2m.c mrflash.c mrfrnd.c mrdouble.c mrround.c \
    mrbuild.c mrflsh1.c mrpi.c mrflsh2.c mrflsh3.c mrflsh4.c mrmuldv.c; do
    g++ -x c++ -c -m64 -O2 -fpermissive -Wno-write-strings -I../include "${src}"
done

ar rcs miracl.a mrcore.o mrarth0.o mrarth1.o mrarth2.o mralloc.o mrsmall.o mrzzn2.o mrzzn3.o
ar r miracl.a mrio1.o mrio2.o mrjack.o mrgcd.o mrxgcd.o mrarth3.o mrbits.o mrecn2.o mrzzn4.o
ar r miracl.a mrrand.o mrprime.o mrcrt.o mrscrt.o mrmonty.o mrcurve.o mrsroot.o mrzzn2b.o
ar r miracl.a mrpower.o mrfast.o mrshs.o mrshs256.o mraes.o mrlucas.o mrstrong.o mrgcm.o
ar r miracl.a mrflash.o mrfrnd.o mrdouble.o mrround.o mrbuild.o
ar r miracl.a mrflsh1.o mrpi.o mrflsh2.o mrflsh3.o mrflsh4.o
ar r miracl.a mrbrick.o mrebrick.o mrec2m.o mrgf2m.o mrmuldv.o mrshs512.o mrsha3.o mrfpe.o
ranlib miracl.a

cp miracl.a ..
cp miracl.a ../libmiracl.a
cp miracl.a ./libmiracl.a
rm -f ./*.o
if [[ "${generated_mrmuldv}" -eq 1 ]]; then
    rm -f ./mrmuldv.c
fi
