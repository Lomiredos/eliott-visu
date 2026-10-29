#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Arbre de fichiers d'un dossier (panneaux System, Component).
//
// Le contenu du dossier est garde en cache et relu au plus une fois par
// seconde (ou sur refresh()) : pas de parcours du disque a chaque frame.
// Dossiers puis fichiers, tries par nom.
// ---------------------------------------------------------------------------
class FolderTree
{
public:
    using FileAction = std::function<void(const std::filesystem::path &)>;

    // _extensions : fichiers montres (".hpp"...). _excludeStems : noms de
    // fichier (sans extension) masques, ex. registres generes.
    FolderTree(std::vector<std::string> _extensions, std::vector<std::string> _excludeStems = {});

    // Dessine l'arbre de _dir. Renvoie le fichier clique cette frame (vide
    // sinon). _contextMenu : contenu du menu clic droit d'un fichier
    // (appele entre BeginPopupContextItem et EndPopup), optionnel.
    std::filesystem::path draw(const std::filesystem::path &_dir, const FileAction &_onDoubleClick,
                               const FileAction &_contextMenu = nullptr);

    // Relit le dossier au prochain draw() (fichier cree ou supprime).
    void refresh() { m_stale = true; }

private:
    struct Node
    {
        std::filesystem::path path;
        std::string label;
        std::vector<Node> children; // vide pour un fichier
        bool isDir = false;
    };

    std::vector<Node> scan(const std::filesystem::path &_dir) const;
    bool accepts(const std::filesystem::path &_file) const;
    void drawNodes(const std::vector<Node> &_nodes, const FileAction &_onDoubleClick,
                   const FileAction &_contextMenu, std::filesystem::path &_clicked) const;

    std::vector<std::string> m_extensions;
    std::vector<std::string> m_excludeStems;
    std::filesystem::path m_dir;
    std::vector<Node> m_nodes;
    std::chrono::steady_clock::time_point m_lastScan{};
    bool m_stale = true;
};
