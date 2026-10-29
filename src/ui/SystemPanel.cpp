#include "visu/ui/SystemPanel.hpp"

#include "visu/core/Project.hpp"
#include "visu/ui/FolderTree.hpp"
#include "visu/App.hpp"
#include "visu/ui/ShowCodePanel.hpp"
#include "visu/ui/ConfirmModal.hpp"

#include "visu/ui/CreateSystemModal.hpp"

#include "imgui.h"
#include "visu/helpers/OpenExternal.hpp"
#include "visu/helpers/SystemGen.hpp"
#include "visu/helpers/ComponentGetter.hpp"

namespace fs = std::filesystem;

void SystemPanel::askDelete(const fs::path &_dir, const std::string &_name)
{
    App::getInstance().openModal(std::make_unique<ConfirmModal>(
        "Supprimer le systeme ?",
        "Supprimer " + _name + " (.json, .hpp, .cpp) ? Cette action est definitive.",
        std::vector<ModalChoice>{
            {"Supprimer",
             [this, _dir, _name]()
             {
                 std::string err = systemgen::deleteSystem(_dir, _name);
                 m_status = err.empty() ? "Systeme " + _name + " supprime." : err;

                 // Ferme les lecteurs qui montraient un fichier de ce systeme.
                 for (ShowCodePanel *p : App::getInstance().getPanels<ShowCodePanel>())
                     if (p->path().parent_path() == _dir && p->path().stem() == _name)
                         p->visible = false;
             }},
            {"Annuler", nullptr}}));
}

void SystemPanel::draw(Project *project)
{
    if (!project || !project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        m_code.keepAlive();
        return;
    }

    fs::path dir = project->systemsDir();
    fs::path clicked;

    // Colonne de gauche (redimensionnable) : actions + arbres de systemes.
    ImGui::BeginChild("##systems", ImVec2(280.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    // Toujours propose : createSystem cree Systems/ s'il manque.
    if (ImGui::Button("Create new System"))
    {
        // Liste des composants = union moteur + projet, MEME source que
        // l'inspecteur (getAllComponentDefaults). Fini la divergence.
        std::vector<std::string> comps;
        for (const auto &kv : getAllComponentDefaults(project->componentsCatalog()))
            comps.push_back(kv.first);

        App &app = App::getInstance();
        // Genere le trio (.json/.hpp/.cpp) dans <projet>/Systems/, rebranche
        // le registre, puis ouvre le stub .cpp dans VS Code.
        app.openModal(std::make_unique<CreateSystemModal>(std::move(comps),
                                                          [dir, this](SystemInfoCreation _data)
                                                          {
                                                              systemgen::CreateResult r = systemgen::createSystem(dir, _data);
                                                              if (r.error.empty())
                                                              {
                                                                  m_status = "Systeme cree.";
                                                                  openInVSCode(r.path);
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
        auto openCode = [](const fs::path &p)
        {
            fs::path cpp = p;
            cpp.replace_extension(".cpp");
            openInVSCode(fs::exists(cpp) ? cpp : p);
        };

        // Systemes du PROJET (on masque le fichier de registration).
        // Clic droit : suppression, seulement pour un systeme gere par
        // l'editeur (trio a la racine de Systems/, avec son .json) : c'est
        // le seul cas que deleteSystem et le registre savent traiter.
        ImGui::TextDisabled("Projet");
        clicked = drawFolderTree(dir, openCode, {".hpp"}, {"RegisterSystems"},
                                 [this, dir](const fs::path &p)
                                 {
                                     const std::string name = p.stem().string();
                                     const bool managed = p.parent_path() == dir &&
                                                          fs::exists(dir / (name + ".json"));
                                     if (ImGui::MenuItem("Supprimer", nullptr, false, managed))
                                         askDelete(dir, name);
                                     if (!managed)
                                         ImGui::SetItemTooltip("Systeme hors registre (sous-dossier ou sans .json) : a supprimer a la main.");
                                 });

        // Systemes MOTEUR (ee-core, submodule) -- lecture seule.
        fs::path engineDir = project->engineIncludeDir() / "systems";
        ImGui::TextDisabled("Moteur");
        fs::path clickedEngine = drawFolderTree(engineDir, openCode, {".hpp"},
                                                {"SystemScheduler", "RenderSystem", "PickingSystem"});
        if (clicked.empty())
            clicked = clickedEngine;
    }
    else
        ImGui::Text("Dossier introuvable : %s", dir.string().c_str());

    ImGui::EndChild();

    // Colonne de droite : lecteur de code integre au panel.
    ImGui::SameLine();
    m_code.draw();

    if (!clicked.empty())
        m_code.open(clicked);
}
