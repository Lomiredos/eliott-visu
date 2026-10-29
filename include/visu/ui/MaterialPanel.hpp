#pragma once

#include "visu/ui/Panel.hpp"

#include "visu/core/MaterialStore.hpp"
#include "visu/render/GLRenderer.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Panneau "Materials" : les .mat du projet (Assets/), creation, suppression,
// et reglage du materiau selectionne avec un apercu en direct.
// Un materiau se glisse sur une entite (hierarchie, preview) ou sur le champ
// path d'un MaterialComponent.
// ---------------------------------------------------------------------------
class MaterialPanel : public Panel
{
public:
    const char *name() const override { return "Materials"; }
    void draw(Project *project) override;

private:
    void drawList(Project *project);
    void drawEditor(Project *project);
    bool drawMapSlot(Project *project, int _slot);
    void select(Project *project, const std::string &_path);
    void save(Project *project);
    void refresh(Project *project, bool _force = false);
    std::filesystem::file_time_type selectedMtime(Project *project) const;

    std::string m_selected; // chemin relatif a la racine du projet
    ee::core::MaterialAsset m_edit;
    bool m_dirty = false;      // modifie, pas encore ecrit
    std::filesystem::file_time_type m_mtime{}; // du fichier quand on l'a lu/ecrit
    bool m_exists = false;     // le fichier selectionne existe encore
    std::vector<std::string> m_mats; // .mat du projet (rafraichi 1 fois/s)
    std::chrono::steady_clock::time_point m_lastRefresh{};
    std::string m_newName;
    std::string m_status;
    int m_previewShape = 0;    // 0 sphere, 1 cube
    float m_previewYaw = 0.6f; // rotation de l'apercu (glisser la souris)
    ee::render::GLRenderer m_gl; // apercu + miniatures des textures
};
