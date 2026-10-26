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
#include "visu/runtime/GameSession.hpp" // defaultCamera : picking en pause
#include "visu/systems/RenderSystem.hpp"   // sceneCameras
#include "visu/render/SplitScreen.hpp"
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

// Glisser-deposer d'une entite (payload = son index dans la scene, int).
static constexpr const char *kEntityPayload = "EE_ENTITY";

static std::string entityDisplayName(const SceneInfo &_scene, int _index) {
  const std::string &nm = _scene.entities[_index].name;
  return nm.empty() ? ("Entity " + std::to_string(_index)) : nm;
}

static bool entityHasComponent(const EntityInfo &_ent, const std::string &_comp) {
  return std::any_of(_ent.components.begin(), _ent.components.end(),
                     [&](const ComponentInstance &_c) { return _c.name == _comp; });
}

// Champ "entity" : affiche l'entite visee ; on la change en deposant une
// entite de la hierarchie dessus (refusee si elle n'a pas _required), X la
// retire, double-clic selectionne l'entite visee. true si modifie.
static bool entityRefField(App &_app, const char *_label, int &_value,
                           const std::string &_required,
                           const SceneInfo &_scene) {
  const int count = static_cast<int>(_scene.entities.size());
  bool changed = false;

  std::string text;
  if (_value < 0)
    text = _required.empty() ? "(aucune)" : "(aucune : " + _required + ")";
  else if (_value >= count)
    text = "(introuvable)";
  else
    text = entityDisplayName(_scene, _value);

  const ImGuiStyle &st = ImGui::GetStyle();
  const float clearW = ImGui::GetFrameHeight();
  const float w = ImGui::CalcItemWidth() - clearW - st.ItemInnerSpacing.x;

  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
  ImGui::PushStyleColor(ImGuiCol_Button, st.Colors[ImGuiCol_FrameBg]);
  ImGui::Button((text + "##ref").c_str(), ImVec2(w, 0));
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();

  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && _value >= 0 &&
      _value < count)
    _app.selectedEntity() = _value;
  if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload())
    ImGui::SetTooltip("Glisse une entite de la hierarchie ici%s%s",
                      _required.empty() ? "" : "\nRequis : ",
                      _required.c_str());

  if (ImGui::BeginDragDropTarget()) {
    const ImGuiPayload *peek = ImGui::AcceptDragDropPayload(
        kEntityPayload, ImGuiDragDropFlags_AcceptPeekOnly);
    if (peek) {
      int index = *static_cast<const int *>(peek->Data);
      bool ok = index >= 0 && index < count &&
                (_required.empty() ||
                 entityHasComponent(_scene.entities[index], _required));
      if (ok) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(kEntityPayload))
          if (p->IsDelivery() && _value != index) {
            _value = index;
            changed = true;
          }
      } else {
        // Mauvais type d'entite : cadre rouge, depot ignore.
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                            ImGui::GetItemRectMax(),
                                            IM_COL32(230, 70, 70, 255), 0.0f,
                                            0, 2.0f);
        ImGui::SetTooltip("Il faut une entite avec %s", _required.c_str());
      }
    }
    ImGui::EndDragDropTarget();
  }

  ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
  ImGui::BeginDisabled(_value < 0);
  if (ImGui::Button("X##clearref", ImVec2(clearW, 0))) {
    _value = -1;
    changed = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
  ImGui::TextUnformatted(_label);
  return changed;
}

