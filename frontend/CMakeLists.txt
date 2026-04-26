
#project(fronend)


 

#############################################
#               Build fronend.exe           #
#############################################

file(GLOB SRC_FRONTEND
    ${CMAKE_SOURCE_DIR}/frontend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/backend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/codec/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/native/*.c)
include_directories(${CMAKE_SOURCE_DIR}/frontend/)
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/native)
include_directories(${CMAKE_SOURCE_DIR}/libOPRF)
set(SRC_FRONTEND_NO_MAIN ${SRC_FRONTEND})
list(REMOVE_ITEM SRC_FRONTEND_NO_MAIN ${CMAKE_SOURCE_DIR}/frontend/main.cpp)

set(SRC_KECCAK
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/Keccak-f.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/transformations.cpp)

file(GLOB SRC_OBFMLKEM_MIN
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/backend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/codec/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/native/*.c)

if(NOT PQPSI_BUILD_RBOKVS_BENCH_ONLY)
    add_executable(frontend.exe ${SRC_FRONTEND} ${SRC_KECCAK})
endif()

if(NOT PQPSI_BUILD_RBOKVS_BENCH_ONLY)
    add_executable(construction_permutation_bench
        ${CMAKE_SOURCE_DIR}/frontend/benchmarks/ConstructionPermutation_Bench.cpp
        ${SRC_KECCAK})
endif()

add_executable(pqpsi_rbokvs_bench
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/PqPsi_RbOkvs_Bench.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/check.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/recv.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/send.cpp
    ${CMAKE_SOURCE_DIR}/libOPRF/Hashing/polyFFT.cpp
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK})

add_executable(rbokvs_g_check
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/RbOkvs_G_Check.cpp)

add_executable(rbokvs_pqpsi_test
    ${CMAKE_SOURCE_DIR}/frontend/tests/RbOkvs_PqPsi_Test.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/check.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/recv.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/send.cpp
    ${CMAKE_SOURCE_DIR}/libOPRF/Hashing/polyFFT.cpp
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK})

add_executable(pqpsi_tests
    ${CMAKE_SOURCE_DIR}/tests/pqpsi_tests.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/check.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/recv.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/send.cpp
    ${CMAKE_SOURCE_DIR}/libOPRF/Hashing/polyFFT.cpp
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK})


#############################################
#            Link our libraries             #
#############################################
#target_link_libraries(frontend.exe libPSI_Tests)
if(NOT PQPSI_BUILD_RBOKVS_BENCH_ONLY)
    target_link_libraries(frontend.exe libOPRF)
    target_link_libraries(frontend.exe libOTe)
    target_link_libraries(frontend.exe libPaXoS)
endif()
target_link_libraries(pqpsi_rbokvs_bench cryptoTools)
target_link_libraries(rbokvs_g_check cryptoTools)
target_link_libraries(rbokvs_pqpsi_test cryptoTools)
target_link_libraries(pqpsi_tests cryptoTools)

# Reminder: RB-OKVS benchmark needs NTL/gf2x for OKVS code and GMP/libsodium for obf-mlkem.
find_library(PQPSI_GMP_LIB NAMES gmp HINTS /usr/local/lib /opt/homebrew/lib)
find_library(PQPSI_SODIUM_LIB NAMES sodium HINTS /usr/local/lib /opt/homebrew/lib)
find_library(PQPSI_NTL_LIB NAMES ntl HINTS /usr/local/lib /opt/homebrew/lib)
find_library(PQPSI_GF2X_LIB NAMES gf2x HINTS /usr/local/lib /opt/homebrew/lib)
if(PQPSI_GMP_LIB AND PQPSI_SODIUM_LIB AND PQPSI_NTL_LIB AND PQPSI_GF2X_LIB)
    target_link_libraries(pqpsi_rbokvs_bench
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB}
        ${PQPSI_NTL_LIB}
        ${PQPSI_GF2X_LIB})
    target_link_libraries(rbokvs_g_check
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB}
        ${PQPSI_NTL_LIB}
        ${PQPSI_GF2X_LIB})
    target_link_libraries(rbokvs_pqpsi_test
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB}
        ${PQPSI_NTL_LIB}
        ${PQPSI_GF2X_LIB})
    target_link_libraries(pqpsi_tests
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB}
        ${PQPSI_NTL_LIB}
        ${PQPSI_GF2X_LIB})
else()
    message(WARNING "pqpsi_rbokvs_bench, rbokvs_g_check, rbokvs_pqpsi_test, and pqpsi_tests need libgmp, libsodium, libntl, and libgf2x available in the system library path.")
endif()
