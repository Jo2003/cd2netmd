cmake_minimum_required(VERSION 3.10)

# set the project name and version
project(cd2netmd VERSION 0.2.0)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SOURCES 
	AudioCD_Helpers.cpp
	CAudioCD.cpp
	WinHttpWrapper.cpp
	cd2netmd.cpp
	utils.cpp
)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-W -Wall")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-Os")

SET(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc")
SET(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

add_executable(cd2netmd ${SOURCES})
target_link_libraries(cd2netmd "winhttp")
target_link_libraries(cd2netmd "iconv")
