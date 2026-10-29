#include "visu/App.hpp"

#include "visu/core/MeshStore.hpp"
#include "visu/core/Project.hpp"
#include "visu/helpers/ComponentGetter.hpp"
#include "visu/helpers/EntityClipboard.hpp"
#include "visu/ui/ComponentPanel.hpp"
#include "visu/ui/ConfirmModal.hpp"
#include "visu/ui/MainPanel.hpp"
#include "visu/ui/MaterialPanel.hpp"
#include "visu/ui/EngineSettingsPanel.hpp"
#include "visu/ui/Modal.hpp"
#include "visu/ui/EditorCommands.hpp"
#include "visu/ui/Panel.hpp"
#include "visu/ui/SystemPanel.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <imgui_stdlib.h>
#include <ImGuizmo.h>

#include "FontAwesomeSolid900.h" // police compressee, embarquee dans l'exe
#include "IconsFontAwesome6.h"   // defines ICON_FA_* + ICON_MIN/MAX_FA

#if defined(__linux__)
#include <nfd.h>
#else
#include "tinyfiledialogs.h"
#endif
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
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

// Outils portables installes par le hub (PC sans droits admin) dans
// <dossier de l'exe>/tools : cmake (Build, catalogue) et, sous Windows, le
// compilateur MinGW (Build, et ses DLL pour lancer le jeu). Ajoutes au PATH
// meme quand l'editeur est lance sans passer par le hub.
static void addPortableToolsToPath() {
#ifdef _WIN32
  const char sep = ';';
#else
  const char sep = ':';
#endif
  const std::filesystem::path tools =
      std::filesystem::path(GetExecutablePath()).parent_path() / "tools";
  for (const char *tool : {"mingw", "cmake"}) {
    std::error_code ec;
    std::filesystem::path bin = tools / tool / "bin";
    if (!std::filesystem::is_directory(bin, ec))
      continue;
    const std::string dir = bin.make_preferred().string();
    const char *current = std::getenv("PATH");
    std::string path = current ? current : "";
    if (path.find(dir) != std::string::npos)
      continue; // deja la (lance par le hub)
    path = dir + sep + path;
#ifdef _WIN32
    _putenv_s("PATH", path.c_str());
#else
    setenv("PATH", path.c_str(), 1);
#endif
  }
}

static void buildDefaultLayout(ImGuiID dockspace_id, const ImVec2 &size) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, size);

  ImGui::DockBuilderDockWindow("Main", dockspace_id);
  ImGui::DockBuilderDockWindow("System", dockspace_id);
  ImGui::DockBuilderDockWindow("Component", dockspace_id);
  ImGui::DockBuilderDockWindow("Materials", dockspace_id);
  ImGui::DockBuilderDockWindow("Reglages", dockspace_id);

  ImGui::DockBuilderFinish(dockspace_id);
}

App::App() = default;

App::~App() { shutdown(); }

// Pas dans le destructeur seul : celui du singleton tourne pendant exit(),
// quand le chargeur GL n'est plus fiable (crash dans ImGui_ImplOpenGL3_Shutdown).
void App::shutdown() {
  // Le jeu ne doit jamais survivre a l'editeur ; sa texture est liberee
  // tant que le contexte GL existe encore.
  m_game.stop();
  if (!m_window)
    return;
  m_game.releaseGl();

  glfwMakeContextCurrent(m_window);
  // Panneaux et modale peuvent tenir des ressources GL (apercus...).
  m_currentModal.reset();
  m_pendingPanels.clear();
  m_panels.clear();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(m_window);
  glfwTerminate();
  m_window = nullptr;
}

