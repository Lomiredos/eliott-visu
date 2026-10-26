#include "visu/ui/MainPanel.hpp"

#include "visu/App.hpp"
#include "visu/core/Project.hpp"
#include "visu/core/SceneInfo.hpp"
#include "visu/helpers/BuildProject.hpp"
#include "visu/helpers/ComponentGetter.hpp"
#include "visu/helpers/OpenExternal.hpp"
#include "visu/helpers/SceneGen.hpp"
#include "visu/ui/ConfirmModal.hpp"
#include "visu/ui/CreateSceneModal.hpp"

#include "imgui.h"
#include <imgui_stdlib.h> // ImGui::InputText(const char*, std::string*)
#include <memory>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h> // capture souris + touches pour la navigation camera
#include <ImGuizmo.h>

#include "visu/core/GameProcess.hpp"
#include "visu/runtime/GlfwKeys.hpp" // touches par position physique, comme le jeu

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <variant>

#include "IconsFontAwesome6.h"
#include "visu/helpers/FilesGetter.hpp"

static float asFloat(const FieldValue &v, float def = 0.0f) {
  if (auto p = std::get_if<float>(&v))
    return *p;
  if (auto p = std::get_if<int>(&v))
    return static_cast<float>(*p);
  if (auto p = std::get_if<bool>(&v))
    return *p ? 1.0f : 0.0f;
  return def;
}

static const std::vector<std::string> *findEnumOptions(
    const std::map<std::string, std::map<std::string, std::vector<std::string>>>
        &_opts,
    const std::string &_comp, const std::string &_field) {
  auto compIt = _opts.find(_comp);
  if (compIt == _opts.end())
    return nullptr;
  auto fieldIt = compIt->second.find(_field);
  if (fieldIt == compIt->second.end())
    return nullptr;
  return &fieldIt->second;
}

// _scene = scene affichee : celle de l'editeur, ou le snapshot du jeu pendant
// le Play (lecture seule : pas de creation/suppression/changement de scene).
static void drawHierarchy(App &app, Project *project,
                          MainPanel::DeletionState &del,
                          std::string &sceneStatus, SceneInfo &scene,
                          bool playing) {
  int &selected = app.selectedEntity();
  ImGui::TextUnformatted(scene.name.c_str());
  ImGui::SameLine();
  ImGui::BeginDisabled(playing);
  if (ImGui::Button("Change Scene")) {
    ImGui::OpenPopup("Scene_Selector");
  }
  ImGui::EndDisabled();

  if (ImGui::BeginPopup("Scene_Selector")) {
    std::filesystem::path scenesPath = project->scenesDataDir();
    std::vector<std::string> sceneFiles =
        getFilesInFolderWith(scenesPath, {".json"});

    std::vector<std::string> sceneNames;
    for (const std::string &f : sceneFiles)
      sceneNames.push_back(std::filesystem::path(f).stem().string());

    for (const std::string &sceneName : sceneNames) {
      if (ImGui::Selectable(sceneName.c_str())) {
        if (app.sceneDirty()) {
          app.openModal(std::make_unique<ConfirmModal>(
              "Changer de Scene ?",
              "Il y a des modification non sauvegarder dans la scene.",
              std::vector<ModalChoice>{
                  {"Sauvegarder et changer",
                   [&app, sceneName]() {
                     app.saveCurrentScene();
                     app.loadSceneData(sceneName);
                   }},
                  {"Changer sans sauvegarder",
                   [&app, sceneName]() { app.loadSceneData(sceneName); }},
                  {"Annuler"}}));
        } else {
          app.loadSceneData(sceneName);
        }
      }
    }
    ImGui::Separator();
    if (ImGui::Button("+ Nouvelle scene")) {
      app.openModal(std::make_unique<CreateSceneModal>(
          [project, &sceneStatus](SceneInfoCreation _data) {
            scenegen::CreateResult r =
                scenegen::createScene(project->scenesDataDir(), _data);
            if (r.error.empty()) {
              sceneStatus = "Scene creee.";
              openInVSCode(r.path);
            } else {
              sceneStatus = r.error;
            }
          }));
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!sceneStatus.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", sceneStatus.c_str());
  }

  ImGui::Separator();
  ImGui::BeginDisabled(playing);
  if (ImGui::Button("Create Entity")) {
    scene.entities.push_back({});
    selected = static_cast<int>(scene.entities.size()) - 1;
    app.markSceneDirty();
  }
  ImGui::SameLine();
  if (ImGui::Button("Save"))
    app.saveCurrentScene();
  ImGui::EndDisabled();
  if (playing) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Play : valeurs du jeu");
  } else if (app.sceneDirty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "* non sauvegarde");
  }

  ImGui::Separator();

  for (int i = 0; i < static_cast<int>(scene.entities.size()); ++i) {
    ImGui::SetNextItemAllowOverlap();

    const std::string &nm = scene.entities[i].name;
    std::string display = nm.empty() ? ("Entity " + std::to_string(i)) : nm;
    std::string label = display + "##ent" + std::to_string(i);
    if (ImGui::Selectable(label.c_str(), selected == i))
      selected = i;

    std::string icon = std::string(ICON_FA_TRASH) + "##" + std::to_string(i);

    float iconW = ImGui::CalcTextSize(icon.c_str()).x +
                  ImGui::GetStyle().FramePadding.x * 2.0f;

    ImGui::SameLine(ImGui::GetContentRegionMax().x - iconW);

    ImGui::BeginDisabled(playing);
    if (ImGui::SmallButton(icon.c_str())) {

      del.entityToDelete = i;
      del.openDeleteEntityModal = true;
    }
    ImGui::EndDisabled();
  }
}

