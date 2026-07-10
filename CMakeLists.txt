cmake_minimum_required(VERSION 3.22)
project(GistDB LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(gistdb_core STATIC
  src/storage/validity_bitmap.cpp
  src/storage/fixed_width_column.cpp
  src/storage/varchar_column.cpp
  src/storage/zone_map.cpp
  src/storage/column_footer_entry.cpp
  src/storage/row_group_footer_entry.cpp
  src/storage/footer.cpp
)
target_include_directories(gistdb_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(gistdb_core PUBLIC cxx_std_23)
target_compile_options(gistdb_core PUBLIC -Wall -Wextra -Wpedantic)
target_include_directories(gistdb_core INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(gistdb_core INTERFACE cxx_std_23)
target_compile_options(gistdb_core INTERFACE -Wall -Wextra -Wpedantic)

enable_testing()
add_subdirectory(tests)