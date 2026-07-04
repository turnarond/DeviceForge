cmake_minimum_required(VERSION 2.6)

PROJECT (videoservice)

INCLUDE($ENV{CVSRCDIR}CMakeCommon)

############FOR_MODIFIY_BEGIN#######################
#Setting Source Files
SET(SRCS ${SRCS} DevCtrlTask.cpp InfoMgrCmd.cpp TimerTask.cpp MsgDispatchTask.cpp NetWrapper.cpp RemoteProcTask.cpp VideoCfg.cpp VideoService.cpp)

#Setting Target Name (executable file name | library name)
SET(TARGET_NAME videoservice)
#Setting library type used when build a library
SET(LIB_TYPE STATIC)

SET(LINK_LIBS ACE cvcomm netqueue tinyxml servicebase licverify videodblibrary  videoprotoparse cppsqlite sqlite3 cvlog rmapi videoauth amapi amrapi amsvrlib intl iconv)
SET(LINK_LIBS ${LINK_LIBS} shmqueue)

IF(COMPILE_WITHOUT_EPASS_LIC)

ELSE(COMPILE_WITHOUT_EPASS_LIC)
#	MESSAGE("Link With License")
	SET(LINK_LIBS ${LINK_LIBS} License)
ENDIF(COMPILE_WITHOUT_EPASS_LIC)

IF(UNIX)
	if( CMAKE_SIZEOF_VOID_P EQUAL 4 )
		IF(CMAKE_SYSTEM MATCHES "Linux")
			SET(LINK_LIBS ${LINK_LIBS} SentinelKeys32)
		ENDIF(CMAKE_SYSTEM MATCHES "Linux")
	endif( CMAKE_SIZEOF_VOID_P EQUAL 4 )
	#SET(LINK_LIBS ${LINK_LIBS} rt pthread nsl)
	IF(HPUX)
		SET(LINK_LIBS ${LINK_LIBS} pthread)
	ENDIF(HPUX)
	
	IF(CMAKE_SYSTEM MATCHES "SunOS.*")
		SET(LINK_LIBS ${LINK_LIBS} socket)
	ENDIF(CMAKE_SYSTEM MATCHES "SunOS.*")
ELSE(UNIX)
	SET(LINK_LIBS ${LINK_LIBS} exceptionreport dbghelp)
ENDIF(UNIX)

############FOR_MODIFIY_END#########################
INCLUDE($ENV{CVSRCDIR}CMakeCommonExec)
IF(MSVC)
	if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
		set_target_properties(${TARGET_NAME} PROPERTIES STATIC_LIBRARY_FLAGS "/machine:x64")
	endif( CMAKE_SIZEOF_VOID_P EQUAL 8 )	
ENDIF(MSVC)
