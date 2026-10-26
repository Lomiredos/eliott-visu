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
    const char *kSlotLabels[ee::core::kMaterialMapCount] = {
        "Albedo", "Normal", "Metallic", "Roughness", "AO", "Emissive"};
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

void MaterialPanel::draw(Project *project)
{
    if (!project || !project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        return;
    }

    ImGui::BeginChild("##materials", ImVec2(260.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    drawList(project);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##material_edit", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    drawEditor(project);
    ImGui::EndChild();

    // Ecrit le fichier quand on lache le reglage (pas a chaque frame d'un drag).
    if (m_dirty && !ImGui::IsAnyItemActive())
        save(project);
}

void MaterialPanel::drawList(Project *project)
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
        if (fs::exists(project->root() / rel))
            m_status = "\"" + m_newName + "\" existe deja.";
        else if (ee::core::saveMaterialFile(project->root() / rel, ee::core::MaterialAsset{}))
        {
            m_status = "Materiau cree.";
            m_newName.clear();
            select(project, rel);
        }
        else
            m_status = "Ecriture impossible.";
    }
    if (!m_newName.empty() && !valid)
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Nom : lettres, chiffres, _");
    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::Separator();

    std::vector<std::string> mats = listAssets(project->root(), {".mat"});
    if (mats.empty())
        ImGui::TextDisabled("Aucun materiau (Assets/**/*.mat)");
    for (const std::string &m : mats)
    {
        std::string label = fs::path(m).stem().string() + "##" + m;
        if (ImGui::Selectable(label.c_str(), m == m_selected))
            select(project, m);
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
                fs::path file = project->root() / m;
                std::string rel = m;
                App::getInstance().openModal(std::make_unique<ConfirmModal>(
                    "Supprimer le materiau ?",
                    "Supprimer " + rel + " ?\nLes entites qui l'utilisent repasseront en couleur unie.",
                    std::vector<ModalChoice>{
                        {"Supprimer",
                         [this, file, rel]()
                         {
                             std::error_code ec;
                             fs::remove(file, ec);
                             if (m_selected == rel)
                                 m_selected.clear();
                             m_status = ec ? "Suppression impossible." : "Materiau supprime.";
                         }},
                        {"Annuler", nullptr}}));
            }
            ImGui::EndPopup();
        }
    }
}

void MaterialPanel::select(Project *project, const std::string &_path)
{
    if (m_dirty)
        save(project);
    m_selected = _path;
    auto loaded = ee::core::loadMaterialFile(project->root() / _path);
    m_edit = loaded ? *loaded : ee::core::MaterialAsset{};
    m_dirty = false;
}

void MaterialPanel::save(Project *project)
{
    m_dirty = false;
    if (m_selected.empty())
        return;
    if (!ee::core::saveMaterialFile(project->root() / m_selected, m_edit))
        m_status = "Ecriture impossible : " + m_selected;
    ee::core::setMaterialInCache(m_selected, m_edit);
}

bool MaterialPanel::drawMapSlot(Project *project, int _slot)
{
    std::string &path = m_edit.maps[_slot];
    bool changed = false;
    ImGui::PushID(_slot);

    // Miniature (texture chargee par le renderer de l'apercu).
    const float thumb = ImGui::GetFrameHeight() * 2.0f;
    bool srgb = _slot == static_cast<int>(ee::render::MapSlot::Albedo) ||
                _slot == static_cast<int>(ee::render::MapSlot::Emissive);
    unsigned int tex = path.empty() ? 0 : m_gl.loadTexture(ee::core::assetPath(path).string(), srgb);
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
        for (const std::string &img : listAssets(project->root(), imageExtensions()))
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

void MaterialPanel::drawEditor(Project *project)
{
    if (m_selected.empty())
    {
        ImGui::TextDisabled("Selectionne ou cree un materiau");
        return;
    }
    if (!fs::exists(project->root() / m_selected))
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
        if (m_previewShape == 0)
            m_gl.drawMesh(m_gl.builtin(ee::render::Prim::Sphere), {0, 0, 0}, {1, 1, 1},
                          {0.0f, m_previewYaw * 57.29578f, 0.0f}, mat);
        else
            m_gl.drawMesh(m_gl.builtin(ee::render::Prim::Cube), {0, 0, 0}, {1.4f, 1.4f, 1.4f},
                          {20.0f, m_previewYaw * 57.29578f, 0.0f}, mat);
        m_gl.endScene();
        ImGui::Image((ImTextureID)(intptr_t)m_gl.texture(), ImVec2((float)ps, (float)ps),
                     ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
            m_previewYaw += ImGui::GetIO().MouseDelta.x * 0.01f;
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
    ImGui::PopItemWidth();

    ImGui::SeparatorText("Maps");
    for (int i = 0; i < ee::core::kMaterialMapCount; ++i)
        changed |= drawMapSlot(project, i);

    if (changed)
    {
        // Apercu en direct partout (scene, jeu) avant meme l'ecriture.
        ee::core::setMaterialInCache(m_selected, m_edit);
        m_dirty = true;
    }
}
