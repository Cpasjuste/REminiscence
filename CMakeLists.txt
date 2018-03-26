cmake_minimum_required(VERSION 3.0)
#set(CMAKE_VERBOSE_MAKEFILE ON)

# delete cmake cache folder before changing this options
option(BUILD_SDL1 "Build with SDL1 support" OFF)
option(BUILD_SDL2 "Build with SDL2 support" OFF)
option(BUILD_PSP2 "Build with PSP2 support" OFF)
option(BUILD_NX "Build with NX (SWITCH) support" ON)

# setup toolchain
include(Toolchain.cmake)

project(REminiscence)

##############
# COMMON
##############
set(RE_INC .)

file(GLOB RE_SRC *.cpp)

set(RE_CFLAGS -DUSE_MODPLUG -DUSE_TREMOR -DUSE_ZLIB)

set(RE_LDFLAGS SDL2 modplug vorbisidec ogg z)

#####################
# PLATORM SPECIFIC
#####################
if (BUILD_PSP2)
    #####################
    # PSP2 PLATORM
    #####################
elseif (BUILD_NX)
    #####################
    # NX PLATORM
    #####################
    list(REMOVE_ITEM RE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/dynlib.cpp)
    list(APPEND RE_SRC switch.c)
    list(APPEND RE_CFLAGS -D__SWITCH__
            -I${DEVKITPRO}/portlibs/switch/include/SDL2
            )
    list(APPEND RE_LDFLAGS
            nx m
            -specs=${DEVKITPRO}/libnx/switch.specs
            )
elseif (BUILD_SDL2)
    #####################
    # SDL2 PLATORM
    #####################
elseif (BUILD_SDL1)
    #####################
    # SDL1 PLATORM
    #####################
endif (BUILD_PSP2)

##########################
# REminiscence executable
##########################
add_executable(${CMAKE_PROJECT_NAME}.elf ${RE_SRC})
target_include_directories(${CMAKE_PROJECT_NAME}.elf PRIVATE ${RE_INC})
target_compile_options(${CMAKE_PROJECT_NAME}.elf PRIVATE ${RE_CFLAGS})
target_link_libraries(${CMAKE_PROJECT_NAME}.elf ${RE_LDFLAGS})

#####################
# PSP2 (vita) target
#####################
if (BUILD_PSP2)
    vita_create_self(${CMAKE_PROJECT_NAME}.self ${CMAKE_PROJECT_NAME}.elf)
    vita_create_vpk(${CMAKE_PROJECT_NAME}.vpk "OPENBOR01" ${CMAKE_PROJECT_NAME}.self
            VERSION "01.00" NAME "${CMAKE_PROJECT_NAME}")
endif (BUILD_PSP2)

#####################
# NX (SWITCH) target
#####################
if (BUILD_NX)
    add_custom_target(${CMAKE_PROJECT_NAME}.nro
            DEPENDS ${CMAKE_PROJECT_NAME}.elf
            COMMAND elf2nro ${CMAKE_PROJECT_NAME}.elf ${CMAKE_PROJECT_NAME}.nro)
endif (BUILD_NX)
