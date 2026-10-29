#pragma once

#include "visu/ui/Panel.hpp"
#include "visu/ui/CodeDock.hpp"
#include "visu/ui/FolderTree.hpp"

#include <filesystem>
#include <string>

class SystemPanel : public Panel
{

public:
    const char *name() const override { return "System"; }
    void draw(Project *_project) override;
    void drawHidden() override { m_code.keepAlive(); }

private:
    // Demande confirmation (avec la liste de ce qui sera touche) puis
    // supprime le systeme : fichiers + registre.
    void askDelete(const Project &_project, const std::string &_name);

    std::string m_status; // resultat de la derniere creation/suppression de systeme
    CodeDock m_code{"SystemCode"};
    FolderTree m_projectTree{{".hpp"}, {"RegisterSystems"}};
    // Systemes internes du moteur (pas des systemes de jeu) : masques.
    FolderTree m_engineTree{{".hpp"}, {"SystemScheduler", "RenderSystem", "PickingSystem"}};
};
