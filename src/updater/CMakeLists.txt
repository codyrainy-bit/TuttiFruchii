# SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# TuttiFruchii-updater.exe: installs a downloaded update once TuttiFruchii has closed.
add_executable(tuttifruchii-updater main.cpp updater.rc ../frontend_common/update_installer.cpp)
target_include_directories(tuttifruchii-updater PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(tuttifruchii-updater PRIVATE cxx_std_20)
target_link_libraries(tuttifruchii-updater PRIVATE user32 shell32)
set_target_properties(tuttifruchii-updater PROPERTIES WIN32_EXECUTABLE ON
                      OUTPUT_NAME "TuttiFruchii-updater")

# The helper runs from a temporary folder, away from the DLLs next to TuttiFruchii.exe.
if (MSVC)
    set_property(TARGET tuttifruchii-updater PROPERTY MSVC_RUNTIME_LIBRARY
                 "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(tuttifruchii-updater PRIVATE -static)
endif()

# The PGO profile is trained on the emulator and does nothing for the updater, and Defender flagged
# Eden's PGO-built updater as a trojan (eden-emu/eden#4706, fix from eden-emu/eden#4707).
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(tuttifruchii-updater PRIVATE -fno-profile-instr-use)
endif()
