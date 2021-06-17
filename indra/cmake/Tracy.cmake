# -*- cmake -*-

include(Variables)
#include(FreeType)
#include(GLH)

set(TRACY_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/tracy
#    ${GLH_INCLUDE_DIR}
    )

set(TRACY_LIBRARIES tracy)

