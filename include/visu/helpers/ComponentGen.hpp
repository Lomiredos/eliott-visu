#pragma once

#include "visu/core/SceneInfo.hpp"
#include "visu/ui/CreateComponentModal.hpp"

#include <filesystem>
#include <string>
#include <vector>

class Project;

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

    // Tout ce que la suppression d'un composant du projet touche, calcule
    // avant de demander confirmation.
    struct DeletePlan
    {
        std::string name;
        std::vector<std::filesystem::path> files;   // Components/<Nom>.hpp (+ .cpp)
        std::vector<std::filesystem::path> scenes;  // scenes dont des entites l'ont
        int instances = 0;                          // nombre d'entites concernees
        std::vector<std::filesystem::path> systems; // sidecars dont la signature le contient
        bool inCatalog = false;                     // entree dans Assets/Components.json
        // Code ecrit a la main qui le mentionne encore : a corriger a la main.
        std::vector<std::filesystem::path> codeRefs;
    };

    DeletePlan planDeleteComponent(const Project &_project, const std::string &_name);

    // Supprime toute la couche du composant : fichiers, registres, catalogue,
    // instances dans les scenes (sur le disque), signatures des systemes.
    // La scene ouverte dans l'editeur se nettoie a part (App::purgeComponent).
    // Renvoie un message d'erreur (vide si succes).
    std::string deleteComponent(const Project &_project, const DeletePlan &_plan);

    // Retire le composant _name de toutes les entites. Renvoie le nombre
    // d'entites modifiees.
    int removeComponentInstances(SceneInfo &_scene, const std::string &_name);
}
