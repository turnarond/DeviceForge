cmake_minimum_required(VERSION 3.12)
# MODULE_DESC: Message queue library
project(lwmsgq VERSION 1.0.0)

# include(../../CMakeCommonLib) - removed for standalone build

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL  "3.31")
    cmake_policy(SET CMP0177 NEW) 
endif ()

## Specify additional locations of header files
## Your package locations should be listed before other locations
include_directories(
    ./include
    ${LW_COMMON_DIR}
    ${LW_INCLUDE_DIR}
)

link_directories(
    ${LW_LIB_DIR}
)

## Declare a C++ library
add_library(${PROJECT_NAME} SHARED
    list.c
    lwmsgq.c
)

# Enable automatic symbol export on Windows (generates .lib import library)
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
endif()

target_link_libraries(${PROJECT_NAME}
    lwevent
    ${TARGET_LIBRARIES}
)

# Ensure lwevent is built before lwmsgq
add_dependencies(${PROJECT_NAME} lwevent)

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/lwmsgq.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/lwmsgq.hpp"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/list.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/lwmsgq_version.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)