// _play non nul pendant le Play : les champs modifies sont envoyes au jeu
// (pas a la scene), et la structure (composants, nom) est en lecture seule.
static void drawInspector(App &app, Project *project,
                          MainPanel::DeletionState &del, SceneInfo &scene,
                          MainPanel::PlayState *play) {
  int selected = app.selectedEntity();
  const bool playing = play != nullptr;

  if (selected < 0 || selected >= static_cast<int>(scene.entities.size())) {
    ImGui::TextDisabled("Selectionne une entite");
    return;
  }

  EntityInfo &ent = scene.entities[selected];
  {
    std::string display =
        ent.name.empty() ? ("Entity " + std::to_string(selected)) : ent.name;
    ImGui::Text("%s", display.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(playing);
    if (ImGui::SmallButton("Rename"))
      ImGui::OpenPopup("rename_entity");
    ImGui::EndDisabled();
  }
  if (ImGui::BeginPopup("rename_entity")) {
    if (ImGui::IsWindowAppearing())
      ImGui::SetKeyboardFocusHere();
    if (ImGui::InputText("##rename", &ent.name,
                         ImGuiInputTextFlags_EnterReturnsTrue))
      ImGui::CloseCurrentPopup();
    if (ImGui::IsItemEdited())
      app.markSceneDirty();
    ImGui::SameLine();
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::Separator();

  std::map<std::string, std::map<std::string, std::vector<std::string>>>
      enumOptions = getAllComponentEnumOptions(project->componentsCatalog());

  for (int c = 0; c < static_cast<int>(ent.components.size()); ++c) {
    ComponentInstance &ci = ent.components[c];
    ImGui::PushID(c);

    ImGui::SetNextItemAllowOverlap();
    bool open = ImGui::CollapsingHeader(ci.name.c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen);

    const char *icon = ICON_FA_TRASH;

    float iconW =
        ImGui::CalcTextSize(icon).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    ImGui::SameLine(ImGui::GetContentRegionMax().x - iconW);

    ImGui::BeginDisabled(playing);
    if (ImGui::SmallButton(icon)) {
      del.deleteEntity = selected;
      del.deleteComp = c;
      del.openDeleteModal = true;
    }
    ImGui::EndDisabled();

    if (open) {
      if (ci.values.empty())
        ImGui::TextDisabled("(aucun champ)");
      for (auto &kv : ci.values) {

        const char *lbl = kv.first.c_str();
        bool edited = false;
        if (auto p = std::get_if<float>(&kv.second))
          edited = ImGui::DragFloat(lbl, p, 0.05f);
        else if (auto p = std::get_if<int>(&kv.second))
          edited = ImGui::DragInt(lbl, p);
        else if (auto p = std::get_if<bool>(&kv.second))
          edited = ImGui::Checkbox(lbl, p);
        else if (auto p = std::get_if<std::string>(&kv.second)) {
          const std::vector<std::string> *opts =
              findEnumOptions(enumOptions, ci.name, kv.first);
          if (opts && !opts->empty()) {
            if (ImGui::BeginCombo(lbl, p->c_str())) {
              for (const std::string &opt : *opts) {
                bool isSelected = (*p == opt);
                if (ImGui::Selectable(opt.c_str(), isSelected)) {
                  *p = opt;
                  edited = true;
                }
                if (isSelected)
                  ImGui::SetItemDefaultFocus();
              }
              ImGui::EndCombo();
            }
          } else {
            edited = ImGui::InputText(lbl, p);
          }
        }
        if (playing) {
          // Envoye au jeu ; tant qu'on manipule le champ, il est protege
          // des snapshots (voir syncPlayView).
          if (edited)
            app.game().sendSetField(selected, ci.name, kv.first, kv.second);
          if (edited || ImGui::IsItemActive()) {
            play->editEntity = selected;
            play->editComp = ci.name;
            play->editField = kv.first;
            play->editValue = kv.second;
            play->editTime = ImGui::GetTime();
          }
        } else if (edited)
          app.markSceneDirty();
      }
    }

    ImGui::PopID();
  }

  ImGui::Separator();

  ImGui::BeginDisabled(playing);
  if (ImGui::Button("Add Component"))
    ImGui::OpenPopup("add_comp");
  ImGui::EndDisabled();

  if (ImGui::BeginPopup("add_comp")) {
    std::map<std::string, std::map<std::string, FieldValue>> catalog =
        getAllComponentDefaults(project->componentsCatalog());

    bool any = false;
    for (const auto &entry : catalog) {
      const std::string &cname = entry.first;
      bool already = std::any_of(
          ent.components.begin(), ent.components.end(),
          [&](const ComponentInstance &ci) { return ci.name == cname; });
      if (already)
        continue;

      any = true;
      if (ImGui::Selectable(cname.c_str())) {
        ComponentInstance ci;
        ci.name = cname;
        ci.values = entry.second;
        ent.components.push_back(std::move(ci));
        app.markSceneDirty();
      }
    }
    if (!any)
      ImGui::TextDisabled("(rien a ajouter)");

    ImGui::EndPopup();
  }
}

static void drawDeleteConfirm(App &app, MainPanel::DeletionState &del) {
  if (del.openDeleteModal) {
    ImGui::OpenPopup("Supprimer le composant ?");
    del.openDeleteModal = false;
  }

  if (ImGui::BeginPopupModal("Supprimer le composant ?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    SceneInfo &scene = app.sceneData();
    bool valid = del.deleteEntity >= 0 &&
                 del.deleteEntity < static_cast<int>(scene.entities.size()) &&
                 del.deleteComp >= 0 &&
                 del.deleteComp <
                     static_cast<int>(
                         scene.entities[del.deleteEntity].components.size());

    if (valid)
      ImGui::Text("Supprimer \"%s\" ?", scene.entities[del.deleteEntity]
                                            .components[del.deleteComp]
                                            .name.c_str());
    else
      ImGui::TextUnformatted("Supprimer ce composant ?");

    ImGui::Spacing();

    if (ImGui::Button("Supprimer")) {
      if (valid) {
        auto &comps = scene.entities[del.deleteEntity].components;
        comps.erase(comps.begin() + del.deleteComp);
        app.markSceneDirty();
      }
      del.deleteEntity = del.deleteComp = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Annuler")) {
      del.deleteEntity = del.deleteComp = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

static void drawDeleteEntityConfirm(App &app, MainPanel::DeletionState &del) {
  if (del.openDeleteEntityModal) {
    ImGui::OpenPopup("Supprimer l'entite ?");
    del.openDeleteEntityModal = false;
  }

  if (ImGui::BeginPopupModal("Supprimer l'entite ?", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    SceneInfo &scene = app.sceneData();
    bool valid = del.entityToDelete >= 0 &&
                 del.entityToDelete < static_cast<int>(scene.entities.size());

    if (valid) {
      const std::string &nm = scene.entities[del.entityToDelete].name;
      std::string display =
          nm.empty() ? ("Entity " + std::to_string(del.entityToDelete)) : nm;
      ImGui::Text("Supprimer \"%s\" et tous ses composants ?", display.c_str());
    } else
      ImGui::TextUnformatted("Supprimer cette entite ?");

    ImGui::Spacing();

    if (ImGui::Button("Supprimer")) {
      if (valid) {
        scene.entities.erase(scene.entities.begin() + del.entityToDelete);

        int &sel = app.selectedEntity();
        if (sel == del.entityToDelete)
          sel = -1;
        else if (sel > del.entityToDelete)
          --sel;
        app.markSceneDirty();
      }
      del.entityToDelete = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Annuler")) {
      del.entityToDelete = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// Champ multiligne en lecture seule (curseur, selection, Ctrl+C), pour les logs.
static void readOnlyLog(const char *_id, std::string &_text) {
  ImGui::InputTextMultiline(_id, &_text, ImVec2(720, 320),
                            ImGuiInputTextFlags_ReadOnly);
}

static void drawBuildPlay(App &app, Project *project, std::string &buildStatus,
                          std::string &buildLog, bool &openBuildErrorPopup,
                          MainPanel::PlayState &play) {
  GameProcess &game = app.game();
  const bool playing = game.running();

  ImGui::BeginDisabled(playing);
  if (ImGui::Button("Build")) {
    projectbuild::BuildResult r = projectbuild::build(project->root());
    if (r.ok)
      buildStatus = "Build OK.";
    else {
      buildStatus = "Echec du build.";
      buildLog = r.log;
      openBuildErrorPopup = true;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();

  if (!playing) {
    // Play = sauvegarde + build incremental + lancement (process separe).
    if (ImGui::Button(ICON_FA_PLAY " Play")) {
      app.saveCurrentScene();
      projectbuild::BuildResult r = projectbuild::build(project->root());
      if (!r.ok) {
        buildStatus = "Echec du build : jeu non lance.";
        buildLog = r.log;
        openBuildErrorPopup = true;
      } else {
        std::string err;
        std::filesystem::path log = project->root() / "build" / "play.log";
        if (game.start(project->executablePath(), log, play.mode, err)) {
          buildStatus.clear();
          play.status.clear();
        } else
          play.status = err;
      }
    }
    ImGui::SameLine();
    int mode = play.mode == ee::ipc::PlayMode::Window ? 1 : 0;
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("##playmode", &mode, "Dans la preview\0Fenetre separee\0"))
      play.mode = mode == 1 ? ee::ipc::PlayMode::Window : ee::ipc::PlayMode::Preview;
  } else {
    if (ImGui::Button(ICON_FA_STOP " Stop"))
      game.stop();
    ImGui::SameLine();
    if (game.state() == GameProcess::State::Starting &&
        game.secondsSinceStart() > 5.0)
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                         "Le jeu ne se connecte pas (moteur du projet a jour ?)");
    else if (game.state() == GameProcess::State::Starting)
      ImGui::TextDisabled("Lancement du jeu...");
    else if (game.mode() == ee::ipc::PlayMode::Window)
      ImGui::TextDisabled("En cours (fenetre separee)");
    else
      ImGui::TextDisabled("En cours (preview)");
  }

  if (!buildStatus.empty())
    ImGui::TextDisabled("%s", buildStatus.c_str());
  if (!play.status.empty())
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", play.status.c_str());

  if (openBuildErrorPopup) {
    ImGui::OpenPopup("Erreur de build");
    openBuildErrorPopup = false;
  }
  if (ImGui::BeginPopupModal("Erreur de build", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Le build a echoue. Sortie :");
    readOnlyLog("##buildlog", buildLog);
    if (ImGui::Button("Fermer"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // Le jeu a plante : l'editeur, lui, est toujours la.
  if (play.openCrashPopup) {
    ImGui::OpenPopup("Le jeu s'est arrete");
    play.openCrashPopup = false;
  }
  if (ImGui::BeginPopupModal("Le jeu s'est arrete", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(play.crashMessage.c_str());
    ImGui::TextDisabled("Sortie du jeu (build/play.log) :");
    readOnlyLog("##playlog", play.crashLog);
    if (ImGui::Button("Fermer"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

static bool drawGizmo(App &app, ScenePreview &preview,
                      const MainPanel::NavState &nav, ImVec2 imgMin, int pw,
                      int ph, unsigned int tex) {
  ImGuizmo::BeginFrame();

  int sel = app.selectedEntity();
  SceneInfo &sc = app.sceneData();
  if (tex == 0 || nav.navMode || sel < 0 ||
      sel >= static_cast<int>(sc.entities.size()))
    return false;

  ComponentInstance *tf = nullptr;
  for (auto &ci : sc.entities[sel].components)
    if (ci.name == "TransformComponent") {
      tf = &ci;
      break;
    }
  if (!tf)
    return false;

  auto gv = [&](const char *k) -> float {
    auto it = tf->values.find(k);
    return it != tf->values.end() ? asFloat(it->second) : 0.0f;
  };

  float vmat[16], pmat[16];
  preview.getViewMatrix(vmat);
  preview.getProjMatrix(pmat);

  float scx = gv("ScaleX"), scy = gv("ScaleY"), scz = gv("ScaleZ");
  if (scx <= 0.0f)
    scx = 1.0f;
  if (scy <= 0.0f)
    scy = 1.0f;
  if (scz <= 0.0f)
    scz = 1.0f;
  float translation[3] = {gv("PositionX"), gv("PositionY"), gv("PositionZ")};
  float rotation[3] = {gv("EuleurX"), gv("EuleurY"), gv("EuleurZ")};
  float scale[3] = {scx, scy, scz};
  float model[16];
  ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, model);

  ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
  if (nav.gizmoMode == 1)
    op = ImGuizmo::ROTATE;
  else if (nav.gizmoMode == 2)
    op = ImGuizmo::SCALE;
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist();
  ImGuizmo::SetRect(imgMin.x, imgMin.y, static_cast<float>(pw),
                    static_cast<float>(ph));
  ImGuizmo::Manipulate(vmat, pmat, op, ImGuizmo::WORLD, model);

  if (ImGuizmo::IsUsing()) {
    ImGuizmo::DecomposeMatrixToComponents(model, translation, rotation, scale);
    tf->values["PositionX"] = translation[0];
    tf->values["PositionY"] = translation[1];
    tf->values["PositionZ"] = translation[2];
    tf->values["EuleurX"] = rotation[0];
    tf->values["EuleurY"] = rotation[1];
    tf->values["EuleurZ"] = rotation[2];
    tf->values["ScaleX"] = scale[0];
    tf->values["ScaleY"] = scale[1];
    tf->values["ScaleZ"] = scale[2];
    app.markSceneDirty();
  }
  return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

static void handlePreviewClicks(App &app, ScenePreview &preview,
                                MainPanel::NavState &nav, GLFWwindow *win,
                                unsigned int tex, bool gizmoActive,
                                const SceneInfo &shown) {

  if (tex != 0 && !gizmoActive && ImGui::IsItemHovered() &&
      ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    nav.navMode = true;
    nav.navJustEntered = true;
    if (win)
      glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  }

  if (tex != 0 && !nav.navMode && !gizmoActive && ImGui::IsItemHovered() &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    ImVec2 mp = ImGui::GetMousePos();
    float w = rmax.x - rmin.x, h = rmax.y - rmin.y;
    if (w > 0.0f && h > 0.0f) {
      float u = (mp.x - rmin.x) / w;
      float vv = (mp.y - rmin.y) / h;
      float ndcX = 2.0f * u - 1.0f;
      float ndcY = 1.0f - 2.0f * vv;
      int hit = preview.pick(shown, ndcX, ndcY, w / h);
      if (hit >= 0)
        app.selectedEntity() = hit;
    }
  }
}

static void updateNavOrShowToolbar(ScenePreview &preview,
                                   MainPanel::NavState &nav, GLFWwindow *win) {
  if (nav.navMode) {
    ImGuiIO &io = ImGui::GetIO();
    float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;

    if (win) {
      double mx, my;
      glfwGetCursorPos(win, &mx, &my);
      if (nav.navJustEntered) {
        nav.lastMouseX = mx;
        nav.lastMouseY = my;
        nav.navJustEntered = false;
      }
      float dx = static_cast<float>(mx - nav.lastMouseX);
      float dy = static_cast<float>(my - nav.lastMouseY);
      nav.lastMouseX = mx;
      nav.lastMouseY = my;
      preview.addYawPitch(dx * 0.0025f, -dy * 0.0025f);
    }

    float speed = 5.0f * dt, f = 0.0f, r = 0.0f, u = 0.0f;
    if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_Z))
      f += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_S))
      f -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_D))
      r += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_Q))
      r -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_Space))
      u += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift))
      u -= 1.0f;
    preview.moveLocal(f * speed, r * speed, u * speed);

    if (io.MouseWheel != 0.0f)
      preview.dolly(io.MouseWheel * 0.5f);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      nav.navMode = false;
      if (win)
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    ImGui::TextDisabled(
        "ZQSD deplacer | souris tourner | molette zoom | Echap sortir");
  } else {
    if (ImGui::RadioButton("Deplacer", nav.gizmoMode == 0))
      nav.gizmoMode = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Tourner", nav.gizmoMode == 1))
      nav.gizmoMode = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Redim.", nav.gizmoMode == 2))
      nav.gizmoMode = 2;
    ImGui::SameLine();
    ImGui::TextDisabled("| Double-clic pour naviguer");
  }
}

// Clavier/souris de l'editeur -> etat d'input du jeu. Touches lues par
// POSITION physique (meme table que le jeu), pas via ImGui qui les traduit
// selon la langue du clavier.
static ee::ipc::InputState readCapturedInput(GLFWwindow *_win, ImVec2 _imgMin) {
  ee::ipc::InputState in;
  if (!_win)
    return in;
  int keyCount = 0;
  const int *table = ee::runtime::detail::glfwKeyTable(keyCount);
  for (int i = 0; i < keyCount; ++i)
    if (glfwGetKey(_win, table[i]) == GLFW_PRESS && table[i] != GLFW_KEY_ESCAPE)
      in.keysDown.push_back(i);

  static const int s_mouseTable[] = {
      GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_MOUSE_BUTTON_RIGHT,
      GLFW_MOUSE_BUTTON_4, GLFW_MOUSE_BUTTON_5};
  for (int i = 0; i < 5; ++i)
    in.mouse[i] = glfwGetMouseButton(_win, s_mouseTable[i]) == GLFW_PRESS;

  ImVec2 mp = ImGui::GetMousePos(); // relatif a l'image du jeu
  in.mouseX = mp.x - _imgMin.x;
  in.mouseY = mp.y - _imgMin.y;
  return in;
}

// Mode preview : l'image vient du jeu, les inputs y partent quand on a
// clique dedans (Echap rend la main).
static void drawGamePreview(App &app, MainPanel::PlayState &play, int pw,
                            int ph) {
  GameProcess &game = app.game();
  game.sendResize(pw, ph);

  unsigned int tex = game.texture();
  ImVec2 size(static_cast<float>(pw), static_cast<float>(ph));
  if (tex != 0)
    ImGui::Image((ImTextureID)(intptr_t)tex, size, ImVec2(0, 1), ImVec2(1, 0));
  else {
    ImGui::Dummy(size);
    ImVec2 c = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(ImVec2(c.x + 12, c.y + 12),
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                        "Lancement du jeu...");
  }
  ImVec2 imgMin = ImGui::GetItemRectMin();
  ImVec2 imgMax = ImGui::GetItemRectMax();
  GLFWwindow *win = glfwGetCurrentContext();

  if (!play.captured && ImGui::IsItemClicked(ImGuiMouseButton_Left))
    play.captured = true;

  bool escDown = win && glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
  if (play.captured && escDown && !play.escWasDown)
    play.captured = false;
  play.escWasDown = escDown;

  // Toujours envoye : un etat vide quand on ne capture pas relache tout.
  game.sendInput(play.captured ? readCapturedInput(win, imgMin)
                               : ee::ipc::InputState{});

  if (play.captured) {
    ImGui::GetWindowDrawList()->AddRect(imgMin, imgMax,
                                        IM_COL32(80, 200, 255, 255), 0.0f, 0, 2.0f);
    ImGui::TextDisabled("Inputs envoyes au jeu | Echap pour rendre la main");
  } else
    ImGui::TextDisabled("Clique dans l'image pour jouer");
}

static void drawScenePreview(App &app, ScenePreview &preview,
                             MainPanel::NavState &nav,
                             MainPanel::PlayState &play) {
  float hintH = ImGui::GetTextLineHeightWithSpacing();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  int pw = static_cast<int>(avail.x);
  int ph = static_cast<int>(avail.y - hintH);
  if (pw <= 0 || ph <= 0)
    return;

  if (play.active && app.game().mode() == ee::ipc::PlayMode::Preview) {
    drawGamePreview(app, play, pw, ph);
    return;
  }

  // Edition, ou Play en fenetre separee : vue editeur de la scene affichee
  // (le snapshot du jeu pendant le Play). Pas de gizmo pendant le Play.
  SceneInfo &shown = play.active ? play.view : app.sceneData();
  unsigned int tex = preview.render(shown, pw, ph, app.selectedEntity());
  if (tex != 0)
    ImGui::Image((ImTextureID)(intptr_t)tex,
                 ImVec2(static_cast<float>(pw), static_cast<float>(ph)),
                 ImVec2(0, 1), ImVec2(1, 0));

  ImVec2 imgMin = ImGui::GetItemRectMin();
  GLFWwindow *win = glfwGetCurrentContext();

  bool gizmoActive =
      !play.active && drawGizmo(app, preview, nav, imgMin, pw, ph, tex);
  if (play.active)
    ImGuizmo::BeginFrame();
  handlePreviewClicks(app, preview, nav, win, tex, gizmoActive, shown);
  updateNavOrShowToolbar(preview, nav, win);
}

// Suit la session de jeu : snapshot -> vue affichee, fin de session, crash.
static void syncPlayView(App &app, MainPanel::PlayState &play) {
  GameProcess &game = app.game();

  if (game.state() == GameProcess::State::Crashed) {
    play.crashMessage = game.crashMessage();
    play.crashLog = game.log();
    play.openCrashPopup = true;
    game.acknowledgeCrash();
  }

  if (!game.running()) {
    if (play.active) { // fin de session : retour a la scene, intacte
      play.active = false;
      play.view = SceneInfo{};
      play.captured = false;
      play.editTime = -1.0;
    }
    return;
  }

  if (!play.active) { // debut : en attendant le 1er snapshot, la scene telle quelle
    play.active = true;
    play.view = app.sceneData();
    play.viewVersion = game.snapshotVersion();
  }

  if (game.snapshot() && game.snapshotVersion() != play.viewVersion) {
    play.view = *game.snapshot();
    play.viewVersion = game.snapshotVersion();

    // Champ en cours d'edition : on garde la valeur de l'utilisateur.
    if (play.editTime >= 0.0 && ImGui::GetTime() - play.editTime < 0.3 &&
        play.editEntity >= 0 &&
        play.editEntity < static_cast<int>(play.view.entities.size()))
      for (ComponentInstance &ci : play.view.entities[play.editEntity].components)
        if (ci.name == play.editComp && ci.values.count(play.editField))
          ci.values[play.editField] = play.editValue;
  }
}

void MainPanel::drawHidden() {
  // Onglet Main masque pendant le Play : plus aucun input vers le jeu
  // (sinon une touche enfoncee au moment du changement resterait bloquee).
  if (m_play.captured) {
    m_play.captured = false;
    App::getInstance().game().sendInput(ee::ipc::InputState{});
  }
}

void MainPanel::draw(Project *project) {
  if (!project || !project->isValid()) {
    ImGui::TextDisabled("Aucun projet ouvert");
    return;
  }

  App &app = App::getInstance();
  app.loadSceneIfNeeded();
  syncPlayView(app, m_play);

  // Pendant le Play, hierarchie et inspecteur montrent l'etat du jeu.
  SceneInfo &shown = m_play.active ? m_play.view : app.sceneData();

  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float totalW = ImGui::GetContentRegionAvail().x;
  const float leftW = totalW * 0.20f;
  const float rightW = totalW * 0.25f;
  const float centerW = totalW - leftW - rightW - 2.0f * spacing;

  ImGui::BeginChild("HierarchyRegion", ImVec2(leftW, 0), true);
  drawHierarchy(app, project, m_del, m_sceneStatus, shown, m_play.active);
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("CenterRegion", ImVec2(centerW, 0), true);
  ImGui::Text("Projet ouvert : %s", project->name().c_str());
  ImGui::Separator();
  drawBuildPlay(app, project, m_buildStatus, m_buildLog, m_openBuildErrorPopup,
                m_play);
  ImGui::Separator();
  drawScenePreview(app, m_preview, m_nav, m_play);
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("InspectorRegion", ImVec2(rightW, 0), true);
  drawInspector(app, project, m_del, shown, m_play.active ? &m_play : nullptr);
  ImGui::EndChild();

  drawDeleteConfirm(app, m_del);
  drawDeleteEntityConfirm(app, m_del);
}
