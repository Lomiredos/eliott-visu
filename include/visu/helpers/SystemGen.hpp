#pragma once

#include "visu/ui/CreateSystemModal.hpp" // SystemInfoCreation

#include <filesystem>

// ---------------------------------------------------------------------------
// Generation d'un systeme dans le PROJET (les systemes n'appartiennent pas au
// moteur). Ecrit, dans <projet>/systems/, le trio de fichiers d'un systeme :
//   <Nom>.json  : le sidecar (signature, priorite) lu par le runtime + l'editeur
//   <Nom>.hpp   : la declaration, convention uniforme void <Nom>(FrameContext&)
//   <Nom>.cpp   : le stub a completer (le corps du systeme, ouvert dans VS Code)
// Puis REGENERE RegisterSystems.{hpp,cpp} (nom -> fonction) en scannant le
// dossier -> un systeme cree est automatiquement branche au registre du jeu.
// ---------------------------------------------------------------------------

#include <string>

namespace systemgen
{
    // Resultat de creation : chemin du .cpp genere si succes (error vide),
    // sinon path vide + message d'erreur (nom invalide, collision, ecriture KO).
    struct CreateResult
    {
        std::filesystem::path path;
        std::string error;
    };

    // Renvoie le .cpp genere (a ouvrir) ou un resultat en erreur.
    CreateResult createSystem(const std::filesystem::path &_systemsDir,
                              const SystemInfoCreation &_def);

    // Supprime le trio <Nom>.json/.hpp/.cpp puis regenere RegisterSystems.
    // Renvoie un message d'erreur (vide si succes).
    std::string deleteSystem(const std::filesystem::path &_systemsDir,
                             const std::string &_name);
}
