# OceanTerm

基于 wxWidgets 和 libvterm 构建的现代化终端模拟器，专为高效的 SSH 连接和文件传输而设计。

## 功能特性

- **多终端会话**：支持在标签页界面中管理多个 SSH 连接
- **文件传输**：内置 SFTP 文件传输功能，支持拖放操作
- **自定义标题栏**：可自定义的窗口标题栏，带有标签页管理功能
- **国际化**：支持英语和中文语言
- **设备管理**：保存和管理连接配置文件，支持搜索和过滤
- **安全性**：主密码保护存储的凭据
- **深色主题**：现代化的深色 UI 设计

## 系统要求

- C++11 兼容编译器
- CMake 3.10 或更高版本
- wxWidgets 3.2 或更高版本
- libvterm
- vcpkg（用于依赖管理）

## 编译

### Windows (Visual Studio)

```bash
# 使用 vcpkg 安装依赖
vcpkg install wxwidgets:x64-windows

# 编译项目
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### Linux

```bash
# 安装依赖
sudo apt-get install libwxgtk3.0-gtk3-dev libvterm-dev cmake build-essential

# 编译项目
mkdir build
cd build
cmake ..
make
```

### macOS

```bash
# 使用 Homebrew 安装依赖
brew install wxwidgets libvterm cmake

# 编译项目
mkdir build
cd build
cmake ..
make

# 打包为 DMG（可选）
./package_macos.sh
```

## 使用方法

1. 运行应用程序
2. 首次启动时设置主密码（用于加密存储的凭据）
3. 点击 "+" 按钮添加新的终端连接
4. 配置 SSH 连接设置（主机、端口、用户名、认证方式）
5. 连接到远程服务器
6. 使用抽屉菜单（汉堡图标）访问设置和溢出标签页

## 设备管理

- **添加连接**：点击 "+" 按钮，填写连接详细信息并保存
- **打开连接**：点击设备列表上的 "打开" 按钮进行连接
- **删除连接**：点击 "删除" 按钮移除配置文件
- **搜索**：使用搜索框过滤连接列表

## 文件传输

连接到远程服务器后，使用内置的 SFTP 功能：
- 上传和下载文件
- 浏览远程目录
- 通过拖放管理远程文件

## 项目结构

- `src/` - 源代码文件
- `locales/` - 翻译文件（.po 格式）
- `imgui/` - ImGui 库集成
- `libvterm/` - libvterm 终端模拟库
- `build/` - 编译输出目录

## 配置

设置保存在工作区目录的 `settings.json` 文件中。连接配置文件使用主密码加密。

## 安全性

- 主密码加密所有存储的凭据
- 没有密码恢复机制 - 请妥善保管您的主密码
- 使用标准协议的安全 SSH 连接

## 许可证

详情请参阅 LICENSE 文件。

## 下载

预构建的 macOS DMG 安装包可在 [发布](https://github.com/redsalmons/OTerm/releases) 部分获取。
