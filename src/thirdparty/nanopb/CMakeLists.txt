cmake_minimum_required(VERSION 3.12)
# MODULE_DESC: Lightweight Protocol Buffers encoder/decoder (vendored)
project(nanopb LANGUAGES C VERSION 0.4.9)

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
    pb_common.c
    pb_decode.c
    pb_encode.c
)

# Enable automatic symbol export on Windows (generates .lib file)
set_target_properties(${PROJECT_NAME} PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_link_libraries(
    ${PROJECT_NAME}
    ${TARGET_LIBRARIES}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/pb.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/pb_common.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/pb_decode.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(FILES  "${CMAKE_CURRENT_LIST_DIR}/pb_encode.h"
    DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)
