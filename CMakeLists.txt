cmake_minimum_required(VERSION 3.16)
project(OceanTerm VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find packages using vcpkg
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(Libssh2 CONFIG REQUIRED)
find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base gl)
find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED)

# Create executable
add_executable(OceanTerm WIN32
    src/AppWindow.cpp
    src/CustomTitleBar.cpp
    src/ConnectInfo.cpp
    src/ConnectionDialog.cpp
    src/DeviceConfig.cpp
    src/SSHManager.cpp
    src/VTermManager.cpp
    src/TermGLCanvas.cpp
    src/TerminalThread.cpp
    src/FontAtlas.cpp
    src/FileTransferDialog.cpp
    src/SSHFileThread.cpp
    src/FileTransferThread.cpp
    src/GlobalConfig.cpp
    src/TranslationHelper.cpp
    src/SettingsDialog.cpp
    src/MasterPasswordDialog.cpp
    src/MonospaceFontDialog.cpp
    src/LocalTerminalManager.cpp
    src/LocalTerminalThread.cpp
)

# Add include directories
target_include_directories(OceanTerm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libvterm/include
)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(OceanTerm PRIVATE /utf-8 /DHAVE_LIBVTERM)
    target_link_options(OceanTerm PRIVATE /SUBSYSTEM:WINDOWS)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(OceanTerm PRIVATE -Wall -Wextra -DHAVE_LIBVTERM)
endif()

# Link libraries
target_link_libraries(OceanTerm PRIVATE
    nlohmann_json::nlohmann_json
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    libssh2::libssh2
    ${CMAKE_CURRENT_SOURCE_DIR}/libvterm/build/Release/vterm.lib
    ws2_32
    crypt32
    wx::core
    wx::base
    wx::gl
    OpenGL::GL
    OpenSSL::Crypto
    )

# Platform-specific libraries
if(APPLE)
    target_link_libraries(OceanTerm PRIVATE "-framework Foundation")
elseif(UNIX)
    target_link_libraries(OceanTerm PRIVATE pthread)
endif()

# Copy resources if they exist
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/resources)
    add_custom_command(TARGET OceanTerm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
        $<TARGET_FILE_DIR:OceanTerm>/resources
    )
endif()

# Copy config directory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config)
    add_custom_command(TARGET OceanTerm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:OceanTerm>/config
    )
endif()

# Copy locales directory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/locales)
    add_custom_command(TARGET OceanTerm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/locales
        $<TARGET_FILE_DIR:OceanTerm>/locales
    )
endif()
