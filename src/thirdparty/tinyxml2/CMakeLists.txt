cmake_minimum_required(VERSION 3.15)
# MODULE_DESC: Lightweight XML parser (vendored)
project(tinyxml2 LANGUAGES CXX VERSION 7.0.1)

# include(../../CMakeCommonLib) - removed for standalone build

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

add_library(${PROJECT_NAME} SHARED
    tinyxml2.cpp
)

# Define TINYXML2_EXPORT to enable __declspec(dllexport) on Windows
target_compile_definitions(${PROJECT_NAME} PRIVATE TINYXML2_EXPORT)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION ${LW_LIB_DIR}
  LIBRARY DESTINATION ${LW_LIB_DIR}
  RUNTIME DESTINATION ${LW_BIN_DIR}
)

## Mark cpp header files for installation
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/tinyxml2.h"
  DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

