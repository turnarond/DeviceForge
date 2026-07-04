cmake_minimum_required(VERSION 2.6)

PROJECT (videotimesynchservice)

INCLUDE($ENV{CVSRCDIR}CMakeCommon)

############FOR_MODIFIY_BEGIN#######################
#Setting Source Files
SET(SRCS ${SRCS} NetWrapper.cpp TimerTask.cpp VideoTimeSynchCfg.cpp VideoTimeSynchService.cpp)

#Setting Target Name (executable file name | library name)
SET(TARGET_NAME videotimesynchservice)
#Setting library type used when build a library
SET(LIB_TYPE STATIC)

SET(LINK_LIBS ACE cvcomm netqueue cvlog tinyxml videoprotoparse  rmapi cvrda amapi )

#MESSAGE(${LINK_LIBS})

############FOR_MODIFIY_END#########################
INCLUDE($ENV{CVSRCDIR}CMakeCommonExec)

IF(MSVC)
	if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
		set_target_properties(${TARGET_NAME} PROPERTIES STATIC_LIBRARY_FLAGS "/machine:x64")
	endif( CMAKE_SIZEOF_VOID_P EQUAL 8 )
ENDIF(MSVC)
