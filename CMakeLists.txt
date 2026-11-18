cmake_minimum_required(VERSION 3.27)
project(hikvision_decrypter LANGUAGES CXX)

option(HIKVISION_BUILD_GUI "Build the native desktop application" ON)
include(CTest)
option(HIKVISION_GUI_SMOKE_TESTS "Build hidden native GUI integration tests (requires a display)" OFF)

function(hikvision_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /permissive- /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

add_library(hikvision_core STATIC
    hikvision-decrypter/AES.cpp
    hikvision-decrypter/hikvision.cpp
    hikvision-decrypter/preview.cpp)
target_include_directories(hikvision_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/hikvision-decrypter")
target_compile_features(hikvision_core PUBLIC cxx_std_17)
hikvision_warnings(hikvision_core)

if(HIKVISION_BUILD_GUI)
    find_package(wxWidgets 3.2.12 REQUIRED COMPONENTS core base)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    add_library(hikvision_gui STATIC hikvision-decrypter/cMain.cpp)
    target_link_libraries(hikvision_gui PUBLIC hikvision_core wxWidgets::wxWidgets PRIVATE Threads::Threads)
    hikvision_warnings(hikvision_gui)

    add_executable(hikvision-decrypter WIN32 hikvision-decrypter/cApp.cpp)
    target_link_libraries(hikvision-decrypter PRIVATE hikvision_gui)
    hikvision_warnings(hikvision-decrypter)

    if(BUILD_TESTING AND HIKVISION_GUI_SMOKE_TESTS)
        add_executable(gui-preview-smoke tests/gui_preview_smoke.cpp)
        target_link_libraries(gui-preview-smoke PRIVATE hikvision_gui Threads::Threads)
        hikvision_warnings(gui-preview-smoke)
        add_dependencies(gui-preview-smoke hikvision-decrypter)
        add_test(NAME gui-preview-integration COMMAND gui-preview-smoke)
        add_test(NAME gui-preview-chinese COMMAND gui-preview-smoke --zh)
        set_tests_properties(gui-preview-integration gui-preview-chinese PROPERTIES TIMEOUT 45)
    endif()
    if(WIN32)
        enable_language(RC)
        target_sources(hikvision-decrypter PRIVATE resources/app.rc)
        # FindwxWidgets exposes import libraries, not runtime DLL targets.
        # Copy just base/core beside the executable for local native builds.
        file(GLOB wx_release_dlls "${wxWidgets_LIB_DIR}/wxbase*u_vc*.dll" "${wxWidgets_LIB_DIR}/wxmsw*u_core_vc*.dll")
        file(GLOB wx_debug_dlls "${wxWidgets_LIB_DIR}/wxbase*ud_vc*.dll" "${wxWidgets_LIB_DIR}/wxmsw*ud_core_vc*.dll")
        if(wx_release_dlls AND wx_debug_dlls)
            add_custom_command(TARGET hikvision-decrypter POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<IF:$<CONFIG:Debug>,${wx_debug_dlls},${wx_release_dlls}>"
                    "$<TARGET_FILE_DIR:hikvision-decrypter>"
                COMMAND_EXPAND_LISTS VERBATIM)
        elseif(wx_release_dlls)
            add_custom_command(TARGET hikvision-decrypter POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${wx_release_dlls}
                    "$<TARGET_FILE_DIR:hikvision-decrypter>" VERBATIM)
        endif()
        if(MSVC)
            target_link_options(hikvision-decrypter PRIVATE /MANIFEST:NO)
        endif()
    endif()
endif()

if(BUILD_TESTING)
    add_executable(core-tests tests/core_tests.cpp)
    target_link_libraries(core-tests PRIVATE hikvision_core)
    hikvision_warnings(core-tests)
    add_test(NAME core-regression COMMAND core-tests)
    add_executable(preview-tests tests/preview_tests.cpp)
    target_link_libraries(preview-tests PRIVATE hikvision_core)
    hikvision_warnings(preview-tests)
    add_test(NAME preview-regression COMMAND preview-tests)
endif()
