#pragma once

#include "visu/core/GameProcess.hpp"
#include "visu/core/SceneHistory.hpp"
#include "visu/core/SceneInfo.hpp"
#include "visu/ui/Panel.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;
class Project;
class Modal;

struct ModalInfos {
  std::function<void(bool flag, bool state)> functionCheck;
  std::string title;
  std::string text;
  std::pair<std::string, std::function<void()>> choices;
};

class App {
private:
  GLFWwindow *m_window = nullptr;
  std::unique_ptr<Project> m_project;
  std::vector<std::unique_ptr<Panel>> m_panels;
  std::vector<std::unique_ptr<Panel>> m_pendingPanels;
  std::unique_ptr<Modal> m_currentModal;
  bool m_modalJustOpen = false;
  bool m_needDefaultLayout = false;
  unsigned int m_dockspaceId = 0;

  SceneInfo m_scene;
  int m_selectedEntity = -1;
  bool m_sceneLoaded = false;
  bool m_sceneDirty = false;
  SceneHistory m_history; // Ctrl+Z / Ctrl+Y

  bool m_quitConfirmed = false;
  bool m_openCommandList = false; // Ctrl+K
  std::string m_commandFilter;
  double m_ctrlOTime = -1.0; // Ctrl+O en attente de P
  bool m_buildRequested = false; // Ctrl+B, traite par MainPanel
  bool m_playRequested = false;  // Ctrl+P (Play ou Stop)
  bool m_rebuildPlayRequested = false; // Ctrl+Shift+B (relance si en jeu)
  struct Command {
    std::string keys, description;
  };
  std::vector<Command> m_commands; // relu a chaque Ctrl+K

  GameProcess m_game; // le jeu lance par Play (process separe)

  App();
  ~App();

public:
  static App &getInstance() {
    static App instance = App();
    return instance;
  }

  bool init();
  void run();
  void openProject(const std::filesystem::path &path);
  void requestPanel(std::unique_ptr<Panel> _panel);
  void openModal(std::unique_ptr<Modal> _modal);

  SceneInfo &sceneData() { return m_scene; }
  int &selectedEntity() { return m_selectedEntity; }
  void loadSceneIfNeeded();
  void loadSceneData(const std::string &_sceneName);
  void saveCurrentScene();

  GameProcess &game() { return m_game; }

  void markSceneDirty() {
    m_sceneDirty = true;
    m_history.touch();
  }
  bool sceneDirty() const { return m_sceneDirty; }

  bool canUndo() const;
  bool canRedo() const;
  void undo();
  void redo();

  // Raccourcis Build / Play : vrais une seule frame, consommes par MainPanel.
  bool consumeBuildRequest() { return std::exchange(m_buildRequested, false); }
  bool consumePlayRequest() { return std::exchange(m_playRequested, false); }
  bool consumeRebuildPlayRequest() {
    return std::exchange(m_rebuildPlayRequested, false);
  }

  void requestQuit();
  void requestOpenProject();
  void requestChangeProject();

  template <typename T> T *getPanel(bool unlockedOnly = false) {
    for (auto &p : m_panels) {
      if (!p->visible)
        continue;
      if (auto *found = dynamic_cast<T *>(p.get()))
        if (!unlockedOnly || !found->isLocked())
          return found;
    }
    return nullptr;
  }
  template <typename T> std::vector<T *> getPanels() {
    std::vector<T *> out;
    for (auto &p : m_panels)
      if (p->visible)
        if (auto *found = dynamic_cast<T *>(p.get()))
          out.push_back(found);
    return out;
  }
  const std::filesystem::path &getProjectRoot() const;

private:
  void flushPanel();
  void drawMenuBar();
  void drawPanels();
  void updateHistory();
  void drawCommandList();
  void loadCommands();
  void handleShortcuts();
};
