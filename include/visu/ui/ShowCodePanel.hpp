#pragma once

#include "visu/ui/Panel.hpp"
#include "visu/core/SystemInfo.hpp"

#include "TextEditor.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class ShowCodePanel : public Panel
{
    std::string m_title;
    std::string m_idSuffix;
    std::filesystem::path m_filePath;
    TextEditor m_editor;
    unsigned int m_dockNode = 0;
    unsigned int m_rootDock = 0; // dockspace racine ou la fenetre est dockee (0 = flottante)
    std::optional<SystemInfo> m_info;

public:
    ShowCodePanel(const std::filesystem::path &_path);

    const char *name() const override { return m_title.c_str(); }
    void draw(Project *_project) override;
    void changePath(const std::filesystem::path &_path);

    unsigned int dockNode() const { return m_dockNode; }
    const std::filesystem::path &path() const { return m_filePath; }
    // Ferme les lecteurs qui affichent l'un de _files (fichiers supprimes).
    static void closeShowing(const std::vector<std::filesystem::path> &_files);
    // true si la fenetre est dockee dans le dockspace _dockspaceId (meme
    // apres un split interne). Une fenetre sortie a la main renvoie false.
    bool isDockedIn(unsigned int _dockspaceId) const
    {
        return _dockspaceId != 0 && m_rootDock == _dockspaceId;
    }
};
