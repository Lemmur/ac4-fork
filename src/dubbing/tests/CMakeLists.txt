#
# Audacity: A Digital Audio Editor
#

set(MODULE_TEST dubbing_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/environment.cpp

    ${CMAKE_CURRENT_LIST_DIR}/dubbingdomain_tests.cpp
    )

set(MODULE_TEST_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/..
    )

set(MODULE_TEST_LINK
    dubbing
    au3wrap
    au3-project
    au3-project-file-io
    au3-project-history
    au3-track
    au3-strings
    wxBase
    )

include(SetupGTest)
