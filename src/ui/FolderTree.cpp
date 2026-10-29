#include "visu/ui/FolderTree.hpp"
#include "imgui.h"

namespace fs = std::filesystem;

fs::path drawFolderTree(
    const fs::path &_dir,
    std::function<void(const std::filesystem::path &)> _onDoubleClick,
    const std::vector<std::string> &_showExtensions,
    const std::vector<std::string> &_excludeStems,
    std::function<void(const std::filesystem::path &)> _contextMenu) {
  fs::path clicked;
  std::error_code ec;
  for (fs::directory_iterator it(_dir, ec), end; it != end && !ec;
       it.increment(ec)) {
    const auto &entry = *it;
    if (entry.is_directory()) {
      if (ImGui::TreeNode(entry.path().filename().string().c_str())) {
        fs::path sub = drawFolderTree(entry.path(), _onDoubleClick,
                                      _showExtensions, _excludeStems,
                                      _contextMenu);
        if (!sub.empty())
          clicked = sub;
        ImGui::TreePop();
      }
    } else {
      bool valide = false;
      if (_showExtensions.empty() == false) {
        for (const std::string &ext : _showExtensions) {
          if (entry.path().extension() == ext) {
            valide = true;
            break;
          }
        }
      }
      if (valide == false)
        continue;

      // Exclusion par nom de fichier (sans extension) : plomberie generee.
      const std::string stem = entry.path().stem().string();
      bool exclu = false;
      for (const std::string &s : _excludeStems)
        if (s == stem) {
          exclu = true;
          break;
        }
      if (exclu)
        continue;

      ImGui::TreeNodeEx(entry.path().filename().string().c_str(),
                        ImGuiTreeNodeFlags_Leaf |
                            ImGuiTreeNodeFlags_NoTreePushOnOpen);
      if (ImGui::IsItemClicked()) {
        clicked = entry.path();
      }
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        _onDoubleClick(entry.path());
      if (_contextMenu && ImGui::BeginPopupContextItem()) {
        _contextMenu(entry.path());
        ImGui::EndPopup();
      }
    }
  }
  return clicked;
}
