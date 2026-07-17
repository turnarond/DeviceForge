cmake_minimum_required(VERSION 2.6)
PROJECT(multimedia)
SUBDIRS(videoservice dwservice led ledapi)
IF(${CMAKE_SYSTEM_NAME} MATCHES Windows)
	SUBDIRS(dwdrv leddrv videodrv)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES Windows)