// Apres suppression de l'entite _removed : les references vers elle passent
// a -1 et celles vers les entites suivantes sont decalees (index - 1).
static void fixEntityRefsAfterRemove(
    SceneInfo &_scene, int _removed,
    const std::map<std::string, std::map<std::string, std::string>> &_refFields) {
  for (EntityInfo &ent : _scene.entities)
    for (ComponentInstance &ci : ent.components) {
      auto comp = _refFields.find(ci.name);
      if (comp == _refFields.end())
        continue;
      for (const auto &field : comp->second) {
        auto it = ci.values.find(field.first);
        if (it == ci.values.end())
          continue;
        int *v = std::get_if<int>(&it->second);
        if (!v)
          continue;
        if (*v == _removed)
          *v = -1;
        else if (*v > _removed)
          --*v;
      }
    }
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
    // Glisser l'entite sur un champ "entity" de l'inspecteur.
    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload(kEntityPayload, &i, sizeof(int));
      ImGui::TextUnformatted(display.c_str());
      ImGui::EndDragDropSource();
    }

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
  std::map<std::string, std::map<std::string, std::string>> refFields =
      getAllComponentEntityFields(project->componentsCatalog());

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
        const std::string *refRequired = nullptr;
        if (auto c = refFields.find(ci.name); c != refFields.end())
          if (auto f = c->second.find(kv.first); f != c->second.end())
            refRequired = &f->second;

        if (refRequired) {
          if (!std::holds_alternative<int>(kv.second)) // ancienne valeur
            kv.second = static_cast<int>(asFloat(kv.second, -1.0f));
          edited = entityRefField(app, lbl, std::get<int>(kv.second),
                                  *refRequired, scene);
        } else if (auto p = std::get_if<float>(&kv.second))
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

