
#project(libTests)

file(GLOB_RECURSE SRCS *.cpp)
list(REMOVE_ITEM SRCS "${CMAKE_CURRENT_SOURCE_DIR}/obf-mlkem/ObfMlKem_Bench.cpp")
add_library(libTests STATIC ${SRCS})

target_include_directories(libTests PUBLIC ${CMAKE_SOURCE_DIR}) 
target_link_libraries(libTests libPSI)

add_executable(obf_mlkem_bench obf-mlkem/ObfMlKem_Bench.cpp)
target_include_directories(obf_mlkem_bench PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(obf_mlkem_bench libPSI)
