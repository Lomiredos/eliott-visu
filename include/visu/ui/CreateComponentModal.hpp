#pragma once
#include "visu/ui/Modal.hpp"

#include <functional>
#include <string>
#include <vector>

struct ComponentFieldDef
{
    std::string name;
    std::string type = "float";
    std::string required; // type "entity" : composant exige ("" = aucun)
};

struct ComponentInfoCreation
{
    std::string name;
    std::vector<ComponentFieldDef> fields;
};

class CreateComponentModal : public Modal
{
private:
    std::string m_name;
    std::vector<ComponentFieldDef> m_fields;
    std::vector<std::string> m_componentNames; // choix du composant requis (champ entity)

    std::function<void(ComponentInfoCreation)> m_onClose;

    ComponentInfoCreation buildResult();

public:
    // _componentNames : composants existants, proposes comme composant requis
    // d'un champ "entity". _name : nom pre-rempli (recherche de l'inspecteur).
    CreateComponentModal(std::function<void(ComponentInfoCreation)> _cb,
                         std::vector<std::string> _componentNames = {},
                         std::string _name = "");
    const char *Id() const override { return "CreateComponentModal"; }
    bool Draw() override;
};
