cmake_minimum_required(VERSION 3.24)
project(CncCam VERSION 0.1.0
        DESCRIPTION "CNC CAM - porte do app Python para C++/Qt"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Um unico perfil de CRT (/MD) para tudo: o Qt e o GEOS pre-compilados sao Release.
# RelWithDebInfo = otimizado + PDB; serve para depurar sem trocar de runtime.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(CNC_BUILD_TESTS "Compila os testes" ON)
option(CNC_BUILD_APP   "Compila o aplicativo" ON)
set(CNC_VTK_ROOT "" CACHE PATH "Diretorio CMake do SDK do VTK (opcional)")
set(CNC_TEST_PLATFORM "windows" CACHE STRING "Plataforma Qt dos testes (windows | offscreen)")

# MuPDF e usado pelo importador PDF nativo. O caminho padrao aponta para a
# distribuicao de desenvolvimento que ja acompanha o ambiente local; ele pode
# ser substituido com -DCNC_MUPDF_ROOT=... em outra maquina.
set(CNC_MUPDF_ROOT "${CMAKE_SOURCE_DIR}/../CNC-CAM/venv/Lib/site-packages/pymupdf/mupdf-devel"
    CACHE PATH "Raiz do MuPDF C++")
set(CNC_MUPDF_LIBRARY "${CNC_MUPDF_ROOT}/lib/mupdfcpp64.lib")
set(CNC_MUPDF_DLL "${CNC_MUPDF_ROOT}/../mupdfcpp64.dll")
set(CNC_HAVE_MUPDF OFF)
if(EXISTS "${CNC_MUPDF_ROOT}/include/mupdf/classes.h" AND
   EXISTS "${CNC_MUPDF_LIBRARY}" AND EXISTS "${CNC_MUPDF_DLL}")
    set(CNC_HAVE_MUPDF ON)
endif()

if(MSVC)
    # Keep Ninja's byte prefix consistent with MSVC's actual language pack.
    # PT-only Build Tools fall back to Portuguese even when VSLANG=1033 is set;
    # CMake's OEM-codepage detection can otherwise cache a mojibake prefix and
    # silently miss all header dependencies.
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file: ")
    get_filename_component(_cnc_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${_cnc_msvc_bin}/1046/clui.dll" AND NOT EXISTS "${_cnc_msvc_bin}/1033/clui.dll")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "Observação: incluindo arquivo: ")
    endif()
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc /external:W0)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets PrintSupport Svg Test)
find_package(GEOS REQUIRED CONFIG)

# A simulacao 3D usa VTK quando o SDK esta disponivel. O fallback OpenGL continua
# compilavel em maquinas que ainda nao instalaram o SDK.
if(CNC_VTK_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${CNC_VTK_ROOT}")
endif()
find_package(VTK 9.7 QUIET COMPONENTS
    CommonCore CommonDataModel CommonColor CommonExecutionModel
    FiltersCore FiltersSources RenderingCore RenderingOpenGL2)
if(VTK_FOUND)
    set(CNC_HAVE_VTK ON)
    message(STATUS "VTK encontrado: ${VTK_VERSION}")
else()
    set(CNC_HAVE_VTK OFF)
    message(STATUS "VTK nao encontrado: usando viewport OpenGL de compatibilidade")
endif()

# -- terceiros vendorizados ------------------------------------------------
add_library(cnc_clipper STATIC third_party/clipper/clipper.cpp)
target_include_directories(cnc_clipper PUBLIC third_party/clipper)
# o fonte do Clipper tem acentos Latin-1 em comentarios (C4828); nao e codigo nosso
target_compile_options(cnc_clipper PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W1 /wd4828>)

add_library(cnc_json INTERFACE)
target_include_directories(cnc_json INTERFACE third_party)   # <nlohmann/json.hpp>

add_library(cnc_doctest INTERFACE)
target_include_directories(cnc_doctest INTERFACE third_party/doctest)   # <doctest.h>

add_subdirectory(src)
if(CNC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

