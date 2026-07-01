#include "SplitManager.h"
#include "SplitContainer.h"
#include "TerminalPanel.h"
#include "LocalTerminalContainer.h"
#include "TermGLCanvas.h"
#include <wx/log.h>
#include <wx/simplebook.h>
#include <fstream>
#include <filesystem>

wxDEFINE_EVENT(wxEVT_CLOSE_PANEL, wxCommandEvent);

#define SM_LOG(msg) \
    do { \
        std::ofstream f((std::filesystem::temp_directory_path() / "oterm_alert.log").string(), std::ios::app); \
        if (f.is_open()) f << "[SPLITMGR] " << msg << std::endl; \
    } while(0)

SplitManager::SplitManager(wxWindow* parent) 
    : m_parent(parent), m_tree(std::make_unique<SplitTree>()), m_alive(std::make_shared<bool>(true)) {
    if (m_parent) {
        m_parent->Bind(wxEVT_CLOSE_PANEL, &SplitManager::OnClosePanelEvent, this);
    }
}

SplitManager::~SplitManager() {
    SM_LOG("SplitManager destructor called");
    *m_alive = false; // 标记为已销毁
    
    // 解绑事件以防止在对象销毁后调用事件处理器
    // 检查 m_parent 是否有效（未被销毁）
    if (m_parent && !m_parent->IsBeingDeleted()) {
        m_parent->Unbind(wxEVT_CLOSE_PANEL, &SplitManager::OnClosePanelEvent, this);
        SM_LOG("SplitManager: event unbound");
    } else {
        SM_LOG("SplitManager: parent is null or being deleted, skipping unbind");
    }
    
    if (m_tree) {
        m_tree->Clear();
        SM_LOG("SplitManager: tree cleared");
    }
    SM_LOG("SplitManager destructor done");
}

void SplitManager::ApplySplitCallbackToPanel(TerminalPanel* panel) {
    if (!panel) return;
    TermGLCanvas* canvas = panel->GetCanvas();
    if (canvas) {
        if (m_splitCallback) canvas->SetSplitCallback(m_splitCallback);
        if (m_closeCallback) canvas->SetCloseCallback(m_closeCallback);
        SM_LOG("ApplySplitCallbackToPanel: callbacks set on panel=" << panel << " canvas=" << canvas);
    }
}

void SplitManager::Initialize(std::shared_ptr<ISplitable> rootContent) {
    m_tree->SetRoot(rootContent);
    // 设置 root panel 的 callbacks
    TerminalPanel* rootPanel = dynamic_cast<TerminalPanel*>(rootContent.get());
    if (rootPanel) {
        ApplySplitCallbackToPanel(rootPanel);
        SM_LOG("Initialize: callbacks set on root panel=" << rootPanel);
    }
}

SplitContainer* SplitManager::CreateContainer(wxWindow* parent, wxSplitMode mode) {
    return new SplitContainer(parent, mode);
}

void SplitManager::Freeze() {
    if (m_parent) {
        m_parent->Freeze();
    }
}

void SplitManager::Thaw() {
    if (m_parent) {
        m_parent->Thaw();
    }
}

wxWindow* SplitManager::GetRootWindow() const {
    if (!m_tree || !m_tree->GetRoot()) {
        return nullptr;
    }
    
    SplitNode* root = m_tree->GetRoot();
    if (root->type == SplitNodeType::Leaf) {
        return root->content->GetWindow();
    } else {
        return root->container;
    }
}

