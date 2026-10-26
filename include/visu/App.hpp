#pragma once

#include "visu/core/SceneInfo.hpp"
#include "visu/ui/Panel.hpp"
#include <filesystem>
#include <functional>
#include <memory>
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

  bool m_quitConfirmed = false;

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

  void markSceneDirty() { m_sceneDirty = true; }
  bool sceneDirty() const { return m_sceneDirty; }

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
};
