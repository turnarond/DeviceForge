cmake_minimum_required(VERSION 3.23)

project(lwcommunicate)

set(CMAKE_CXX_STANDARD 11)

include(../../CMakeCommonLib)

include_directories(
    ${LW_INCLUDE_DIR}
    ${LW_INCLUDE_DIR}/libevent
    ./
)

link_directories(
    ${LW_LIB_DIR}
)

add_library(lwcommunicate SHARED
    lwconn_base.cpp
    lwconn_tcp_client.cpp
    lwconn_tcp_server.cpp
    lwconn_udp.cpp
    lwconn_serial.cpp
    lwconn_pool.cpp
    lwconn_manager.cpp
    lwconn_utils.cpp
    lwconn_device.cpp
    # test_client.cpp
)

target_link_libraries(lwcommunicate
    lwcomm
)
if(NOT WIN32)
    target_compile_options(lwcommunicate PRIVATE -Wno-unused-parameter)
endif()

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

## Mark cpp header files for installation
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/lwconn_base.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_tcp_client.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_tcp_server.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_udp.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_serial.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_pool.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_manager.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_utils.h"
               "${CMAKE_CURRENT_LIST_DIR}/lwconn_device.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
