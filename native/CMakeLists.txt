# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
#
# The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
# Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
# The Original Code is AnyXboxSeries.
# The Original Developer is the Initial Developer.
# The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
#

cmake_minimum_required(VERSION 3.23)
project(wsx_compat LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build with MSVC for Windows x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options(/W4 /permissive-)

add_library(d3d12_xs SHARED d3d12_xs.cpp d3d12_xs.def)
set(WSX_XMEM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xbox2windows")
add_library(xmem SHARED
    "${WSX_XMEM_SOURCE_DIR}/xmem_shim.cpp"
    "${WSX_XMEM_SOURCE_DIR}/xmem_shim.def"
    "${WSX_XMEM_SOURCE_DIR}/era_heap.cpp"
    "${WSX_XMEM_SOURCE_DIR}/era_query.cpp")
target_include_directories(xmem PRIVATE "${WSX_XMEM_SOURCE_DIR}")
find_program(WSX_CARGO_EXECUTABLE cargo)
set(WSX_TOMBOX_XMEM_AVAILABLE FALSE)
if(WSX_CARGO_EXECUTABLE)
    execute_process(
        COMMAND "${WSX_CARGO_EXECUTABLE}" -vV
        OUTPUT_VARIABLE WSX_CARGO_DETAILS
        RESULT_VARIABLE WSX_CARGO_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(WSX_CARGO_RESULT EQUAL 0 AND
       WSX_CARGO_DETAILS MATCHES "host: x86_64-pc-windows-msvc")
        set(WSX_TOMBOX_XMEM_AVAILABLE TRUE)
    else()
        message(STATUS "Skipping TomBox XMem candidate: Cargo host must be x86_64-pc-windows-msvc.")
    endif()
endif()
if(WSX_TOMBOX_XMEM_AVAILABLE)
    set(WSX_TOMBOX_XMEM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tombox-xmem")
    set(WSX_TOMBOX_XMEM_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/tombox-xmem-target")
    set(WSX_TOMBOX_XMEM_CARGO_DLL
        "${WSX_TOMBOX_XMEM_TARGET_DIR}/x86_64-pc-windows-msvc/release/xmem.dll")
    set(WSX_TOMBOX_XMEM_CANDIDATE_DLL
        "${CMAKE_CURRENT_BINARY_DIR}/candidates/tombox-xmem/xmem.dll")
    file(GLOB_RECURSE WSX_TOMBOX_XMEM_SOURCES CONFIGURE_DEPENDS
        "${WSX_TOMBOX_XMEM_ROOT}/src/*.rs"
        "${WSX_TOMBOX_XMEM_ROOT}/tests/*.rs")
    add_custom_command(
        OUTPUT "${WSX_TOMBOX_XMEM_CANDIDATE_DLL}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${WSX_TOMBOX_XMEM_TARGET_DIR}"
            "${WSX_CARGO_EXECUTABLE}" build --offline
            --manifest-path "${WSX_TOMBOX_XMEM_ROOT}/Cargo.toml"
            --target x86_64-pc-windows-msvc --release
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/candidates/tombox-xmem"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${WSX_TOMBOX_XMEM_CARGO_DLL}" "${WSX_TOMBOX_XMEM_CANDIDATE_DLL}"
        DEPENDS "${WSX_TOMBOX_XMEM_ROOT}/Cargo.toml"
                "${WSX_TOMBOX_XMEM_ROOT}/Cargo.lock"
                ${WSX_TOMBOX_XMEM_SOURCES}
        BYPRODUCTS "${WSX_TOMBOX_XMEM_CARGO_DLL}"
        VERBATIM)
    add_custom_target(wsx_xmem_tombox_candidate ALL
        DEPENDS "${WSX_TOMBOX_XMEM_CANDIDATE_DLL}")
    add_executable(wsx_xmem_tombox_smoke xmem_tombox_smoke.cpp)
    add_dependencies(wsx_xmem_tombox_smoke wsx_xmem_tombox_candidate)
endif()
add_library(wsx_pe_map SHARED "${WSX_XMEM_SOURCE_DIR}/pe_map.cpp")
add_library(wsx_graphics_runtime STATIC graphics_probe.cpp)
target_link_libraries(wsx_graphics_runtime PUBLIC d3d12_xs dxgi d3dcompiler)

add_executable(wsx_graphics_probe graphics_probe_main.cpp)
target_link_libraries(wsx_graphics_probe PRIVATE wsx_graphics_runtime user32)
add_executable(wsx_shader_probe shader_probe.cpp shader_compiler.cpp)
target_link_libraries(wsx_shader_probe PRIVATE wsx_graphics_runtime)
add_executable(wsx_startup_trace startup_trace.cpp)
add_executable(wsx_xmem_shim_test xmem_shim_test.cpp)
add_dependencies(wsx_xmem_shim_test xmem)
add_executable(wsx_pe_map_test pe_map_test.cpp)
add_executable(wsx_pe_map_fixture_exe pe_map_fixture_exe.cpp)
add_dependencies(wsx_pe_map_test wsx_pe_map wsx_pe_map_fixture_exe)
target_compile_options(wsx_pe_map_fixture_exe PRIVATE /GS- /Zl)
target_link_options(wsx_pe_map_fixture_exe PRIVATE /NODEFAULTLIB /ENTRY:WsxPeMapFixtureEntry /SUBSYSTEM:CONSOLE)

add_library(wsx_boot_graphics SHARED boot_graphics.cpp)
target_link_libraries(wsx_boot_graphics PRIVATE wsx_graphics_runtime)
add_library(wsx_sample_game SHARED sample_game.cpp window_presenter.cpp)
target_compile_definitions(wsx_sample_game PRIVATE UNICODE _UNICODE)
add_library(wsx_gameinput_input STATIC gameinput_input.cpp)
target_link_libraries(wsx_sample_game PRIVATE dxgi user32 xinput wsx_gameinput_input)
add_executable(wsx_bootstrap bootstrap.cpp)
target_link_libraries(wsx_bootstrap PRIVATE wsx_graphics_runtime)
add_dependencies(wsx_bootstrap wsx_boot_graphics wsx_sample_game)
install(TARGETS d3d12_xs xmem wsx_pe_map wsx_graphics_probe wsx_shader_probe wsx_startup_trace wsx_bootstrap wsx_boot_graphics wsx_sample_game
        RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../Start-SignalRun.cmd" DESTINATION .)
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/../CREDITS.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/../CHANGELOG.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/../CONTRIBUTING.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
        DESTINATION .)
install(FILES "${WSX_XMEM_SOURCE_DIR}/LICENSE"
        DESTINATION licenses RENAME "Xbox2Windows-MIT.txt")
if(WSX_TOMBOX_XMEM_AVAILABLE)
    install(FILES "${WSX_TOMBOX_XMEM_CANDIDATE_DLL}"
            DESTINATION candidates/tombox-xmem)
    install(FILES
            "${WSX_TOMBOX_XMEM_ROOT}/LICENSE-MIT"
            "${WSX_TOMBOX_XMEM_ROOT}/README.md"
            DESTINATION candidates/tombox-xmem)
endif()
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/inspect_xbox_imports.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/inspect_ui_resources.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/rbf0_tree.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/prepare_shader.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/rdna2_scalar.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/shader_container.py"
        DESTINATION tools)
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/BUILD.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/COMPATIBILITY.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/status.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/frame.png"
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/SHADERS.md"
        DESTINATION docs)

include(CTest)
set(WSX_DXC_DIR "" CACHE PATH "Directory with x64 dxcompiler.dll and dxil.dll for optional shader tests")
if(BUILD_TESTING)
    add_executable(wsx_gameinput_mapping_test gameinput_input_test.cpp)
    target_link_libraries(wsx_gameinput_mapping_test PRIVATE wsx_gameinput_input)
    add_test(NAME gameinput_mapping COMMAND wsx_gameinput_mapping_test)
    add_executable(wsx_gameinput_devices_test gameinput_devices_test.cpp)
    target_link_libraries(wsx_gameinput_devices_test PRIVATE wsx_gameinput_input)
    add_test(NAME gameinput_devices COMMAND wsx_gameinput_devices_test)
    add_executable(wsx_root_signature_test root_signature_test.cpp)
    add_dependencies(wsx_root_signature_test d3d12_xs)
    add_test(NAME root_signature COMMAND wsx_root_signature_test "$<TARGET_FILE:d3d12_xs>")
    add_test(NAME graphics_probe COMMAND wsx_graphics_probe)
    add_test(NAME graphics_probe_standard_adapters COMMAND wsx_graphics_probe --legacy-dxgi)
    add_test(NAME graphics_probe_warp COMMAND wsx_graphics_probe --warp)
    add_test(NAME graphics_probe_present COMMAND wsx_graphics_probe --present)
    add_test(NAME graphics_probe_present_warp COMMAND wsx_graphics_probe --warp --present)
    add_test(NAME startup_trace COMMAND wsx_startup_trace --observe-ms 30000 --
             "$<TARGET_FILE:wsx_graphics_probe>" --warp)
    add_test(NAME startup_trace_gameinput COMMAND wsx_startup_trace --observe-ms 30000 --
             "$<TARGET_FILE:wsx_bootstrap>" --warp --module "$<TARGET_FILE:wsx_sample_game>")
    add_test(NAME bootstrap_graphics COMMAND wsx_bootstrap)
    add_test(NAME signal_run COMMAND wsx_bootstrap --sample)
    add_test(NAME signal_run_warp COMMAND wsx_bootstrap --warp --module "$<TARGET_FILE:wsx_sample_game>")
    add_test(NAME xmem_shim COMMAND wsx_xmem_shim_test "$<TARGET_FILE_DIR:wsx_xmem_shim_test>/xmem.dll")
    set_tests_properties(xmem_shim PROPERTIES TIMEOUT 30)
    if(WSX_TOMBOX_XMEM_AVAILABLE)
        add_test(NAME tombox_xmem_candidate
            COMMAND wsx_xmem_tombox_smoke "${WSX_TOMBOX_XMEM_CANDIDATE_DLL}")
        add_test(NAME tombox_xmem_source_tests
            COMMAND "${CMAKE_COMMAND}" -E env
                "CARGO_TARGET_DIR=${WSX_TOMBOX_XMEM_TARGET_DIR}"
                "${WSX_CARGO_EXECUTABLE}" test --offline
                --manifest-path "${WSX_TOMBOX_XMEM_ROOT}/Cargo.toml"
                --target x86_64-pc-windows-msvc --release -- --test-threads=1)
        set_tests_properties(tombox_xmem_candidate tombox_xmem_source_tests
            PROPERTIES TIMEOUT 120)
    endif()
    add_test(NAME pe_map_fixture COMMAND wsx_pe_map_test
             "$<TARGET_FILE_DIR:wsx_pe_map_test>/wsx_pe_map.dll" "$<TARGET_FILE:wsx_pe_map_fixture_exe>")
    set_tests_properties(pe_map_fixture PROPERTIES TIMEOUT 30)
    set_tests_properties(signal_run signal_run_warp PROPERTIES
        ENVIRONMENT "WSX_SAMPLE_SELF_TEST=1" TIMEOUT 45)
    set_tests_properties(graphics_probe graphics_probe_standard_adapters graphics_probe_warp
        graphics_probe_present graphics_probe_present_warp bootstrap_graphics PROPERTIES TIMEOUT 45)
    set_tests_properties(graphics_probe_present graphics_probe_present_warp PROPERTIES
        PASS_REGULAR_EXPRESSION "DXGI triangle presented and verified")
    set_tests_properties(startup_trace PROPERTIES
        TIMEOUT 60 PASS_REGULAR_EXPRESSION "Process exited: 0x00000000")
    set_tests_properties(startup_trace_gameinput PROPERTIES
        TIMEOUT 60 ENVIRONMENT "WSX_SAMPLE_SELF_TEST=1"
        PASS_REGULAR_EXPRESSION "XInput fallback remains active")
    if(WSX_DXC_DIR)
        if(NOT EXISTS "${WSX_DXC_DIR}/dxcompiler.dll" OR NOT EXISTS "${WSX_DXC_DIR}/dxil.dll")
            message(FATAL_ERROR "WSX_DXC_DIR must contain the matching x64 dxcompiler.dll and dxil.dll.")
        endif()
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        set(shader_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../tools/shader_smoke.py")
        add_test(NAME shader_sm66 COMMAND wsx_shader_probe --dxc-dir "${WSX_DXC_DIR}")
        add_test(NAME shader_sm66_warp COMMAND wsx_shader_probe --dxc-dir "${WSX_DXC_DIR}" --warp)
        add_test(NAME shader_rdna2 COMMAND "${Python3_EXECUTABLE}" "${shader_smoke}"
                 "$<TARGET_FILE:wsx_shader_probe>" "${WSX_DXC_DIR}")
        add_test(NAME shader_rdna2_warp COMMAND "${Python3_EXECUTABLE}" "${shader_smoke}"
                 "$<TARGET_FILE:wsx_shader_probe>" "${WSX_DXC_DIR}" --warp)
        add_test(NAME shader_dxr_1_1 COMMAND wsx_shader_probe --dxc-dir "${WSX_DXC_DIR}" --ray-query)
        add_test(NAME shader_dxr_1_1_warp COMMAND wsx_shader_probe --dxc-dir "${WSX_DXC_DIR}" --ray-query --warp)
        set_tests_properties(shader_sm66 shader_sm66_warp shader_rdna2 shader_rdna2_warp
                             shader_dxr_1_1 shader_dxr_1_1_warp PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
endif()
