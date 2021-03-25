This directory contains several examples, that are used by the documentation site.

To compile them, navigate to this directory (`modules/howtos/examples/`), make sure you have libcouchbase library
and headers installed in the system, as well as C++ compiler and `cmake` build tool. Then run the following sequence:

    mkdir build
    cd build
    cmake ..
    cmake --build .