static void drawDeleteEntityConfirm(App &app, Project *project,
                                    MainPanel::DeletionState &del) {
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
        fixEntityRefsAfterRemove(
            scene, del.entityToDelete,
            getAllComponentEntityFields(project->componentsCatalog()));

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
  // Ctrl+Shift+B : build + Play ; si le jeu tourne, il est arrete et relance
  // (stop est synchrone : le Play part dans la meme frame).
  const bool rebuildPlayKey = app.consumeRebuildPlayRequest();
  if (rebuildPlayKey && game.running()) {
    game.stop();
    // Nouvelle session : la vue repart de la scene, pas du vieux snapshot.
    play.active = false;
    play.view = SceneInfo{};
    play.captured = false;
    play.editTime = -1.0;
  }
  const bool playing = game.running();
  // Raccourcis Ctrl+B / Ctrl+P (lus par App), equivalents aux boutons.
  const bool buildKey = app.consumeBuildRequest() && !playing;
  const bool playKey = app.consumePlayRequest() || rebuildPlayKey;

  ImGui::BeginDisabled(playing);
  if (ImGui::Button("Build") || buildKey) {
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
    if (ImGui::Button(ICON_FA_PLAY " Play") || playKey) {
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
    if (ImGui::Button(ICON_FA_STOP " Stop") || playKey)
      game.stop();
    ImGui::SameLine();
    if (game.mode() == ee::ipc::PlayMode::Preview &&
        game.state() == GameProcess::State::Running) {
      if (ImGui::Button(game.paused() ? ICON_FA_PLAY " Reprendre"
                                      : ICON_FA_PAUSE " Pause")) {
        game.setPaused(!game.paused());
        play.captured = false; // en pause, plus d'inputs vers le jeu
      }
      ImGui::SameLine();
    }
    if (game.state() == GameProcess::State::Starting &&
        game.secondsSinceStart() > 5.0)
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                         "Le jeu ne se connecte pas (moteur du projet a jour ?)");
    else if (game.state() == GameProcess::State::Starting)
      ImGui::TextDisabled("Lancement du jeu...");
    else if (game.mode() == ee::ipc::PlayMode::Window)
      ImGui::TextDisabled("En cours (fenetre separee)");
    else if (game.paused())
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "En pause");
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
                                const SceneInfo &shown, bool overInset) {
  if (overInset)
    return; // clic sur l'apercu camera : pas de picking ni de navigation

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
  const bool hovered = ImGui::IsItemHovered();

  // Clic dans l'image : le jeu prend les inputs ; en pause, il selectionne
  // l'entite sous la souris (rayon depuis la camera du jeu, sur le dernier
  // snapshot). Clic ailleurs (autre panneau, bouton Pause...) : rend les inputs.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (hovered && game.paused()) {
      // Ecran partage : la case sous la souris donne la camera, et le rayon
      // part de cette camera a travers le point de la case.
      std::vector<ee::render::Camera> cams;
      for (const ee::systems::SceneCamera &sc : ee::systems::sceneCameras(play.view))
        cams.push_back(sc.cam);
      if (cams.empty())
        cams.push_back(ee::runtime::defaultCamera());
      std::vector<ee::render::Viewport> views = ee::render::splitScreen(
          static_cast<int>(cams.size()), static_cast<int>(imgMax.x - imgMin.x),
          static_cast<int>(imgMax.y - imgMin.y));
      ImVec2 mp = ImGui::GetMousePos();
      float mx = mp.x - imgMin.x, my = mp.y - imgMin.y;
      for (size_t i = 0; i < views.size(); ++i) {
        const ee::render::Viewport &v = views[i];
        if (mx < v.x || my < v.y || mx >= v.x + v.w || my >= v.y + v.h)
          continue;
        float ndcX = 2.0f * (mx - v.x) / v.w - 1.0f;
        float ndcY = 1.0f - 2.0f * (my - v.y) / v.h;
        int hit = ScenePreview::pickFrom(play.view, cams[i], ndcX, ndcY,
                                         static_cast<float>(v.w) / v.h);
        if (hit >= 0)
          app.selectedEntity() = hit;
        break;
      }
    } else if (hovered)
      play.captured = true;
    else
      play.captured = false;
  }
  if (game.paused())
    play.captured = false;

  bool escDown = win && glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
  if (play.captured && escDown && !play.escWasDown)
    play.captured = false;
  play.escWasDown = escDown;

  // Toujours envoye : un etat vide quand on ne capture pas relache tout.
  game.sendInput(play.captured ? readCapturedInput(win, imgMin)
                               : ee::ipc::InputState{});
  // Le jeu dessine le collider de l'entite selectionnee.
  game.sendSelect(app.selectedEntity());

  if (game.paused()) {
    const char *msg = "PAUSE";
    ImVec2 ts = ImGui::CalcTextSize(msg);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(imgMin.x + (imgMax.x - imgMin.x - ts.x) * 0.5f, imgMin.y + 12.0f),
        IM_COL32(255, 200, 60, 255), msg);
  }

  if (play.captured) {
    ImGui::GetWindowDrawList()->AddRect(imgMin, imgMax,
                                        IM_COL32(80, 200, 255, 255), 0.0f, 0, 2.0f);
    ImGui::TextDisabled("Inputs envoyes au jeu | Echap pour rendre la main");
  } else if (game.paused())
    ImGui::TextDisabled("Jeu en pause : clic pour selectionner une entite");
  else
    ImGui::TextDisabled("Clique dans l'image pour jouer");
}