void SplitManager::Split(ISplitable* target, wxSplitMode mode) {
    if (!target || !target->CanSplit()) {
        SM_LOG("Split: target cannot be split");
        return;
    }
    
    Freeze();
    
    SplitNode* targetNode = m_tree->FindNode(target);
    if (!targetNode || targetNode->type != SplitNodeType::Leaf) {
        SM_LOG("Split: target node not found or not a leaf");
        Thaw();
        return;
    }
    
    // Check if parent is still valid before using it
    if (!m_parent) {
        SM_LOG("Split: parent is null, cannot create new panel");
        Thaw();
        return;
    }
    
    auto newContent = std::shared_ptr<TerminalPanel>(
        new TerminalPanel(m_parent.get(), std::make_unique<LocalTerminalContainer>(24, 80, "")),
        [](TerminalPanel* p) { 
            if (p && !p->IsBeingDeleted()) { 
                p->Destroy(); 
            }
        }
    );
    
    auto newPanel = std::dynamic_pointer_cast<TerminalPanel>(newContent);
    TermGLCanvas* newCanvas = nullptr;
    if (newPanel) {
        newCanvas = new TermGLCanvas(newPanel.get(), false);
        newPanel->SetCanvas(newCanvas);
        ApplySplitCallbackToPanel(newPanel.get());
        // Setup canvas connection to enable keyboard input and display
        newPanel->SetupCanvasConnection();
    }
    
    auto targetPanel = dynamic_cast<TerminalPanel*>(target);
    if (targetPanel && targetPanel->GetCanvas()) {
        targetPanel->GetCanvas()->ReinitializeGLContext();
    }
    
    PerformSplit(targetNode, mode, newContent);
    
    // Set focus to new panel's canvas after layout completes
    if (newCanvas) {
        newCanvas->CallAfter([newCanvas]() {
            newCanvas->SetFocus();
            SM_LOG("Split: SetFocus called via CallAfter on newCanvas=" << newCanvas);
        });
    }
    
    Thaw();
}

void SplitManager::PerformSplit(SplitNode* targetNode, wxSplitMode mode, std::shared_ptr<ISplitable> newContent) {
    if (!targetNode || targetNode->type != SplitNodeType::Leaf) {
        SM_LOG("PerformSplit: invalid target node");
        return;
    }
    
    auto targetContent = targetNode->content;
    wxWindow* targetWindow = targetContent->GetWindow();
    if (!targetWindow) {
        SM_LOG("PerformSplit: target window is null");
        return;
    }
    
    bool isRoot = (m_tree->GetRoot() == targetNode);
    
    SM_LOG("PerformSplit: isRoot=" << isRoot << " mode=" << mode 
           << " targetWindow=" << targetWindow 
           << " shown=" << targetWindow->IsShown()
           << " size=" << targetWindow->GetSize().GetWidth() << "x" << targetWindow->GetSize().GetHeight());
    
    wxWindow* newWindow = newContent->GetWindow();
    if (!newWindow) {
        SM_LOG("PerformSplit: new window is null");
        return;
    }
    
    if (isRoot) {
        // Root split: create SplitContainer, reparent both windows into it,
        // then replace the notebook page with the container.
        wxWindow* containerParent = targetWindow->GetParent();
        
        SplitContainer* container = CreateContainer(containerParent, mode);
        
        container->SetChildren(targetWindow, newWindow);
        
        // Set initial ratio to 50%
        container->ScaleSashPosition(0.5f);
        
        // Replace in notebook if parent is wxSimplebook
        wxSimplebook* notebook = dynamic_cast<wxSimplebook*>(containerParent);
        if (notebook) {
            int pageIndex = notebook->FindPage(targetWindow);
            SM_LOG("PerformSplit: notebook pageIndex=" << pageIndex << " page count=" << notebook->GetPageCount());
            if (pageIndex != wxNOT_FOUND) {
                wxString pageText = notebook->GetPageText(pageIndex);
                notebook->RemovePage(pageIndex);
                notebook->InsertPage(pageIndex, container, pageText, true);
            } else {
                // notebook 为空，直接添加页面
                SM_LOG("PerformSplit: notebook is empty, adding new page");
                notebook->AddPage(container, "Terminal", true);
            }
        }
        
        auto leftNode = SplitNode::CreateLeaf(targetContent);
        auto rightNode = SplitNode::CreateLeaf(newContent);
        auto branchNode = SplitNode::CreateBranch(container, std::move(leftNode), std::move(rightNode), mode);
        m_tree->SetRootNode(std::move(branchNode));
        
        container->Show(true);
        container->Layout();
        container->Refresh();
        container->Update();
        
        SM_LOG("PerformSplit: root split done, container=" << container);
        
    } else {
        // Nested split: find parent branch, replace target window with a new SplitContainer
        SplitNode* parentBranch = m_tree->FindParentNode(m_tree->GetRoot(), targetContent.get());
        if (!parentBranch || parentBranch->type != SplitNodeType::Branch) {
            SM_LOG("PerformSplit: cannot find parent branch");
            return;
        }
        
        SplitContainer* parentContainer = parentBranch->container;
        wxWindow* containerParent = parentContainer;
        
        // Create new container as child of parent container
        SplitContainer* nestedContainer = CreateContainer(containerParent, mode);
        
        nestedContainer->SetChildren(targetWindow, newWindow);
        
        // Set initial ratio to 50%
        nestedContainer->ScaleSashPosition(0.5f);
        
        // Replace targetWindow with nestedContainer in parent container
        // The parent container manages two windows directly - we need to swap one out
        wxWindow* first = parentContainer->GetFirst();
        wxWindow* second = parentContainer->GetSecond();
        
        if (first == targetWindow) {
            parentContainer->SetChildren(nestedContainer, second);
        } else if (second == targetWindow) {
            parentContainer->SetChildren(first, nestedContainer);
        } else {
            SM_LOG("PerformSplit: targetWindow not found in parent container");
            delete nestedContainer;
            return;
        }
        
        // Update tree
        bool isLeftChild = (parentBranch->left.get() == targetNode);
        auto nestedLeft = SplitNode::CreateLeaf(targetContent);
        auto nestedRight = SplitNode::CreateLeaf(newContent);
        auto nestedBranch = SplitNode::CreateBranch(nestedContainer, std::move(nestedLeft), std::move(nestedRight), mode);
        
        if (isLeftChild) {
            parentBranch->left = std::move(nestedBranch);
        } else {
            parentBranch->right = std::move(nestedBranch);
        }
        
        nestedContainer->Show(true);
        parentContainer->Layout();
        parentContainer->Refresh();
        parentContainer->Update();
        
        SM_LOG("PerformSplit: nested split done");
    }
}

