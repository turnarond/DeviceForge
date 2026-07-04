############FOR_MODIFIY_BEGIN#######################
#Setting Project Name
PROJECT (videodblibrary)
INCLUDE($ENV{CVSRCDIR}CMakeCommon)

#Setting Source Files
SET(SRCS ${SRCS} VideoDBLibrary.cpp)

#Setting Target Name (executable file name | library name)
SET(TARGET_NAME videodblibrary)
#Setting library type used when build a library
SET(LIB_TYPE SHARED)

SET(LINK_LIBS ACE cvcomm cvlog cppsqlite sqlite3 cvlogimpl)

############FOR_MODIFIY_END#########################

INCLUDE($ENV{CVSRCDIR}CMakeCommonLib)