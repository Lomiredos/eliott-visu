#include "visu/ui/CreateComponentModal.hpp"

#include "visu/helpers/NameValidation.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <utility>

CreateComponentModal::CreateComponentModal(std::function<void(const ComponentInfoCreation &)> _cb,
                                           std::vector<std::string> _componentNames,
                                           std::string _name)
    : m_name(std::move(_name)), m_componentNames(std::move(_componentNames))
{
    m_onClose = std::move(_cb);
    m_fields.push_back({"", "float"}); // un champ vide par defaut
}

ComponentInfoCreation CreateComponentModal::buildResult()
{
    ComponentInfoCreation res;
    res.name = m_name;
    for (const auto &f : m_fields)
        if (!f.name.empty())
            res.fields.push_back(f);
    return res;
}

bool CreateComponentModal::draw()
{
    bool finished = false;

    if (ImGui::BeginPopupModal(id(), nullptr, 0))
    {
        ImGui::InputTextWithHint("Component Name", "Your name...", &m_name);
        ImGui::Separator();
        ImGui::TextUnformatted("Champs :");

        // entity = reference vers une autre entite (glisser-deposer dans l'inspecteur)
        static const char *kTypes[] = {"float", "int", "bool", "string", "entity"};
        constexpr int kTypeCount = 5;

        int toRemove = -1;
        for (size_t i = 0; i < m_fields.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##fname", "champ...", &m_fields[i].name);
            ImGui::SameLine();

            int typeIdx = 0;
            for (int t = 0; t < kTypeCount; ++t)
                if (m_fields[i].type == kTypes[t])
                {
                    typeIdx = t;
                    break;
                }
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::Combo("##ftype", &typeIdx, kTypes, kTypeCount))
                m_fields[i].type = kTypes[typeIdx];

            if (m_fields[i].type == "entity")
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(170.0f);
                const std::string &req = m_fields[i].required;
                if (ImGui::BeginCombo("##freq", req.empty() ? "(toute entite)" : req.c_str()))
                {
                    if (ImGui::Selectable("(toute entite)", req.empty()))
                        m_fields[i].required.clear();
                    for (const std::string &n : m_componentNames)
                        if (ImGui::Selectable(n.c_str(), req == n))
                            m_fields[i].required = n;
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Composant que l'entite glissee doit avoir");
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                toRemove = static_cast<int>(i);

            ImGui::PopID();
        }
        if (toRemove >= 0)
            m_fields.erase(m_fields.begin() + toRemove);

        if (ImGui::Button("+ Ajouter un champ"))
            m_fields.push_back({"", "float"});

        ImGui::Separator();
        bool nameValid = namevalidation::isValidIdentifier(m_name);
        if (!m_name.empty() && !nameValid)
            ImGui::TextColored(
                ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Nom invalide (identifiant C++ : lettres/chiffres/_, pas de chiffre en tete).");
        if (!nameValid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Créer"))
        {
            m_onClose(buildResult());
            finished = true;
            ImGui::CloseCurrentPopup();
        }
        if (!nameValid)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Annuler"))
        {
            ImGui::CloseCurrentPopup();
            finished = true;
        }

        ImGui::EndPopup();
    }

    return finished;
}
