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
add_library(wsx_graphics_runtime STATIC graphics_probe.cpp)
target_link_libraries(wsx_graphics_runtime PUBLIC d3d12_xs dxgi d3dcompiler)

add_executable(wsx_graphics_probe graphics_probe_main.cpp)
target_link_libraries(wsx_graphics_probe PRIVATE wsx_graphics_runtime)

add_library(wsx_boot_graphics SHARED boot_graphics.cpp)
target_link_libraries(wsx_boot_graphics PRIVATE wsx_graphics_runtime)
add_executable(wsx_bootstrap bootstrap.cpp)
target_link_libraries(wsx_bootstrap PRIVATE wsx_graphics_runtime)
add_dependencies(wsx_bootstrap wsx_boot_graphics)
install(TARGETS d3d12_xs wsx_graphics_probe wsx_bootstrap wsx_boot_graphics RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(wsx_root_signature_test root_signature_test.cpp)
    add_dependencies(wsx_root_signature_test d3d12_xs)
    add_test(NAME root_signature COMMAND wsx_root_signature_test "$<TARGET_FILE:d3d12_xs>")
    add_test(NAME graphics_probe COMMAND wsx_graphics_probe)
    add_test(NAME graphics_probe_standard_adapters COMMAND wsx_graphics_probe --legacy-dxgi)
    add_test(NAME graphics_probe_warp COMMAND wsx_graphics_probe --warp)
    add_test(NAME bootstrap_graphics COMMAND wsx_bootstrap)
    set_tests_properties(graphics_probe graphics_probe_standard_adapters graphics_probe_warp
        bootstrap_graphics PROPERTIES TIMEOUT 45)
endif()
