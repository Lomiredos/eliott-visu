#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

std::filesystem::path drawFolderTree(const std::filesystem::path &_dir,
                                     std::function<void(const std::filesystem::path &)> _onDoubleClick,
                                     const std::vector<std::string> &_showExtensions = {""},
                                     const std::vector<std::string> &_excludeStems = {},
                                     // Optionnel : contenu du menu clic droit d'un fichier
                                     // (appele entre BeginPopupContextItem/EndPopup).
                                     std::function<void(const std::filesystem::path &)> _contextMenu = nullptr);