void SplitManager::Close(TerminalPanel* panel) {
    if (!panel) {
        SM_LOG("Close: panel is null");
        return;
    }
    if (!m_parent) {
        SM_LOG("Close: parent is null, cannot post event");
        return;
    }
    SM_LOG("Close: posting close event for panel=" << panel);
    wxCommandEvent* evt = new wxCommandEvent(wxEVT_CLOSE_PANEL);
    evt->SetClientData(panel);
    wxQueueEvent(m_parent.get(), evt);
}

void SplitManager::OnClosePanelEvent(wxCommandEvent& event) {
    TerminalPanel* panel = static_cast<TerminalPanel*>(event.GetClientData());
    SM_LOG("OnClosePanelEvent: panel=" << panel);
    DoClose(panel);
}

void SplitManager::DoClose(TerminalPanel* panel) {
    if (!panel) {
        SM_LOG("DoClose: panel is null");
        return;
    }
    
    // Find the correct notebook page index before doing any changes
    m_lastClosePageIndex = -1;
    wxSimplebook* notebook = dynamic_cast<wxSimplebook*>(m_parent.get());
    if (notebook) {
        wxWindow* current = panel;
        while (current) {
            int index = notebook->FindPage(current);
            if (index != wxNOT_FOUND) {
                m_lastClosePageIndex = index;
                break;
            }
            current = current->GetParent();
            if (current == notebook) break;
        }
    }
    SM_LOG("DoClose: found m_lastClosePageIndex=" << m_lastClosePageIndex << " for panel=" << panel);
    
    Freeze();
    
    SplitNode* targetNode = m_tree->FindNode(panel);
    if (!targetNode || targetNode->type != SplitNodeType::Leaf) {
        SM_LOG("DoClose: node not found or not a leaf");
        Thaw();
        return;
    }
    
    SplitNode* root = m_tree->GetRoot();
    if (root == targetNode) {
        SM_LOG("DoClose: cannot close last panel (target is root)");
        Thaw();
        return;
    }
    
    // 如果root是Leaf，说明这是最后一个panel，不允许关闭
    if (root->type == SplitNodeType::Leaf) {
        SM_LOG("DoClose: cannot close last panel (root is leaf)");
        Thaw();
        return;
    }
    
    SplitNode* parentBranch = m_tree->FindParentNode(root, panel);
    if (!parentBranch || parentBranch->type != SplitNodeType::Branch) {
        SM_LOG("DoClose: cannot find parent branch");
        Thaw();
        return;
    }
    
    bool isLeftChild = (parentBranch->left.get() == targetNode);
    
    panel->Shutdown();
    targetNode->content.reset();
    
    std::unique_ptr<SplitNode> sibling = std::move(isLeftChild ? parentBranch->right : parentBranch->left);
    
    SplitContainer* containerToDestroy = parentBranch->container;
    parentBranch->container = nullptr;
    containerToDestroy->SetChildren(nullptr, nullptr);
    
    // 树更新
    std::function<SplitNode*(SplitNode*)> findGrandparent = [&](SplitNode* node) -> SplitNode* {
        if (!node || node->type == SplitNodeType::Leaf) return nullptr;
        if (node->left.get() == parentBranch || node->right.get() == parentBranch) return node;
        SplitNode* found = findGrandparent(node->left.get());
        if (found) return found;
        return findGrandparent(node->right.get());
    };
    SplitNode* grandparent = findGrandparent(root);
    
    if (grandparent) {
        bool parentIsLeft = (grandparent->left.get() == parentBranch);
        
        SM_LOG("DoClose: sibling type=" << (sibling->type == SplitNodeType::Leaf ? "Leaf" : "Branch"));
        
        // Replace parentBranch with sibling in grandparent
        if (parentIsLeft) {
            grandparent->left = std::move(sibling);
        } else {
            grandparent->right = std::move(sibling);
        }
        
        // Composite pattern: grandparent's container should handle both Leaf and Branch children
        // Update the container's children directly instead of destroying it
        if (grandparent->container) {
            SM_LOG("DoClose: updating grandparent container children (composite pattern)");
            wxWindow* firstWin = nullptr;
            wxWindow* secondWin = nullptr;
            
            if (grandparent->left) {
                firstWin = (grandparent->left->type == SplitNodeType::Leaf) 
                    ? grandparent->left->content->GetWindow() 
                    : grandparent->left->container;
            }
            if (grandparent->right) {
                secondWin = (grandparent->right->type == SplitNodeType::Leaf) 
                    ? grandparent->right->content->GetWindow() 
                    : grandparent->right->container;
            }
            
            grandparent->container->SetChildren(firstWin, secondWin);
            grandparent->container->UpdateLayout();
        }
    } else {
        m_tree->SetRootNode(std::move(sibling));
    }
    
    SM_LOG("DoClose: tree updated, preparing UI cleanup");
    
    // 彻底剥离：强制 Reparent 所有子窗口到安全位置
    if (containerToDestroy) {
        containerToDestroy->Hide();
        
        wxWindowList& children = containerToDestroy->GetChildren();
        SM_LOG("DoClose: container has " << children.size() << " children");
        for (auto it = children.begin(); it != children.end(); ) {
            wxWindow* child = *it;
            ++it;
            if (child) {
                SM_LOG("DoClose: reparenting child=" << child << " to m_parent");
                child->Reparent(m_parent);
                child->Hide();
            }
        }
        
        // 从 notebook 移除
        wxSimplebook* notebook = dynamic_cast<wxSimplebook*>(m_parent.get());
        if (notebook) {
            int pageIndex = notebook->FindPage(containerToDestroy);
            SM_LOG("DoClose: container page index=" << pageIndex << " page count before=" << notebook->GetPageCount());
            if (pageIndex != wxNOT_FOUND) {
                notebook->RemovePage(pageIndex);
                SM_LOG("DoClose: container removed from notebook, page count after=" << notebook->GetPageCount());
            }
        }
        
        containerToDestroy->Destroy();
        SM_LOG("DoClose: old container destroyed");
    }
    
    // Don't do grandparent container destruction here - let RebuildUIFromTree handle it
    // The tree structure has been updated, RebuildUIFromTree will rebuild the UI properly
    
    SM_LOG("DoClose: scheduling UI rebuild in CallAfter");
    Thaw();
    
    // 延迟重建 UI，确保 Destroy 事件完成
    auto alive = m_alive; // 捕获 shared_ptr
    if (m_parent) {
        m_parent->CallAfter([this, alive]() {
            if (!*alive) return; // 检查是否已销毁
            SM_LOG("DoClose: CallAfter executing RebuildUIFromTree");
            RebuildUIFromTree();
            if (m_parent) {
                m_parent->Layout();
            }
            SM_LOG("DoClose: CallAfter UI rebuild done");
        });
    }
}

