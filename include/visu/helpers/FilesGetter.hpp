#pragma once
#include <filesystem>
#include <string>
#include <vector>

inline std::vector<std::string>
getFilesInFolderWith(std::filesystem::path &_folderPath,
                     std::vector<std::string> _extensionsRequired) {
  if (!std::filesystem::is_directory(_folderPath))
    return {};

  std::vector<std::string> names;
  for (const std::filesystem::directory_entry &file :
       std::filesystem::directory_iterator(_folderPath)) {

    if (!file.is_regular_file())
      continue;

    bool isValide = false;
    for (const std::string &ext : _extensionsRequired) {
      if (file.path().extension() == ext) {
        isValide = true;
        break;
      }
    }
    if (!isValide)
      continue;
    names.push_back(file.path().filename().string());
  }
  return names;
};
inline std::vector<std::string>
getFilesInFolderWithout(std::filesystem::path &_folderPath,
                        std::vector<std::string> _extensionsDenied) {
  if (!std::filesystem::is_directory(_folderPath))
    return {};

  std::vector<std::string> names;
  for (const std::filesystem::directory_entry &file :
       std::filesystem::directory_iterator(_folderPath)) {

    if (!file.is_regular_file())
      continue;

    bool isValide = true;
    for (const std::string &ext : _extensionsDenied) {
      if (file.path().extension() == ext) {
        isValide = false;
        break;
      }
    }
    if (!isValide)
      continue;
    names.push_back(file.path().filename().string());
  }
  return names;
};