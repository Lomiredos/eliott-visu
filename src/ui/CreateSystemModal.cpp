#include "visu/ui/CreateSystemModal.hpp"

#include "visu/helpers/NameValidation.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

CreateSystemModal::CreateSystemModal(
    std::vector<std::string> _components,
    std::function<void(SystemInfoCreation)> _cb) {
  // La liste vient du CALLER (source unique : union moteur + projet). La modal
  // reste "dumb" : elle ne scanne aucun catalogue elle-meme.
  m_componentsNames = std::move(_components);
  m_valide.assign(m_componentsNames.size(), false);
  m_onClose = _cb;
}

SystemInfoCreation CreateSystemModal::buildResult() {

  std::vector<std::string> noms;
  for (size_t i = 0; i < m_componentsNames.size(); i++)
    if (m_valide[i])
      noms.push_back(m_componentsNames[i]);

  return SystemInfoCreation{m_sysName, noms};
}

bool CreateSystemModal::Draw() {
  bool finished = false;

  if (ImGui::BeginPopupModal(Id(), NULL, 0)) {
    ImGui::InputTextWithHint("System Name", "Your name...", &m_sysName);
    ImGui::Separator();
    ImGui::Text("Required Components:");
    for (size_t i = 0; i < m_componentsNames.size(); i++) {
      bool coche = m_valide[i];
      if (ImGui::Checkbox(m_componentsNames[i].c_str(), &coche))
        m_valide[i] = coche;
    }
    ImGui::Separator();
    bool isnameValide = namevalidation::isValidIdentifier(m_sysName);
    if (!m_sysName.empty() && !isnameValide)
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                         "Nom invalide (identifiant C++ : lettres/chiffres/_, "
                         "pas de chiffre en tete).");
    if (!isnameValide)
      ImGui::BeginDisabled();
    if (ImGui::Button("Créer")) {
      m_onClose(buildResult());
      finished = true;
      ImGui::CloseCurrentPopup();
    }
    if (!isnameValide)
      ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Annuler")) {
      ImGui::CloseCurrentPopup();
      finished = true;
    }
    ImGui::EndPopup();
  }

  return finished;
}
