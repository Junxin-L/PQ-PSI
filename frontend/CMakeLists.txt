
#project(fronend)


 

#############################################
#               Build fronend.exe           #
#############################################

file(GLOB SRC_FRONTEND
    ${CMAKE_SOURCE_DIR}/frontend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/backend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/codec/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/session/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/native/*.c)
include_directories(${CMAKE_SOURCE_DIR}/frontend/)
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/native)
set(SRC_FRONTEND_NO_MAIN ${SRC_FRONTEND})
list(REMOVE_ITEM SRC_FRONTEND_NO_MAIN ${CMAKE_SOURCE_DIR}/frontend/main.cpp)

set(SRC_KECCAK
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/Keccak-f.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/transformations.cpp)

file(GLOB SRC_OBFMLKEM_MIN
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/backend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/codec/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/native/*.c)

add_executable(frontend.exe ${SRC_FRONTEND} ${SRC_KECCAK})

add_executable(construction_permutation_bench
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/ConstructionPermutation_Bench.cpp
    ${SRC_KECCAK})

add_executable(pqpsi_rbokvs_bench
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/PqPsi_RbOkvs_Bench.cpp
    ${CMAKE_SOURCE_DIR}/frontend/pqpsi/pqpsi.cpp
    ${SRC_OBFMLKEM_MIN}
    ${SRC_KECCAK})


#############################################
#            Link our libraries             #
#############################################
#target_link_libraries(frontend.exe libPSI_Tests)
target_link_libraries(frontend.exe libOPRF)
target_link_libraries(frontend.exe libOTe)
target_link_libraries(frontend.exe libPaXoS)
target_link_libraries(pqpsi_rbokvs_bench cryptoTools)

# Reminder: obf-mlkem backend needs GMP and libsodium for Kemeleon + randombytes/hash
find_library(PQPSI_GMP_LIB NAMES gmp HINTS /usr/local/lib /opt/homebrew/lib)
find_library(PQPSI_SODIUM_LIB NAMES sodium HINTS /usr/local/lib /opt/homebrew/lib)
if(PQPSI_GMP_LIB AND PQPSI_SODIUM_LIB)
    target_link_libraries(pqpsi_rbokvs_bench ${PQPSI_GMP_LIB} ${PQPSI_SODIUM_LIB})
else()
    message(WARNING "pqpsi_rbokvs_bench needs libgmp + libsodium; install via brew install gmp libsodium")
endif()