void SplitManager::RebuildUIRecursive(SplitNode* node, wxWindow* parentWindow) {
    if (!node) return;
    
    if (node->type == SplitNodeType::Leaf) {
        wxWindow* win = node->content->GetWindow();
        if (win) {
            // Check if window is being destroyed to prevent use-after-free
            if (win->IsBeingDeleted()) {
                SM_LOG("RebuildUIRecursive: window is being deleted, skipping: " << win);
                return;
            }
            win->Reparent(parentWindow);
            win->Show(true);
            
            // Reinitialize GL context after reparenting to avoid white screen on Windows
            TerminalPanel* panel = dynamic_cast<TerminalPanel*>(win);
            if (panel && panel->GetCanvas()) {
                panel->GetCanvas()->ReinitializeGLContext();
            }
        }
    } else {
        SplitContainer* container = node->container;
        if (container) {
            // Check if container is being destroyed to prevent use-after-free
            if (container->IsBeingDeleted()) {
                SM_LOG("RebuildUIRecursive: container is being deleted, skipping: " << container);
                return;
            }
            container->Reparent(parentWindow);
            container->Show(true);
            RebuildUIRecursive(node->left.get(), container);
            RebuildUIRecursive(node->right.get(), container);
            wxWindow* firstWin = nullptr;
            wxWindow* secondWin = nullptr;
            if (node->left) {
                firstWin = (node->left->type == SplitNodeType::Leaf) 
                    ? node->left->content->GetWindow() 
                    : node->left->container;
            }
            if (node->right) {
                secondWin = (node->right->type == SplitNodeType::Leaf) 
                    ? node->right->content->GetWindow() 
                    : node->right->container;
            }
            if (firstWin && secondWin) {
                container->SetChildren(firstWin, secondWin);
            }
            container->Show(true);
            
            // For SplitContainer, Layout() does nothing because it has no sizers.
            // We must call UpdateLayout() explicitly to position children.
            container->UpdateLayout();
            
            container->Refresh();
            container->Update();
            SM_LOG("RebuildUIRecursive: branch done, container=" << container);
        }
    }
}

