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
#include "visu/ui/BuildProgress.hpp"
#include "visu/helpers/ComponentGetter.hpp"

#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Composants proposes comme "requis" d'un champ entity : catalogue (moteur +
// projet) + composants du projet pas encore passes dans le catalogue.
static std::vector<std::string> componentNames(Project *_project)
{
    std::set<std::string> names;
    for (const auto &kv : getAllComponentDefaults(_project->componentsCatalog()))
        names.insert(kv.first);
    for (const std::string &n : componentgen::projectComponentNames(_project->componentsDir()))
        names.insert(n);
    return {names.begin(), names.end()};
}

void openCreateComponent(Project *_project, const std::string &_name,
                         std::function<void(const std::string &)> _onStatus)
{
    fs::path dir = _project->componentsDir();
    App::getInstance().openModal(std::make_unique<CreateComponentModal>(
        [dir, _onStatus](ComponentInfoCreation _data)
        {
            componentgen::CreateResult r = componentgen::createComponent(dir, _data);
            if (r.error.empty())
            {
                _onStatus("Composant cree.");
                openInVSCode(r.path);
            }
            else
                _onStatus(r.error);
        },
        componentNames(_project), _name));
}

void ComponentPanel::drawHidden()
{
    m_code.keepAlive();
    Project *project = App::getInstance().project();
    if (project && project->isValid())
        updateCatalog(project, false);
}

// Raccourci Ctrl+G, C + fin de la generation (resultat, popup d'erreur).
void ComponentPanel::updateCatalog(Project *project, bool _visible)
{
    if (App::getInstance().consumeCatalogRequest() &&
        !catalog::start(m_catalogBuild, project->root()))
        m_status = "Un build est deja en cours.";

    projectbuild::BuildResult r;
    if (!m_catalogBuild.finished(r))
        return;
    if (r.ok)
    {
        m_status = "Catalogue genere.";
        return;
    }
    m_status = "Echec de la compilation.";
    m_buildLog = r.log;
    m_openErrorPopup = true;
    // Onglet cache : on le met devant, sinon le popup d'erreur reste invisible.
    if (!_visible)
        ImGui::SetWindowFocus(name());
}

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
        openCreateComponent(project, "", [this](const std::string &_s) { m_status = _s; });

    ImGui::SameLine();

    // Generation par VRAIE compilation : compile gen_components (test de
    // validite des structs), puis ecrit Components.json ; sinon popup d'erreur.
    // Dans un thread, avec barre de progression ; grise pendant tout build
    // (Build/Play du panneau Main compris : un seul cmake a la fois).
    updateCatalog(project, true);
    const bool building = projectbuild::AsyncBuild::anyRunning();
    ImGui::BeginDisabled(building);
    if (ImGui::Button("Generate catalog (build)"))
        catalog::start(m_catalogBuild, project->root());
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Ctrl+G, C");
    if (m_catalogBuild.running())
        drawBuildProgress(m_catalogBuild, "Catalogue");
    else if (building)
        ImGui::TextDisabled("Build du jeu en cours...");
    else if (!m_status.empty())
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
        // Tous les .hpp du projet (composants ou non) restent visibles ;
        // seuls les registres generes sont masques.
        clicked = drawFolderTree(dir, openHpp, {".hpp"},
                                 {"RegisterComponents", "RegisterWorldComponents"});
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
