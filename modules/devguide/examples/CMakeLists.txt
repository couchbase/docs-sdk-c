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

macro(add_example name)
    add_executable(${name} c/${name}.cc)
    target_link_libraries(${name} project_options project_warnings couchbase)
endmacro()


add_example(bulk-get)
add_example(bulk-store)
add_example(connecting)
add_example(counter)
add_example(updating)
add_example(retrieving)
add_example(expiration)
add_example(subdoc-retrieving)
add_example(subdoc-updating)
add_example(query-atplus)
add_example(query-consistency)
add_example(query-criteria)
add_example(query-placeholders)
if (Threads_FOUND)
    add_example(cas)
    target_link_libraries(cas Threads::Threads)
    add_example(pessimistic-lock)
    target_link_libraries(pessimistic-lock Threads::Threads)
endif()

add_example(management-bucket-flush)
add_example(management-bucket-create)
add_example(management-bucket-drop)

add_example(client-settings)
