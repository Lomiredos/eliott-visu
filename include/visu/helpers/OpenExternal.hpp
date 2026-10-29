#pragma once

#include <filesystem>

// Ouvre _file dans VS Code, sans bloquer l'editeur, avec _workspace (la
// racine du projet) comme dossier de travail : si une fenetre l'a deja
// ouvert, VS Code la remet devant et y affiche le fichier (pas de nouvelle
// fenetre), et les reglages du projet (.vscode/, clangd) s'appliquent.
// Necessite `code` dans le PATH.
void openInVSCode(const std::filesystem::path &_file, const std::filesystem::path &_workspace);
