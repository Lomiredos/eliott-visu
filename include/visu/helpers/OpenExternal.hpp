#pragma once
#include <filesystem>
void openInDefaultApp(const std::filesystem::path &path);

// Ouvre un fichier dans VS Code (`code <path>`). Necessite `code` dans le PATH.
void openInVSCode(const std::filesystem::path &path);

// Lance un executable (dossier de travail = dossier de l'exe).
void launchProgram(const std::filesystem::path &exePath);