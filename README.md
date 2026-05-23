# OceanTerm

A modern terminal emulator application built with wxWidgets and libvterm.

## Features

- **Multiple Terminal Sessions**: Support for multiple SSH connections in tabbed interface
- **File Transfer**: Built-in SFTP file transfer capabilities
- **Custom Title Bar**: Customizable window title bar with tab management
- **Internationalization**: Support for English and Chinese languages
- **Device Management**: Save and manage connection profiles
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
```

## Usage

1. Run the application
2. Click the "+" button to add a new terminal connection
3. Configure SSH connection settings
4. Connect to your remote server
5. Use the drawer menu (hamburger icon) to access settings and overflow tabs

## Project Structure

- `src/` - Source code files
- `locales/` - Translation files (.po format)
- `imgui/` - ImGui library integration
- `libvterm/` - libvterm terminal emulation library
- `build/` - Build output directory

## Configuration

Settings are saved in `settings.json` in the workspace directory.

## License

See LICENSE file for details.
