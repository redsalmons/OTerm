#pragma once

#include <memory>
#include <wx/splitter.h>
#include "ISplitable.h"

class SplitContainer;

enum class SplitNodeType {
    Leaf,      // Contains ISplitable (TerminalPanel)
    Branch     // Contains SplitContainer
};

struct SplitNode {
    SplitNodeType type;
    
    // For leaf nodes
    std::shared_ptr<ISplitable> content;
    
    // For branch nodes
    SplitContainer* container = nullptr;
    std::unique_ptr<SplitNode> left;
    std::unique_ptr<SplitNode> right;
    wxSplitMode splitMode;
    
    SplitNode(SplitNodeType t) : type(t) {}
    
    static std::unique_ptr<SplitNode> CreateLeaf(std::shared_ptr<ISplitable> content) {
        auto node = std::make_unique<SplitNode>(SplitNodeType::Leaf);
        node->content = content;
        return node;
    }
    
    static std::unique_ptr<SplitNode> CreateBranch(SplitContainer* container, 
                                                     std::unique_ptr<SplitNode> left,
                                                     std::unique_ptr<SplitNode> right,
                                                     wxSplitMode mode) {
        auto node = std::make_unique<SplitNode>(SplitNodeType::Branch);
        node->container = container;
        node->left = std::move(left);
        node->right = std::move(right);
        node->splitMode = mode;
        return node;
    }
};
