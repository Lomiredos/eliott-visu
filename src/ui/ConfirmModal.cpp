#include "visu/ui/ConfirmModal.hpp"

#include <imgui.h>

ConfirmModal::ConfirmModal(std::string _title, std::string _text,
                           std::vector<ModalChoice> _choices)
    : m_title(std::move(_title)), m_text(std::move(_text)),
      m_choices(std::move(_choices))
{
}

bool ConfirmModal::Draw()
{
    bool finished = false;

    if (ImGui::BeginPopupModal(Id(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (!m_text.empty())
        {
            ImGui::TextUnformatted(m_text.c_str());
            ImGui::Spacing();
        }

        for (size_t i = 0; i < m_choices.size(); ++i)
        {
            if (i > 0)
                ImGui::SameLine();
            if (ImGui::Button(m_choices[i].label.c_str()))
            {
                if (m_choices[i].onClick)
                    m_choices[i].onClick();
                finished = true;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    return finished;
}
