#include "visu/ui/ComponentPanel.hpp"

#include "visu/core/Project.hpp"

#include "imgui.h"
#include <imgui_stdlib.h> // ImGui::InputTextMultiline(const char*, std::string*)
#include "visu/helpers/OpenExternal.hpp"

#include "visu/App.hpp"
#include "visu/ui/ConfirmModal.hpp"
#include "visu/ui/DeletionSummary.hpp"
#include "visu/ui/ShowCodePanel.hpp"

#include "visu/ui/CreateComponentModal.hpp"
#include "visu/helpers/ComponentGen.hpp"
#include "visu/helpers/GenerateCatalog.hpp"
#include "visu/ui/BuildProgress.hpp"
#include "visu/helpers/ComponentCatalog.hpp"

#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Composants proposes comme "requis" d'un champ entity : catalogue (moteur +
// projet) + composants du projet pas encore passes dans le catalogue.
static std::vector<std::string> componentNames(Project *_project)
{
    std::set<std::string> names;
    for (const auto &kv : componentCatalog(_project->componentsCatalog()).defaults)
        names.insert(kv.first);
    for (const std::string &n : componentgen::projectComponentNames(_project->componentsDir()))
        names.insert(n);
    return {names.begin(), names.end()};
}

void openCreateComponent(Project *_project, const std::string &_name,
                         const std::function<void(const std::string &)> &_onStatus)
{
    fs::path dir = _project->componentsDir();
    fs::path root = _project->root();
    App::getInstance().openModal(std::make_unique<CreateComponentModal>(
        [dir, root, _onStatus](const ComponentInfoCreation &_data)
        {
            componentgen::CreateResult r = componentgen::createComponent(dir, _data);
            if (r.error.empty())
            {
                _onStatus("Composant cree.");
                openInVSCode(r.path, root);
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
void ComponentPanel::updateCatalog(Project *_project, bool _visible)
{
    if (App::getInstance().consumeCatalogRequest() &&
        !catalog::start(m_catalogBuild, _project->root()))
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

void ComponentPanel::askDelete(const Project &_project, const std::string &_name)
{
    const componentgen::DeletePlan plan = componentgen::planDeleteComponent(_project, _name);

    DeletionSummary summary(_project.root());
    summary.section("Fichiers supprimes :", plan.files);
    summary.line("Registres des composants regeneres.");
    if (plan.inCatalog)
        summary.line("Retire du catalogue (Assets/Components.json).");
    summary.line("");
    summary.section("Retire de " + std::to_string(plan.instances) + " entite(s), dans les scenes :",
                    plan.scenes);
    summary.section("Retire de la signature des systemes :", plan.systems);
    summary.section("A corriger a la main (le code mentionne encore " + _name + ") :",
                    plan.codeRefs);
    summary.line("Cette action est definitive.");

    const Project project = _project; // la modale peut survivre au pointeur du panneau
    App::getInstance().openModal(std::make_unique<ConfirmModal>(
        "Supprimer le composant " + _name + " ?", summary.text(),
        std::vector<ModalChoice>{{"Supprimer",
                                  [this, project, plan]()
                                  {
                                      std::string err =
                                          componentgen::deleteComponent(project, plan);
                                      App::getInstance().purgeComponent(plan.name);
                                      m_status = err.empty()
                                                     ? "Composant " + plan.name + " supprime."
                                                     : err;
                                      if (err.empty() && !plan.codeRefs.empty())
                                          m_status += " Du code le mentionne encore.";
                                      m_projectTree.refresh();
                                      ShowCodePanel::closeShowing(plan.files);
                                  }},
                                 {"Annuler", nullptr}}));
}

void ComponentPanel::draw(Project *_project)
{
    if (!_project || !_project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        m_code.keepAlive();
        return;
    }

    fs::path dir = _project->componentsDir();

    // Colonne de gauche (redimensionnable) : actions + arbres de composants.
    ImGui::BeginChild("##components", ImVec2(280.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    if (ImGui::Button("Create new Component"))
        openCreateComponent(_project, "",
                            [this](const std::string &_s)
                            {
                                m_status = _s;
                                m_projectTree.refresh();
                            });

    ImGui::SameLine();

    // Generation par VRAIE compilation : compile gen_components (test de
    // validite des structs), puis ecrit Components.json ; sinon popup d'erreur.
    // Dans un thread, avec barre de progression ; grise pendant tout build
    // (Build/Play du panneau Main compris : un seul cmake a la fois).
    updateCatalog(_project, true);
    const bool building = projectbuild::AsyncBuild::anyRunning();
    ImGui::BeginDisabled(building);
    if (ImGui::Button("Generate catalog (build)"))
        catalog::start(m_catalogBuild, _project->root());
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

    auto openHpp = [root = _project->root()](const fs::path &_p) { openInVSCode(_p, root); };

    // Composants du PROJET (on masque les fichiers de registration).
    ImGui::TextDisabled("Projet");
    fs::path clicked;
    // Clic droit : suppression, seulement pour un composant (header avec
    // Reflect<Nom>) a la racine de Components/, comme le registre.
    auto contextMenu = [this, dir, _project](const fs::path &_p)
    {
        const bool component = _p.parent_path() == dir && componentgen::isComponentHeader(_p);
        const bool busy = App::getInstance().isBusy();
        if (ImGui::MenuItem("Supprimer", nullptr, false, component && !busy))
            askDelete(*_project, _p.stem().string());
        if (busy)
            ImGui::SetItemTooltip("Impossible pendant le Play ou un build.");
        else if (!component)
            ImGui::SetItemTooltip("Pas un composant du registre (sous-dossier ou sans "
                                  "Reflect<Nom>) : a supprimer a la main.");
    };
    if (fs::exists(dir))
        clicked = m_projectTree.draw(dir, openHpp, contextMenu);
    else
        ImGui::TextDisabled("(aucun composant -- cree-en un)");

    // Composants MOTEUR (submodule du projet) -- lecture seule.
    fs::path engineDir = _project->engineIncludeDir() / "components";
    ImGui::TextDisabled("Moteur");
    fs::path clickedEngine = m_engineTree.draw(engineDir, openHpp);
    if (clicked.empty())
        clicked = clickedEngine;

    ImGui::EndChild();

    // Colonne de droite : lecteur de code integre au panel.
    ImGui::SameLine();
    m_code.draw();

    if (!clicked.empty())
        m_code.open(clicked);
}
