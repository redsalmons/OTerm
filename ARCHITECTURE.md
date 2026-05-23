# OceanTerm 架构文档

## 系统概述

OceanTerm 是一个基于 OpenGL 的 SSH 终端应用程序，使用现代图形库和终端模拟器来提供高性能的终端体验。

## 核心组件架构

### 1. GLFW (窗口管理)
```cpp
// 作用：窗口创建、事件处理、OpenGL上下文管理
class OpenGLWindow {
    bool Initialize(int width, int height, const char* title);
    void SwapBuffers();
    bool ShouldClose() const;
    void PollEvents();
};
```

**职责：**
- 创建和管理 OpenGL 窗口
- 处理键盘和鼠标事件
- 提供 OpenGL 上下文
- 管理窗口生命周期

### 2. ImGui (用户界面层)
```cpp
// 作用：提供即时模式 GUI 元素和调试界面
namespace ImGui {
    // 渲染 UI 元素、按钮、文本输入框
}
```

**职责：**
- 提供调试界面和控制面板
- 处理用户输入（如文本输入）
- 显示系统状态和性能指标
- 可选的覆盖层，不影响主要渲染

### 3. libvterm (终端模拟器)
```cpp
// 作用：VT220 终端模拟和字符处理
VTerm* vterm_new(rows, cols);
VTermScreen* vterm_obtain_screen(vterm);
```

**职责：**
- 解析 ANSI 转义序列
- 维护终端状态（光标位置、颜色等）
- 处理字符编码和显示属性
- 提供终端屏幕缓冲区访问

### 4. Skia (2D 图形渲染)
```cpp
// 作用：2D 图形渲染和字体绘制
SkCanvas* canvas;
SkFont font;
SkBitmap bitmap;
```

**职责：**
- 提供高性能 2D 图形渲染
- 字体渲染和文本绘制
- 管理位图和画布状态
- 支持硬件加速渲染

### 5. OpenGL (硬件加速渲染)
```cpp
// 作用：硬件加速图形渲染和纹理管理
GLuint textureId;
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
```

**职责：**
- 硬件加速图形渲染
- 纹理管理和上传
- 坐标系统转换
- 视口和投影管理

## 数据流架构

### SSH 数据流
```
SSH服务器 → libssh2 → SSHConnection → WriteData() → libvterm
```

**流程说明：**
1. **SSH 连接**: libssh2 建立到远程服务器的安全连接
2. **数据接收**: SSHConnection::ProcessData() 接收服务器输出
3. **数据解析**: WriteData() 将数据写入 libvterm 进行解析
4. **终端渲染**: libvterm 解析 ANSI 序列并更新内部状态

### 渲染数据流
```
libvterm → DamageCallback → MarkDamage → RenderDamage → SkCanvas → OpenGL → 屏幕
```

**流程说明：**
1. **变更检测**: libvterm 检测屏幕内容变化并触发 DamageCallback
2. **区域标记**: MarkDamage() 标记需要重绘的行列区域
3. **内容渲染**: RenderDamage() 读取 libvterm 状态并渲染到 SkCanvas
4. **纹理上传**: SkCanvas 内容上传到 OpenGL 纹理
5. **屏幕显示**: OpenGL 渲染纹理到屏幕

## 坐标系统

### 窗口坐标系
- **原点**: 左上角 (0, 0)
- **X 轴**: 向右增加
- **Y 轴**: 向下增加
- **尺寸**: 1024×768 像素

### Canvas 坐标系
- **原点**: 左上角 (0, 0)
- **X 轴**: 向右增加
- **Y 轴**: 向下增加
- **尺寸**: 1024×768 像素（与窗口对齐）

### 终端坐标系
- **原点**: 左上角 (0, 0)
- **单位**: 字符行列 (row, col)
- **转换**: 终端坐标 → 像素坐标
```cpp
float x = col * charWidth_;
float y = row * charHeight_;
```

### OpenGL 纹理坐标系
- **原点**: 左下角 (0, 0)
- **S 坐标**: (0, 1) 对应纹理顶部
- **T 坐标**: (1, 0) 对应纹理底部
- **转换**: Canvas 坐标 → OpenGL 纹理坐标需要 Y 轴翻转

## 元素布局关系

