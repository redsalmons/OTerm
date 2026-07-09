#include "LocalTerminalContainer.h"
#include <fstream>
#include <filesystem>
#include <iostream>

#define CONTAINER_LOG(msg) ((void)0)

LocalTerminalContainer::LocalTerminalContainer(int rows, int cols, const std::string& shell)
    : m_terminalManager(),
      m_vtermManager(),
      m_event_proxy(std::make_shared<EventProxy>()),
      m_frontBuffer(),
      m_backBuffer(),
      m_rows(rows),
      m_cols(cols),
      m_hasDamage(false) {
    
    CONTAINER_LOG("LocalTerminalContainer constructor rows=" << rows << " cols=" << cols << " shell=" << shell);
    
    m_frontBuffer.resize(rows, cols);
    m_backBuffer.resize(rows, cols);

    m_vtermManager.initialize(rows, cols);
    m_vtermManager.set_damage_callback([this](VTermRect rect, const std::vector<std::vector<VTermManager::TerminalCell>>& cells) {
        m_hasDamage = true;
    });

    if (!m_terminalManager.Start(shell)) {
        std::cerr << "LocalTerminalContainer: Failed to start LocalTerminalManager" << std::endl;
    }

#ifdef _WIN32
    // Timer setup
    m_readTimer.SetOwner(this, READ_TIMER_ID);
    Bind(wxEVT_TIMER, &LocalTerminalContainer::OnReadTimer, this, READ_TIMER_ID);
    
    // Start reading with 15ms intervals (~60 FPS polling)
    m_readTimer.Start(15);
#else
    // Unix: Register FD with the event loop
    auto registerFd = [this]() {
        wxEventLoopBase* loop = wxEventLoop::GetActive();
        if (loop && m_terminalManager.GetFd() >= 0 && !m_fdSource) {
            m_fdSource = loop->AddSourceForFD(m_terminalManager.GetFd(), this, wxEVENT_SOURCE_INPUT);
            ProcessRead(); // Drain any data that arrived before registration
        }
    };

    if (wxEventLoop::GetActive()) {
        registerFd();
    } else if (wxTheApp) {
        // Use CallAfter to ensure the event loop is active, particularly for the initial tab created during app startup.
        wxTheApp->CallAfter(registerFd);
    }
#endif
    
    // Update initial screen display
    UpdateBackBuffer();
    TriggerDamage();
}

LocalTerminalContainer::~LocalTerminalContainer() {
    StopTerminal();
}

const ScreenBuffer* LocalTerminalContainer::GetFrontBuffer() const {
    return &m_frontBuffer;
}

void LocalTerminalContainer::CopyFrontBuffer(ScreenBuffer& dest) const {
    dest = m_frontBuffer;
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
#ifdef _WIN32
    m_readTimer.Stop();
#else
    if (m_fdSource) {
        delete m_fdSource;
        m_fdSource = nullptr;
    }
#endif
    m_terminalManager.Stop();
}

void LocalTerminalContainer::QueueInput(const std::string& input) {
    if (input.empty()) return;
    m_terminalManager.Write(input.c_str(), input.length());
}

void LocalTerminalContainer::Resize(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return;

    m_rows = rows;
    m_cols = cols;

    m_vtermManager.resize(rows, cols);
    m_terminalManager.Resize(rows, cols);

    m_frontBuffer.resize(rows, cols);
    m_frontBuffer.clear();
    m_backBuffer.resize(rows, cols);
    m_backBuffer.clear();

    UpdateBackBuffer();
    TriggerDamage();
}

void LocalTerminalContainer::Scroll(int lines) {
    if (lines > 0) {
        m_vtermManager.scroll_up(lines);
    } else {
        m_vtermManager.scroll_down(-lines);
    }
    UpdateBackBuffer();
    TriggerDamage();
}

void LocalTerminalContainer::ResetScrollToBottom() {
    m_vtermManager.set_scroll_offset(0);
    UpdateBackBuffer();
    TriggerDamage();
}

bool LocalTerminalContainer::IsSessionAlive() const {
    return m_terminalManager.IsRunning();
}

bool LocalTerminalContainer::IsInAlternateScreen() const {
    return m_vtermManager.is_in_alternate_screen();
}

int LocalTerminalContainer::GetScrollOffset() const {
    return m_vtermManager.get_scroll_offset();
}

#ifndef _WIN32
void LocalTerminalContainer::OnReadWaiting() {
    ProcessRead();
}

void LocalTerminalContainer::OnWriteWaiting() {
    // Nothing to do for write
}

void LocalTerminalContainer::OnExceptionWaiting() {
    StopTerminal();
    TriggerDamage();
}
#endif

#ifdef _WIN32
void LocalTerminalContainer::OnReadTimer(wxTimerEvent& event) {
    if (!m_terminalManager.IsRunning()) {
        m_readTimer.Stop();
        return;
    }
    ProcessRead();
}
#endif

void LocalTerminalContainer::ProcessRead() {
    if (!m_terminalManager.IsRunning()) {
        return;
    }

    char buffer[8192];
    bool has_data = false;

    // Read loop (since it's non-blocking PeekNamedPipe)
    while (true) {
        int bytesRead = m_terminalManager.Read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            m_vtermManager.write_input_no_flush(buffer, bytesRead);
            has_data = true;
        } else if (bytesRead == -2) {
            // EAGAIN / No data available
            break;
        } else if (bytesRead == 0) {
            // EOF: shell has exited
            StopTerminal();
            TriggerDamage();
            return;
        } else {
            // Error
            StopTerminal();
            TriggerDamage();
            return;
        }
    }

    if (has_data) {
        m_vtermManager.flush_damage();
    }

    if (m_hasDamage) {
        UpdateBackBuffer();
        TriggerDamage();
        m_hasDamage = false;
    }
}

void LocalTerminalContainer::UpdateBackBuffer() {
    int vterm_rows = m_vtermManager.get_rows();
    for (int row = 0; row < vterm_rows; ++row) {
        const auto& row_cells = m_vtermManager.get_screen_row(row);
        for (int col = 0; col < m_cols && col < (int)row_cells.size(); ++col) {
            const auto& cell = row_cells[col];
            CellInstance& inst = m_backBuffer.cells[row][col];
            inst.cell_x = (float)col;
            inst.cell_y = (float)row;
            inst.fg_color = cell.fg_color;
            inst.bg_color = cell.bg_color;
            inst.char_code = cell.char_code;
            inst.width = cell.width;
            inst.attrs = cell.attrs;
        }
    }

    VTermPos cursor_pos = m_vtermManager.get_cursor_pos();
    m_backBuffer.cursor_row = cursor_pos.row;
    m_backBuffer.cursor_col = cursor_pos.col;

    SwapBuffers();
}

void LocalTerminalContainer::SwapBuffers() {
    std::swap(m_frontBuffer, m_backBuffer);
}

void LocalTerminalContainer::TriggerDamage() {
    if (m_event_proxy) {
        VTermPos cursor = m_vtermManager.get_cursor_pos();
        m_event_proxy->PostDamageEvent(m_rows, m_cols, cursor.row, cursor.col, 0);
    }
}
