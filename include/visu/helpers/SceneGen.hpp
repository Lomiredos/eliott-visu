#pragma once

#include "visu/ui/CreateSceneModal.hpp" // SceneInfoCreation

#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// Creation d'une scene dans le PROJET (Assets/ScenesDatas/). Une scene n'est
// qu'un fichier JSON (SceneInfo) : pas de classe C++, pas de parent/heritage
// -- la logique de jeu vit dans les Systems, jamais dans la scene elle-meme.
// ---------------------------------------------------------------------------

namespace scenegen
{
    // Resultat de creation : chemin du .json genere si succes (error vide),
    // sinon path vide + message d'erreur (nom invalide, collision, ecriture KO).
    struct CreateResult
    {
        std::filesystem::path path;
        std::string error;
    };

    CreateResult createScene(const std::filesystem::path &_scenesDataDir,
                             const SceneInfoCreation &_def);
}
