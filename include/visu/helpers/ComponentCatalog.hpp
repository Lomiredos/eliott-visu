#pragma once

#include "visu/core/SceneInfo.hpp" // FieldValue

#include <filesystem>
#include <map>
#include <string>
#include <vector>



struct LayoutNode{
    enum class Kind {Field, Group, Separator, Space, Label} kind = Kind::Field;
    std::string name; // genre x    
    std::string key; // c'est le nom complet Position.x
    std::string text; //si on est un label
    std::vector<LayoutNode> children; // si on est un groupe
    bool isArray = false; // y'a besoins d'un commentaire la ?
};

struct ComponentCatalog
{
    template <typename T>
    using PerField = std::map<std::string, std::map<std::string, T>>; 

    PerField<FieldValue> defaults;
    PerField<std::string> fieldTypes;
    PerField<std::vector<std::string>> enumOptions;
    PerField<std::string> entityFields;

    std::map<std::string, std::vector<LayoutNode>> layouts;

    const std::string *requiredComponent(const std::string &_comp, const std::string &_field) const;
    const std::vector<std::string> *enumValues(const std::string &_comp,
                                               const std::string &_field) const;
    bool isColor(const std::string &_comp, const std::string &_field) const;
};

const ComponentCatalog &componentCatalog(const std::filesystem::path &_projectCatalog);

bool syncSceneWithCatalog(SceneInfo &_scene, const ComponentCatalog &_catalog);
