# C SDK Examples

Most of these examples should be platform-independent. Some use POSIX functionality.

On POSIX systems you can simply install the dependencies and then use `CMake` to build the samples.

## Install Build Dependencies
The following dependencies must be installed before the examples can be built. We recommend using OS specific utilities
such as `brew`, `apt-get`, and similar package management utilities (depending on your environment).
- **libcouchbase >= 3.2.0+** (e.g., `brew install libcouchbase`)
- **cmake >= 3.20.0+** (e.g., `brew install cmake`)

## Building the Examples
The typical `CMake` build instructions should work on any OS that supports `CMake` as long as the appropriate
dependencies are installed and available.
1. `mkdir build; cd build` - _create an empty build dir_
2. `cmake ..` - _configure make files in build dir_
3. `cmake --build .` - _build all source and create binaries_
4. _the binaries will be in the current directory (`build`)_
5. `rm -rf build` - _to remove build dir and start over_

## Connecting to Couchbase
The examples have connection strings, usernames, and passwords that may be useful for local development.
These can be changed if you want to temporarily test with another deployed instance.