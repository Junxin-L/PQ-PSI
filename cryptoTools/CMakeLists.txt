

project(cryptoTools)


if(1)
	message(STATUS "cryptoTools: Intel ASM SHA1 is disabled; using the portable SHA1 implementation.")

	# define this so that the asm is disabled.
	add_definitions(-DNO_INTEL_ASM_SHA1=1)
	set(shaNasmOutput )
	add_custom_target(sha_asm)
	
else()
		set(shaNasm "${CMAKE_SOURCE_DIR}/cryptoTools/Crypto/asm/sha_lnx.S")
        set(shaNasmOutput "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/cryptoTools.dir/Crypto/sha_lnx.S.o")
      
        add_custom_command(
                OUTPUT ${shaNasmOutput}
                DEPENDS ${shaNasm} 
                COMMENT "nasm -f elf64 ${shaNasm} -o ${shaNasmOutput}"
                COMMAND "nasm" "-f elf64" "${shaNasm}" "-o ${shaNasmOutput}"
                VERBATIM)


	# mark this asm output input to everything.
	add_custom_target(sha_asm DEPENDS ${shaNasmOutput})
endif()


file(GLOB_RECURSE SRCS *.cpp)

include_directories(${CMAKE_SOURCE_DIR}/cryptoTools)
add_library(cryptoTools ${SRCS} ${shaNasmOutput})
add_dependencies(cryptoTools sha_asm)



###########################################################################  
###########################################################################  
#                        Link external libraries                          #
#                        -----------------------                          #
#                                                                         #
#  Define the expected location for miracl and boost.                     #
#  Boost will be found using the findBoost  module in CMake               #
#  It should look in the location specified and then look elsewhere       # 
#                                                                         #
###########################################################################  
  

 
find_library(
  MIRACL_LIB 
  NAMES miracl libmiracl
  HINTS "${Miracl_Dirs}/miracl/source/" "${Miracl_Dirs}/miracl/")
  
# if we cant fint it, throw an error
if(NOT MIRACL_LIB)
  Message(${MIRACL_LIB})
  message(FATAL_ERROR "Failed to find miracl at " ${Miracl_Lib_Dirs})
endif()


if(WIN32)
  set(Boost_USE_STATIC_LIBS        ON)
  set(Boost_USE_STATIC_RUNTIME     ON)
else()
  set(Boost_USE_STATIC_LIBS        OFF)
  set(Boost_USE_STATIC_RUNTIME     OFF)
endif()
set(Boost_USE_MULTITHREADED      ON)

find_package(Boost REQUIRED COMPONENTS system thread)

if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS}) 
  #message( "Found Boost at ${Boost_LIBRARIES}")
else()
   message(FATAL_ERROR "Failed to find boost at " ${Boost_Lib_Dirs})
endif()



#target_link_libraries(cryptoTools sha_asm)
target_link_libraries(cryptoTools ${MIRACL_LIB})
target_link_libraries(cryptoTools ${Boost_LIBRARIES})


#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
