cmake_minimum_required(VERSION 3.22.1)
project(MaterialBenchNdk)

find_package(Vulkan REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_subdirectory(external/boringssl)

find_program(VULKAN_GLSLC_EXECUTABLE NAMES glslc)

if(NOT VULKAN_GLSLC_EXECUTABLE)
    message(STATUS "glslc not found in PATH. Please, indicate path to glslc.exe manualy.")
    set(VULKAN_GLSLC_EXECUTABLE "" CACHE FILEPATH "Path to glslc.exe (part of Vulkan SDK)")
endif()

set(SOURCES
        utils.cpp
        cpu_math.cpp
        cpu_crypto.cpp
        ram.cpp
        rom_random.cpp
        rom_seq.cpp
        vulkan_compute.cpp
)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gemm_shader_tiled.comp -o ${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gemm_shader_tiled.comp
        VERBATIM
)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv.h
        COMMAND ${Python3_EXECUTABLE} -c "import sys; p=r'${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv'; out=r'${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv.h'; name='gemm_shader_tiled_comp_spv'; d=open(p,'rb').read(); open(out,'w').write('unsigned char %s[] = {%s};\\nunsigned int %s_len = %d;\\n' % (name, ','.join('0x%02x' % b for b in d), name, len(d)))"
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv
        VERBATIM
)

add_custom_target(compile_shader DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv.h
)

add_library(materialbench SHARED ${SOURCES})

add_dependencies(materialbench compile_shader)

target_sources(materialbench PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv.h
)

set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv.h PROPERTY GENERATED TRUE)

target_include_directories(materialbench PRIVATE
        external/boringssl/include
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_features(materialbench PRIVATE cxx_std_17)

target_compile_options(materialbench PRIVATE
        -O3
        -funroll-loops
        -flto=thin
)

target_link_libraries(materialbench
        android
        log
        ssl
        crypto
        jnigraphics
        vulkan
)
