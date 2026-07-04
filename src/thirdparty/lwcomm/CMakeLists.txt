cmake_minimum_required(VERSION 3.15)
############FOR_MODIFIY_BEGIN#######################
#Setting Project Name
# MODULE_DESC: Low-level communication helpers (process name, fs paths)
PROJECT(lwcomm VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# include(../../CMakeCommonLib) - removed for standalone build

#Setting Source Files
SET(SOURCE_FILES
    src/comm.cpp
    src/comm_impl.cpp
    src/file_helper.cpp 
    src/string_helper.cpp
    src/time_helper.cpp 
    src/system_info.cpp
    src/file_system.cpp
    src/lwbase64.cpp
)

include_directories(
    ${LW_COMMON_DIR}
    ${LW_INCLUDE_DIR}
)

link_directories(
    ${LW_LIB_DIR}
)

## Declare a C++ library
add_library(${PROJECT_NAME} SHARED
    ${SOURCE_FILES}
)

if (${SYSTEM_NAME} MATCHES "qnx")
    target_link_libraries(${PROJECT_NAME}
)
elseif (${SYSTEM_NAME} MATCHES "android")
    target_link_libraries(${PROJECT_NAME}
)
elseif (${SYSTEM_NAME} MATCHES "sylixos")
    target_link_libraries(${PROJECT_NAME}
        crypto
        ${TARGET_LIBRARIES}
    )
elseif (${SYSTEM_NAME} MATCHES "windows")
    target_link_libraries(${PROJECT_NAME}
        ws2_32
        iphlpapi
        ${TARGET_LIBRARIES}
    )
else ()
    target_link_libraries(${PROJECT_NAME}
        pthread
        crypto
    )
endif()

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

## Mark cpp header files for installation
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/lwcomm.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

