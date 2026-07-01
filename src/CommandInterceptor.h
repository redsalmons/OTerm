#pragma once

#include <string>
#include <vector>
#include <functional>

// CommandInterceptor checks if user input commands should be intercepted
// Currently intercepts commands starting with "ssh" or "device"
class CommandInterceptor {
public:
    // Result type for command interception check
    enum class InterceptionResult {
        Normal,      // Command should be sent normally
        Intercepted  // Command should be intercepted (send Ctrl+C instead of Enter)
    };

    CommandInterceptor();
    ~CommandInterceptor();

    // Check if a command should be intercepted
    // command: The full command line input by user
    // Returns InterceptionResult indicating whether to intercept
    InterceptionResult ShouldIntercept(const std::string& command);

    // Get the list of intercepted command prefixes
    const std::vector<std::string>& GetInterceptedPrefixes() const;

    // Add a new command prefix to intercept
    void AddInterceptedPrefix(const std::string& prefix);

    // Remove a command prefix from interception list
    void RemoveInterceptedPrefix(const std::string& prefix);

private:
    // Parse command line to extract the actual command (first word)
    std::string ExtractCommand(const std::string& commandLine);

    // Check if a command starts with any intercepted prefix
    bool IsInterceptedCommand(const std::string& command);

    // List of command prefixes to intercept
    std::vector<std::string> m_interceptedPrefixes;
};
