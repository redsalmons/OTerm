#ifndef LOCAL_TERMINAL_CONTAINER_H
#define LOCAL_TERMINAL_CONTAINER_H

#include "LocalTerminalThread.h"
#include "ScreenBuffer.h"
#include "EventProxy.h"

class LocalTerminalContainer {
public:
    LocalTerminalContainer(int rows = 24, int cols = 80, const std::string& shell = "");
    ~LocalTerminalContainer();

    LocalTerminalThread* GetThread() const { return m_thread; }
    EventProxyPtr GetEventProxy() const { return m_event_proxy; }
    
    // 获取渲染数据
    const ScreenBuffer* GetFrontBuffer() const;
    
    // 设置UI处理器（用于切换渲染目标）
    void SetUIHandler(wxWindow* ui_handler);
    void ClearUIHandler();

    void StopTerminal(); // 停止终端线程（完全销毁）
    void QueueInput(const std::string& input);
    void Resize(int rows, int cols);
    void Scroll(int lines);
    void ResetScrollToBottom();

private:
    LocalTerminalThread* m_thread;
    EventProxyPtr m_event_proxy;
};

#endif
