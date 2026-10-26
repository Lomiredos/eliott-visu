#pragma once

#include "visu/ui/Panel.hpp"
#include "visu/ui/CodeDock.hpp"

#include <filesystem>
#include <string>

class SystemPanel : public Panel
{

public:

    const char *name() const override { return "System"; }
    void draw(Project *project) override;
    void drawHidden() override { m_code.keepAlive(); }

private:
    // Demande confirmation puis supprime le systeme (fichiers + registre).
    void askDelete(const std::filesystem::path &_dir, const std::string &_name);

    std::string m_status; // resultat de la derniere creation/suppression de systeme
    CodeDock m_code{"SystemCode"};
};
