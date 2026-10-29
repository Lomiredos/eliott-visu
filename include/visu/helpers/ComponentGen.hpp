#pragma once

#include "visu/ui/CreateComponentModal.hpp"

#include <filesystem>
#include <string>

namespace componentgen
{
    // Resultat de creation : chemin du .hpp genere si succes (error vide),
    // sinon path vide + message d'erreur (nom invalide, collision, ecriture KO).
    struct CreateResult
    {
        std::filesystem::path path;
        std::string error;
    };

    CreateResult createComponent(const std::filesystem::path &_componentsDir,
                                 const ComponentInfoCreation &_def);
}
