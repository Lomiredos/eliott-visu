#pragma once

#include "visu/ui/CreateSystemModal.hpp" // SystemInfoCreation

#include <filesystem>

// ---------------------------------------------------------------------------
// Generation d'un systeme dans le PROJET (les systemes n'appartiennent pas au
// moteur). Ecrit, dans <projet>/Systems/, le trio de fichiers d'un systeme :
//   <Nom>.json  : le sidecar (signature, priorite) lu par le runtime + l'editeur
//   <Nom>.hpp   : la classe, derivee de ee::ecs::UpdateSystem
//   <Nom>.cpp   : le stub a completer (le corps du systeme, ouvert dans VS Code)
// Puis REGENERE RegisterSystems.{hpp,cpp} (nom -> fonction) en scannant le
// dossier -> un systeme cree est automatiquement branche au registre du jeu.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

class Project;

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

    // Tout ce que la suppression d'un systeme du projet touche, calcule avant
    // de demander confirmation. Les scenes ne referencent pas les systemes :
    // la couche d'un systeme = son trio + sa ligne dans le registre.
    struct DeletePlan
    {
        std::string name;
        std::vector<std::filesystem::path> files; // <Nom>.json/.hpp/.cpp presents
        // Code ecrit a la main qui le mentionne encore : a corriger a la main.
        std::vector<std::filesystem::path> codeRefs;
    };

    DeletePlan planDeleteSystem(const Project &_project, const std::string &_name);

    // Supprime le trio <Nom>.json/.hpp/.cpp puis regenere RegisterSystems.
    // Renvoie un message d'erreur (vide si succes).
    std::string deleteSystem(const std::filesystem::path &_systemsDir, const std::string &_name);
}
