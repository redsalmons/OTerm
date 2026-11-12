set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# Overlay of builtin arm64-osx: compile for macOS 11.0 so binaries run on
# all Apple Silicon Macs (M1+). CMAKE_OSX_DEPLOYMENT_TARGET set here would
# not propagate to port builds, so inject the flag via VCPKG_*_FLAGS.
set(VCPKG_C_FLAGS " -mmacosx-version-min=11.0 ")
set(VCPKG_CXX_FLAGS " -mmacosx-version-min=11.0 ")
