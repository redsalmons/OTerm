set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)

# Overlay of builtin x64-osx: compile for macOS 11.0 (see arm64-osx.cmake).
set(VCPKG_C_FLAGS " -mmacosx-version-min=11.0 ")
set(VCPKG_CXX_FLAGS " -mmacosx-version-min=11.0 ")
