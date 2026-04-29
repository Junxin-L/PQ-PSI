enable_language(C)

set(SRCS
    kem.c
    indcpa.c
    polyvec.c
    poly.c
    ntt.c
    cbd.c
    reduce.c
    verify.c
    fips202.c
    symmetric-shake.c
    randombytes.c
    KyberOT.c
)

add_library(KyberOT STATIC ${SRCS})

target_compile_definitions(KyberOT PUBLIC KYBER_K=3)

target_include_directories(KyberOT PUBLIC
                    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
                    $<INSTALL_INTERFACE:include>)
target_include_directories(KyberOT PUBLIC
                    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
                    $<INSTALL_INTERFACE:include>)
