#!/bin/bash

# macOS Packaging Script for OceanTerm
# This script creates a .app bundle and bundles all required dylibs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-macos"
APP_NAME="OceanTerm"
APP_VERSION="1.0.2"
APP_BUNDLE="${BUILD_DIR}/${APP_NAME}.app"
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
EXECUTABLE="${BUILD_DIR}/bin/${APP_NAME}"

echo "=== OceanTerm macOS Packaging Script ==="
echo "Build directory: ${BUILD_DIR}"
echo "App bundle: ${APP_BUNDLE}"

# Check if executable exists
if [ ! -f "${EXECUTABLE}" ]; then
    echo "Error: Executable not found at ${EXECUTABLE}"
    echo "Please build the project first: cd build-macos && cmake --build ."
    exit 1
fi

# Remove existing app bundle if it exists
if [ -d "${APP_BUNDLE}" ]; then
    echo "Removing existing app bundle..."
    rm -rf "${APP_BUNDLE}"
fi

# Create app bundle structure
echo "Creating app bundle structure..."
mkdir -p "${MACOS_DIR}"
mkdir -p "${RESOURCES_DIR}"

# Copy executable
echo "Copying executable..."
cp "${EXECUTABLE}" "${MACOS_DIR}/"
chmod +x "${MACOS_DIR}/${APP_NAME}"

# Copy resources
echo "Copying resources..."
if [ -f "OceanTerm.icns" ]; then
    cp "OceanTerm.icns" "${RESOURCES_DIR}/"
fi
if [ -d "${BUILD_DIR}/bin/config" ]; then
    mkdir -p "${RESOURCES_DIR}/config"
    # Copy all files except oc.json
    find "${BUILD_DIR}/bin/config" -type f ! -name "oc.json" -exec cp {} "${RESOURCES_DIR}/config/" \;
fi
if [ -d "${BUILD_DIR}/bin/locales" ]; then
    cp -r "${BUILD_DIR}/bin/locales" "${RESOURCES_DIR}/"
fi
if [ -d "${BUILD_DIR}/bin/logs" ]; then
    mkdir -p "${RESOURCES_DIR}/logs"
fi

# Create Info.plist
echo "Creating Info.plist..."
cat > "${CONTENTS_DIR}/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>OceanTerm</string>
    <key>CFBundleIdentifier</key>
    <string>com.oceanterm.app</string>
    <key>CFBundleName</key>
    <string>OceanTerm</string>
    <key>CFBundleDisplayName</key>
    <string>OceanTerm</string>
    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>CFBundleIconFile</key>
    <string>OceanTerm</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
</dict>
</plist>
EOF

# Bundle dynamic libraries
echo "Bundling dynamic libraries..."

# Add @rpath to executable so it can find libraries in Resources and homebrew
install_name_tool -add_rpath "@executable_path/../Resources" "${MACOS_DIR}/${APP_NAME}" 2>/dev/null || true
install_name_tool -add_rpath "/opt/homebrew/lib" "${MACOS_DIR}/${APP_NAME}" 2>/dev/null || true
install_name_tool -add_rpath "/usr/local/lib" "${MACOS_DIR}/${APP_NAME}" 2>/dev/null || true

