#
# Audacity: A Digital Audio Editor
#

declare_module(dubbing)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/dubbingmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dubbingmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/dubbingtypes.h

    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingproject.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingproject.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingstateextension.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingstateextension.h
    )

set(MODULE_LINK
    au3-project
    au3-project-file-io
    au3-project-history
    au3-xml
    wxBase # au3 public headers pull in wx headers (same as au3wrap)
    )

set(MODULE_USE_PCH OFF)

setup_module()

if (AU_BUILD_DUBBING_TESTS)
    add_subdirectory(tests)
endif()
