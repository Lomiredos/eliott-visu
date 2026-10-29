#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace codereferences
{
    // Fichiers source (.hpp/.h/.cpp) sous _dirs qui contiennent _identifier
    // en mot entier. Les registres generes par l'editeur et les fichiers de
    // _ignored sont exclus. Sert a lister le code a corriger a la main avant
    // une suppression.
    std::vector<std::filesystem::path> filesMentioning(
        const std::vector<std::filesystem::path> &_dirs, const std::string &_identifier,
        const std::vector<std::filesystem::path> &_ignored);
}
