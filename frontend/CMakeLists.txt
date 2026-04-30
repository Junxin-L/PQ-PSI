
#############################################
#               Build frontend targets      #
#############################################

file(GLOB SRC_FRONTEND
    ${CMAKE_SOURCE_DIR}/frontend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/kem/eckem/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/kem/obf-mlkem/backend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/kem/obf-mlkem/codec/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/kem/obf-mlkem/native/*.c)
include_directories(${CMAKE_SOURCE_DIR}/frontend/)
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/frontend/kem/obf-mlkem/native)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/monocypher/src)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/XKCP/lib/common)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/XKCP/lib/low/Xoodoo)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/XKCP/lib/low/Xoodoo/plain)
set(SRC_FRONTEND_NO_MAIN ${SRC_FRONTEND})
list(REMOVE_ITEM SRC_FRONTEND_NO_MAIN ${CMAKE_SOURCE_DIR}/frontend/main.cpp)

set(SRC_KECCAK
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/Keccak-f.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/transformations.cpp)

set(SRC_SNEIK
    ${CMAKE_SOURCE_DIR}/thirdparty/sneik/sneik_f512.c)

set(SRC_MONOCYPHER
    ${CMAKE_SOURCE_DIR}/thirdparty/monocypher/src/monocypher.c)

set(SRC_XOODOO
    ${CMAKE_SOURCE_DIR}/thirdparty/XKCP/lib/low/Xoodoo/plain/Xoodoo-plain.c)

file(GLOB SRC_OBFMLKEM_MIN
    ${CMAKE_SOURCE_DIR}/frontend/kem/obf-mlkem/backend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/kem/obf-mlkem/codec/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/kem/obf-mlkem/native/*.c)

file(GLOB SRC_ECKEM_MIN
    ${CMAKE_SOURCE_DIR}/frontend/kem/eckem/*.cpp)

add_executable(frontend.exe ${SRC_FRONTEND} ${SRC_KECCAK} ${SRC_SNEIK} ${SRC_MONOCYPHER} ${SRC_XOODOO})

add_executable(construction_permutation_bench
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/ConstructionPermutation_Bench.cpp
    ${SRC_KECCAK}
    ${SRC_SNEIK}
    ${SRC_XOODOO})

add_executable(pqpsi_rbokvs_bench
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/PqPsi_RbOkvs_Bench.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/check.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/recv.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/send.cpp
    ${SRC_ECKEM_MIN}
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK}
    ${SRC_SNEIK}
    ${SRC_MONOCYPHER}
    ${SRC_XOODOO})

add_executable(pqpsi_party_bench
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/PqPsi_Party_Bench.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/recv.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/send.cpp
    ${SRC_ECKEM_MIN}
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK}
    ${SRC_SNEIK}
    ${SRC_MONOCYPHER}
    ${SRC_XOODOO})

add_executable(rbokvs_g_check
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/RbOkvs_G_Check.cpp)

add_executable(rbokvs_pqpsi_test
    ${CMAKE_SOURCE_DIR}/frontend/tests/RbOkvs_PqPsi_Test.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/check.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/recv.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/send.cpp
    ${SRC_ECKEM_MIN}
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK}
    ${SRC_SNEIK}
    ${SRC_MONOCYPHER}
    ${SRC_XOODOO})

add_executable(pqpsi_tests
    ${CMAKE_SOURCE_DIR}/tests/pqpsi_tests.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/check.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/recv.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/protocols/send.cpp
    ${SRC_ECKEM_MIN}
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK}
    ${SRC_SNEIK}
    ${SRC_MONOCYPHER}
    ${SRC_XOODOO})


#############################################
#            Link our libraries             #
#############################################
target_link_libraries(frontend.exe cryptoTools)
target_link_libraries(pqpsi_rbokvs_bench cryptoTools)
target_link_libraries(pqpsi_party_bench cryptoTools)
target_link_libraries(rbokvs_g_check cryptoTools)
target_link_libraries(rbokvs_pqpsi_test cryptoTools)
target_link_libraries(pqpsi_tests cryptoTools)
target_link_libraries(construction_permutation_bench cryptoTools)

# obf-mlkem/Kemeleon uses GMP and libsodium.
find_library(PQPSI_GMP_LIB NAMES gmp HINTS /usr/local/lib /opt/homebrew/lib)
find_library(PQPSI_SODIUM_LIB NAMES sodium HINTS /usr/local/lib /opt/homebrew/lib)
if(PQPSI_GMP_LIB AND PQPSI_SODIUM_LIB)
    target_link_libraries(frontend.exe
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB})
    target_link_libraries(pqpsi_rbokvs_bench
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB})
    target_link_libraries(pqpsi_party_bench
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB})
    target_link_libraries(rbokvs_g_check
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB})
    target_link_libraries(rbokvs_pqpsi_test
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB})
    target_link_libraries(pqpsi_tests
        ${PQPSI_GMP_LIB}
        ${PQPSI_SODIUM_LIB})
else()
    message(WARNING "frontend.exe, pqpsi_rbokvs_bench, pqpsi_party_bench, rbokvs_g_check, rbokvs_pqpsi_test, and pqpsi_tests need libgmp and libsodium available in the system library path.")
endif()
