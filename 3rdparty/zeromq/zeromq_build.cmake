include(ExternalProject)
include(FetchContent)

# ExternalProject seems to be the best solution for including zeromq.
# The projects defines options which clash with and pollute our CMake cache.

# Define the compile flags for Windows
if(WIN32)
    set(WIN_CMAKE_ARGS "-DCMAKE_CXX_FLAGS_DEBUG=$<IF:$<BOOL:${STATIC_WINDOWS_RUNTIME}>,/MTd,/MDd> /Zi /Ob0 /Od /RTC1" 
                       "-DCMAKE_CXX_FLAGS_RELEASE=$<IF:$<BOOL:${STATIC_WINDOWS_RUNTIME}>,/MT,/MD> /O2 /Ob2 /DNDEBUG"
                       "-DCMAKE_C_FLAGS_DEBUG=$<IF:$<BOOL:${STATIC_WINDOWS_RUNTIME}>,/MTd,/MDd> /Zi /Ob0 /Od /RTC1"
                       "-DCMAKE_C_FLAGS_RELEASE=$<IF:$<BOOL:${STATIC_WINDOWS_RUNTIME}>,/MT,/MD> /O2 /Ob2 /DNDEBUG"
                       )
else()
    set(WIN_CMAKE_ARGS "")
endif()


ExternalProject_Add(
    ext_zeromq
    PREFIX "${CMAKE_BINARY_DIR}/zeromq"
    URL "https://github.com/zeromq/libzmq/releases/download/v4.3.3/zeromq-4.3.3.tar.gz"
    URL_HASH MD5=78acc277d95e10812d71b2b3c3c3c9a9
    # do not update
    UPDATE_COMMAND ""
    CMAKE_ARGS
        # Does not seem to work. We have to directly set the flags on Windows.
        #-DCMAKE_POLICY_DEFAULT_CMP0091:STRING=NEW 
        #-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=${CMAKE_MSVC_RUNTIME_LIBRARY}
        -DBUILD_STATIC=ON
        -DBUILD_SHARED=OFF
        -DBUILD_TESTS=OFF
        -DENABLE_CPACK=OFF
        -DENABLE_CURVE=OFF
        -DZMQ_BUILD_TESTS=OFF
        -DWITH_DOCS=OFF
        -DWITH_PERF_TOOL=OFF
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        ${WIN_CMAKE_ARGS}
)

# cppzmq is header only. we just need to download
FetchContent_Declare(
    ext_cppzmq
    URL "https://github.com/zeromq/cppzmq/archive/v4.7.1.tar.gz"
    URL_HASH MD5=e85cf23b5aed263c2c5c89657737d107
)
FetchContent_GetProperties(ext_cppzmq)
if(NOT ext_cppzmq_POPULATED)
    FetchContent_Populate(ext_cppzmq)
    # do not add subdirectory here
endif()

if( WIN32 )
    # On windows the lib name is more complicated
    set(ZEROMQ_LIBRARIES libzmq-${CMAKE_VS_PLATFORM_TOOLSET}-mt-s$<$<CONFIG:Debug>:gd>-4_3_3 )
 
    # On windows we need to link some additional libs. We will use them 
    # directly as targets in find_dependencies.cmake.
    # The following code is taken from the zeromq CMakeLists.txt and collects
    # the additional libs in ZEROMQ_ADDITIONAL_LIBS.
    include(CheckCXXSymbolExists)
    set(CMAKE_REQUIRED_LIBRARIES "ws2_32.lib")
    check_cxx_symbol_exists(WSAStartup "winsock2.h" HAVE_WS2_32)

    set(CMAKE_REQUIRED_LIBRARIES "rpcrt4.lib")
    check_cxx_symbol_exists(UuidCreateSequential "rpc.h" HAVE_RPCRT4)

    set(CMAKE_REQUIRED_LIBRARIES "iphlpapi.lib")
    check_cxx_symbol_exists(GetAdaptersAddresses "winsock2.h;iphlpapi.h" HAVE_IPHLPAPI)
    set(CMAKE_REQUIRED_LIBRARIES "")

    if(HAVE_WS2_32)
        list(APPEND ZEROMQ_ADDITIONAL_LIBS ws2_32)
    endif()
    if(HAVE_RPCRT4)
        list(APPEND ZEROMQ_ADDITIONAL_LIBS rpcrt4)
    endif()
    if(HAVE_IPHLPAPI)
        list(APPEND ZEROMQ_ADDITIONAL_LIBS iphlpapi)
    endif()

else()
    set(ZEROMQ_LIBRARIES zmq)
endif()
ExternalProject_Get_Property( ext_zeromq INSTALL_DIR )
set(ZEROMQ_LIB_DIR ${INSTALL_DIR}/lib)
set(ZEROMQ_INCLUDE_DIRS "${INSTALL_DIR}/include/;${ext_cppzmq_SOURCE_DIR}/")
