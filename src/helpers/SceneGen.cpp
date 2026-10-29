#include "visu/helpers/SceneGen.hpp"

#include "visu/helpers/NameValidation.hpp"

#include <fstream>

namespace fs = std::filesystem;

namespace {
bool writeFile(const fs::path &_path, const std::string &_content) {
  std::ofstream f(_path, std::ios::binary);
  if (!f)
    return false;
  f << _content;
  return f.good();
}
} // namespace

namespace scenegen {
CreateResult createScene(const fs::path &_scenesDataDir,
                         const SceneInfoCreation &_def) {
  if (!namevalidation::isValidIdentifier(_def.name))
    return {{}, "Nom invalide (identifiant requis)."};

  const std::string &name = _def.name;

  fs::path jsonPath = _scenesDataDir / (name + ".json");
  if (fs::exists(jsonPath))
    return {{}, "\"" + name + "\" existe deja."};

  std::error_code ec;
  fs::create_directories(_scenesDataDir, ec);

  if (!writeFile(jsonPath, "{\n  \"entities\": []\n}\n"))
    return {{}, "Ecriture impossible (fichier verrouille ?)."};

  return {jsonPath, {}};
}
} // namespace scenegen
