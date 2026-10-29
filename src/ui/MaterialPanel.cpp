#include "visu/ui/MaterialPanel.hpp"

#include "visu/App.hpp"
#include "visu/core/MeshStore.hpp"
#include "visu/core/Project.hpp"
#include "visu/helpers/AssetList.hpp"
#include "visu/helpers/NameValidation.hpp"
#include "visu/systems/RenderSystem.hpp"
#include "visu/ui/ConfirmModal.hpp"

#include "imgui.h"
#include <imgui_stdlib.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace
{
    const char *kSlotLabels[ee::core::kMaterialMapCount] = {"Albedo",    "Normal", "Metallic",
                                                            "Roughness", "AO",     "Emissive"};
    const char *kSlotHelp[ee::core::kMaterialMapCount] = {
        "Couleur de base (multipliee par la couleur Albedo)",
        "Relief (normal map, espace tangent, vert = haut / OpenGL)",
        "Metal : blanc = metal, noir = non metal (canal R)",
        "Rugosite : blanc = mat, noir = lisse (canal R)",
        "Occlusion ambiante : coins sombres (canal R)",
        "Lumiere propre (multipliee par la couleur Emissive)"};

    bool colorEdit(const char *_label, ee::Color3 &_c)
    {
        float rgb[3] = {_c.r, _c.g, _c.b};
        if (!ImGui::ColorEdit3(_label, rgb))
            return false;
        _c = {rgb[0], rgb[1], rgb[2]};
        return true;
    }
}

