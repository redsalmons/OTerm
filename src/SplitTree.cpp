#include "SplitTree.h"
#include <wx/log.h>

void SplitTree::SetRoot(std::shared_ptr<ISplitable> content) {
    m_root = SplitNode::CreateLeaf(content);
}

void SplitTree::Clear() {
    m_root.reset();
}

void SplitTree::SetRootNode(std::unique_ptr<struct SplitNode> node) {
    m_root = std::move(node);
}

struct SplitNode* SplitTree::FindParentNode(struct SplitNode* node, ISplitable* target) {
    if (!node) return nullptr;
    
    // Check if left child contains target (directly or recursively)
    if (node->left) {
        if (node->left->type == SplitNodeType::Leaf && node->left->content.get() == target) {
            return node;
        }
        // Recursively search in left branch
        struct SplitNode* found = FindParentNode(node->left.get(), target);
        if (found) return found;
    }
    
    // Check if right child contains target (directly or recursively)
    if (node->right) {
        if (node->right->type == SplitNodeType::Leaf && node->right->content.get() == target) {
            return node;
        }
        // Recursively search in right branch
        struct SplitNode* found = FindParentNode(node->right.get(), target);
        if (found) return found;
    }
    
    return nullptr;
}

struct SplitNode* SplitTree::FindNodeRecursive(struct SplitNode* node, ISplitable* target) {
    if (!node) return nullptr;
    
    if (node->type == SplitNodeType::Leaf) {
        if (node->content.get() == target) {
            return node;
        }
    } else {
        // Check left and right branches
        struct SplitNode* found = FindNodeRecursive(node->left.get(), target);
        if (found) return found;
        return FindNodeRecursive(node->right.get(), target);
    }
    
    return nullptr;
}

struct SplitNode* SplitTree::FindNode(ISplitable* target) {
    return FindNodeRecursive(m_root.get(), target);
}

void SplitTree::SplitNode(ISplitable* target, wxSplitMode mode, std::shared_ptr<ISplitable> newContent) {
    struct SplitNode* targetNode = FindNode(target);
    if (!targetNode || targetNode->type != SplitNodeType::Leaf) {
        wxLogError("SplitNode: target not found or not a leaf node");
        return;
    }
    
    // TODO: Implement split logic
    // This will be implemented with SplitManager
}
