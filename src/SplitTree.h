#pragma once

#include <memory>
#include <vector>
#include "ISplitable.h"
#include "SplitNode.h"

// SplitTree manages the binary tree structure
class SplitTree {
public:
    SplitTree() = default;
    ~SplitTree() = default;
    
    // Initialize with root content
    void SetRoot(std::shared_ptr<ISplitable> content);
    
    // Split a specific node
    void SplitNode(ISplitable* target, wxSplitMode mode, std::shared_ptr<ISplitable> newContent);
    
    // Find node containing the target
    struct SplitNode* FindNode(ISplitable* target);
    
    // Get root node
    struct SplitNode* GetRoot() { return m_root.get(); }
    
    // Clear the tree
    void Clear();
    
    // Set root directly (for tree restructuring)
    void SetRootNode(std::unique_ptr<struct SplitNode> node);
    
    // Find parent node of a target
    struct SplitNode* FindParentNode(struct SplitNode* node, ISplitable* target);
    
private:
    std::unique_ptr<struct SplitNode> m_root;
    
    struct SplitNode* FindNodeRecursive(struct SplitNode* node, ISplitable* target);
};
