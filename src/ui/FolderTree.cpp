#include "visu/ui/FolderTree.hpp"

#include "imgui.h"

#include <algorithm>

namespace fs = std::filesystem;

FolderTree::FolderTree(std::vector<std::string> _extensions, std::vector<std::string> _excludeStems)
    : m_extensions(std::move(_extensions)), m_excludeStems(std::move(_excludeStems))
{
}

bool FolderTree::accepts(const fs::path &_file) const
{
    const std::string ext = _file.extension().string();
    const std::string stem = _file.stem().string();
    return std::find(m_extensions.begin(), m_extensions.end(), ext) != m_extensions.end() &&
           std::find(m_excludeStems.begin(), m_excludeStems.end(), stem) == m_excludeStems.end();
}

std::vector<FolderTree::Node> FolderTree::scan(const fs::path &_dir) const
{
    std::vector<Node> nodes;
    std::error_code ec;
    for (fs::directory_iterator it(_dir, ec), end; it != end && !ec; it.increment(ec))
    {
        std::error_code typeEc;
        Node node;
        node.path = it->path();
        node.label = node.path.filename().string();
        node.isDir = it->is_directory(typeEc);
        if (node.isDir)
            node.children = scan(node.path);
        else if (!accepts(node.path))
            continue;
        nodes.push_back(std::move(node));
    }
    std::sort(nodes.begin(), nodes.end(), [](const Node &_a, const Node &_b)
              { return _a.isDir != _b.isDir ? _a.isDir : _a.label < _b.label; });
    return nodes;
}

fs::path FolderTree::draw(const fs::path &_dir, const FileAction &_onDoubleClick,
                          const FileAction &_contextMenu)
{
    const auto now = std::chrono::steady_clock::now();
    if (m_stale || _dir != m_dir || now - m_lastScan >= std::chrono::seconds(1))
    {
        m_nodes = scan(_dir);
        m_dir = _dir;
        m_lastScan = now;
        m_stale = false;
    }

    fs::path clicked;
    drawNodes(m_nodes, _onDoubleClick, _contextMenu, clicked);
    return clicked;
}

void FolderTree::drawNodes(const std::vector<Node> &_nodes, const FileAction &_onDoubleClick,
                           const FileAction &_contextMenu, fs::path &_clicked) const
{
    for (const Node &node : _nodes)
    {
        if (node.isDir)
        {
            if (ImGui::TreeNode(node.label.c_str()))
            {
                drawNodes(node.children, _onDoubleClick, _contextMenu, _clicked);
                ImGui::TreePop();
            }
            continue;
        }

        ImGui::TreeNodeEx(node.label.c_str(),
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        if (ImGui::IsItemClicked())
            _clicked = node.path;
        if (_onDoubleClick && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            _onDoubleClick(node.path);
        if (_contextMenu && ImGui::BeginPopupContextItem())
        {
            _contextMenu(node.path);
            ImGui::EndPopup();
        }
    }
}
