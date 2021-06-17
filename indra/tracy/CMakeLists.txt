# -*- cmake -*-

project(tracy)

include(00-Common)
include(OpenGL)

include_directories(
    ${TRACY_INCLUDE_DIRS}
    )
include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    )

set(tracy_SOURCE_FILES
    TracyClient.cpp
    )
    
set(tracy_HEADER_FILES
    CMakeLists.txt

    Tracy.hpp
    TracyOpenGL.hpp
    )

set_source_files_properties(${tracy_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND tracy_SOURCE_FILES ${tracy_HEADER_FILES})

if (BUILD_HEADLESS)
  add_library (tracyheadless
    ${tracy_SOURCE_FILES}
    )

#  set_property(TARGET tracyheadless
#    PROPERTY COMPILE_DEFINITIONS LL_MESA=1 LL_MESA_HEADLESS=1
#      )

  target_link_libraries(tracyheadless
    ${LLCOMMON_LIBRARIES}
    ${OPENGL_HEADLESS_LIBRARIES})

endif (BUILD_HEADLESS)

add_library (tracy ${tracy_SOURCE_FILES})

if (SDL_FOUND)
  set_property(TARGET tracy
    PROPERTY COMPILE_DEFINITIONS LL_SDL=1
    )
endif (SDL_FOUND)

# Libraries on which this library depends, needed for Linux builds
# Sort by high-level to low-level
target_link_libraries(tracy 
    ${LLCOMMON_LIBRARIES}
    ${OPENGL_LIBRARIES})

