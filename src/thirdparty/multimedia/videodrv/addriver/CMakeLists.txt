cmake_minimum_required(VERSION 2.6)

############FOR_MODIFIY_BEGIN#######################
#Setting Project Name
PROJECT (addriver)

INCLUDE($ENV{CVSRCDIR}CMakeCommon)

#Setting Source Files
SET(SRCS ${SRCS}  ADDriver.cpp ADMatrixDriver.cpp)

#Setting Target Name (executable file name | library name)
SET(TARGET_NAME  addriver)
#Setting library type used when build a library
#STATIC/SHARED
SET(LIB_TYPE SHARED)

SET(LINK_LIBS tinyxml ACE cvcomm cvlog cvlogimpl)

##############################################################################################################
# Add this to Resolve "relocations remain against allocatable but non-writable sections and relocation error"#
##############################################################################################################
INCLUDE($ENV{CVSRCDIR}CMakeCommonLib)