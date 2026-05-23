# OceanTerm 类关系架构图

## 🏗️ 整体架构概览

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Application.cpp                        │
│                     (主程序入口 + SDL渲染)                      │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      NeovimEmbed.cpp                        │
│                  (Neovim协议处理 + SSH集成)                    │
└─────────────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
        ┌─────────────────────┐  ┌─────────────────────┐
        │   AnsiParser.cpp   │  │   SshBackend.cpp   │
        │   (ANSI转义解析)   │  │   (SSH连接管理)     │
        └─────────────────────┘  └─────────────────────┘
```

## 📊 详细类关系

### **1. Application (主程序类)**
```cpp
class Application {
    // 核心组件
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    TTF_Font* font_;
    
    // 渲染器
    Renderer term_ren_;
    
    // Neovim嵌入
    NeovimEmbed g_neovim_;
    
    // 终端状态
    TerminalState g_state_;
};
```

**职责：**
- SDL初始化和主循环
- 事件处理（键盘、鼠标、窗口）
- 协调渲染和Neovim处理

---

### **2. NeovimEmbed (核心协议处理)**
```cpp
class NeovimEmbed {
    // 连接管理
    ConnectionMode connection_mode_;
    SshBackend* ssh_backend_;
    
    // ANSI解析器
    std::unique_ptr<AnsiParser> ansi_parser_;
    
    // Neovim网格数据
    std::unordered_map<int, Grid> grids_;
    
    // 消息处理
    msgpack::unpacker unpacker_;
    int next_msgid_;
};
```

**职责：**
- Neovim MessagePack-RPC协议处理
- SSH连接管理
- ANSI转义序列解析
- 网格数据管理

---

### **3. AnsiParser (ANSI转义解析器)**
```cpp
class AnsiParser {
    // 解析状态
    AnsiState state_;
    TerminalState state_;
    
    // 回调函数
    TextHandler text_handler_;
    CursorHandler cursor_handler_;
    ClearHandler clear_handler_;
};
```

**职责：**
- ANSI转义序列解析
- 终端状态维护（颜色、样式、光标）
- 文本、光标、清屏事件分发

---

### **4. SshBackend (SSH连接管理)**
```cpp
class SshBackend {
    // SSH连接
    LIBSSH2_SESSION* session_;
    LIBSSH2_CHANNEL* channel_;
    
    // 连接状态
    bool connected_;
    std::string host_;
    int port_;
};
```

**职责：**
- SSH连接建立和断开
- 数据读写
- 通道管理

---

### **5. Renderer (渲染器)**
```cpp
class Renderer {
    // SDL组件
    SDL_Renderer* ren_;
    TTF_Font* font_;
    
    // 字符尺寸
    int char_w_, char_h_;
    
    // 纹理缓存
    std::unordered_map<std::string, SDL_Texture*> texture_cache_;
};
```

**职责：**
- 字符渲染
- 纹理缓存管理
- 光标绘制

---

## 🔄 数据流关系

### **SSH模式数据流：**
```
远程服务器输出
    ↓
SshBackend::ReadData()
    ↓
NeovimEmbed::ProcessSshData()
    ↓
AnsiParser::ProcessInput()
    ↓
┌─────────────────────────────────────────┐
│  ANSI解析器状态机                    │
│  NORMAL → ESCAPE → CSI → PARAM      │
└─────────────────────────────────────────┘
    ↓
回调函数触发：
├─ HandleAnsiText() → InsertTextIntoBuffer()
├─ HandleAnsiCursor() → nvim_win_set_cursor()
└─ HandleAnsiClear() → nvim_command("clear")
    ↓
本地Neovim进程
    ↓
MessagePack响应
    ↓
NeovimEmbed::ProcessNeovimOutput()
    ↓
Application::OnNeovimRedraw()
    ↓
Renderer::DrawCell()
```

### **本地模式数据流：**
```
本地Neovim进程
    ↓
MessagePack响应
    ↓
NeovimEmbed::ProcessNeovimOutput()
    ↓
Application::OnNeovimRedraw()
    ↓
Renderer::DrawCell()
```

---

## 🎯 关键设计模式

### **1. 观察者模式**
```cpp
// AnsiParser使用回调机制通知NeovimEmbed
ansi_parser_->SetTextHandler([this](const std::string& text, const TerminalState& state) {
    this->HandleAnsiText(text, state);
});
```

### **2. 状态模式**
```cpp
// AnsiParser的状态机
enum class AnsiState {
    NORMAL,      // 普通文本
    ESCAPE,      // 遇到ESC字符
    CSI,         // 控制序列开始 [
    PARAM,       // 参数解析
    FINAL        // 最终字符
};
```

### **3. 策略模式**
```cpp
// 不同连接模式的处理策略
if (connection_mode_ == ConnectionMode::SSH) {
    ProcessSshData();
} else {
    uv_run(loop_, UV_RUN_NOWAIT);
}
```

---

## 🔗 依赖关系

### **编译时依赖：**
```
Application.cpp
├─ NeovimEmbed.h
├─ Application.h (Cell结构体)
└─ SDL2/SDL_ttf

NeovimEmbed.cpp
├─ NeovimEmbed.h
├─ AnsiParser.h
├─ SshBackend.h
└─ msgpack.hpp

AnsiParser.cpp
├─ AnsiParser.h
└─ functional (回调)

SshBackend.cpp
├─ SshBackend.h
└─ libssh2.h
```

### **运行时依赖：**
```
Application (主线程)
    │
    ├─> NeovimEmbed (同线程)
    │     │
    │     ├─> AnsiParser (同线程)
    │     │
    │     └─> SshBackend (同线程)
    │
    └─> Renderer (同线程)
```

---

## 📋 接口定义

### **AnsiParser回调接口：**
```cpp
using TextHandler = std::function<void(const std::string&, const TerminalState&)>;
using CursorHandler = std::function<void(int row, int col)>;
using ClearHandler = std::function<void(int mode)>;
```

### **NeovimEmbed公共接口：**
```cpp
// 连接管理
bool Initialize();
bool StartNeovim();
void RunEventLoopOnce();

// 配置
void SetConnectionMode(ConnectionMode mode);
void SetSshConfig(const std::string& host, int port, const std::string& user, const std::string& pass);

// 回调设置
std::function<void(const msgpack::object&)> on_redraw_event;
```

---

## 🎨 架构优势

### **1. 模块化设计**
- 每个类职责单一
- 低耦合高内聚
- 易于测试和维护

### **2. 可扩展性**
- 支持多种连接模式（本地/SSH）
- ANSI解析器可独立扩展
- 渲染器可替换实现

### **3. 异步处理**
- 非阻塞I/O
- 事件驱动架构
- 响应式设计

### **4. 协议分离**
- SSH连接与Neovim协议分离
- ANSI解析与渲染分离
- 数据处理与UI分离

这个架构确保了系统的灵活性、可维护性和性能。