```
┌─────────────────────────────────────────────────────────────┐
│                    ImGui (UI 层)                      │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────┐  │
│  │              libvterm (终端层)              │  │
│  │  ┌─────────────────────────────────────────────┐  │  │
│  │  │            Skia (渲染层)              │  │  │
│  │  │  ┌─────────────────────────────────────┐  │  │  │
│  │  │  │         OpenGL (硬件层)      │  │  │  │
│  │  │  └─────────────────────────────────────┘  │  │  │
│  │  └─────────────────────────────────────────────┘  │  │
│  └─────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                  GLFW (窗口层)                      │
└─────────────────────────────────────────────────────────────┘
```

## 关键接口

### SSH 数据接口
```cpp
class SSHConnection {
    void SetDataCallback(std::function<void(const char*, size_t)> callback);
    void ProcessData();
    bool IsConnected();
};
```

### 终端渲染接口
```cpp
class SkiaTerminal {
    void WriteData(const char* data, size_t length);
    void SetDamageCallback(DamageCallback callback);
    void RenderDamage(SkCanvas* canvas, SkFont& font);
    void MarkDamage(int start_row, int end_row, int start_col, int end_col);
};
```

### 窗口管理接口
```cpp
class OpenGLWindow {
    bool Initialize(int width, int height, const char* title);
    void SwapBuffers();
    bool ShouldClose() const;
    void SetResizeCallback(std::function<void(int, int)> callback);
};
```

## 性能特性

### 渲染优化
- **Damage 跟踪**: 只重绘变化的区域，避免全屏刷新
- **纹理缓存**: OpenGL 纹理上传后缓存，避免重复上传
- **硬件加速**: 利用 GPU 进行最终渲染

### 内存管理
- **Skia 位图**: 使用 Skia 管理的位图，支持硬件加速
- **智能缓存**: 字符和字体渲染结果缓存
- **RAII 模式**: C++ RAII 管理资源生命周期

## 调试和监控

### 性能指标
- 帧率监控
- 内存使用统计
- 渲染时间测量
- SSH 连接状态监控

### 调试工具
- ImGui 调试面板
- 坐标系统可视化
- 渲染管道状态检查
- 像素级内容验证

这个架构设计确保了高性能、可维护性和可扩展性，同时提供了清晰的组件分离和接口定义。

## High DPI 缩放问题解决方案

### 问题根源
Canvas 只占据屏幕左下角 1/4 区域的原因是逻辑坐标与物理像素坐标不匹配：

```cpp
// 错误做法：使用逻辑窗口尺寸
int logical_width, logical_height;
glfwGetWindowSize(window, &logical_width, &logical_height);
glViewport(0, 0, logical_width, logical_height);

// 正确做法：使用物理像素尺寸
int physical_width, physical_height;
glfwGetFramebufferSize(window, &physical_width, &physical_height);
glViewport(0, 0, physical_width, physical_height);
```

### 修复方案
1. **更新视口设置**：使用 `glfwGetFramebufferSize()` 获取物理像素尺寸
2. **同步 Canvas 尺寸**：确保 Skia Surface 尺寸与物理像素匹配
3. **修正纹理坐标映射**：处理 Y 轴翻转和缩放问题
4. **处理 UI 缩放**：设置 ImGui 的缩放因子

### 快速修复代码
```cpp
// 在窗口大小回调中
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    // 使用物理像素尺寸设置视口
    glViewport(0, 0, width, height);
    
    // 更新 Skia Surface 尺寸
    // re-create SkSurface(width, height)
    
    // 处理 ImGui 缩放
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    ImGui::GetIO().FontGlobalScale = xscale;
}
```

### 关键修复点
- **Application.cpp**: 修改 InitializeSkia() 和窗口回调使用物理像素
- **SkiaTerminal.cpp**: 确保 Canvas 尺寸与物理像素匹配
- **ImGui 集成**: 正确处理高 DPI 缩放

### 验证方法
1. 检查 `glfwGetFramebufferSize()` 返回的尺寸是否为物理像素
2. 验证 Canvas 尺寸与窗口尺寸的比例
3. 测试在不同 DPI 设置下的显示效果
4. 监控 OpenGL 视口设置是否正确

### 预期效果
- Canvas 填满整个窗口区域（不再是 1/4）
- 文字和 UI 元素正确缩放
- 在高 DPI 显示器上显示清晰
- 保持终端功能的完整性