void MaterialPanel::draw(Project *_project)
{
    if (!_project || !_project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        return;
    }
    refresh(_project);

    ImGui::BeginChild("##materials", ImVec2(260.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    drawList(_project);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##material_edit", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    drawEditor(_project);
    ImGui::EndChild();

    // Ecrit le fichier quand on lache le reglage (pas a chaque frame d'un drag).
    if (m_dirty && !ImGui::IsAnyItemActive())
        save(_project);
}

void MaterialPanel::drawList(Project *_project)
{
    ImGui::SetNextItemWidth(-70.0f);
    bool create = ImGui::InputTextWithHint("##newmat", "Nouveau materiau...", &m_newName,
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool valid = namevalidation::isValidIdentifier(m_newName);
    ImGui::BeginDisabled(!valid);
    create = ImGui::Button("Creer") || create;
    ImGui::EndDisabled();
    if (create && valid)
    {
        std::string rel = "Assets/Materials/" + m_newName + ".mat";
        if (fs::exists(_project->root() / rel))
            m_status = "\"" + m_newName + "\" existe deja.";
        else if (ee::core::saveMaterialFile(_project->root() / rel, ee::core::MaterialAsset{}))
        {
            m_status = "Materiau cree.";
            m_newName.clear();
            refresh(_project, true);
            select(_project, rel);
        }
        else
            m_status = "Ecriture impossible.";
    }
    if (!m_newName.empty() && !valid)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Nom : lettres, chiffres, _");
    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::Separator();

    if (m_mats.empty())
        ImGui::TextDisabled("Aucun materiau (Assets/**/*.mat)");
    for (const std::string &m : m_mats)
    {
        std::string label = fs::path(m).stem().string() + "##" + m;
        if (ImGui::Selectable(label.c_str(), m == m_selected))
            select(_project, m);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nGlisser sur une entite ou un champ path", m.c_str());

        // Glisser le materiau : sur une entite (hierarchie, preview) ou un champ.
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kAssetPayload, m.c_str(), m.size() + 1);
            ImGui::TextUnformatted(fs::path(m).stem().string().c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Supprimer"))
            {
                fs::path file = _project->root() / m;
                std::string rel = m;
                Project *proj = _project;
                App::getInstance().openModal(std::make_unique<ConfirmModal>(
                    "Supprimer le materiau ?",
                    "Supprimer " + rel +
                        " ?\nLes entites qui l'utilisent repasseront en couleur unie.",
                    std::vector<ModalChoice>{{"Supprimer",
                                              [this, file, rel, proj]()
                                              {
                                                  std::error_code ec;
                                                  fs::remove(file, ec);
                                                  if (m_selected == rel)
                                                  {
                                                      m_selected.clear();
                                                      m_dirty = false;
                                                  }
                                                  m_status = ec ? "Suppression impossible."
                                                                : "Materiau supprime.";
                                                  refresh(proj, true);
                                              }},
                                             {"Annuler", nullptr}}));
            }
            ImGui::EndPopup();
        }
    }
}

// Liste des .mat et etat du fichier selectionne, relus au plus 1 fois/s
// (parcourir Assets/ a chaque frame coute cher sur un gros projet).
void MaterialPanel::refresh(Project *_project, bool _force)
{
    auto now = std::chrono::steady_clock::now();
    if (!_force && now - m_lastRefresh < std::chrono::seconds(1))
        return;
    m_lastRefresh = now;
    m_mats = listAssets(_project->root(), {".mat"});

    if (m_selected.empty())
        return;
    std::error_code ec;
    m_exists = fs::exists(_project->root() / m_selected, ec) && !ec;

    // Modifie hors de l'editeur (texte, git...) : on relit si rien n'est en cours.
    if (m_exists && !m_dirty && selectedMtime(_project) != m_mtime)
    {
        if (auto loaded = ee::core::loadMaterialFile(_project->root() / m_selected))
        {
            m_edit = *loaded;
            m_status = "Recharge (modifie hors de l'editeur).";
        }
        m_mtime = selectedMtime(_project);
    }
}

fs::file_time_type MaterialPanel::selectedMtime(Project *_project) const
{
    std::error_code ec;
    auto t = fs::last_write_time(_project->root() / m_selected, ec);
    return ec ? fs::file_time_type{} : t;
}

void MaterialPanel::select(Project *_project, const std::string &_path)
{
    if (m_dirty)
        save(_project);
    m_selected = _path;
    auto loaded = ee::core::loadMaterialFile(_project->root() / _path);
    m_edit = loaded ? *loaded : ee::core::MaterialAsset{};
    m_mtime = selectedMtime(_project);
    m_exists = loaded.has_value() || fs::exists(_project->root() / _path);
    m_dirty = false;
}

void MaterialPanel::save(Project *_project)
{
    m_dirty = false;
    if (m_selected.empty())
        return;
    const fs::path file = _project->root() / m_selected;

    // Le fichier a change sur le disque depuis qu'on l'a lu : on ne l'ecrase
    // pas, on reprend la version du disque.
    // Sinon (ou en cas d'echec d'ecriture), le cache revient a ce qui est sur le
    // disque : les entites n'affichent pas des valeurs non enregistrees.
    bool conflict = selectedMtime(_project) != m_mtime;
    if (conflict || !ee::core::saveMaterialFile(file, m_edit))
    {
        m_status = conflict ? "Modifie hors de l'editeur : retouche annulee, fichier recharge."
                            : "Ecriture impossible : " + m_selected;
        if (auto onDisk = ee::core::loadMaterialFile(file))
        {
            m_edit = *onDisk;
            ee::core::setMaterialInCache(m_selected, *onDisk);
        }
        m_mtime = selectedMtime(_project);
        return;
    }
    m_mtime = selectedMtime(_project);
    ee::core::setMaterialInCache(m_selected, m_edit);
}

bool MaterialPanel::drawMapSlot(Project *_project, int _slot)
{
    std::string &path = m_edit.maps[_slot];
    bool changed = false;
    ImGui::PushID(_slot);

    // Miniature (texture chargee par le renderer de l'apercu).
    const float thumb = ImGui::GetFrameHeight() * 2.0f;
    bool srgb = _slot == static_cast<int>(ee::render::MapSlot::Albedo) ||
                _slot == static_cast<int>(ee::render::MapSlot::Emissive);
    unsigned int tex =
        path.empty() ? 0 : m_gl.loadTexture(ee::core::assetPath(path).string(), srgb);
    if (tex != 0)
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(thumb, thumb), ImVec2(0, 1), ImVec2(1, 0));
    else
        ImGui::Dummy(ImVec2(thumb, thumb));
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextUnformatted(kSlotLabels[_slot]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", kSlotHelp[_slot]);
    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
    std::string shown = path.empty() ? "(aucune)" : fs::path(path).filename().string();
    if (ImGui::BeginCombo("##map", shown.c_str()))
    {
        for (const std::string &img : cachedAssets(_project->root(), imageExtensions()))
            if (ImGui::Selectable(img.c_str(), img == path) && img != path)
            {
                path = img;
                changed = true;
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && !path.empty())
        ImGui::SetTooltip("%s", path.c_str());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::BeginDisabled(path.empty());
    if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), 0)))
    {
        path.clear();
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();

    ImGui::PopID();
    return changed;
}

