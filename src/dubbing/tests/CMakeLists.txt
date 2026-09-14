#
# Audacity: A Digital Audio Editor
#

set(MODULE_TEST dubbing_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/environment.cpp

    ${CMAKE_CURRENT_LIST_DIR}/dubbingdomain_tests.cpp

    ${CMAKE_CURRENT_LIST_DIR}/dubbingimport_tests.cpp

    # DropController линкуется из importer.lib (importFromSystemClipboard);
    # тянуть весь projectscene в тест не нужно — компилируем точечно
    ${CMAKE_SOURCE_DIR}/src/projectscene/view/tracksitemsview/dropcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/projectscene/view/tracksitemsview/dropcontroller.h
    )

set(MODULE_TEST_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_SOURCE_DIR}/src/trackedit          # внутренние заголовки trackedit включаются относительно модуля
    ${CMAKE_SOURCE_DIR}/src/importexport/import
    )

set(MODULE_TEST_LINK
    dubbing
    au3wrap
    au3-project
    au3-project-file-io
    au3-project-history
    au3-track
    au3-strings
    trackedit # реальные Au3TracksInteraction / Au3ProjectHistory + моки
    importer  # реальный Au3Importer (строительный блок importIntoTrack)
    # прямые зависимости заголовков Au3Importer (au3importer.h -> Import.h и др.)
    au3-file-formats
    au3-basic-ui
    au3-import-export
    au3-import-export-modules
    au3-label-track
    au3-math
    au3-numeric-formats
    au3-stretching-sequence
    au3-tags
    au3-wave-track
    wxBase
    )

set(MODULE_TEST_DATA_ROOT ${CMAKE_CURRENT_LIST_DIR})

include(SetupGTest)
