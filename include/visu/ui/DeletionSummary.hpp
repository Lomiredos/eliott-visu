#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Texte d'une fenetre de confirmation de suppression : une section par
// liste non vide, chemins relatifs a la racine du projet.
class DeletionSummary
{
public:
    explicit DeletionSummary(std::filesystem::path _root) : m_root(std::move(_root)) {}

    void section(const std::string &_title, const std::vector<std::filesystem::path> &_files);
    void line(const std::string &_text) { m_text += _text + "\n"; }
    const std::string &text() const { return m_text; }

private:
    std::filesystem::path m_root;
    std::string m_text;
};
