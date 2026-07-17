cmake_minimum_required(VERSION 3.12)
project(crud)

set(CMAKE_CXX_STANDARD 11)

include(../../CMakeCommonLib)

link_directories(
    ${LW_LIB_DIR}
)

add_library(crud-lib
    src/controller/StaticController.hpp
    src/controller/UserController.hpp
    src/db/UserDb.hpp
    src/dto/PageDto.hpp
    src/dto/StatusDto.hpp
    src/dto/UserDto.hpp
    src/service/UserService.cpp
    src/service/UserService.hpp
    src/AppComponent.hpp
    src/DatabaseComponent.hpp
    src/SwaggerComponent.hpp
    src/ErrorHandler.cpp
    src/ErrorHandler.hpp
)

install(TARGETS crud-lib
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

## include directories
if(${SYSTEM_NAME} MATCHES "sylixos")
target_include_directories(crud-lib
    PUBLIC src
    ${LW_INCLUDE_DIR}
    ${SYLIXOS_BASE_PATH}/libsqlite3/sqlite3
)
else()
target_include_directories(crud-lib
    PUBLIC src
    ${LW_INCLUDE_DIR}
)
endif() 

if(${SYSTEM_NAME} MATCHES "sylixos")
    SET(OATPP_ADD_LINK_LIBS
        stdc++
        dsohandle
        vpmpdm
        cextern
    )
endif()

target_link_libraries(crud-lib
    # Oat++
    PUBLIC oatpp
    PUBLIC oatpp-swagger
    PUBLIC oatpp-sqlite
    oatpp
    vsoa_dto
    lwcomm
    ${OATPP_ADD_LINK_LIBS}
)

if(CMAKE_SYSTEM_NAME MATCHES Linux)
    find_package(Threads REQUIRED)
    target_link_libraries(crud-lib INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
endif()

## add executables

add_executable(crud-exe src/App.cpp)
target_link_libraries(crud-exe
    crud-lib
    oatpp
    ${OATPP_ADD_LINK_LIBS}
)

install(TARGETS crud-exe
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

add_executable(crud-test
    test/tests.cpp
    test/app/TestClient.hpp
    test/app/TestDatabaseComponent.hpp
    test/app/TestComponent.hpp
    test/UserControllerTest.hpp
    test/UserControllerTest.cpp
)

target_link_libraries(
    crud-test
    crud-lib
    oatpp
    oatpp-test
    ${OATPP_ADD_LINK_LIBS}
)

enable_testing()
# add_test(project-tests crud-test)

install(TARGETS crud-test
    ARCHIVE DESTINATION ${LW_LIB_DIR}
    LIBRARY DESTINATION ${LW_LIB_DIR}
    RUNTIME DESTINATION ${LW_BIN_DIR}
)

file(GLOB SQL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/sql/*.sql")
install(
    FILES ${SQL_FILES}
    DESTINATION "${LW_DATA_DIR}"
)
