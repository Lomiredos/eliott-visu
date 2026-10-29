#include "visu/ui/SystemPanel.hpp"

#include "visu/core/Project.hpp"
#include "visu/App.hpp"
#include "visu/ui/ShowCodePanel.hpp"
#include "visu/ui/ConfirmModal.hpp"
#include "visu/ui/DeletionSummary.hpp"

#include "visu/ui/CreateSystemModal.hpp"

#include "imgui.h"
#include "visu/helpers/OpenExternal.hpp"
#include "visu/helpers/SystemGen.hpp"
#include "visu/helpers/ComponentCatalog.hpp"

namespace fs = std::filesystem;

void SystemPanel::askDelete(const Project &_project, const std::string &_name)
{
    const systemgen::DeletePlan plan = systemgen::planDeleteSystem(_project, _name);

    DeletionSummary summary(_project.root());
    summary.section("Fichiers supprimes :", plan.files);
    summary.line("Registre des systemes (RegisterSystems) regenere.");
    summary.line("");
    summary.section("A corriger a la main (le code mentionne encore " + _name + ") :",
                    plan.codeRefs);
    summary.line("Cette action est definitive.");

    const fs::path dir = _project.systemsDir();
    App::getInstance().openModal(std::make_unique<ConfirmModal>(
        "Supprimer le systeme " + _name + " ?", summary.text(),
        std::vector<ModalChoice>{{"Supprimer",
                                  [this, dir, plan]()
                                  {
                                      std::string err = systemgen::deleteSystem(dir, plan.name);
                                      m_status =
                                          err.empty() ? "Systeme " + plan.name + " supprime." : err;
                                      if (err.empty() && !plan.codeRefs.empty())
                                          m_status += " Du code le mentionne encore.";
                                      m_projectTree.refresh();
                                      ShowCodePanel::closeShowing(plan.files);
                                  }},
                                 {"Annuler", nullptr}}));
}

void SystemPanel::draw(Project *_project)
{
    if (!_project || !_project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        m_code.keepAlive();
        return;
    }

    fs::path dir = _project->systemsDir();
    fs::path clicked;

    // Colonne de gauche (redimensionnable) : actions + arbres de systemes.
    ImGui::BeginChild("##systems", ImVec2(280.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    // Toujours propose : createSystem cree Systems/ s'il manque.
    if (ImGui::Button("Create new System"))
    {
        // Liste des composants = union moteur + projet, meme source que
        // l'inspecteur (componentCatalog).
        std::vector<std::string> comps;
        for (const auto &kv : componentCatalog(_project->componentsCatalog()).defaults)
            comps.push_back(kv.first);

        App &app = App::getInstance();
        // Genere le trio (.json/.hpp/.cpp) dans <projet>/Systems/, rebranche
        // le registre, puis ouvre le stub .cpp dans VS Code.
        app.openModal(std::make_unique<CreateSystemModal>(
            std::move(comps),
            [dir, root = _project->root(), this](const SystemInfoCreation &_data)
            {
                systemgen::CreateResult r = systemgen::createSystem(dir, _data);
                if (r.error.empty())
                {
                    m_status = "Systeme cree.";
                    m_projectTree.refresh();
                    openInVSCode(r.path, root);
                }
                else
                    m_status = r.error;
            }));
    }

    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());

    if (fs::exists(dir))
    {
        ImGui::Separator();

        // Ouvre le .cpp (ou le .hpp si header-only) dans VS Code au double-click.
        auto openCode = [root = _project->root()](const fs::path &_p)
        {
            fs::path cpp = _p;
            cpp.replace_extension(".cpp");
            openInVSCode(fs::exists(cpp) ? cpp : _p, root);
        };

        // Systemes du PROJET (on masque le fichier de registration).
        // Clic droit : suppression, seulement pour un systeme gere par
        // l'editeur (trio a la racine de Systems/, avec son .json) : c'est
        // le seul cas que deleteSystem et le registre savent traiter.
        ImGui::TextDisabled("Projet");
        clicked = m_projectTree.draw(
            dir, openCode,
            [this, dir, _project](const fs::path &_p)
            {
                const std::string name = _p.stem().string();
                const bool managed = _p.parent_path() == dir && fs::exists(dir / (name + ".json"));
                const bool busy = App::getInstance().isBusy();
                if (ImGui::MenuItem("Supprimer", nullptr, false, managed && !busy))
                    askDelete(*_project, name);
                if (busy)
                    ImGui::SetItemTooltip("Impossible pendant le Play ou un build.");
                else if (!managed)
                    ImGui::SetItemTooltip("Systeme hors registre (sous-dossier ou sans .json) : a "
                                          "supprimer a la main.");
            });

        // Systemes MOTEUR (submodule du projet) -- lecture seule.
        fs::path engineDir = _project->engineIncludeDir() / "systems";
        ImGui::TextDisabled("Moteur");
        fs::path clickedEngine = m_engineTree.draw(engineDir, openCode);
        if (clicked.empty())
            clicked = clickedEngine;
    }
    else
        ImGui::TextDisabled("(aucun systeme -- cree-en un)");

    ImGui::EndChild();

    // Colonne de droite : lecteur de code integre au panel.
    ImGui::SameLine();
    m_code.draw();

    if (!clicked.empty())
        m_code.open(clicked);
}
