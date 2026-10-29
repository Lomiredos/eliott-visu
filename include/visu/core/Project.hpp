#pragma once

#include <filesystem>
#include <string>

class Project {
public:
  explicit Project(std::filesystem::path _root);

  bool isValid() const;
  std::string name() const;

  const std::filesystem::path &root() const { return m_root; }
  std::filesystem::path systemsDir() const;
  std::filesystem::path componentsDir() const;
  // Headers du moteur (submodule du projet) : components/, systems/, ...
  std::filesystem::path engineIncludeDir() const;
  std::filesystem::path componentsCatalog() const;
  std::filesystem::path scenesDataDir() const;
  std::filesystem::path sceneFile(std::string _sceneName) const;
  std::filesystem::path executablePath() const;

private:
  std::filesystem::path m_root;
};
