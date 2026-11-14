# Network Play library: shared by both games (linked into the runtime), the launcher and the
# standalone relay. No dependencies on Windows (WinHTTP, Winsock); mbed TLS on Linux for wss://.
cmake_minimum_required(VERSION 3.20)
project(excite_netplay CXX)

if(NOT TARGET excite_netplay)
add_library(excite_netplay STATIC
    src/connection.cpp
    src/protocol.cpp
    src/relay_server.cpp
    src/session.cpp
    src/ws_util.cpp)
target_include_directories(excite_netplay PUBLIC include)
target_compile_features(excite_netplay PUBLIC cxx_std_20)
set_target_properties(excite_netplay PROPERTIES UNITY_BUILD OFF)
if(WIN32)
    target_link_libraries(excite_netplay PUBLIC ws2_32 winhttp iphlpapi)
else()
    # The runtime passes its own mbed TLS target; standalone builds look for the system one.
    if(NOT NETPLAY_MBEDTLS_TARGET)
        find_library(NETPLAY_MBEDTLS_LIB mbedtls)
        find_library(NETPLAY_MBEDX509_LIB mbedx509)
        find_library(NETPLAY_MBEDCRYPTO_LIB mbedcrypto)
        if(NETPLAY_MBEDTLS_LIB AND NETPLAY_MBEDX509_LIB AND NETPLAY_MBEDCRYPTO_LIB)
            add_library(netplay_system_mbedtls INTERFACE)
            target_link_libraries(netplay_system_mbedtls INTERFACE ${NETPLAY_MBEDTLS_LIB} ${NETPLAY_MBEDX509_LIB} ${NETPLAY_MBEDCRYPTO_LIB})
            set(NETPLAY_MBEDTLS_TARGET netplay_system_mbedtls)
        endif()
    endif()
    if(NETPLAY_MBEDTLS_TARGET)
        target_link_libraries(excite_netplay PRIVATE ${NETPLAY_MBEDTLS_TARGET})
        target_compile_definitions(excite_netplay PRIVATE NETPLAY_HAVE_MBEDTLS=1)
    else()
        message(STATUS "Network Play: no mbed TLS found, wss:// servers won't work on this build")
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(excite_netplay PUBLIC Threads::Threads)
endif()
endif()

option(NETPLAY_BUILD_TOOLS "Build ExciteRelay and the Network Play tests" OFF)
if(NETPLAY_BUILD_TOOLS OR PROJECT_IS_TOP_LEVEL)
    add_executable(ExciteRelay tools/relay_main.cpp)
    target_link_libraries(ExciteRelay PRIVATE excite_netplay)
    add_executable(netplay_test tests/netplay_test.cpp)
    target_link_libraries(netplay_test PRIVATE excite_netplay)
endif()
