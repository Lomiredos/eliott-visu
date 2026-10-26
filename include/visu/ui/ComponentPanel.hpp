#pragma once

#include "visu/ui/Panel.hpp"
#include "visu/ui/CodeDock.hpp"

#include <string>

class ComponentPanel : public Panel
{
public:
    const char *name() const override { return "Component"; }
    void draw(Project *project) override;
    void drawHidden() override { m_code.keepAlive(); }

private:
    std::string m_buildLog;         // sortie du compilo/generateur (pour le popup)
    bool m_openErrorPopup = false;  // demande d'ouverture du popup d'erreur
    std::string m_status;           // ligne de statut du dernier "Generate"
    CodeDock m_code{"ComponentCode"};
};
