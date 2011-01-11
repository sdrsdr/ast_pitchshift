project(ast_pitchshift)
cmake_minimum_required(VERSION 2.8)
set ( AFILTER_INCLUDES "../afilter" CACHE PATH "location of pitchshift.h" )
set ( AFILTER_BUILD "../afilter/build" CACHE PATH "location of libpitchshift.a" )

set ( ASTERISK_INCLUDES "/usr/include" CACHE PATH "location of asterisk.h" )


add_definitions(-Wall)
add_library(pitchshift STATIC IMPORTED)
set_target_properties(pitchshift PROPERTIES IMPORTED_LOCATION ${AFILTER_BUILD})

include_directories (${AFILTER_INCLUDES} ${ASTERISK_INCLUDES})
add_library (app_pitchshift SHARED app_pitchshift.c)
target_link_libraries(app_pitchshift pitchshift)