void MaterialPanel::drawEditor(Project *_project)
{
    if (m_selected.empty())
    {
        ImGui::TextDisabled("Selectionne ou cree un materiau");
        return;
    }
    if (!m_exists)
    {
        ImGui::TextDisabled("Fichier introuvable : %s", m_selected.c_str());
        return;
    }

    ImGui::Text("%s", fs::path(m_selected).stem().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_selected.c_str());
    ImGui::Separator();

    // --- Apercu : sphere ou cube, eclairage par defaut, rotation a la souris.
    const int ps = 180;
    {
        ee::render::Camera cam;
        cam.pos = {0.0f, 0.0f, 3.2f};
        cam.yaw = -1.5707963f;
        cam.fovDeg = 40.0f;
        m_gl.setTarget(0);
        m_gl.beginScene(cam, ps, ps);
        m_gl.setLights(ee::render::Lights{});
        ee::render::Material mat = ee::systems::renderMaterial(m_gl, m_edit);
        // Transparent : damier derriere, pour voir a travers.
        if (m_edit.transparent)
            for (int x = -3; x <= 3; ++x)
                for (int y = -3; y <= 3; ++y)
                {
                    float c = ((x + y) & 1) ? 0.85f : 0.25f;
                    m_gl.drawMesh(m_gl.builtin(ee::render::Prim::Cube), {x * 0.5f, y * 0.5f, -1.6f},
                                  {0.5f, 0.5f, 0.05f}, {0.0f, 0.0f, 0.0f},
                                  ee::render::Color{c, c, c});
                }
        if (m_previewShape == 0)
            m_gl.drawMesh(m_gl.builtin(ee::render::Prim::Sphere), {0, 0, 0}, {1, 1, 1},
                          {0.0f, m_previewYaw * 57.29578f, 0.0f}, mat);
        else
            m_gl.drawMesh(m_gl.builtin(ee::render::Prim::Cube), {0, 0, 0}, {1.4f, 1.4f, 1.4f},
                          {20.0f, m_previewYaw * 57.29578f, 0.0f}, mat);
        m_gl.endScene();
        // ImGui::Image n'est pas cliquable : bouton invisible dessous pour le drag.
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##preview", ImVec2((float)ps, (float)ps));
        if (ImGui::IsItemActive())
            m_previewYaw += ImGui::GetIO().MouseDelta.x * 0.01f;
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)m_gl.texture(), p0,
                                             ImVec2(p0.x + ps, p0.y + ps), ImVec2(0, 1),
                                             ImVec2(1, 0));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::RadioButton("Sphere", &m_previewShape, 0);
        ImGui::RadioButton("Cube", &m_previewShape, 1);
        ImGui::TextDisabled("Glisser l'image\npour tourner");
        ImGui::EndGroup();
    }
    ImGui::Separator();

    // --- Valeurs (chacune multiplie sa map si elle en a une).
    bool changed = false;
    ImGui::PushItemWidth(-110.0f);
    changed |= colorEdit("Albedo", m_edit.albedo);
    changed |= ImGui::SliderFloat("Metallic", &m_edit.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Roughness", &m_edit.roughness, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("AO", &m_edit.ao, 0.0f, 1.0f);
    changed |= colorEdit("Emissive", m_edit.emissive);
    changed |= ImGui::DragFloat("Emissive x", &m_edit.emissiveIntensity, 0.05f, 0.0f, 100.0f);
    changed |= ImGui::SliderFloat("Normal force", &m_edit.normalStrength, 0.0f, 3.0f);
    changed |= ImGui::DragFloat2("Tiling", m_edit.tiling, 0.05f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat2("Offset", m_edit.offset, 0.01f, -100.0f, 100.0f);
    changed |= ImGui::Checkbox("Transparent", &m_edit.transparent);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Verre, eau... : dessine apres les objets opaques.\n"
                          "Les reflets restent entiers, seul le diffus s'efface.");
    ImGui::BeginDisabled(!m_edit.transparent);
    changed |= ImGui::SliderFloat("Opacite", &m_edit.opacity, 0.0f, 1.0f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("0 = invisible sauf les reflets, 1 = opaque.\n"
                          "Multipliee par l'alpha de la map Albedo.");
    ImGui::EndDisabled();
    ImGui::PopItemWidth();

    ImGui::SeparatorText("Maps");
    for (int i = 0; i < ee::core::kMaterialMapCount; ++i)
        changed |= drawMapSlot(_project, i);

    if (changed)
    {
        // Apercu en direct partout (scene, jeu) avant meme l'ecriture.
        ee::core::setMaterialInCache(m_selected, m_edit);
        m_dirty = true;
    }
}
