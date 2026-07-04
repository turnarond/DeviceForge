############FOR_MODIFIY_BEGIN#######################
#Setting Project Name
PROJECT (videoauth)
INCLUDE($ENV{CVSRCDIR}CMakeCommon)

SET(CMAKE_INCLUDE_CURRENT_DIR ON)

#Setting Source Files
SET(SRCS ${SRCS}  ObjLockHandle.cpp VideoAuth.cpp)

#Setting Target Name (executable file name | library name)
SET(TARGET_NAME  videoauth)
#Setting library type used when build a library
#STATIC/SHARED
SET(LIB_TYPE SHARED)

SET(LINK_LIBS amsvrlib cvcomm cvlog amrapi amapi)

############FOR_MODIFIY_END#########################
INCLUDE($ENV{CVSRCDIR}CMakeCommonLib)
