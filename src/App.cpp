#include "visu/App.hpp"

#include "visu/core/MeshStore.hpp"
#include "visu/core/Project.hpp"
#include "visu/ui/ComponentPanel.hpp"
#include "visu/ui/ConfirmModal.hpp"
#include "visu/ui/MainPanel.hpp"
#include "visu/ui/Modal.hpp"
#include "visu/ui/Panel.hpp"
#include "visu/ui/SystemPanel.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include "FontAwesomeSolid900.h" // police compressee, embarquee dans l'exe
#include "IconsFontAwesome6.h"   // defines ICON_FA_* + ICON_MIN/MAX_FA

#if defined(__linux__)
#include <nfd.h>
#else
#include "tinyfiledialogs.h"
#endif
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static void glfw_error_callback(int error, const char *description) {
  std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}
static void glfw_window_close_callback(GLFWwindow *window) {
  glfwSetWindowShouldClose(window, GLFW_FALSE);
  App::getInstance().requestQuit();
}

// ##TODO: check des erreur et securisation
static std::string GetExecutablePath() {
#ifdef _WIN32
  char buffer[MAX_PATH];
  GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  return std::string(buffer);
#else
  return std::filesystem::read_symlink("/proc/self/exe").string();
#endif
}

static void buildDefaultLayout(ImGuiID dockspace_id, const ImVec2 &size) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, size);

  ImGui::DockBuilderDockWindow("Main", dockspace_id);
  ImGui::DockBuilderDockWindow("System", dockspace_id);
  ImGui::DockBuilderDockWindow("Component", dockspace_id);

  ImGui::DockBuilderFinish(dockspace_id);
}

App::App() = default;

App::~App() {
  if (m_window) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_window);
    glfwTerminate();
  }
}

void App::flushPanel() {
  for (auto &p : m_pendingPanels) {
    if (p->dock.splitSource != 0) {
      ImGuiID newNode = ImGui::DockBuilderSplitNode(
          p->dock.splitSource, (ImGuiDir)p->dock.splitDir, p->dock.splitRatio,
          &newNode, nullptr);
      p->dock.dockTarget = newNode;
      p->dock.splitSource = 0;
      ImGui::DockBuilderFinish(m_dockspaceId);
    }
    m_panels.push_back(std::move(p));
  }
  m_pendingPanels.clear();

  m_panels.erase(std::remove_if(m_panels.begin(), m_panels.end(),
                                [](const std::unique_ptr<Panel> &p) {
                                  return !p->visible && p->removeOnClose;
                                }),
                 m_panels.end());
}

