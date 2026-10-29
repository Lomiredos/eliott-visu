#pragma once
#include "visu/ui/Modal.hpp"

#include <functional>
#include <string>

struct SceneInfoCreation {
  std::string name;
};

// Une scene = un fichier JSON dans Assets/ScenesDatas/, rien de plus (pas de
// classe C++, pas de parent/heritage : la logique de jeu vit dans les
// Systems, pas dans la scene).
class CreateSceneModal : public Modal {
private:
  std::string m_name;

  std::function<void(SceneInfoCreation)> m_onClose;

public:
  explicit CreateSceneModal(std::function<void(SceneInfoCreation)> _cb);
  const char *Id() const override { return "CreateSceneModal"; }
  bool Draw() override;
};
