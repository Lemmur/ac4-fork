#
# Audacity: A Digital Audio Editor
#

declare_module(dubbing)

set(MODULE_QRC dubbing.qrc)

set(MODULE_SRC
    ${CMAKE_CURRENT_LIST_DIR}/dubbingmodule.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dubbingmodule.h
    ${CMAKE_CURRENT_LIST_DIR}/dubbingtypes.h
    ${CMAKE_CURRENT_LIST_DIR}/dubbingconfiguration.h
    ${CMAKE_CURRENT_LIST_DIR}/idubbingproject.h

    ${CMAKE_CURRENT_LIST_DIR}/import/dubbingjsonreader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/import/dubbingjsonreader.h
    ${CMAKE_CURRENT_LIST_DIR}/import/dubbingimportservice.cpp
    ${CMAKE_CURRENT_LIST_DIR}/import/dubbingimportservice.h

    ${CMAKE_CURRENT_LIST_DIR}/panel/lineslistmodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/panel/lineslistmodel.h
    ${CMAKE_CURRENT_LIST_DIR}/panel/linesfiltermodel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/panel/linesfiltermodel.h
    ${CMAKE_CURRENT_LIST_DIR}/panel/lineworkspacecontroller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/panel/lineworkspacecontroller.h

    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingproject.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingproject.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingstateextension.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingstateextension.h
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingservice.cpp
    ${CMAKE_CURRENT_LIST_DIR}/internal/dubbingservice.h
    )

set(MODULE_QML_IMPORT ${CMAKE_CURRENT_LIST_DIR}/qml)

set(MODULE_LINK
    au3wrap               # DomAccessor, Au3Project
    context               # IGlobalContext
    trackedit             # ITracksInteraction, IProjectHistory, ISelectionController
    playback              # IPlaybackController (позиционирование при двойном клике)
    importer              # IImporter (строительный блок importIntoTrack)
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