// Resultat de la verification du moteur (thread) -> popups. Attend qu'aucune
// autre modale ne soit ouverte.
void App::updateEngineUpdate() {
  using engineupdate::State;
  if (m_currentModal)
    return;
  switch (m_engineUpdate.state()) {
  case State::UpdateAvailable: {
    std::string commits = m_engineUpdate.commits();
    int n = static_cast<int>(std::count(commits.begin(), commits.end(), '\n'));
    // Pas de liste interminable dans la popup.
    std::istringstream lines(commits);
    std::string shown, line;
    for (int i = 0; i < 10 && std::getline(lines, line); ++i)
      shown += "  " + line + "\n";
    if (n > 10)
      shown += "  ... (" + std::to_string(n - 10) + " autres)\n";
    openModal(std::make_unique<ConfirmModal>(
        "Mise a jour du moteur",
        "eliott-engine-3d : " + std::to_string(n) +
            (n > 1 ? " nouveaux commits" : " nouveau commit") + " sur le depot.\n\n" +
            shown + "\nRecuperer la mise a jour (git pull) ?",
        std::vector<ModalChoice>{
            {"Mettre a jour", [this]() { m_engineUpdate.startPull(); }},
            {"Plus tard", [this]() { m_engineUpdate.dismiss(); }}}));
    break;
  }
  case State::Pulled:
    m_engineUpdate.dismiss();
    openModal(std::make_unique<ConfirmModal>(
        "Moteur mis a jour",
        "Le moteur est a jour.\nRecompile l'editeur pour en profiter :\n"
        "  cmake --build build",
        std::vector<ModalChoice>{{"OK", nullptr}}));
    break;
  case State::PullFailed:
    m_engineUpdate.dismiss();
    openModal(std::make_unique<ConfirmModal>(
        "Mise a jour impossible",
        "git pull a echoue :\n\n" + m_engineUpdate.log(),
        std::vector<ModalChoice>{{"OK", nullptr}}));
    break;
  default:
    break;
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
  addPortableToolsToPath();
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
  requestPanel(std::make_unique<MaterialPanel>());
  requestPanel(std::make_unique<EngineSettingsPanel>());
  flushPanel();
  m_engineUpdate.startCheck();
  return true;
}

void App::openProject(const std::filesystem::path &path) {
  m_game.stop(); // le jeu appartient a l'ancien projet
  m_project = std::make_unique<Project>(path);
  ee::core::setMeshBaseDir(m_project->root());
  m_sceneLoaded = false;
  m_selectedEntity = -1;
  m_sceneDirty = false;
  m_history.reset(SceneInfo{});
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
  m_history.reset(m_scene);
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
  m_history.reset(m_scene);
}

void App::saveCurrentScene() {
  if (m_project && m_project->isValid()) {
    saveScene(m_scene, m_project->sceneFile(m_scene.name));
    m_sceneDirty = false;
    m_history.markSaved();
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
      if (ImGui::MenuItem("Ouvrir un projet", "Ctrl+O, P"))
        requestChangeProject();
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edition")) {
      if (ImGui::MenuItem("Annuler", "Ctrl+Z", false, canUndo()))
        undo();
      if (ImGui::MenuItem("Refaire", "Ctrl+Y", false, canRedo()))
        redo();
      ImGui::Separator();
      if (ImGui::MenuItem("Copier l'entite", "Ctrl+C", false, canCopyEntity()))
        copyEntity();
      if (ImGui::MenuItem("Coller l'entite", "Ctrl+V", false, canPasteEntity()))
        pasteEntity();
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

// Pendant le Play, la scene n'est pas modifiee (les edits vont au jeu) :
// pas d'undo/redo.
bool App::canUndo() const { return !m_game.running() && m_history.canUndo(); }
bool App::canRedo() const { return !m_game.running() && m_history.canRedo(); }

void App::undo() {
  if (canUndo() && m_history.undo(m_scene, m_selectedEntity))
    m_sceneDirty = !m_history.atSaved();
}

void App::redo() {
  if (canRedo() && m_history.redo(m_scene, m_selectedEntity))
    m_sceneDirty = !m_history.atSaved();
}

// Cle de la scene pour le presse-papiers : coller dans la meme scene garde
// les references vers les entites non copiees.
static std::string sceneKey(const std::filesystem::path &_root,
                            const SceneInfo &_scene) {
  return _root.string() + "|" + _scene.name;
}

bool App::canCopyEntity() const {
  return m_project && !m_game.running() && m_selectedEntity >= 0 &&
         m_selectedEntity < static_cast<int>(m_scene.entities.size());
}

bool App::canPasteEntity() const {
  return m_project && m_project->isValid() && m_sceneLoaded &&
         !m_game.running();
}

void App::copyEntity() {
  if (!canCopyEntity())
    return;
  std::string text = entityclipboard::copy(
      m_scene, m_selectedEntity, sceneKey(m_project->root(), m_scene));
  if (!text.empty())
    ImGui::SetClipboardText(text.c_str());
}

void App::pasteEntity() {
  if (!canPasteEntity())
    return;
  const char *text = ImGui::GetClipboardText();
  if (!text)
    return;
  int root = entityclipboard::paste(
      m_scene, text, getAllComponentEntityFields(m_project->componentsCatalog()),
      sceneKey(m_project->root(), m_scene));
  if (root < 0)
    return; // pas une copie d'entites
  m_selectedEntity = root;
  markSceneDirty();
}

// Fin de frame : enregistre la modif terminee, puis raccourcis clavier.
void App::updateHistory() {
  // Un widget encore manipule (drag, gizmo, saisie) = modif pas finie.
  const bool editing = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();
  m_history.commit(m_scene, m_selectedEntity, editing);

  // Champ texte actif : Ctrl+Z lui appartient (undo du texte).
  if (editing || ImGui::GetIO().WantTextInput)
    return;
  // Touches par caractere (ImGui suit la disposition du clavier) : Ctrl+Z
  // est la touche marquee Z, en AZERTY comme en QWERTY.
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
    undo();
  else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
           ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift |
                                    ImGuiKey_Z))
    redo();
}

// Raccourcis globaux : Ctrl+S, Ctrl+B, Ctrl+P, Ctrl+Shift+B, et en deux temps Ctrl+O puis P
// (Ctrl tenu ou non) dans la seconde = ouvrir un projet.
void App::handleShortcuts() {
  // Ctrl+S marche aussi pendant une saisie ; pas pendant le Play (la scene
  // affichee est celle du jeu, sauvegardee au lancement).
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) &&
      !m_game.running() && m_sceneLoaded)
    saveCurrentScene();

  // Requetes de la frame precedente non consommees (Main cache) : perdues.
  m_buildRequested = m_playRequested = m_rebuildPlayRequested = false;

  if (ImGui::GetIO().WantTextInput) {
    m_ctrlOTime = -1.0;
    return;
  }
  const double now = ImGui::GetTime();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
    m_ctrlOTime = now;
    return;
  }
  if (m_ctrlOTime >= 0.0 && now - m_ctrlOTime > 1.0)
    m_ctrlOTime = -1.0;

  // P juste apres Ctrl+O = ouvrir un projet (prioritaire sur Ctrl+P).
  if (m_ctrlOTime >= 0.0) {
    if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
      m_ctrlOTime = -1.0;
      requestChangeProject();
    }
    return;
  }

  // Champ texte actif : Ctrl+C/V restent au texte (voir le return plus haut).
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
    copyEntity();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V))
    pasteEntity();

  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_B))
    m_buildRequested = true;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
    m_playRequested = true;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_B))
    m_rebuildPlayRequested = true;
}

