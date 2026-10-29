#include "visu/ui/EngineSettingsPanel.hpp"

#include "visu/core/Project.hpp"

#include "imgui.h"

namespace
{
    // Choix parmi des puissances de 2 (resolutions de shadow map).
    bool resolutionCombo(const char *_label, int &_value, int _min, int _max)
    {
        bool changed = false;
        std::string shown = std::to_string(_value) + " px";
        if (ImGui::BeginCombo(_label, shown.c_str()))
        {
            for (int r = _min; r <= _max; r *= 2)
            {
                std::string item = std::to_string(r) + " px";
                if (ImGui::Selectable(item.c_str(), r == _value) && r != _value)
                {
                    _value = r;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void help(const char *_text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", _text);
    }
}

void EngineSettingsPanel::draw(Project *project)
{
    if (!project || !project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        return;
    }

    // Hors edition : on suit le fichier (modifie ailleurs, autre projet...).
    if (!m_dirty && !ImGui::IsAnyItemActive())
        m_edit = ee::core::engineSettings();

    ee::render::RenderSettings &r = m_edit.render;
    bool changed = false;
    ImGui::PushItemWidth(180.0f);

    ImGui::SeparatorText("Lumieres");
    changed |= ImGui::SliderInt("Lampes max", &r.maxPointLights, 1, 64);
    help("Lampes ponctuelles prises en compte par image : les plus proches\n"
         "de la camera. Chacune coute un calcul par pixel.");

    ImGui::SeparatorText("Ombres du soleil");
    changed |= ImGui::Checkbox("Activer##sun", &r.sunShadows);
    ImGui::BeginDisabled(!r.sunShadows);
    changed |= resolutionCombo("Resolution##sun", r.sunShadowResolution, 512, 8192);
    help("Plus haut = ombres plus nettes, plus de memoire video.");
    changed |= ImGui::DragFloat("Distance", &r.sunShadowDistance, 0.5f, 1.0f, 1000.0f, "%.0f m");
    help("Zone couverte devant la camera. Plus elle est grande, plus les\n"
         "ombres sont floues (meme resolution etalee sur plus de place).");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Ombres des lampes");
    changed |= ImGui::Checkbox("Activer##point", &r.pointShadows);
    ImGui::BeginDisabled(!r.pointShadows);
    changed |= ImGui::SliderInt("Lampes ombrees max", &r.maxShadowedPointLights, 0, 16);
    help("Les plus proches de la camera parmi celles qui ont \"shadows\".\n"
         "Chacune redessine la scene 6 fois : c'est le reglage le plus cher.");
    changed |= resolutionCombo("Resolution##point", r.pointShadowResolution, 128, 2048);
    help("Par face du cube (6 faces par lampe).");
    ImGui::EndDisabled();
    ImGui::PopItemWidth();

    ImGui::Spacing();
    if (ImGui::Button("Valeurs par defaut"))
    {
        m_edit = ee::core::EngineSettings{};
        changed = true;
    }
    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::TextDisabled("%s", ee::core::kEngineSettingsPath);

    if (changed)
    {
        // Apercu en direct (editeur) avant meme l'ecriture.
        ee::core::setEngineSettingsInCache(m_edit);
        m_dirty = true;
    }
    // Ecrit le fichier quand on lache le reglage (pas a chaque frame d'un drag).
    if (m_dirty && !ImGui::IsAnyItemActive())
        save(project);
}

void EngineSettingsPanel::save(Project *project)
{
    m_dirty = false;
    if (ee::core::saveEngineSettingsFile(project->root() / ee::core::kEngineSettingsPath, m_edit))
        m_status.clear();
    else
        m_status = "Ecriture impossible.";
    ee::core::setEngineSettingsInCache(m_edit); // date du fichier ecrit
}
