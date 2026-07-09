cmake_minimum_required(VERSION 3.16)
project(OceanTerm VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json QUIET CONFIG)
find_package(OpenSSL REQUIRED)

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
    COMMAND ${WX_CONFIG_EXECUTABLE} --libs core,base,gl,net
    OUTPUT_VARIABLE WX_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(WX_CXXFLAGS_LIST NATIVE_COMMAND "${WX_CXXFLAGS}")

add_executable(OceanTerm
    src/AppWindow.cpp
    src/CustomTitleBar.cpp
    src/ConnectInfo.cpp
    src/ConnectionDialog.cpp
    src/DeviceConfig.cpp
    src/DeviceListPanel.cpp
    src/AddDeviceDialog.cpp
    src/SSHManager.cpp
    src/SSHTerminalContainer.cpp
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
    src/LocalTerminalContainer.cpp
    src/TerminalPanel.cpp
    src/InfiniteSplitter.cpp
    src/RootPanel.cpp
    src/SplitManager.cpp
    src/SplitTree.cpp
    src/SplitContainer.cpp
    src/SplitterPanel.cpp
    src/EventProxy.cpp
    src/CommandInterceptor.cpp
    src/MacTitleBar.mm
)

set_source_files_properties(src/MacTitleBar.mm PROPERTIES COMPILE_FLAGS "-x objective-c++")

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

target_link_libraries(OceanTerm PRIVATE
    "-lutil"
)

target_link_options(OceanTerm PRIVATE
    "SHELL:${WX_LIBS}"
    "LINKER:-framework,Foundation"
    "LINKER:-framework,Cocoa"
    "LINKER:-framework,IOKit"
    "LINKER:-framework,CoreFoundation"
    "LINKER:-framework,AppKit"
)

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

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/resources)
    add_custom_command(TARGET OceanTerm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
        $<TARGET_FILE_DIR:OceanTerm>/resources
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config)
    add_custom_command(TARGET OceanTerm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:OceanTerm>/config
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/locales)
    add_custom_command(TARGET OceanTerm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/locales
        $<TARGET_FILE_DIR:OceanTerm>/locales
    )
endif()
