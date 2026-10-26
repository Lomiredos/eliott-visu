#pragma once

#include "visu/ui/Panel.hpp"
#include "visu/ui/ScenePreview.hpp"

#include "visu/core/SceneInfo.hpp"
#include "visu/ipc/Protocol.hpp"

#include <cstdint>
#include <string>

class MainPanel : public Panel {
public:
  const char *name() const override { return "Main"; }
  void draw(Project *project) override;
  void drawHidden() override;

  struct DeletionState {
    int deleteEntity = -1;
    int deleteComp = -1;
    bool openDeleteModal = false;
    int entityToDelete = -1;
    bool openDeleteEntityModal = false;
  };

  struct NavState {
    bool navMode = false;
    bool navJustEntered = false;
    int gizmoMode = 0;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
  };

  // Etat du Play (jeu lance dans un process separe, voir GameProcess).
  struct PlayState {
    ee::ipc::PlayMode mode = ee::ipc::PlayMode::Preview; // dernier choix
    bool active = false;      // une session tourne (ou demarre)
    SceneInfo view;           // dernier snapshot du jeu, affiche a la place de la scene
    uint64_t viewVersion = 0;
    bool captured = false;    // clavier/souris envoyes au jeu (mode preview)
    bool escWasDown = false;

    // Dernier champ modifie dans l'inspecteur : protege des snapshots le
    // temps que le jeu l'applique (sinon le slider "saute" en arriere).
    int editEntity = -1;
    std::string editComp, editField;
    FieldValue editValue;
    double editTime = -1.0;

    std::string status;
    std::string crashMessage;
    std::string crashLog;
    bool openCrashPopup = false;
  };

private:
  ScenePreview m_preview;
  DeletionState m_del;
  NavState m_nav;
  PlayState m_play;

  std::string m_buildStatus;
  std::string m_buildLog;
  bool m_openBuildErrorPopup = false;

  std::string m_sceneStatus;
};
