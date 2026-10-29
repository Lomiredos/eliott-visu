#include "visu/ui/CreateSceneModal.hpp"

#include "visu/helpers/NameValidation.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

CreateSceneModal::CreateSceneModal(std::function<void(SceneInfoCreation)> _cb)
{
    m_onClose = _cb;
}

bool CreateSceneModal::Draw()
{
    bool finished = false;

    if (ImGui::BeginPopupModal(Id(), NULL, 0))
    {
        ImGui::InputTextWithHint("Scene Name", "Your name...", &m_name);

        ImGui::Separator();
        bool nameValid = namevalidation::isValidIdentifier(m_name);
        if (!m_name.empty() && !nameValid)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "Nom invalide (identifiant C++ : lettres/chiffres/_, pas de chiffre en tete).");
        if (!nameValid)
            ImGui::BeginDisabled();
        if (ImGui::Button("Créer"))
        {
            m_onClose({m_name});
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
