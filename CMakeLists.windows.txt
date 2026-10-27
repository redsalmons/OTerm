cmake_minimum_required(VERSION 3.16)
project(OceanTerm VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find packages using vcpkg
find_package(nlohmann_json CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(Libssh2 CONFIG REQUIRED)

# Use wxWidgets provided by vcpkg (matching the triplet: arm64-windows / x64-windows / x86-windows)
find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base gl net)

find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED)

# Create executable
add_executable(OceanTerm WIN32
    src/AppWindow.cpp
    src/CustomTitleBar.cpp
    src/ConnectInfo.cpp
    src/ConnectionDialog.cpp
    src/DeviceConfig.cpp
    src/DeviceListPanel.cpp
    src/AddDeviceDialog.cpp
    src/SSHManager.cpp
    src/VTermManager.cpp
    src/TermGLCanvas.cpp
    src/FontAtlas.cpp
    src/FileTransferDialog.cpp
    src/AsynchronousSFTPTask.cpp
    src/GlobalConfig.cpp
    src/TranslationHelper.cpp
    src/SettingsDialog.cpp
    src/MasterPasswordDialog.cpp
    src/MonospaceFontDialog.cpp
    src/LocalTerminalManager.cpp
    src/TerminalPanel.cpp
    src/InfiniteSplitter.cpp
    src/LocalTerminalContainer.cpp
    src/SSHTerminalContainer.cpp
    src/EventProxy.cpp
    src/SplitTree.cpp
    src/SplitManager.cpp
    src/SplitContainer.cpp
    src/SplitterPanel.cpp
    src/RootPanel.cpp
    src/CommandInterceptor.cpp
)

# Add include directories
target_include_directories(OceanTerm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libvterm/include
)

# libvterm static library built per-arch (build_installer.ps1 builds it into libvterm/build-<arch>)
if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" OTERM_ARCH)
else()
    set(OTERM_ARCH "arm64")
endif()
set(VTERM_LIB ${CMAKE_CURRENT_SOURCE_DIR}/libvterm/build-${OTERM_ARCH}/Release/vterm.lib)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(OceanTerm PRIVATE /utf-8 /DHAVE_LIBVTERM)
    target_compile_definitions(OceanTerm PRIVATE UNICODE _UNICODE WXUSINGDLL)
    target_link_options(OceanTerm PRIVATE /SUBSYSTEM:WINDOWS)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(OceanTerm PRIVATE -Wall -Wextra -DHAVE_LIBVTERM -g)
    target_compile_definitions(OceanTerm PRIVATE UNICODE _UNICODE WXUSINGDLL)
endif()

# Link libraries
target_link_libraries(OceanTerm PRIVATE
    nlohmann_json::nlohmann_json
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    libssh2::libssh2
    ${VTERM_LIB}
    ws2_32
    crypt32
    comctl32
    rpcrt4
    wx::core
    wx::base
    wx::gl
    wx::net
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
