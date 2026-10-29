#pragma once

#include "visu/ui/CreateComponentModal.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace componentgen
{
    // Resultat de creation : chemin du .hpp genere si succes (error vide),
    // sinon path vide + message d'erreur (nom invalide, collision, ecriture KO).
    struct CreateResult
    {
        std::filesystem::path path;
        std::string error;
    };

    // Un header est un composant s'il specialise ee::reflection::Reflect<Nom>
    // (Nom = nom du fichier) : c'est ce que les registres generes exigent.
    // Seule definition : registres, liste des composants et panneau l'utilisent.
    bool isComponentHeader(const std::filesystem::path &_hpp);

    // Composants du projet (a la racine de _componentsDir), tries par nom.
    std::vector<std::string> projectComponentNames(const std::filesystem::path &_componentsDir);

    CreateResult createComponent(const std::filesystem::path &_componentsDir,
                                 const ComponentInfoCreation &_def);
}
