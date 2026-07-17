find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

set(NETRELAY_DIR ${CMAKE_SOURCE_DIR}/src/tools/NetRelayTool)

add_executable(tst_nrec
    NetRelayTool/tst_nrec.cpp
    ${NETRELAY_DIR}/RelayRecorder.cpp
    ${NETRELAY_DIR}/RelayRecording.cpp
    ${NETRELAY_DIR}/RelayPlayer.cpp
)
target_include_directories(tst_nrec PRIVATE
    ${NETRELAY_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_nrec PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME tst_nrec COMMAND tst_nrec)

# 让 ctest 直接运行时能找到 Qt 运行时 DLL（否则 Windows 报 0xc0000135 DLL_NOT_FOUND）
get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION_RELEASE)
if(NOT _qt_core_dll)
    get_target_property(_qt_core_dll Qt6::Core IMPORTED_LOCATION)
endif()
if(_qt_core_dll)
    get_filename_component(_qt_bin_dir "${_qt_core_dll}" DIRECTORY)
    set_tests_properties(tst_nrec PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin_dir}")
endif()
