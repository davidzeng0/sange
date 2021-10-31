cmake_minimum_required(VERSION 3.0.0)

project(sange CXX)

include_directories(${CMAKE_JS_INC})
include_directories(${NODE_ADDON_INC})

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSANGE_DEBUG")
endif()

add_library(sange SHARED
	"src/message.cpp"
	"src/wrapper.cpp"
	"src/player.cpp"
	"src/addon.cpp"
)

target_link_libraries(sange avformat avcodec avutil avfilter uv opus ${CMAKE_JS_LIB})
set_target_properties(sange PROPERTIES PREFIX "" SUFFIX ".node")