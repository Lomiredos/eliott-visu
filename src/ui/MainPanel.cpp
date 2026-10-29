#include "visu/ui/MainPanel.hpp"

#include "visu/App.hpp"
#include "visu/core/Project.hpp"
#include "visu/core/SceneInfo.hpp"
#include "visu/helpers/BuildProject.hpp"
#include "visu/helpers/ComponentGetter.hpp"
#include "visu/helpers/OpenExternal.hpp"
#include "visu/helpers/SceneGen.hpp"
#include "visu/ui/BuildProgress.hpp"
#include "visu/ui/ComponentPanel.hpp" // openCreateComponent
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
#include "visu/runtime/DefaultCamera.hpp" // picking en pause
#include "visu/systems/RenderSystem.hpp"   // sceneCameras
#include "visu/scene/SceneHierarchy.hpp"       // parent/enfant
#include "visu/render/SplitScreen.hpp"
#include "visu/runtime/GlfwKeys.hpp" // touches par position physique, comme le jeu

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <variant>

#include "IconsFontAwesome6.h"
#include "visu/helpers/FilesGetter.hpp"
#include "visu/helpers/AssetList.hpp"
#include "visu/core/Color3.hpp"

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

// Champ couleur ("#rrggbb" dans la scene) : selecteur de couleur.
static bool colorField(const char *_label, std::string &_hex) {
  ee::Color3 c;
  ee::fromHex(_hex, c);
  float rgb[3] = {c.r, c.g, c.b};
  if (!ImGui::ColorEdit3(_label, rgb))
    return false;
  _hex = ee::toHex({rgb[0], rgb[1], rgb[2]});
  return true;
}

// Champs texte qui sont des chemins d'asset : extensions acceptees.
static const std::vector<std::string> *assetFieldExtensions(const std::string &_comp,
                                                            const std::string &_field) {
  static const std::vector<std::string> mat = {".mat"};
  static const std::vector<std::string> obj = {".obj"};
  if (_comp == "MaterialComponent" && _field == "path")
    return &mat;
  if (_comp == "MeshComponent" && _field == "path")
    return &obj;
  return nullptr;
}

static bool hasExtension(const std::string &_path, const std::vector<std::string> &_exts) {
  std::string ext = std::filesystem::path(_path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char _c) { return static_cast<char>(std::tolower(_c)); });
  return std::find(_exts.begin(), _exts.end(), ext) != _exts.end();
}

// Champ chemin d'asset : liste des fichiers du projet (Assets/) avec la bonne
// extension, ou depot d'un asset glisse (panneau Materiaux). true si modifie.
static bool assetField(const char *_label, std::string &_path,
                       const std::vector<std::string> &_exts,
                       const std::filesystem::path &_root) {
  bool changed = false;
  std::string shown = _path.empty() ? "(aucun)" : std::filesystem::path(_path).stem().string();
  if (ImGui::BeginCombo(_label, shown.c_str())) {
    if (ImGui::Selectable("(aucun)", _path.empty()) && !_path.empty()) {
      _path.clear();
      changed = true;
    }
    for (const std::string &a : listAssets(_root, _exts))
      if (ImGui::Selectable(a.c_str(), a == _path) && a != _path) {
        _path = a;
        changed = true;
      }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered() && !_path.empty())
    ImGui::SetTooltip("%s", _path.c_str());
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(kAssetPayload)) {
      std::string dropped(static_cast<const char *>(p->Data));
      if (hasExtension(dropped, _exts) && dropped != _path) {
        _path = dropped;
        changed = true;
      }
    }
    ImGui::EndDragDropTarget();
  }
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

// Pose le materiau _path sur l'entite (ajoute le MaterialComponent si besoin).
static void assignMaterial(SceneInfo &_scene, int _entity, const std::string &_path) {
  if (_entity < 0 || _entity >= static_cast<int>(_scene.entities.size()))
    return;
  for (ComponentInstance &ci : _scene.entities[_entity].components)
    if (ci.name == "MaterialComponent") {
      ci.values["path"] = _path;
      return;
    }
  ComponentInstance ci;
  ci.name = "MaterialComponent";
  ci.values["path"] = _path;
  _scene.entities[_entity].components.push_back(std::move(ci));
}

