#pragma once

#include <wx/wx.h>

// Interface for splitable components (decoupling UI from business logic)
class ISplitable {
public:
    virtual ~ISplitable() = default;
    
    // Get the actual window for UI operations
    virtual wxWindow* GetWindow() = 0;
    
    // Shutdown: stop threads, release resources
    virtual void Shutdown() = 0;
    
    // Check if this component can be split
    virtual bool CanSplit() const = 0;
};
