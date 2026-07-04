cmake_minimum_required(VERSION 3.12)
# MODULE_DESC: Event notification library (EventCreate/Set/Wait/Destroy)
project(lwevent LANGUAGES C VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 11)

# include(../../CMakeCommonLib) - removed for standalone build

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL  "3.31")
    cmake_policy(SET CMP0177 NEW) 
endif ()

include_directories(
    .
)

link_directories(
    ${LW_LIB_DIR}
)

add_library(${PROJECT_NAME} SHARED
    lwevent.c
)

# Define export macro for Windows DLL
target_compile_definitions(${PROJECT_NAME} PRIVATE LWEVENT_EXPORTS)

if (${SYSTEM_NAME} MATCHES "qnx")
    target_link_libraries( 
        ${PROJECT_NAME}
    )
elseif (${SYSTEM_NAME} MATCHES "android")
    target_link_libraries( 
        ${PROJECT_NAME}
    )
elseif (${SYSTEM_NAME} MATCHES "sylixos")
    target_link_libraries( 
        ${PROJECT_NAME}
        ${TARGET_LIBRARIES}
    )
elseif (${SYSTEM_NAME} MATCHES "windows")
    target_link_libraries( 
        ${PROJECT_NAME}
        ${TARGET_LIBRARIES}
    )
elseif (${SYSTEM_NAME} MATCHES "windows")
    target_link_libraries(
        ${PROJECT_NAME}
    )
else ()
    target_link_libraries(
        ${PROJECT_NAME}
        pthread
    )
endif()

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/lwevent.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)
