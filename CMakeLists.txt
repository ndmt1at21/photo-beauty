cmake_minimum_required(VERSION 3.25)

option(PB_TRACY "Tracy profiler zones" OFF)
if(PB_TRACY)
  list(APPEND VCPKG_MANIFEST_FEATURES tracy)  # must precede project()
endif()

project(photo-beauty LANGUAGES C CXX)
set(PB_VERSION 0.1.0 CACHE STRING "The app's version: .pbd headers, About, installers")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wno-missing-field-initializers)  # Vulkan `Info x{sType}` idiom
  # No fused multiply-add: ARM and FMA targets would round differently from x86-64, the GPU and the CPU references.
  add_compile_options(-ffp-contract=off)
endif()

# Override with -DSLANGC=... where vcpkg's prebuilt slangc cannot run (glibc < 2.34).
find_program(SLANGC slangc REQUIRED
  HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/shader-slang")

find_package(Threads REQUIRED)
find_package(lcms2 CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SPNG CONFIG REQUIRED)
find_package(TIFF REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)

# Kernels: shaders/<name>.slang has entry <name>; shared code in shaders/lib/ (`import`). SPIR-V embedded as
# C arrays <name>_spv, <name>_spv_sizeInBytes in <name>.spv.h.
set(PB_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${PB_SHADER_OUT}")
file(GLOB PB_KERNELS CONFIGURE_DEPENDS shaders/*.slang)
file(GLOB PB_SHADER_LIB CONFIGURE_DEPENDS shaders/lib/*.slang)
foreach(src ${PB_KERNELS})
  get_filename_component(entry "${src}" NAME_WE)
  add_custom_command(
    OUTPUT "${PB_SHADER_OUT}/${entry}.spv.h"
    COMMAND "${SLANGC}" "${src}" -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lib"
            -target spirv -profile spirv_1_3 -entry ${entry} -stage compute -O2  # Vulkan 1.1+
            -source-embed-style u32 -source-embed-name ${entry}_spv -o "${PB_SHADER_OUT}/${entry}.spv.h"
    DEPENDS "${src}" ${PB_SHADER_LIB}
    COMMENT "slangc ${entry}.slang")
  list(APPEND PB_SHADER_HEADERS "${PB_SHADER_OUT}/${entry}.spv.h")
endforeach()

# Every library P0 needs, public, so later tasks only add files.
file(GLOB PB_CORE_SOURCES CONFIGURE_DEPENDS core/*.cpp)
add_library(pb_core ${PB_CORE_SOURCES} ${PB_SHADER_HEADERS})
target_include_directories(pb_core PUBLIC core "${PB_SHADER_OUT}")
target_link_libraries(pb_core PUBLIC Threads::Threads zstd::libzstd ZLIB::ZLIB lcms2::lcms2 TIFF::TIFF nlohmann_json::nlohmann_json
  $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
  $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static> PRIVATE volk::volk)
if(PB_TRACY)
  find_package(Tracy CONFIG REQUIRED)
  target_link_libraries(pb_core PUBLIC Tracy::TracyClient)
  target_compile_definitions(pb_core PUBLIC PB_TRACY)
endif()
set_source_files_properties(core/version.cpp PROPERTIES COMPILE_DEFINITIONS PB_VERSION="${PB_VERSION}")

file(GLOB PB_BENCH_SOURCES CONFIGURE_DEPENDS bench/*.cpp)
add_executable(pb-bench ${PB_BENCH_SOURCES})
target_link_libraries(pb-bench PRIVATE pb_core)

enable_testing()
file(GLOB PB_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(pb-tests ${PB_TEST_SOURCES})
target_link_libraries(pb-tests PRIVATE pb_core)
target_compile_definitions(pb-tests PRIVATE PB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")  # test data, samples/
add_test(NAME pb COMMAND pb-tests)
