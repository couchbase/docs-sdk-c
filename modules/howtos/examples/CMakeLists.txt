cmake_minimum_required(VERSION 3.5.1)
project(devguide LANGUAGES CXX C)

include(cmake/PreventInSourceBuilds.cmake)
include(cmake/StandardProjectSettings.cmake)

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_11)
add_library(project_warnings INTERFACE)

include(cmake/Cache.cmake)

include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

include(cmake/Sanitizers.cmake)
enable_sanitizers(project_options)

include(FindThreads)

add_library(cJSON cJSON.c)

macro(add_example name)
    add_executable(${name} ${name}.cc)
    target_link_libraries(${name} project_options project_warnings couchbase)
endmacro()

add_example(analytics)
target_link_libraries(analytics cJSON)

add_example(array-append-prepend)
add_example(atomic-counters)
add_example(bulk-get)
add_example(durability)
add_example(expiration)
add_example(insert-upsert)
add_example(query-placeholders)
add_example(query-criteria)
add_example(subdoc-retrieving)
add_example(subdoc-updating)
add_example(views)
add_example(fts-basic)

if (Threads_FOUND)
    add_example(cas)
    target_link_libraries(cas Threads::Threads)
endif()
