#include "LocalTerminalContainer.h"
#include <fstream>
#include <filesystem>

#define CONTAINER_LOG(msg) \
    do { \
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app); \
        if (f.is_open()) f << "[CONTAINER] " << msg << std::endl; \
    } while(0)

LocalTerminalContainer::LocalTerminalContainer(int rows, int cols, const std::string& shell)
    : m_thread(nullptr), m_event_proxy(std::make_shared<EventProxy>()) {
    
    CONTAINER_LOG("Constructor START, rows=" << rows << " cols=" << cols << " shell=" << shell);
    
    CONTAINER_LOG("Creating LocalTerminalThread with EventProxy");
    // 创建 LocalTerminalThread with EventProxy
    m_thread = new LocalTerminalThread(m_event_proxy, rows, cols, shell);
    
    CONTAINER_LOG("Starting thread");
    m_thread->Start();
    
    CONTAINER_LOG("Constructor DONE");
}

LocalTerminalContainer::~LocalTerminalContainer() {
    CONTAINER_LOG("Destructor called");
    StopTerminal();
    CONTAINER_LOG("Destructor done");
}

const ScreenBuffer* LocalTerminalContainer::GetFrontBuffer() const {
    if (m_thread) {
        return m_thread->GetFrontBuffer();
    }
    return nullptr;
}

void LocalTerminalContainer::SetUIHandler(wxWindow* ui_handler) {
    if (m_event_proxy) {
        m_event_proxy->SetTarget(ui_handler);
    }
}

void LocalTerminalContainer::ClearUIHandler() {
    if (m_event_proxy) {
        m_event_proxy->SetTarget(nullptr);
    }
}

void LocalTerminalContainer::StopTerminal() {
    if (m_thread) {
        CONTAINER_LOG("Stopping thread");
        m_thread->SetShuttingDown();
        CONTAINER_LOG("Waiting for thread to finish");
        m_thread->Wait();
        CONTAINER_LOG("Thread finished, deleting");
        delete m_thread;
        m_thread = nullptr;
    }
}

void LocalTerminalContainer::QueueInput(const std::string& input) {
    CONTAINER_LOG("QueueInput called: length=" << input.length() << " first_char=" << (input.length() > 0 ? (int)(unsigned char)input[0] : 0));
    if (m_thread) {
        m_thread->QueueInput(input);
        CONTAINER_LOG("QueueInput: queued to thread");
    } else {
        CONTAINER_LOG("QueueInput: thread is null, cannot queue");
    }
}

void LocalTerminalContainer::Resize(int rows, int cols) {
    if (m_thread) {
        m_thread->ResizeVTerm(rows, cols);
    }
}

void LocalTerminalContainer::Scroll(int lines) {
    if (m_thread) {
        m_thread->ScrollVTerm(lines);
    }
}

void LocalTerminalContainer::ResetScrollToBottom() {
    if (m_thread) {
        m_thread->ResetScrollToBottom();
    }
}
