#pragma once

#include <filesystem>
#include <string>

namespace fileio
{
    // Contenu du fichier ("" s'il est absent ou illisible).
    std::string readFile(const std::filesystem::path &_path);

    // Ecrit (ecrase) le fichier. false si verrouille, lecture seule ou
    // chemin invalide.
    bool writeFile(const std::filesystem::path &_path, const std::string &_content);
}