# Function to bundle a dylib and its dependencies recursively
bundle_dylib() {
    local dylib_path="$1"
    local dylib_name=$(basename "${dylib_path}")
    
    # Skip if already bundled
    if [ -f "${RESOURCES_DIR}/${dylib_name}" ]; then
        return 0
    fi
    
    echo "  Bundling: ${dylib_name}"
    
    # If it's a symlink, copy the actual file
    if [ -L "${dylib_path}" ]; then
        local real_path=$(readlink -f "${dylib_path}")
        cp "${real_path}" "${RESOURCES_DIR}/${dylib_name}"
    else
        cp "${dylib_path}" "${RESOURCES_DIR}/"
    fi
    
    # Create symlink for versioned libraries (e.g., libsharpyuv.0.1.2.dylib -> libsharpyuv.0.dylib)
    if [[ "${dylib_name}" =~ \.[0-9]+\.[0-9]+\.[0-9]+\.dylib$ ]]; then
        local short_name=$(echo "${dylib_name}" | sed 's/\.[0-9]*\.[0-9]*\.[0-9]*\.dylib/.dylib/')
        ln -sf "${dylib_name}" "${RESOURCES_DIR}/${short_name}"
        echo "    Created symlink: ${short_name} -> ${dylib_name}"
    fi
    
    # Get all dependencies of this dylib (including @rpath references)
    local deps=$(otool -L "${RESOURCES_DIR}/${dylib_name}" | grep -E "^\s*(/usr/local|/opt/homebrew|@rpath)" | awk '{print $1}')
    
    for dep in ${deps}; do
        # Resolve @rpath to actual path
        local actual_dep="${dep}"
        if [[ "${dep}" == @rpath/* ]]; then
            local rpath_name="${dep#@rpath/}"
            # Try to find in homebrew
            actual_dep="/opt/homebrew/lib/${rpath_name}"
            if [ ! -f "${actual_dep}" ]; then
                actual_dep="/usr/local/lib/${rpath_name}"
            fi
        fi
        
        if [ -f "${actual_dep}" ]; then
            local dep_name=$(basename "${actual_dep}")
            # Recursively bundle dependency
            bundle_dylib "${actual_dep}"
            # Fix the reference to use @rpath (which now points to Resources)
            install_name_tool -change "${dep}" "@rpath/${dep_name}" "${RESOURCES_DIR}/${dylib_name}"
        fi
    done
}

# Get list of dylibs that the executable depends on
DYLIBS=$(otool -L "${MACOS_DIR}/${APP_NAME}" | grep -E "^\s*/(usr/local|opt/homebrew)" | awk '{print $1}')

# Copy and fix each dylib (skip wxWidgets to avoid version conflicts)
for dylib in ${DYLIBS}; do
    if [ -f "${dylib}" ]; then
        # Skip wxWidgets libraries - they'll be found via rpath
        if [[ "${dylib}" == *"wx"* ]]; then
            echo "  Skipping wxWidgets: $(basename "${dylib}") - will use system version via rpath"
            continue
        fi
        bundle_dylib "${dylib}"
        dylib_name=$(basename "${dylib}")
        # Change install name in executable to use @rpath
        install_name_tool -change "${dylib}" "@rpath/${dylib_name}" "${MACOS_DIR}/${APP_NAME}"
    fi
done

# Re-sign all dylibs with ad-hoc signature to fix code signing issues
echo "Re-signing bundled dylibs..."
for dylib in "${RESOURCES_DIR}"/*.dylib; do
    if [ -f "${dylib}" ]; then
        echo "  Signing: $(basename "${dylib}")"
        codesign --force --sign - "${dylib}" 2>/dev/null || true
    fi
done

# Re-sign the executable
echo "Re-signing executable..."
codesign --force --sign - "${MACOS_DIR}/${APP_NAME}" 2>/dev/null || true

# Re-sign the entire app bundle
echo "Re-signing app bundle..."
codesign --force --deep --sign - "${APP_BUNDLE}" 2>/dev/null || true

# Create DMG for distribution
echo "Creating DMG for distribution..."
DMG_NAME="OceanTerm-macos-$(uname -m)-${APP_VERSION}.dmg"
hdiutil create -volname "OceanTerm" -srcfolder "${APP_BUNDLE}" -ov -format UDZO "${BUILD_DIR}/${DMG_NAME}"

echo ""
echo "=== Packaging Complete ==="
echo "App bundle created at: ${APP_BUNDLE}"
echo "DMG created at: ${BUILD_DIR}/${DMG_NAME}"
echo ""
echo "To run the app:"
echo "  open ${APP_BUNDLE}"
