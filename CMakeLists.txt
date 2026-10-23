cmake_minimum_required(VERSION 3.16)
project(OceanTerm VERSION 1.0.5 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    if(WIN32)
        set(CMAKE_BUILD_TYPE RelWithDebInfo)
    else()
        set(CMAKE_BUILD_TYPE Debug)
    endif()
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED)

set(SOURCES
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

if(WIN32)
    # ---- Windows: dependencies provided by vcpkg ----
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(libuv CONFIG REQUIRED)
    find_package(Libssh2 CONFIG REQUIRED)
    find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base gl net)

    add_executable(OceanTerm WIN32 ${SOURCES})

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

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(OceanTerm PRIVATE /utf-8 /DHAVE_LIBVTERM)
        target_compile_definitions(OceanTerm PRIVATE UNICODE _UNICODE WXUSINGDLL)
        target_link_options(OceanTerm PRIVATE /SUBSYSTEM:WINDOWS)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(OceanTerm PRIVATE -Wall -Wextra -DHAVE_LIBVTERM -g)
        target_compile_definitions(OceanTerm PRIVATE UNICODE _UNICODE WXUSINGDLL)
    endif()

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
else()
    # ---- macOS / Unix: wx-config + pkg-config ----
    find_package(PkgConfig REQUIRED)
    find_package(nlohmann_json QUIET CONFIG)

    pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
    pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)
    pkg_check_modules(LIBSSH2 REQUIRED IMPORTED_TARGET libssh2)

    find_program(WX_CONFIG_EXECUTABLE wx-config REQUIRED)
    execute_process(
        COMMAND ${WX_CONFIG_EXECUTABLE} --cxxflags
        OUTPUT_VARIABLE WX_CXXFLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${WX_CONFIG_EXECUTABLE} --libs core,base,gl,net,adv
        OUTPUT_VARIABLE WX_LIBS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    separate_arguments(WX_CXXFLAGS_LIST NATIVE_COMMAND "${WX_CXXFLAGS}")

    if(APPLE)
        list(APPEND SOURCES
            src/MacTitleBar.mm
            src/MacKeyMonitor.mm
            src/MacIMEProxy.mm
        )
    endif()

    add_executable(OceanTerm ${SOURCES})

    if(APPLE)
        set_source_files_properties(src/MacTitleBar.mm src/MacKeyMonitor.mm src/MacIMEProxy.mm PROPERTIES COMPILE_FLAGS "-x objective-c++")
    endif()

    target_include_directories(OceanTerm PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_compile_options(OceanTerm PRIVATE
        -Wall
        -Wextra
        ${WX_CXXFLAGS_LIST}
    )

    target_link_libraries(OceanTerm PRIVATE
        PkgConfig::VTERM
        PkgConfig::LIBUV
        PkgConfig::LIBSSH2
        OpenGL::GL
        OpenSSL::Crypto
    )

    if(APPLE)
        target_link_libraries(OceanTerm PRIVATE "-lutil")
        target_link_options(OceanTerm PRIVATE
            "SHELL:${WX_LIBS}"
            "LINKER:-framework,Foundation"
            "LINKER:-framework,Cocoa"
            "LINKER:-framework,Carbon"
            "LINKER:-framework,IOKit"
            "LINKER:-framework,CoreFoundation"
            "LINKER:-framework,AppKit"
            "LINKER:-framework,Security"
        )
    elseif(UNIX)
        target_link_libraries(OceanTerm PRIVATE pthread)
    endif()

    if(nlohmann_json_FOUND)
        target_link_libraries(OceanTerm PRIVATE nlohmann_json::nlohmann_json)
    else()
        find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
            PATHS
                /opt/homebrew/include
                /usr/local/include
        )
        if(NOT NLOHMANN_JSON_INCLUDE_DIR)
            message(FATAL_ERROR "nlohmann/json.hpp not found. Install it with: brew install nlohmann-json")
        endif()
        target_include_directories(OceanTerm PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
    endif()
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
