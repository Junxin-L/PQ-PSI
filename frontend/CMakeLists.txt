
#project(fronend)


 

#############################################
#               Build fronend.exe           #
#############################################

file(GLOB SRC_FRONTEND
    ${CMAKE_SOURCE_DIR}/frontend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/backend/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/codec/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/session/*.cpp
    ${CMAKE_SOURCE_DIR}/frontend/obf-mlkem/native/*.c)
include_directories(${CMAKE_SOURCE_DIR}/frontend/)

set(SRC_KECCAK
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/Keccak-f.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/KeccakTools/Sources/transformations.cpp)

add_executable(frontend.exe ${SRC_FRONTEND} ${SRC_KECCAK})

add_executable(construction_permutation_bench
    ${CMAKE_SOURCE_DIR}/frontend/benchmarks/ConstructionPermutation_Bench.cpp
    ${SRC_KECCAK})


#############################################
#            Link our libraries             #
#############################################
#target_link_libraries(frontend.exe libPSI_Tests)
target_link_libraries(frontend.exe libOPRF)
target_link_libraries(frontend.exe libOTe)
target_link_libraries(frontend.exe libPaXoS)
