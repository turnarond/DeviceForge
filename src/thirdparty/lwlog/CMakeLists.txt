cmake_minimum_required(VERSION 3.12)

# MODULE_DESC: Lightweight logging library (multi-level, hot-reload, date-based rotation)
PROJECT(lwlog VERSION 1.4.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# include(../../CMakeCommonLib) - removed for standalone build

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL  "3.31")
    cmake_policy(SET CMP0177 NEW) 
endif ()

include_directories(
    ${LW_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${LW_COMMON_DIR}
)


link_directories(
    ${LW_LIB_DIR}
)

## Declare a C++ library
add_library(${PROJECT_NAME} SHARED
    src/lwlog.cpp
    src/lwlog_c.cpp
    src/logger_impl.cpp
    src/pattern_formatter.cpp
    src/console_appender.cpp
    src/file_appender.cpp
    src/log_config_reader.cpp
)

if (${SYSTEM_NAME} MATCHES "qnx")
	if (${SYSTEM_NAME} MATCHES "aarch64")
	include_directories(
		${LW_COMMON_DIR}
		/opt/qnx700/target/qnx7/usr/include/c++/5.4.0
		/opt/qnx700/target/qnx7/usr/include/c++/5.4.0/aarch64-unknown-nto-qnx7.0.0
	)
	elseif(${SYSTEM_NAME} MATCHES "x86_64")
	include_directories(
		${LW_COMMON_DIR}
		/opt/qnx700/target/qnx7/usr/include/c++/5.4.0
		/opt/qnx700/target/qnx7/usr/include/c++/5.4.0/x86_64-pc-nto-qnx7.0.0
	)
	endif()
    target_link_libraries(${PROJECT_NAME}
        lwcomm
        tinyxml2
    )
elseif (${SYSTEM_NAME} MATCHES "android")

    target_link_libraries(${PROJECT_NAME}
        lwcomm
        tinyxml2
    )
elseif (${SYSTEM_NAME} MATCHES "sylixos")
    set(CMAKE_CXX_STANDARD 11)  
    set(CMAKE_CXX_STANDARD_REQUIRED True) 

    target_link_libraries(${PROJECT_NAME}
        lwcomm
        tinyxml2
        ${TARGET_LIBRARIES}
    )
elseif (${SYSTEM_NAME} MATCHES "windows")
    target_link_libraries(${PROJECT_NAME}
        lwcomm
        tinyxml2
        ${TARGET_LIBRARIES}
    )
else ()
    target_link_libraries(${PROJECT_NAME}
        pthread
        lwcomm
        tinyxml2
    )
endif()


install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

## Mark cpp header files for installation
install(FILES  "${CMAKE_CURRENT_LIST_DIR}/lwlog.h"
  DESTINATION ${LW_INCLUDE_DIR}/${PROJECT_NAME}
)

# 测试配置
if (${BUILD_TEST})
   find_package(GTest REQUIRED)
   include_directories(${GTEST_INCLUDE_DIRS})

# 单元测试
add_executable(unit_test test/unit_test.cpp)
target_link_libraries(unit_test
    ${PROJECT_NAME}
    ${GTEST_LIBRARIES}
    pthread
)

# 集成测试
add_executable(integration_test test/integration_test.cpp)
target_link_libraries(integration_test
    ${PROJECT_NAME}
    ${GTEST_LIBRARIES}
    pthread
)

# 边界测试
add_executable(boundary_test test/boundary_test.cpp)
target_link_libraries(boundary_test
    ${PROJECT_NAME}
    ${GTEST_LIBRARIES}
    pthread
)

# 注册测试
enable_testing()
gtest_discover_tests(unit_test)
gtest_discover_tests(integration_test)
gtest_discover_tests(boundary_test)
endif()