#pragma once

#include "visu/ui/Panel.hpp"
#include "visu/ui/CodeDock.hpp"
#include "visu/ui/FolderTree.hpp"
#include "visu/helpers/BuildProject.hpp"

#include <functional>
#include <string>

class Project;

// Ouvre la fenetre "Create new Component" (nom pre-rempli : _name) : genere
// Components/<Nom>.hpp + RegisterComponents, puis ouvre le .hpp dans VS Code.
// _onStatus recoit "Composant cree." ou l'erreur. Panneau Component et
// inspecteur (Add Component > Creer un nouveau composant).
void openCreateComponent(Project *_project, const std::string &_name,
                         const std::function<void(const std::string &)> &_onStatus);

class ComponentPanel : public Panel
{
public:
    const char *name() const override { return "Component"; }
    void draw(Project *_project) override;
    // Onglet cache : le raccourci Ctrl+G, C et la fin de la generation sont
    // quand meme traites.
    void drawHidden() override;

private:
    void updateCatalog(Project *_project, bool _visible);
    // Demande confirmation (avec la liste de ce qui sera touche) puis
    // supprime toute la couche du composant (voir componentgen::deleteComponent).
    void askDelete(const Project &_project, const std::string &_name);

    std::string m_buildLog;        // sortie du compilo/generateur (pour le popup)
    bool m_openErrorPopup = false; // demande d'ouverture du popup d'erreur
    std::string m_status;          // ligne de statut du dernier "Generate"
    CodeDock m_code{"ComponentCode"};
    // Tous les .hpp du projet (composants ou non) ; seuls les registres
    // generes sont masques.
    FolderTree m_projectTree{{".hpp"}, {"RegisterComponents", "RegisterWorldComponents"}};
    // Moteur : l'agregateur Components.hpp n'est pas un composant.
    FolderTree m_engineTree{{".hpp"}, {"Components"}};
    projectbuild::AsyncBuild m_catalogBuild; // generation du catalogue (thread)
};