// Changement de parent demande par un glisser-deposer dans la hierarchie.
struct HierarchyDrop {
  int child = -1;
  int parent = -1; // -1 = racine
  int materialTarget = -1; // materiau (.mat) depose sur une entite
  std::string material;
};

// Une entite de la hierarchie et (si ouverte) ses enfants, recursivement.
static void drawEntityNode(const SceneInfo &scene,
                           const std::vector<std::vector<int>> &children,
                           const std::vector<int> &parents, int i,
                           int &selected, MainPanel::DeletionState &del,
                           bool playing, HierarchyDrop &drop) {
  const std::string display = entityDisplayName(scene, i);
  const bool leaf = children[i].empty();

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick |
                             ImGuiTreeNodeFlags_SpanAvailWidth |
                             ImGuiTreeNodeFlags_DefaultOpen |
                             ImGuiTreeNodeFlags_AllowOverlap;
  if (leaf)
    flags |= ImGuiTreeNodeFlags_Leaf;
  if (selected == i)
    flags |= ImGuiTreeNodeFlags_Selected;

  bool open = ImGui::TreeNodeEx((display + "##ent" + std::to_string(i)).c_str(),
                                flags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    selected = i;

  // Glisser l'entite : sur une autre entite (parent) ou sur un champ
  // "entity" de l'inspecteur.
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(kEntityPayload, &i, sizeof(int));
    ImGui::TextUnformatted(display.c_str());
    ImGui::EndDragDropSource();
  }
  if (!playing && ImGui::BeginDragDropTarget()) {
    const ImGuiPayload *peek = ImGui::AcceptDragDropPayload(
        kEntityPayload, ImGuiDragDropFlags_AcceptPeekOnly);
    if (peek) {
      int child = *static_cast<const int *>(peek->Data);
      // Refus : soi-meme, un de ses propres enfants (boucle), sans Transform.
      bool ok = child != i && !ee::scene::isInSubtree(parents, i, child) &&
                ee::scene::findTransform(scene.entities[child]) &&
                ee::scene::findTransform(scene.entities[i]);
      if (ok) {
        if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(kEntityPayload))
          if (p->IsDelivery()) {
            drop.child = child;
            drop.parent = i;
          }
      } else
        ImGui::SetTooltip(child == i || ee::scene::isInSubtree(parents, i, child)
                              ? "Impossible : boucle de parents"
                              : "Il faut un TransformComponent aux deux");
    }
    // Materiau glisse depuis le panneau Materials.
    if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(kAssetPayload)) {
      std::string path(static_cast<const char *>(p->Data));
      if (hasExtension(path, {".mat"})) {
        drop.materialTarget = i;
        drop.material = path;
      }
    }
    ImGui::EndDragDropTarget();
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

  if (open) {
    for (int c : children[i])
      drawEntityNode(scene, children, parents, c, selected, del, playing, drop);
    ImGui::TreePop();
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

  // Arbre parent/enfant (le parent est le champ Parent du Transform).
  // Glisser une entite sur une autre = en faire son enfant ; sur la zone
  // vide en bas = la remettre a la racine. Elle garde sa place dans le monde.
  const std::vector<int> parents = ee::scene::sceneParents(scene);
  std::vector<std::vector<int>> children(scene.entities.size());
  for (int i = 0; i < static_cast<int>(parents.size()); ++i)
    if (parents[i] >= 0)
      children[parents[i]].push_back(i);

  HierarchyDrop drop;
  for (int i = 0; i < static_cast<int>(scene.entities.size()); ++i)
    if (parents[i] < 0)
      drawEntityNode(scene, children, parents, i, selected, del, playing, drop);

  // Zone vide sous l'arbre : deposer ici = plus de parent.
  ImVec2 rest = ImGui::GetContentRegionAvail();
  ImGui::Dummy(ImVec2(rest.x, std::max(rest.y, 24.0f)));
  if (!playing && ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(kEntityPayload)) {
      drop.child = *static_cast<const int *>(p->Data);
      drop.parent = -1;
    }
    ImGui::EndDragDropTarget();
  }

  // Applique apres le dessin (l'arbre ne change pas pendant qu'on le parcourt).
  if (drop.child >= 0 && ee::scene::reparent(scene, drop.child, drop.parent))
    app.markSceneDirty();
  if (drop.materialTarget >= 0) {
    assignMaterial(scene, drop.materialTarget, drop.material);
    app.markSceneDirty();
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
  std::map<std::string, std::map<std::string, std::string>> fieldTypes =
      getAllComponentFieldTypes(project->componentsCatalog());

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

        // Le parent se change dans la hierarchie (garde la place dans le
        // monde) ; un depot ici deplacerait l'entite.
        if (ci.name == "TransformComponent" && kv.first == "Parent")
          continue;

        const char *lbl = kv.first.c_str();
        bool edited = false;
        const std::string *refRequired = nullptr;
        if (auto c = refFields.find(ci.name); c != refFields.end())
          if (auto f = c->second.find(kv.first); f != c->second.end())
            refRequired = &f->second;

        bool isColor = false;
        if (auto c = fieldTypes.find(ci.name); c != fieldTypes.end())
          if (auto f = c->second.find(kv.first); f != c->second.end())
            isColor = f->second == "color";
        const std::vector<std::string> *assetExts =
            assetFieldExtensions(ci.name, kv.first);

        if (refRequired) {
          if (!std::holds_alternative<int>(kv.second)) // ancienne valeur
            kv.second = static_cast<int>(asFloat(kv.second, -1.0f));
          edited = entityRefField(app, lbl, std::get<int>(kv.second),
                                  *refRequired, scene);
        } else if (isColor && std::holds_alternative<std::string>(kv.second)) {
          edited = colorField(lbl, std::get<std::string>(kv.second));
        } else if (assetExts && std::holds_alternative<std::string>(kv.second)) {
          edited = assetField(lbl, std::get<std::string>(kv.second), *assetExts,
                              project->root());
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

  // Add Component (ou Ctrl+Shift+A) : recherche, fleches + Entree, et en
  // dernier "Creer un nouveau composant" (nom = la recherche).
  static std::string s_addFilter;
  static int s_addHighlight = 0;
  static bool s_addFocus = false;
  static std::string s_addStatus;

  const bool addKey = app.consumeAddComponentRequest() && !playing;
  ImGui::BeginDisabled(playing);
  if (ImGui::Button("Add Component") || addKey) {
    ImGui::OpenPopup("add_comp");
    s_addFilter.clear();
    s_addHighlight = 0;
    s_addFocus = true;
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Ctrl+Shift+A");
  if (!s_addStatus.empty())
    ImGui::TextDisabled("%s", s_addStatus.c_str());

  if (ImGui::BeginPopup("add_comp")) {
    std::map<std::string, std::map<std::string, FieldValue>> catalog =
        getAllComponentDefaults(project->componentsCatalog());

    // Fleches haut/bas gerees par le champ (CallbackHistory) : la navigation
    // clavier d'ImGui ne lui vole pas le focus.
    if (s_addFocus) {
      ImGui::SetKeyboardFocusHere();
      s_addFocus = false;
    }
    ImGui::SetNextItemWidth(260.0f);
    bool enter = ImGui::InputTextWithHint(
        "##addsearch", ICON_FA_MAGNIFYING_GLASS " Rechercher...", &s_addFilter,
        ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackHistory,
        [](ImGuiInputTextCallbackData *_d) {
          int *h = static_cast<int *>(_d->UserData);
          *h += _d->EventKey == ImGuiKey_UpArrow ? -1 : 1;
          return 0;
        },
        &s_addHighlight);

    auto lower = [](std::string _s) {
      std::transform(_s.begin(), _s.end(), _s.begin(),
                     [](unsigned char _c) { return (char)std::tolower(_c); });
      return _s;
    };
    const std::string needle = lower(s_addFilter);
    std::vector<const std::string *> matches;
    for (const auto &entry : catalog) {
      const std::string &cname = entry.first;
      bool already = std::any_of(
          ent.components.begin(), ent.components.end(),
          [&](const ComponentInstance &ci) { return ci.name == cname; });
      if (!already && lower(cname).find(needle) != std::string::npos)
        matches.push_back(&cname);
    }

    // Derniere ligne = creer un composant.
    const int count = static_cast<int>(matches.size()) + 1;
    s_addHighlight = ((s_addHighlight % count) + count) % count;

    std::string toAdd;
    bool create = false;
    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
      bool hl = i == s_addHighlight;
      if (ImGui::Selectable(matches[i]->c_str(), hl) || (enter && hl))
        toAdd = *matches[i];
      if (hl)
        ImGui::SetScrollHereY();
    }
    if (matches.empty())
      ImGui::TextDisabled(s_addFilter.empty() ? "(rien a ajouter)"
                                              : "Aucun composant ne correspond");

    ImGui::Separator();
    std::string createLabel = ICON_FA_PLUS " Creer un nouveau composant";
    if (!s_addFilter.empty())
      createLabel += " \"" + s_addFilter + "\"";
    bool createHl = s_addHighlight == count - 1;
    if (ImGui::Selectable(createLabel.c_str(), createHl) || (enter && createHl))
      create = true;

    if (!toAdd.empty()) {
      ComponentInstance ci;
      ci.name = toAdd;
      ci.values = catalog[toAdd];
      ent.components.push_back(std::move(ci));
      app.markSceneDirty();
      s_addStatus.clear();
      ImGui::CloseCurrentPopup();
    } else if (create) {
      ImGui::CloseCurrentPopup();
      openCreateComponent(project, s_addFilter, [](const std::string &_s) {
        s_addStatus = _s == "Composant cree."
                          ? "Composant cree : genere le catalogue (panneau "
                            "Component) pour pouvoir l'ajouter."
                          : _s;
      });
    }

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
      ImGui::Text("Supprimer \"%s\" et tous ses composants ?\n(ses enfants sont "
                  "gardes, rattaches a son parent)",
                  display.c_str());
    } else
      ImGui::TextUnformatted("Supprimer cette entite ?");

    ImGui::Spacing();

    if (ImGui::Button("Supprimer")) {
      if (valid) {
        // Les enfants restent, a la meme place dans le monde, rattaches au
        // parent de l'entite supprimee.
        {
          std::vector<int> parents = ee::scene::sceneParents(scene);
          int grandParent = parents[del.entityToDelete];
          for (int c = 0; c < static_cast<int>(parents.size()); ++c)
            if (parents[c] == del.entityToDelete)
              ee::scene::reparent(scene, c, grandParent);
        }
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
                          MainPanel::PlayState &play,
                          projectbuild::AsyncBuild &build,
                          bool &launchAfterBuild,
                          std::filesystem::path &buildRoot) {
  GameProcess &game = app.game();
  auto launchGame = [&]() {
    std::string err;
    std::filesystem::path log = project->root() / "build" / "play.log";
    if (game.start(project->executablePath(), log, play.mode, err)) {
      buildStatus.clear();
      play.status.clear();
    } else
      play.status = err;
  };

  // Fin du build (thread) : erreur, ou lancement du jeu si c'etait un Play.
  projectbuild::BuildResult r;
  if (build.finished(r)) {
    if (buildRoot != project->root()) {
      // Build d'un projet ferme depuis : ni lancement, ni statut ici.
    } else if (!r.ok) {
      buildStatus = launchAfterBuild ? "Echec du build : jeu non lance."
                                     : "Echec du build.";
      buildLog = r.log;
      openBuildErrorPopup = true;
    } else if (launchAfterBuild)
      launchGame();
    else
      buildStatus = "Build OK.";
    launchAfterBuild = false;
  }
  // Un build du catalogue (panneau Component) bloque aussi : un seul cmake a
  // la fois dans build/.
  const bool building = projectbuild::AsyncBuild::anyRunning();

  // Ctrl+Shift+B : build + Play ; si le jeu tourne, il est arrete et relance
  // (stop est synchrone : le Play part dans la meme frame).
  const bool rebuildPlayKey = app.consumeRebuildPlayRequest() && !building;
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
  const bool buildKey = app.consumeBuildRequest() && !playing && !building;
  const bool playKey = app.consumePlayRequest() || rebuildPlayKey;

  ImGui::BeginDisabled(playing || building);
  if (ImGui::Button("Build") || buildKey) {
    launchAfterBuild = false;
    buildRoot = project->root();
    build.start(buildRoot);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();

  if (!playing) {
    // Play = sauvegarde + build si une source a change + lancement (process
    // separe) ; apres un build, le lancement se fait a sa fin (plus haut).
    // Ctrl+Shift+B recompile toujours.
    ImGui::BeginDisabled(building);
    if (ImGui::Button(ICON_FA_PLAY " Play") || (playKey && !building)) {
      app.saveCurrentScene();
      if (!rebuildPlayKey &&
          projectbuild::upToDate(project->root(), project->executablePath()))
        launchGame();
      else {
        launchAfterBuild = true;
        buildRoot = project->root();
        build.start(buildRoot);
      }
    }
    ImGui::EndDisabled();
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

  if (build.running())
    drawBuildProgress(build, launchAfterBuild ? "Build avant Play" : "Build");
  else if (building)
    ImGui::TextDisabled("Generation du catalogue en cours...");
  else if (!buildStatus.empty())
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

  // Le gizmo travaille dans le monde ; pour un enfant, le resultat est
  // reconverti en transform local (relatif au parent).
  const std::vector<int> parents = ee::scene::sceneParents(sc);
  const std::vector<ee::scene::Xform> world = ee::scene::sceneWorldXforms(sc);
  const int parent = parents[sel];
  const ee::scene::Xform &wx = world[sel];

  float vmat[16], pmat[16];
  preview.getViewMatrix(vmat);
  preview.getProjMatrix(pmat);

  float scx = wx.scale.x, scy = wx.scale.y, scz = wx.scale.z;
  if (scx <= 0.0f)
    scx = 1.0f;
  if (scy <= 0.0f)
    scy = 1.0f;
  if (scz <= 0.0f)
    scz = 1.0f;
  float translation[3] = {wx.pos.x, wx.pos.y, wx.pos.z};
  float rotation[3] = {wx.eulerDeg.x, wx.eulerDeg.y, wx.eulerDeg.z};
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
    ee::scene::Xform moved = ee::scene::makeXform(
        {translation[0], translation[1], translation[2]},
        {rotation[0], rotation[1], rotation[2]}, {scale[0], scale[1], scale[2]});
    ee::scene::writeLocalXform(
        *tf, parent >= 0 ? ee::scene::relativeTo(world[parent], moved) : moved);
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

  // Materiau glisse sur la preview : pose sur l'entite sous la souris.
  if (tex != 0 && ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(kAssetPayload)) {
      std::string path(static_cast<const char *>(p->Data));
      ImVec2 mp = ImGui::GetMousePos();
      int hit = preview.pick(scene, 2.0f * (mp.x - imgMin.x) / pw - 1.0f,
                             1.0f - 2.0f * (mp.y - imgMin.y) / ph,
                             static_cast<float>(pw) / ph);
      if (hit >= 0 && hasExtension(path, {".mat"})) {
        assignMaterial(scene, hit, path);
        app.selectedEntity() = hit;
        app.markSceneDirty();
      }
    }
    ImGui::EndDragDropTarget();
  }

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
                m_play, m_build, m_launchAfterBuild, m_buildRoot);
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