void SplitManager::RebuildUIFromTree() {
    SplitNode* root = m_tree->GetRoot();
    if (!root) return;
    
    SM_LOG("RebuildUIFromTree: root type=" << (root->type == SplitNodeType::Leaf ? "Leaf" : "Branch"));
    
    if (root->type == SplitNodeType::Leaf) {
        wxWindow* win = root->content->GetWindow();
        if (win) {
            SM_LOG("RebuildUIFromTree: leaf, win=" << win);
            wxSimplebook* notebook = dynamic_cast<wxSimplebook*>(m_parent.get());
            if (notebook) {
                int pageIndex = notebook->FindPage(win);
                SM_LOG("RebuildUIFromTree: leaf, FindPage=" << pageIndex);
                SM_LOG("RebuildUIFromTree: leaf, notebook page count=" << notebook->GetPageCount());
                for (size_t i = 0; i < notebook->GetPageCount(); ++i) {
                    SM_LOG("RebuildUIFromTree: leaf, page " << i << " ptr=" << notebook->GetPage(i));
                }
                if (pageIndex == wxNOT_FOUND) {
                    int targetIndex = m_lastClosePageIndex;
                    if (targetIndex < 0 || targetIndex >= (int)notebook->GetPageCount()) {
                        targetIndex = notebook->GetSelection();
                    }
                    if (targetIndex < 0 || targetIndex >= (int)notebook->GetPageCount()) {
                        targetIndex = notebook->GetPageCount() - 1;
                    }
                    SM_LOG("RebuildUIFromTree: leaf, targetIndex=" << targetIndex);
                    if (targetIndex >= 0 && targetIndex < (int)notebook->GetPageCount()) {
                        wxString pageText = notebook->GetPageText(targetIndex);
                        wxWindow* oldPage = notebook->GetPage(targetIndex);
                        SM_LOG("RebuildUIFromTree: leaf, replacing oldPage=" << oldPage << " with win=" << win);
                        
                        win->Reparent(notebook);
                        win->SetSize(notebook->GetClientSize());
                        win->Show(true);
                        
                        // Reinitialize GL context after reparenting to avoid white screen on Windows
                        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(win);
                        if (panel && panel->GetCanvas()) {
                            panel->GetCanvas()->ReinitializeGLContext();
                        }
                        
                        notebook->RemovePage(targetIndex);
                        notebook->InsertPage(targetIndex, win, pageText, true);
                        notebook->SetSelection(targetIndex);
                        
                        notebook->Layout();
                        notebook->Refresh();
                        notebook->Update();
                        notebook->SendSizeEvent();
                        
                        if (oldPage && oldPage != win) {
                            wxWindow* pageToDestroy = oldPage;
                            auto alive = m_alive;
                            if (m_parent) {
                                m_parent->CallAfter([pageToDestroy, alive]() {
                                    if (!*alive) return;
                                    SM_LOG("RebuildUIFromTree: CallAfter destroying oldPage=" << pageToDestroy);
                                    if (pageToDestroy) {
                                        pageToDestroy->Destroy();
                                        SM_LOG("RebuildUIFromTree: CallAfter oldPage destroyed");
                                    }
                                });
                            }
                        }
                    } else {
                        // notebook 为空，直接添加页面
                        SM_LOG("RebuildUIFromTree: leaf, notebook is empty, adding new page");
                        win->Reparent(notebook);
                        win->SetSize(notebook->GetClientSize());
                        win->Show(true);
                        
                        // Reinitialize GL context after reparenting to avoid white screen on Windows
                        TerminalPanel* panel = dynamic_cast<TerminalPanel*>(win);
                        if (panel && panel->GetCanvas()) {
                            panel->GetCanvas()->ReinitializeGLContext();
                        }
                        
                        notebook->AddPage(win, "Terminal", true);
                        notebook->Layout();
                        notebook->Refresh();
                        notebook->Update();
                    }
                } else {
                    SM_LOG("RebuildUIFromTree: leaf, win already in notebook at page " << pageIndex);
                }
            }
            win->Show(true);
            win->Layout();
            win->Refresh();
            win->Update();
            SM_LOG("RebuildUIFromTree: leaf, done showing win");
        }
    } else {
        SM_LOG("RebuildUIFromTree: branch");
        SplitContainer* container = root->container;
        if (container) {
            // Check if container is being destroyed to prevent use-after-free
            if (container->IsBeingDeleted()) {
                SM_LOG("RebuildUIFromTree: container is being deleted, skipping: " << container);
                return;
            }
            SM_LOG("RebuildUIFromTree: branch, container=" << container);
            wxSimplebook* notebook = dynamic_cast<wxSimplebook*>(m_parent.get());
            if (notebook) {
                int pageIndex = notebook->FindPage(container);
                SM_LOG("RebuildUIFromTree: branch, FindPage=" << pageIndex);
                if (pageIndex == wxNOT_FOUND) {
                    // container 不在 notebook 中，可能刚被提升为 root
                    int targetIndex = m_lastClosePageIndex;
                    if (targetIndex < 0 || targetIndex >= (int)notebook->GetPageCount()) {
                        targetIndex = notebook->GetSelection();
                    }
                    if (targetIndex < 0 || targetIndex >= (int)notebook->GetPageCount()) {
                        targetIndex = notebook->GetPageCount() - 1;
                    }
                    SM_LOG("RebuildUIFromTree: branch (root), targetIndex=" << targetIndex);
                    
                    if (targetIndex >= 0 && targetIndex < (int)notebook->GetPageCount()) {
                        wxString pageText = notebook->GetPageText(targetIndex);
                        wxWindow* oldPage = notebook->GetPage(targetIndex);
                        SM_LOG("RebuildUIFromTree: branch, replacing oldPage=" << oldPage << " with container=" << container);
                        
                        container->Reparent(notebook);
                        container->SetSize(notebook->GetClientSize());
                        container->Show(true);
                        
                        notebook->RemovePage(targetIndex);
                        notebook->InsertPage(targetIndex, container, pageText, true);
                        notebook->SetSelection(targetIndex);
                        
                        if (oldPage && oldPage != container) {
                            wxWindow* pageToDestroy = oldPage;
                            auto alive = m_alive;
                            if (m_parent) {
                                m_parent->CallAfter([pageToDestroy, alive]() {
                                    if (!*alive) return;
                                    pageToDestroy->Destroy();
                                });
                            }
                        }
                    } else {
                        // notebook 为空，直接添加页面
                        SM_LOG("RebuildUIFromTree: branch, notebook is empty, adding new page");
                        container->Reparent(notebook);
                        container->SetSize(notebook->GetClientSize());
                        container->Show(true);
                        notebook->AddPage(container, "Terminal", true);
                    }
                } else {
                    // container 已经在 notebook 中（是 root container），需要重建子节点
                    SM_LOG("RebuildUIFromTree: branch, win already in notebook at page " << pageIndex);
                    // Check if container is still valid before showing
                    if (container && !container->IsBeingDeleted()) {
                        container->Show(true);
                        // Rebuild children to match new tree structure
                        SM_LOG("RebuildUIFromTree: branch, rebuilding children for existing container");
                        RebuildUIRecursive(root->left.get(), container);
                        RebuildUIRecursive(root->right.get(), container);
                    } else {
                        SM_LOG("RebuildUIFromTree: branch, container is null or being deleted, skipping Show");
                        return;
                    }
                }
                
                // Check if notebook is still valid before operations
                if (notebook && !notebook->IsBeingDeleted()) {
                    notebook->Layout();
                    notebook->Refresh();
                    notebook->Update();
                    notebook->SendSizeEvent();
                } else {
                    SM_LOG("RebuildUIFromTree: notebook is null or being deleted, skipping operations");
                    return;
                }
            }
            
            // 确保 container 得到正确尺寸后更新布局
            if (container && !container->IsBeingDeleted()) {
                container->UpdateLayout();
            } else {
                SM_LOG("RebuildUIFromTree: container is null or being deleted, skipping UpdateLayout");
                return;
            }
            
            // Check if children are valid before recursive calls
            if (!root->left || !root->right) {
                SM_LOG("RebuildUIFromTree: children are null, skipping recursive calls");
                return;
            }
            
            SM_LOG("RebuildUIFromTree: calling RebuildUIRecursive for left child");
            RebuildUIRecursive(root->left.get(), container);
            SM_LOG("RebuildUIFromTree: calling RebuildUIRecursive for right child");
            RebuildUIRecursive(root->right.get(), container);
            wxWindow* firstWin = nullptr;
            wxWindow* secondWin = nullptr;
            if (root->left) {
                firstWin = (root->left->type == SplitNodeType::Leaf) 
                    ? root->left->content->GetWindow() 
                    : root->left->container;
            }
            if (root->right) {
                secondWin = (root->right->type == SplitNodeType::Leaf) 
                    ? root->right->content->GetWindow() 
                    : root->right->container;
            }
            if (firstWin && secondWin) {
                container->SetChildren(firstWin, secondWin);
            }
            container->Show(true);
            
            // For SplitContainer, Layout() does nothing because it has no sizers.
            // We must call UpdateLayout() explicitly to position children.
            container->UpdateLayout();
            
            container->Refresh();
            container->Update();
            SM_LOG("RebuildUIFromTree: branch, done");
        }
    }
    
    // 强制刷新 notebook
    wxSimplebook* notebook = dynamic_cast<wxSimplebook*>(m_parent.get());
    if (notebook) {
        SM_LOG("RebuildUIFromTree: refreshing notebook");
        notebook->Layout();
        notebook->Refresh();
        notebook->Update();
        SM_LOG("RebuildUIFromTree: notebook refreshed, page count=" << notebook->GetPageCount());
    }
    SM_LOG("RebuildUIFromTree: done");
}
