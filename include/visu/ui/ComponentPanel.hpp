#pragma once

#include "visu/ui/Panel.hpp"
#include "visu/ui/CodeDock.hpp"
#include "visu/helpers/BuildProject.hpp"

#include <functional>
#include <string>

class Project;

// Ouvre la fenetre "Create new Component" (nom pre-rempli : _name) : genere
// Components/<Nom>.hpp + RegisterComponents, puis ouvre le .hpp dans VS Code.
// _onStatus recoit "Composant cree." ou l'erreur. Panneau Component et
// inspecteur (Add Component > Creer un nouveau composant).
void openCreateComponent(Project *_project, const std::string &_name,
                         std::function<void(const std::string &)> _onStatus);

class ComponentPanel : public Panel
{
public:
    const char *name() const override { return "Component"; }
    void draw(Project *project) override;
    // Onglet cache : le raccourci Ctrl+G, C et la fin de la generation sont
    // quand meme traites.
    void drawHidden() override;

private:
    void updateCatalog(Project *project, bool _visible);

    std::string m_buildLog;         // sortie du compilo/generateur (pour le popup)
    bool m_openErrorPopup = false;  // demande d'ouverture du popup d'erreur
    std::string m_status;           // ligne de statut du dernier "Generate"
    CodeDock m_code{"ComponentCode"};
    projectbuild::AsyncBuild m_catalogBuild; // generation du catalogue (thread)
};
