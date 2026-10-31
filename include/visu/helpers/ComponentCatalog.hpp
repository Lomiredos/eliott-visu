#pragma once

#include "visu/core/SceneInfo.hpp" // FieldValue

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Catalogue des composants : union des composants STANDARD (catalogue moteur,
// assets/Components.json integre au binaire) et de ceux du PROJET
// (Assets/Components.json). Le standard est prioritaire : en cas de nom
// commun, la version projet est ignoree et signalee (stderr).
// ---------------------------------------------------------------------------
struct ComponentCatalog
{
    template <typename T>
    using PerField = std::map<std::string, std::map<std::string, T>>; // composant -> champ -> T

    // Valeur par defaut de chaque champ, typee selon son "type" declare.
    PerField<FieldValue> defaults;
    // Type declare : "float", "int", "bool", "string", "enum", "entity", "color".
    PerField<std::string> fieldTypes;
    // Champs "enum" seulement : valeurs possibles ("options").
    PerField<std::vector<std::string>> enumOptions;
    // Champs "entity" seulement : composant que l'entite visee doit avoir
    // ("" = n'importe laquelle).
    PerField<std::string> entityFields;

    // nullptr si le champ n'est pas de ce genre.
    const std::string *requiredComponent(const std::string &_comp, const std::string &_field) const;
    const std::vector<std::string> *enumValues(const std::string &_comp,
                                               const std::string &_field) const;
    bool isColor(const std::string &_comp, const std::string &_field) const;
};

// Catalogue du projet (standard + _projectCatalog). Parse une seule fois,
// relu seulement quand le fichier change : appelable a chaque frame. La
// reference reste valide jusqu'au prochain appel qui recharge.
const ComponentCatalog &componentCatalog(const std::filesystem::path &_projectCatalog);

// Aligne les champs des composants de la scene sur le catalogue : champ
// nouveau -> ajoute avec sa valeur par defaut, champ disparu -> retire, type
// change -> converti (int <-> float) sinon remis par defaut. Les composants
// absents du catalogue ne sont pas touches. Vrai si la scene a change.
bool syncSceneWithCatalog(SceneInfo &_scene, const ComponentCatalog &_catalog);
