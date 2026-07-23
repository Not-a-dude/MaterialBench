cmake_minimum_required(VERSION 3.22.1)
project(MaterialBenchNdk)

find_package(Vulkan REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

find_program(VULKAN_GLSLC_EXECUTABLE NAMES glslc glslc.exe)
if(NOT VULKAN_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Install Vulkan SDK or shaderc.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O3 -funroll-loops -flto=thin)
add_link_options(-flto=thin)

set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build zstd CLI")
set(ZSTD_LEGACY_LEVEL 0 CACHE STRING "" FORCE)

add_subdirectory(external/boringssl)
add_subdirectory(external/zstd/build/cmake)

set(SOURCES
        utils.cpp
        cpu_system_math.cpp
        cpu_vector_math.cpp
        cpu_crypto.cpp
        cpu_compress.cpp
        ram.cpp
        rom_random.cpp
        rom_seq.cpp
        vulkan_compute.cpp
        vulkan/vulkan_context.cpp
        vulkan/gemm_executor.cpp
        vulkan/compute_service.cpp
)

set_source_files_properties(cpu_crypto.cpp cpu_vector_math.cpp
    PROPERTIES COMPILE_FLAGS "-march=armv8-a+simd")

set(SH_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/gemm_shader_tiled.comp")
set(SH_BINARY "${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv")
set(SH_HEADER "${CMAKE_CURRENT_BINARY_DIR}/gemm_shader_tiled.comp.spv.h")

add_custom_command(
        OUTPUT "${SH_BINARY}"
        COMMAND ${VULKAN_GLSLC_EXECUTABLE} "${SH_SOURCE}" -o "${SH_BINARY}"
        DEPENDS "${SH_SOURCE}"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SH_HEADER}"
        COMMAND ${Python3_EXECUTABLE} -c "d=open(r'${SH_BINARY}','rb').read(); name='gemm_shader_tiled_comp_spv'; open(r'${SH_HEADER}','w').write('alignas(4) const unsigned char %s[] = {%s};\\nconst unsigned int %s_len = %d;\\n' % (name, ','.join('0x%02x' % b for b in d), name, len(d)))"
        DEPENDS "${SH_BINARY}"
        VERBATIM
)

add_library(materialbench SHARED ${SOURCES} "${SH_HEADER}")

set_source_files_properties("${SH_HEADER}" PROPERTIES GENERATED TRUE)

target_include_directories(materialbench PRIVATE
        external/boringssl/include
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(materialbench
        android
        log
        ssl
        crypto
        jnigraphics
        vulkan
        libzstd_static
)