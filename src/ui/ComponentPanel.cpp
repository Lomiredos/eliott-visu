#include "visu/ui/ComponentPanel.hpp"

#include "visu/core/Project.hpp"
#include "visu/ui/FolderTree.hpp"

#include "imgui.h"
#include <imgui_stdlib.h> // ImGui::InputTextMultiline(const char*, std::string*)
#include "visu/helpers/OpenExternal.hpp"

#include "visu/App.hpp"

#include "visu/ui/CreateComponentModal.hpp"
#include "visu/helpers/ComponentGen.hpp"
#include "visu/helpers/GenerateCatalog.hpp"

namespace fs = std::filesystem;

void ComponentPanel::draw(Project *project)
{
    if (!project || !project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        m_code.keepAlive();
        return;
    }

    fs::path dir = project->componentsDir();

    // Colonne de gauche (redimensionnable) : actions + arbres de composants.
    ImGui::BeginChild("##components", ImVec2(280.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    if (ImGui::Button("Create new Component"))
    {
        App &app = App::getInstance();
        // Genere <projet>/Components/<Nom>.hpp (struct + Reflect), regenere
        // RegisterComponents, puis ouvre le .hpp dans VS Code.
        app.openModal(std::make_unique<CreateComponentModal>([dir, this](ComponentInfoCreation _data)
                                                             {
                                                                 componentgen::CreateResult r = componentgen::createComponent(dir, _data);
                                                                 if (r.error.empty())
                                                                 {
                                                                     m_status = "Composant cree.";
                                                                     openInVSCode(r.path);
                                                                 }
                                                                 else
                                                                     m_status = r.error;
                                                             }));
    }

    ImGui::SameLine();

    // Generation par VRAIE compilation : compile gen_components (test de
    // validite des structs), puis ecrit Components.json ; sinon popup d'erreur.
    if (ImGui::Button("Generate catalog (build)"))
    {
        catalog::GenResult r = catalog::generate(project->root());
        if (r.ok)
            m_status = "Catalogue genere.";
        else
        {
            m_status = "Echec de la compilation.";
            m_buildLog = r.log;
            m_openErrorPopup = true;
        }
    }
    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());

    // Popup d'erreur (bloquant) : montre la sortie du compilo.
    if (m_openErrorPopup)
    {
        ImGui::OpenPopup("Erreur de compilation");
        m_openErrorPopup = false;
    }
    if (ImGui::BeginPopupModal("Erreur de compilation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("La generation a echoue. Sortie :");
        // Champ lecture seule : curseur, selection et Ctrl+C possibles, sans edition.
        ImGui::InputTextMultiline("##log", &m_buildLog, ImVec2(720, 320),
                                  ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Fermer"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();

    auto openHpp = [](const fs::path &p)
    { openInVSCode(p); };

    // Composants du PROJET (on masque les fichiers de registration).
    ImGui::TextDisabled("Projet");
    fs::path clicked;
    if (fs::exists(dir))
        clicked = drawFolderTree(dir, openHpp, {".hpp"},
                                 {"RegisterComponents", "RegisterWorldComponents", "Components"});
    else
        ImGui::TextDisabled("(aucun composant -- cree-en un)");

    // Composants MOTEUR (ee-core, submodule) -- lecture seule. On masque
    // l'agregateur Components.hpp (ce n'est pas un composant).
    fs::path engineDir = project->engineIncludeDir() / "components";
    ImGui::TextDisabled("Moteur");
    fs::path clickedEngine = drawFolderTree(engineDir, openHpp, {".hpp"}, {"Components"});
    if (clicked.empty())
        clicked = clickedEngine;

    ImGui::EndChild();

    // Colonne de droite : lecteur de code integre au panel.
    ImGui::SameLine();
    m_code.draw();

    if (!clicked.empty())
        m_code.open(clicked);
}
