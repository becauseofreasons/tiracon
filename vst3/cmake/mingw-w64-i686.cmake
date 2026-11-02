# Cross compiling for Windows 32 bit from Linux (Ubuntu package mingw-w64).
# posix thread model: needed for std::mutex / std::thread; everything is linked statically.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# SSE2 for the denormal flush (FTZ/DAZ) and fast float math; every 32 bit CPU since 2003 has it
set(CMAKE_C_FLAGS_INIT "-msse2 -mfpmath=sse")
set(CMAKE_CXX_FLAGS_INIT "-msse2 -mfpmath=sse")
