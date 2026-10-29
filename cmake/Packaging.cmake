# CPack configuration: installers for Windows and Linux.
#
#   Windows : cpack -G NSIS   (setup .exe)      cpack -G ZIP  (portable)
#   Linux   : cpack -G DEB    cpack -G RPM      cpack -G TGZ
#   AppImage: packaging/linux/build-appimage.sh
#
# Run from the build directory after `cmake --build . --config Release`.

set(CPACK_PACKAGE_NAME "FastCAE")
set(CPACK_PACKAGE_VENDOR "Trần Minh Tâm")
# TODO: thay email ví dụ bằng email thật của tác giả trước khi phát hành
set(CPACK_PACKAGE_CONTACT "Trần Minh Tâm <tranminhtam@example.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "FastCAE - open source CAE: FEM, heat transfer, CFD, CAD import")
set(CPACK_PACKAGE_DESCRIPTION
    "FastCAE is an open source CAE application by Trần Minh Tâm (MIT License). It imports CAD (STEP/IGES/BREP),\
 generates tetrahedral meshes, solves structural, thermal and fluid problems and visualises results.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "FastCAE")
set(CPACK_PACKAGE_FILE_NAME "FastCAE-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

if(FASTCAE_GUI_ENABLED)
    set(CPACK_PACKAGE_EXECUTABLES "fastcae" "FastCAE")
endif()

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "FastCAE ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "FastCAE")
    set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_NSIS_MODIFY_PATH ON)                       # optional: add bin\ to PATH (CLI)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/fastcae.ico")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/fastcae.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/fastcae.ico")
    endif()
    # Associate *.fcae case files with FastCAE.
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKCR '.fcae' '' 'FastCAE.Case'
        WriteRegStr HKCR 'FastCAE.Case' '' 'FastCAE case file'
        WriteRegStr HKCR 'FastCAE.Case\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\fastcae.exe\\\" \\\"%1\\\"'")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegKey HKCR '.fcae'
        DeleteRegKey HKCR 'FastCAE.Case'")
    # Optional MSI via WiX:  cpack -G WIX
    set(CPACK_WIX_UPGRADE_GUID "7B3C1E52-6F0A-4D55-9A47-3C1B9E0F6A21")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "FastCAE")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
endif()

include(CPack)
