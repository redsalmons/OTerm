# OceanTerm

A modern terminal emulator application built with wxWidgets and libvterm, designed for efficient SSH connections and file transfers.

## Features

- **Multiple Terminal Sessions**: Support for multiple SSH connections in a tabbed interface
- **File Transfer**: Built-in SFTP file transfer with drag-and-drop support
- **Custom Title Bar**: Customizable window title bar with tab management
- **Internationalization**: Support for English and Chinese languages
- **Device Management**: Save and manage connection profiles with search and filter
- **Security**: Master password protection for stored credentials
- **Dark Theme**: Modern dark UI design

## Requirements

- C++11 compatible compiler
- CMake 3.10 or higher
- wxWidgets 3.2 or higher
- libvterm
- vcpkg (for dependency management)

## Building

### Windows (Visual Studio)

```bash
# Install dependencies using vcpkg
vcpkg install wxwidgets:x64-windows

# Build the project
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### Linux

```bash
# Install dependencies
sudo apt-get install libwxgtk3.0-gtk3-dev libvterm-dev cmake build-essential

# Build the project
mkdir build
cd build
cmake ..
make
```

### macOS

```bash
# Install dependencies using Homebrew
brew install wxwidgets libvterm cmake

# Build the project
mkdir build
cd build
cmake ..
make

# Package as DMG (optional)
./package_macos.sh
```

## Usage

1. Run the application
2. Set up your master password on first launch (encrypts stored credentials)
3. Click the "+" button to add a new terminal connection
4. Configure SSH connection settings (host, port, username, authentication)
5. Connect to your remote server
6. Use the drawer menu (hamburger icon) to access settings and overflow tabs

## Device Management

- **Add Connection**: Click "+" button, fill in connection details, and save
- **Open Connection**: Click "Open" button on device list to connect
- **Delete Connection**: Click "Delete" button to remove a profile
- **Search**: Use search box to filter connection list

## File Transfer

After connecting to a remote server, use the built-in SFTP feature to:
- Upload and download files
- Browse remote directories
- Manage remote files with drag-and-drop

## Project Structure

- `src/` - Source code files
- `locales/` - Translation files (.po format)
- `imgui/` - ImGui library integration
- `libvterm/` - libvterm terminal emulation library
- `build/` - Build output directory

## Configuration

Settings are saved in `settings.json` in the workspace directory. Connection profiles are encrypted using the master password.

## Security

- Master password encrypts all stored credentials
- No password recovery mechanism - keep your master password safe
- Secure SSH connections with standard protocols

## License

See LICENSE file for details.

## Download

Pre-built macOS DMG packages are available in the [Releases](https://github.com/redsalmons/OTerm/releases) section.
