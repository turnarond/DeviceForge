############FOR_MODIFIY_BEGIN#######################
#Setting Project Name
PROJECT (videoprotoparse)
INCLUDE($ENV{CVSRCDIR}CMakeCommon)

#Setting Source Files
SET(SRCS ${SRCS} VideoProtoPacker.cpp VideoProtoParser.cpp)

#Setting Target Name (executable file name | library name)
SET(TARGET_NAME videoprotoparse)
#Setting library type used when build a library
SET(LIB_TYPE STATIC)

SET(LINK_LIBS )

############FOR_MODIFIY_END#########################
INCLUDE($ENV{CVSRCDIR}CMakeCommonLib)

IF(MSVC)
	if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
		set_target_properties(${TARGET_NAME} PROPERTIES STATIC_LIBRARY_FLAGS "/machine:x64")
	endif( CMAKE_SIZEOF_VOID_P EQUAL 8 )
ENDIF(MSVC)