// Camera selectionnee : apercu flottant de ce qu'elle voit, en haut a droite
// de la preview (meme rapport largeur/hauteur que la preview, donc que le jeu
// lance dedans). Renvoie true si la souris est dessus.
static bool drawCameraInset(App &app, ScenePreview &preview,
                            const SceneInfo &scene, ImVec2 imgMin, int pw,
                            int ph) {
  ee::render::Camera cam;
  int sel = app.selectedEntity();
  if (!ee::systems::entityCamera(scene, sel, cam))
    return false;

  // Forme de l'apercu = celle de la case de cette camera dans l'ecran
  // partage du jeu (lance dans la preview, donc a la taille de la preview).
  float aspect = static_cast<float>(pw) / ph;
  std::vector<ee::systems::SceneCamera> cams = ee::systems::sceneCameras(scene);
  for (size_t i = 0; i < cams.size(); ++i)
    if (cams[i].entity == sel) {
      ee::render::Viewport v = ee::render::splitScreen(
          static_cast<int>(cams.size()), pw, ph)[i];
      aspect = static_cast<float>(v.w) / v.h;
    }

  const float margin = 8.0f;
  int iw = std::max(160, static_cast<int>(pw * 0.3f));
  iw = std::min(iw, pw - 2 * static_cast<int>(margin));
  int ih = static_cast<int>(iw / aspect);
  if (ih > ph / 2) { // case tres haute : on borne par la hauteur
    ih = ph / 2;
    iw = static_cast<int>(ih * aspect);
  }
  if (iw <= 0 || ih <= 0)
    return false;

  unsigned int tex = preview.renderCameraView(scene, cam, iw, ih);
  if (tex == 0)
    return false;

  ImVec2 pmin(imgMin.x + pw - margin - iw, imgMin.y + margin);
  ImVec2 pmax(pmin.x + iw, pmin.y + ih);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(ImVec2(pmin.x - 3, pmin.y - 3), ImVec2(pmax.x + 3, pmax.y + 3),
                    IM_COL32(0, 0, 0, 160), 4.0f);
  dl->AddImage((ImTextureID)(intptr_t)tex, pmin, pmax, ImVec2(0, 1),
               ImVec2(1, 0));
  dl->AddRect(ImVec2(pmin.x - 1, pmin.y - 1), ImVec2(pmax.x + 1, pmax.y + 1),
              IM_COL32(140, 190, 255, 255), 0.0f, 0, 1.5f);

  const std::string &nm = scene.entities[sel].name;
  std::string label = nm.empty() ? "Camera" : nm;
  dl->AddText(ImVec2(pmin.x + 6, pmin.y + 4), IM_COL32(0, 0, 0, 200),
              label.c_str());
  dl->AddText(ImVec2(pmin.x + 5, pmin.y + 3), IM_COL32(230, 240, 255, 255),
              label.c_str());

  return ImGui::IsMouseHoveringRect(pmin, pmax);
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

  if (play.active) {
    drawGamePreview(app, play, pw, ph);
    return;
  }

  // Fenetre separee : le jeu n'est pas relie a l'editeur, rien a montrer ici.
  if (app.game().running() &&
      app.game().mode() == ee::ipc::PlayMode::Window) {
    ImGui::Dummy(ImVec2(static_cast<float>(pw), static_cast<float>(ph)));
    ImVec2 rmin = ImGui::GetItemRectMin();
    const char *msg = "Playing in another window\nFor debug, play in editor";
    ImVec2 ts = ImGui::CalcTextSize(msg);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(rmin.x + (pw - ts.x) * 0.5f, rmin.y + (ph - ts.y) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), msg);
    return;
  }

  // Edition : vue editeur de la scene.
  SceneInfo &scene = app.sceneData();
  unsigned int tex = preview.render(scene, pw, ph, app.selectedEntity());
  if (tex != 0)
    ImGui::Image((ImTextureID)(intptr_t)tex,
                 ImVec2(static_cast<float>(pw), static_cast<float>(ph)),
                 ImVec2(0, 1), ImVec2(1, 0));

  ImVec2 imgMin = ImGui::GetItemRectMin();
  GLFWwindow *win = glfwGetCurrentContext();

  bool gizmoActive = drawGizmo(app, preview, nav, imgMin, pw, ph, tex);
  bool overInset = drawCameraInset(app, preview, scene, imgMin, pw, ph);
  handlePreviewClicks(app, preview, nav, win, tex, gizmoActive, scene,
                      overInset);
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

  // Seul le Play dans la preview est relie a l'editeur ; en fenetre separee,
  // hierarchie et inspecteur restent sur la scene.
  const bool linked =
      game.running() && game.mode() == ee::ipc::PlayMode::Preview;
  if (!linked) {
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
  drawDeleteEntityConfirm(app, project, m_del);
}