// Liste des commandes (Ctrl+K) : assets/Commands.json du depot, relu a chaque
// ouverture ; sinon la copie integree au binaire (editeur installe seul).
void App::loadCommands() {
  std::string text = visu::kEditorCommandsJson;
  if (std::ifstream f{visu::kEditorCommandsPath}) {
    std::stringstream ss;
    ss << f.rdbuf();
    text = ss.str();
  }

  m_commands.clear();
  nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
  if (!j.is_array()) {
    m_commands.push_back({"(erreur)", "Commands.json invalide"});
    return;
  }
  for (const nlohmann::json &c : j)
    if (c.is_object())
      m_commands.push_back(
          {c.value("keys", ""), c.value("description", "")});
}

// Popup ephemere : recherche, liste scrollable, Echap ou clic dehors ferme.
void App::drawCommandList() {
  if (!ImGui::GetIO().WantTextInput &&
      ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K)) {
    m_openCommandList = true;
    m_commandFilter.clear();
    loadCommands();
  }
  if (m_openCommandList) {
    ImGui::OpenPopup("Commandes");
    m_openCommandList = false;
  }

  ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                 vp->WorkPos.y + vp->WorkSize.y * 0.25f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(480, 0));
  if (!ImGui::BeginPopup("Commandes"))
    return;

  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filtre", "Rechercher une commande...",
                           &m_commandFilter);

  auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  const std::string filter = lower(m_commandFilter);

  ImGui::BeginChild("liste", ImVec2(0, 240));
  if (ImGui::BeginTable("cmds", 2, ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("touches", ImGuiTableColumnFlags_WidthFixed, 160);
    ImGui::TableSetupColumn("description");
    for (const Command &c : m_commands) {
      if (!filter.empty() &&
          lower(c.keys).find(filter) == std::string::npos &&
          lower(c.description).find(filter) == std::string::npos)
        continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", c.keys.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(c.description.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();

  if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
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
    m_game.update(); // avant les panels : snapshot et image de cette frame
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

    updateEngineUpdate();
    updateHistory();
    handleShortcuts();
    drawCommandList();
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
