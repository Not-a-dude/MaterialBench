cmake_minimum_required(VERSION 3.22.1)
project(MaterialBenchNdk)

find_package(Vulkan REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_subdirectory(external/boringssl)

find_program(VULKAN_GLSLC_EXECUTABLE NAMES glslc glslc.exe)
if(NOT VULKAN_GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Install Vulkan SDK or shaderc.")
endif()

set(SOURCES
        utils.cpp
        cpu_math.cpp
        cpu_crypto.cpp
        cpu_av1.cpp
        ram.cpp
        rom_random.cpp
        rom_seq.cpp
        vulkan_compute.cpp
)

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
        COMMAND ${Python3_EXECUTABLE} -c "import sys; d=open(r'${SH_BINARY}','rb').read(); out=open(r'${SH_HEADER}','w'); name='gemm_shader_tiled_comp_spv'; out.write('unsigned char %s[] = {%s};\\nunsigned int %s_len = %d;\\n' % (name, ','.join('0x%02x' % b for b in d), name, len(d))); out.close()"
        DEPENDS "${SH_BINARY}"
        VERBATIM
)

add_library(materialbench SHARED ${SOURCES} "${SH_HEADER}")

set_source_files_properties("${SH_HEADER}" PROPERTIES GENERATED TRUE)

target_include_directories(materialbench PRIVATE
        external/boringssl/include
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_features(materialbench PRIVATE cxx_std_17)

target_compile_options(materialbench PRIVATE -O3 -funroll-loops -flto=thin)

target_link_libraries(materialbench
        android
        log
        ssl
        crypto
        jnigraphics
        vulkan
)