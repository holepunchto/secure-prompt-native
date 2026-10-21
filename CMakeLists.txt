cmake_minimum_required(VERSION 3.25)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(cmake-bare REQUIRED PATHS node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)
find_package(cmake-napi REQUIRED PATHS node_modules/cmake-napi)
find_package(cmake-npm REQUIRED PATHS node_modules/cmake-npm)

project(secure_prompt_native C)

fetch_package("github:holepunchto/libmprotect")

add_bare_module(secure_prompt_native_bare)

target_sources(
  ${secure_prompt_native_bare}
  PRIVATE
    binding.c
    src/editor.c
)

target_link_libraries(
  ${secure_prompt_native_bare}
  PRIVATE
    mprotect_static
)

add_napi_module(secure_prompt_native_node)

target_sources(
  ${secure_prompt_native_node}
  PRIVATE
    binding.c
    src/editor.c
)

target_link_libraries(
  ${secure_prompt_native_node}
  PRIVATE
    mprotect_static
)

resolve_node_module(bare-compat-napi compat)

target_include_directories(
  ${secure_prompt_native_node}
  PRIVATE
    "${compat}/include"
)
