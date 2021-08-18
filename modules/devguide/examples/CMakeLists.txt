cmake_minimum_required(VERSION 3.20)
project(devguide LANGUAGES CXX C)

# restrict features to POSIX C11 standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
add_definitions("-D_POSIX_C_SOURCE=200112L")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wshadow")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wshadow")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(PreventInSourceBuilds)
include(StandardProjectSettings)

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_11)
add_library(project_warnings INTERFACE)

include(Cache)

include(CompilerWarnings)
set_project_warnings(project_warnings)

include(Sanitizers)
enable_sanitizers(project_options)

# Find the thread library
include(FindThreads)

# Find the Couchbase library
find_package(COUCHBASE 3.2 REQUIRED)

macro(add_example name)
    add_executable( ${name}
        c/${name}.cc
    )
    target_include_directories( ${name}
        PRIVATE
        "${COUCHBASE_INCLUDE_DIRS}"
    )
    target_link_libraries( ${name}
        project_options
        project_warnings
        COUCHBASE::COUCHBASE
    )
endmacro()

macro(add_thread_example name)
    if (Threads_FOUND)
        add_example(${name})
        target_link_libraries( ${name} Threads::Threads)
    endif()
endmacro()

# All examples in alphabetical order (group
add_example(bulk-get)
add_example(bulk-store)
add_thread_example(cas)
add_example(client-settings)
add_example(connecting)
#add_example(connecting-cert-auth) # TODO: Refactor connecting-cert-auth
#add_example(connecting-ssl) # TODO: Refactor connecting-ssl
add_example(counter)
#add_example(create-remove-bucket) # TODO: Refactor create-remove-bucket
add_example(durability)
add_example(expiration)
add_example(fts-basic)
add_example(management-bucket-create)
add_example(management-bucket-drop)
add_example(management-bucket-flush)
add_example(n1ql-create-primary-index)
add_thread_example(pessimistic-lock)
add_example(query-atplus)
add_example(query-consistency)
#add_example(query-create-index) # TODO: Refactor query-create-index
add_example(query-criteria)
add_example(query-placeholders)
add_example(retrieving)
add_example(subdoc-retrieving)
add_example(subdoc-updating)
add_example(updating)
