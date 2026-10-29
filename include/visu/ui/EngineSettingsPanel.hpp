#pragma once

#include "visu/ui/Panel.hpp"

#include "visu/core/EngineSettings.hpp"

#include <string>

// ---------------------------------------------------------------------------
// Panneau "Reglages" : reglages du moteur pour le projet ouvert
// (Assets/EngineSettings.json) : lampes, ombres. Appliques en direct a la
// vue de l'editeur et au jeu lance (le fichier est relu au plus 1 fois/s).
// ---------------------------------------------------------------------------
class EngineSettingsPanel : public Panel
{
public:
    const char *name() const override { return "Reglages"; }
    void draw(Project *_project) override;

private:
    void save(Project *_project);

    ee::core::EngineSettings m_edit;
    bool m_dirty = false; // modifie, pas encore ecrit
    std::string m_status;
};
