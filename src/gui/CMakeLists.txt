set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(fastcae WIN32
    main_gui.cpp
    MainWindow.cpp
    MainWindow.hpp
    Viewport.cpp
    Viewport.hpp
    ParametricDialog.cpp
    ParametricDialog.hpp
    SeriesPlotDialog.cpp
    SeriesPlotDialog.hpp
    ParamEditDialog.cpp
    ParamEditDialog.hpp
    AboutDialog.cpp
    ScriptDock.cpp
    ScriptDock.hpp
    ComponentLibraryDialog.cpp
    ComponentLibraryDialog.hpp
    FrameEditDialogs.cpp
    FrameEditDialogs.hpp
    AboutDialog.hpp
    HelpDialog.cpp
    HelpDialog.hpp
    BcDialog.cpp
    BcDialog.hpp
    SolverManagerDialog.cpp
    SolverManagerDialog.hpp
    Ribbon.cpp
    Ribbon.hpp
    Icons.cpp
    Icons.hpp
    Theme.hpp
    resources.qrc)

target_link_libraries(fastcae PRIVATE fastcae_core Qt6::Widgets Qt6::OpenGLWidgets)
target_compile_definitions(fastcae PRIVATE FASTCAE_DEV_PYTHON="${CMAKE_SOURCE_DIR}/python")
if(WIN32)
    target_sources(fastcae PRIVATE ${CMAKE_SOURCE_DIR}/packaging/windows/fastcae.rc)
endif()

install(TARGETS fastcae RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Windows: copy the Qt DLLs/plugins (and the software OpenGL fallback) next to the
# exe at install time. Linux bundles Qt through packaging/linux/build-appimage.sh.
if(WIN32 AND Qt6_VERSION VERSION_GREATER_EQUAL 6.5 AND COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET fastcae
        OUTPUT_SCRIPT FASTCAE_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${FASTCAE_DEPLOY_SCRIPT})
endif()
