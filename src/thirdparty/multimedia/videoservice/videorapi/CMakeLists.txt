############FOR_MODIFIY_BEGIN#######################
#Setting Project Name
PROJECT (videorapi)
INCLUDE($ENV{CVSRCDIR}CMakeCommon)

#Setting Source Files
SET(SRCS ${SRCS}  GlobalManager.cpp NetWrapper.cpp VideoRAPI.cpp)

#Setting Target Name (executable file name | library name)
SET(TARGET_NAME  videorapi )
#Setting library type used when build a library
#STATIC/SHARED
SET(LIB_TYPE SHARED)

SET(LINK_LIBS ACE netqueue cvcomm cvlog videoprotoparse)

############FOR_MODIFIY_END#########################
INCLUDE($ENV{CVSRCDIR}CMakeCommonLib)
