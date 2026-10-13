# ---- nucleo: CAD + CAM + simulacao + importadores (sem widgets) -------------
file(GLOB_RECURSE CNC_CORE_SOURCES CONFIGURE_DEPENDS
     geo/*.cpp cad/*.cpp cam/*.cpp sim/*.cpp io/*.cpp utils/*.cpp)
add_library(cnc_core STATIC ${CNC_CORE_SOURCES})
target_include_directories(cnc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cnc_core PUBLIC GEOS_USE_ONLY_R_API)
target_link_libraries(cnc_core PUBLIC Qt6::Core Qt6::Gui cnc_json cnc_clipper GEOS::geos_c)
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
endif()
