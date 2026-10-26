#pragma once

#include <map>
#include <string>
#include <vector>
#include <filesystem>

#include "visu/core/SceneInfo.hpp" // FieldValue

namespace fs = std::filesystem;

// Lit un catalogue Components.json et renvoie, par composant, ses champs avec
// leur valeur par defaut, TYPEE selon le "type" declare dans le catalogue :
// { "TransformComponent": { "x": 0.0f, ... }, "TagComponent": { "name": "" } }
std::map<std::string, std::map<std::string, FieldValue>> getComponentDefaults(const fs::path &_catalog);

// Union des composants STANDARD (catalogue moteur, assets/Components.json integre au binaire)
// et des composants du PROJET (_projectCatalog). Le standard est prioritaire :
// en cas de nom commun, la version projet est ignoree et signalee (stderr).
std::map<std::string, std::map<std::string, FieldValue>> getAllComponentDefaults(const fs::path &_projectCatalog);

// Pour les champs de type "enum" d'un catalogue, la liste des valeurs
// possibles declarees ("options") : { "ColliderComponent": { "Shape":
// ["Sphere", "Box", "Capsule"] } }. Les champs non-enum n'apparaissent pas.
std::map<std::string, std::map<std::string, std::vector<std::string>>> getComponentEnumOptions(const fs::path &_catalog);

// Union STANDARD + PROJET des options d'enum, meme logique que
// getAllComponentDefaults.
std::map<std::string, std::map<std::string, std::vector<std::string>>> getAllComponentEnumOptions(const fs::path &_projectCatalog);

// Champs de type "entity" (reference vers une autre entite) : pour chacun, le
// composant que l'entite visee doit avoir ("" = n'importe laquelle) :
// { "PlayerComponent": { "Camera": "CameraComponent" } }. Standard + projet.
std::map<std::string, std::map<std::string, std::string>> getAllComponentEntityFields(const fs::path &_projectCatalog);
