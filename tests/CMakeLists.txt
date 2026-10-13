file(GLOB CNC_TEST_SOURCES CONFIGURE_DEPENDS *.cpp)
add_executable(cnc_tests ${CNC_TEST_SOURCES})
target_link_libraries(cnc_tests PRIVATE cnc_gui cnc_doctest Qt6::Widgets Qt6::Test)
target_compile_definitions(cnc_tests PRIVATE
    CNC_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CNC_UNDER_TEST=1)
add_test(NAME cnc_tests COMMAND cnc_tests)
set_tests_properties(cnc_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=${CNC_TEST_PLATFORM}")
if(CNC_HAVE_MUPDF)
    add_custom_command(TARGET cnc_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CNC_MUPDF_DLL}" "$<TARGET_FILE_DIR:cnc_tests>")
endif()
