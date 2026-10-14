# ---- nucleo: CAD + CAM + simulacao + importadores (sem widgets) -------------
file(GLOB_RECURSE CNC_CORE_SOURCES CONFIGURE_DEPENDS
     geo/*.cpp cad/*.cpp cam/*.cpp sim/*.cpp io/*.cpp utils/*.cpp)
add_library(cnc_core STATIC ${CNC_CORE_SOURCES})
target_include_directories(cnc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cnc_core PUBLIC GEOS_USE_ONLY_R_API)
target_link_libraries(cnc_core PUBLIC Qt6::Core Qt6::Gui Qt6::PrintSupport cnc_json cnc_clipper GEOS::geos_c)
if(CNC_HAVE_MUPDF)
    target_compile_definitions(cnc_core PUBLIC CNC_HAVE_MUPDF=1)
    target_include_directories(cnc_core PRIVATE "${CNC_MUPDF_ROOT}/include")
    target_link_libraries(cnc_core PUBLIC "${CNC_MUPDF_LIBRARY}")
else()
    message(WARNING "MuPDF nao encontrado: importacao PDF nativa desativada")
endif()

# ---- interface grafica: tema, viewport, ferramentas de desenho, paineis, janela --------
file(GLOB_RECURSE CNC_GUI_SOURCES CONFIGURE_DEPENDS ui/*.cpp viewport/*.cpp tools/*.cpp app/*.cpp)
list(FILTER CNC_GUI_SOURCES EXCLUDE REGEX "app/main\.cpp$")
add_library(cnc_gui STATIC ${CNC_GUI_SOURCES})
target_link_libraries(cnc_gui PUBLIC cnc_core Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
                      Qt6::PrintSupport Qt6::Svg)
if(CNC_HAVE_VTK)
    target_compile_definitions(cnc_gui PUBLIC CNC_HAVE_VTK=1)
    target_link_libraries(cnc_gui PUBLIC ${VTK_LIBRARIES})
    vtk_module_autoinit(TARGETS cnc_gui MODULES ${VTK_LIBRARIES})
endif()

# Os icones, figuras e o logo (copiados do app em Python) vao DENTRO do executavel: ":/assets/...".
file(GLOB_RECURSE CNC_ASSET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/*")
qt_add_resources(cnc_gui cnc_assets PREFIX "/assets" BASE "${CMAKE_SOURCE_DIR}/assets"
                 FILES ${CNC_ASSET_FILES})

# ---- aplicativo ----------------------------------------------------------------------
if(CNC_BUILD_APP)
    add_executable(cnccam WIN32 app/main.cpp)
    set_target_properties(cnccam PROPERTIES OUTPUT_NAME "CNC-CAM")
    target_link_libraries(cnccam PRIVATE cnc_gui)
    if(CNC_HAVE_MUPDF)
        add_custom_command(TARGET cnccam POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CNC_MUPDF_DLL}" "$<TARGET_FILE_DIR:cnccam>")
    endif()
    if(WIN32)
        # Make the generated executable launchable by double-clicking it in
        # Explorer, just like the Python distribution.  The build environment
        # puts Qt on PATH for scripts/run.bat, but Explorer does not inherit
        # that setup, so deploy Qt DLLs and plugins beside CNC-CAM.exe.
        find_program(CNC_WINDEPLOYQT
            NAMES windeployqt.exe windeployqt6.exe
            HINTS "${CMAKE_PREFIX_PATH}" "${Qt6_DIR}/../../../bin"
            PATH_SUFFIXES bin)
        if(CNC_WINDEPLOYQT)
            add_custom_command(TARGET cnccam POST_BUILD
                COMMAND "${CNC_WINDEPLOYQT}"
                    --release --no-translations --no-system-d3d-compiler
                    "$<TARGET_FILE:cnccam>"
                COMMENT "Deploying Qt runtime beside CNC-CAM.exe")
        else()
            message(WARNING "windeployqt nao encontrado; CNC-CAM.exe exigira Qt no PATH")
        endif()
        if(CNC_HAVE_VTK)
            add_custom_command(TARGET cnccam POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:cnccam>"
                    $<TARGET_RUNTIME_DLLS:cnccam>
                COMMAND_EXPAND_LISTS
                COMMENT "Deploying VTK runtime beside CNC-CAM.exe")
        endif()
    endif()
endif()