bool App::init() {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
    return false;

  const char *glsl_version = "#version 330";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  m_window = glfwCreateWindow(1280, 720, "EE-Visu", nullptr, nullptr);
  if (!m_window) {
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(m_window);
  glfwSwapInterval(0);
  glfwSetWindowCloseCallback(m_window, glfw_window_close_callback);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  io.ConfigViewportsNoDecoration = false;

  static const std::string iniPath =
      (std::filesystem::path(GetExecutablePath()).parent_path() / "imgui.init")
          .string();
  io.IniFilename = iniPath.c_str();

  ImFontConfig defaultFontConfig;
  defaultFontConfig.SizePixels = 13.0f;
  io.Fonts->AddFontDefault(&defaultFontConfig);
  static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  ImFontConfig iconConfig;
  iconConfig.MergeMode = true;
  iconConfig.PixelSnapH = true;
  iconConfig.GlyphMinAdvanceX = 13.0f;
  io.Fonts->AddFontFromMemoryCompressedTTF(FontAwesomeSolid900_compressed_data,
                                           FontAwesomeSolid900_compressed_size,
                                           13.0f, &iconConfig, iconRanges);

  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(m_window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);
  if (!io.IniFilename || !std::filesystem::exists(io.IniFilename))
    m_needDefaultLayout = true;

  requestPanel(std::make_unique<SystemPanel>());
  requestPanel(std::make_unique<ComponentPanel>());
  requestPanel(std::make_unique<MainPanel>());
  flushPanel();
  return true;
}

void App::openProject(const std::filesystem::path &path) {
  m_project = std::make_unique<Project>(path);
  ee::core::setMeshBaseDir(m_project->root());
  m_sceneLoaded = false;
  m_selectedEntity = -1;
  m_sceneDirty = false;
}

const std::filesystem::path &App::getProjectRoot() const {
  return m_project->root();
}

void App::loadSceneIfNeeded() {
  if (m_sceneLoaded)
    return;

  m_scene = SceneInfo{};
  m_scene.name = "BaseScene";
  m_selectedEntity = -1;
  if (m_project && m_project->isValid())
    if (auto loaded = loadScene(m_project->sceneFile("BaseScene")))
      m_scene = *loaded;
  m_sceneLoaded = true;
  m_sceneDirty = false;
}

void App::loadSceneData(const std::string &_sceneName) {
  m_selectedEntity = -1;
  m_sceneLoaded = false;
  m_sceneDirty = false;
  m_scene = SceneInfo{};

  m_scene.name = _sceneName;
  if (m_project && m_project->isValid())
    if (auto loaded = loadScene(m_project->sceneFile(_sceneName)))
      m_scene = *loaded;
  m_sceneLoaded = true;
  m_sceneDirty = false;
}

void App::saveCurrentScene() {
  if (m_project && m_project->isValid()) {
    saveScene(m_scene, m_project->sceneFile(m_scene.name));
    m_sceneDirty = false;
  }
}

void App::requestQuit() {
  if (m_sceneDirty && !m_quitConfirmed) {
    // il reste des edits -> demander confirmation
    openModal(std::make_unique<ConfirmModal>(
        "Quitter ?", "La scene a des modifications non sauvegardees.",
        std::vector<ModalChoice>{
            {"Sauvegarder et quitter",
             [this]() {
               saveCurrentScene();
               m_quitConfirmed = true;
               glfwSetWindowShouldClose(m_window, GLFW_TRUE);
             }},
            {"quitter sans sauvegarder",
             [this]() {
               m_quitConfirmed = true;
               glfwSetWindowShouldClose(m_window, GLFW_TRUE);
             }},
            {"Annuler", nullptr}}));
  } else {
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
  }
}
void App::requestPanel(std::unique_ptr<Panel> _panel) {
  m_pendingPanels.push_back(std::move(_panel));
}

void App::openModal(std::unique_ptr<Modal> _modal) {
  m_currentModal = std::move(_modal);
  m_modalJustOpen = true;
}

void App::requestOpenProject() {
#if defined(__linux__)
  // Selecteur natif via le portail XDG (pas de dialogue dans le terminal).
  if (NFD_Init() != NFD_OKAY)
    return;
  nfdu8char_t *selected = nullptr;
  if (NFD_PickFolderU8(&selected, nullptr) == NFD_OKAY) {
    openProject(selected);
    NFD_FreePathU8(selected);
  }
  NFD_Quit();
#else
  const char *selected =
      tinyfd_selectFolderDialog("Choisir l'emplacement du projet", "");
  if (selected != nullptr) {
    openProject(selected);
  }
#endif
}

void App::requestChangeProject() {
  if (m_sceneDirty) {
    openModal(std::make_unique<ConfirmModal>(
        "Change Project ?",
        "Le project a des modification non sauvegarder.",
        std::vector<ModalChoice>{
            {"Sauvegarder et changer",
             [this]() {
               saveCurrentScene();
               requestOpenProject();
             }},
            {"Changer sans sauvegarder", [this]() { requestOpenProject(); }},
            {"Annuler", nullptr}}));
  } else {
    requestOpenProject();
  }
}

void App::drawMenuBar() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Fichier")) {
      if (ImGui::MenuItem("Quitter"))
        requestQuit();
      if (ImGui::MenuItem("Ouvrir un projet"))
        requestChangeProject();
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Fenetre")) {
      for (auto &p : m_panels)
        ImGui::MenuItem(p->name(), nullptr, &p->visible);
      ImGui::Separator();
      if (ImGui::MenuItem("Reset layout"))
        m_needDefaultLayout = true;

      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }
}

void App::drawPanels() {
  for (auto &p : m_panels) {
    if (!p->visible)
      continue;
    if (p->dock.dockTarget != 0) {
      ImGui::SetNextWindowDockID(p->dock.dockTarget, ImGuiCond_Once);
      p->dock.dockTarget = 0;
    }

    if (ImGui::Begin(p->name(), &p->visible))
      p->draw(m_project.get());
    else
      p->drawHidden();
    ImGui::End();
  }
}

void App::run() {
  while (!glfwWindowShouldClose(m_window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    m_dockspaceId = ImGui::DockSpaceOverViewport(
        0, viewport, ImGuiDockNodeFlags_AutoHideTabBar);

    if (m_needDefaultLayout) {
      m_needDefaultLayout = false;
      buildDefaultLayout(m_dockspaceId, viewport->Size);
    }
    drawMenuBar();
    drawPanels();

    if (m_currentModal) {
      if (m_modalJustOpen) {
        ImGui::OpenPopup(m_currentModal->Id());
        m_modalJustOpen = false;
      }
      bool finished = m_currentModal->Draw();
      if (finished)
        m_currentModal.reset();
    }

    flushPanel();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(m_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      GLFWwindow *backup_current_context = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(backup_current_context);
    }

    glfwSwapBuffers(m_window);
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}
