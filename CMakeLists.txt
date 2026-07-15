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
  src/storage/file_header.cpp
  src/storage/disk_manager.cpp
  src/catalog/table_object.cpp
  src/serialization/byte_io.cpp
  src/catalog/schema_section.cpp
  src/catalog/catalog.cpp
  src/execution/data_chunk.cpp
  src/execution/bound_expression.cpp
  src/execution/expression_evaluator.cpp
  src/execution/filter_operator.cpp
  src/execution/aggregate_accumulator.cpp
  src/execution/hash_join_operator.cpp
  src/binder/ast.cpp
  src/binder/parser.cpp
  src/binder/type_coercion.cpp
  src/binder/logical_plan.cpp
  src/binder/resolution.cpp
  src/binder/binder.cpp
  src/optimizer/predicate_utils.cpp
  src/optimizer/predicate_pushdown.cpp
  src/optimizer/column_pruning.cpp
  src/optimizer/optimizer.cpp
  src/storage/replacer.cpp
  src/execution/aggregation_operator.cpp
)
target_include_directories(gistdb_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(gistdb_core PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/libpg_query
    ${CMAKE_SOURCE_DIR}/third_party/libpg_query/protobuf
    ${CMAKE_SOURCE_DIR}/third_party/libpg_query/vendor
)
target_compile_features(gistdb_core PUBLIC cxx_std_23)
target_compile_options(gistdb_core PUBLIC -Wall -Wextra -Wpedantic)
target_include_directories(gistdb_core INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(gistdb_core INTERFACE cxx_std_23)
target_compile_options(gistdb_core INTERFACE -Wall -Wextra -Wpedantic)

add_library(pg_query STATIC IMPORTED)
set_target_properties(pg_query PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/third_party/libpg_query/libpg_query.a
)
target_link_libraries(gistdb_core PUBLIC pg_query)

enable_testing()
add_subdirectory(tests)