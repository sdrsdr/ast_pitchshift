project(ast_pitchshift)
cmake_minimum_required(VERSION 2.6)

set ( AFILTER_SRC "../afilter" CACHE PATH "location of afilter files" )
set ( ASTERISK_INCLUDES "/usr/include" CACHE PATH "location of asterisk.h" )
set ( ASTERISK_MODULES "/usr/lib/asterisk/modules/" CACHE PATH "where to install the asterisk application" )

add_definitions(-Wall)

add_subdirectory(${AFILTER_SRC} "${CMAKE_CURRENT_BINARY_DIR}/libpitchshift")
set_target_properties(pitchshift PROPERTIES COMPILE_FLAGS "-fPIC")

include_directories (${AFILTER_SRC} ${ASTERISK_INCLUDES})
add_library (app_pitchshift SHARED app_pitchshift.c)

set_target_properties(app_pitchshift PROPERTIES PREFIX "" OUTPUT_NAME app_pitchshift)

target_link_libraries(app_pitchshift pitchshift fftw3)
install(TARGETS app_pitchshift DESTINATION ${ASTERISK_MODULES} PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)