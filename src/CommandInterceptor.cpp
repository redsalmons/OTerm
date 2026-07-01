#include "CommandInterceptor.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <filesystem>

#define INTERCEPTOR_LOG(msg) \
    do { \
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app); \
        if (f.is_open()) f << "[INTERCEPTOR] " << msg << std::endl; \
    } while(0)

CommandInterceptor::CommandInterceptor() {
    // Initialize with default intercepted prefixes
    m_interceptedPrefixes = {"oc ssh", "oc device"};
    INTERCEPTOR_LOG("CommandInterceptor initialized with prefixes: oc ssh, oc device");
}

CommandInterceptor::~CommandInterceptor() {
    INTERCEPTOR_LOG("CommandInterceptor destroyed");
}

std::string CommandInterceptor::ExtractCommand(const std::string& commandLine) {
    // Parse the command line to extract the first word (the actual command)
    // Skip leading whitespace
    size_t start = commandLine.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return ""; // Empty or whitespace-only line
    }

    // Find the end of the first word
    size_t end = commandLine.find_first_of(" \t\n\r", start);
    if (end == std::string::npos) {
        return commandLine.substr(start); // Single word command
    }

    return commandLine.substr(start, end - start);
}

bool CommandInterceptor::IsInterceptedCommand(const std::string& commandLine) {
    // Check if the command line starts with any intercepted prefix
    // Skip leading whitespace first
    size_t start = commandLine.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return false;
    }

    std::string trimmedCommand = commandLine.substr(start);

    for (const auto& prefix : m_interceptedPrefixes) {
        if (trimmedCommand.length() >= prefix.length() &&
            trimmedCommand.substr(0, prefix.length()) == prefix) {
            INTERCEPTOR_LOG("Command line '" << trimmedCommand << "' matches intercepted prefix '" << prefix << "'");
            return true;
        }
    }
    return false;
}

CommandInterceptor::InterceptionResult CommandInterceptor::ShouldIntercept(const std::string& command) {
    if (command.empty()) {
        INTERCEPTOR_LOG("Empty command, not intercepting");
        return InterceptionResult::Normal;
    }

    INTERCEPTOR_LOG("Checking command line: '" << command << "'");

    if (IsInterceptedCommand(command)) {
        INTERCEPTOR_LOG("Command line '" << command << "' should be INTERCEPTED");
        return InterceptionResult::Intercepted;
    }

    INTERCEPTOR_LOG("Command line '" << command << "' is NORMAL");
    return InterceptionResult::Normal;
}

const std::vector<std::string>& CommandInterceptor::GetInterceptedPrefixes() const {
    return m_interceptedPrefixes;
}

void CommandInterceptor::AddInterceptedPrefix(const std::string& prefix) {
    if (std::find(m_interceptedPrefixes.begin(), m_interceptedPrefixes.end(), prefix) == m_interceptedPrefixes.end()) {
        m_interceptedPrefixes.push_back(prefix);
        INTERCEPTOR_LOG("Added intercepted prefix: " << prefix);
    }
}

void CommandInterceptor::RemoveInterceptedPrefix(const std::string& prefix) {
    auto it = std::find(m_interceptedPrefixes.begin(), m_interceptedPrefixes.end(), prefix);
    if (it != m_interceptedPrefixes.end()) {
        m_interceptedPrefixes.erase(it);
        INTERCEPTOR_LOG("Removed intercepted prefix: " << prefix);
    }
}
