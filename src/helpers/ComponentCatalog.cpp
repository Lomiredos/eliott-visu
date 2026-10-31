#include "visu/helpers/ComponentCatalog.hpp"

#include "visu/helpers/FileIO.hpp"
#include "visu/helpers/StandardComponents.hpp"

#include <nlohmann/json.hpp>

#include <iostream>

namespace fs = std::filesystem;

namespace
{
    template <typename T>
    const T *findField(const ComponentCatalog::PerField<T> &_map, const std::string &_comp,
                       const std::string &_field)
    {
        auto comp = _map.find(_comp);
        if (comp == _map.end())
            return nullptr;
        auto field = comp->second.find(_field);
        return field == comp->second.end() ? nullptr : &field->second;
    }

    FieldValue defaultValue(const nlohmann::json &_field, const std::string &_type)
    {
        if (_type == "bool")
            return _field.value("default", false);
        if (_type == "int")
            return _field.value("default", 0);
        if (_type == "string" || _type == "enum")
            return _field.value("default", std::string{});
        if (_type == "entity") // index de l'entite visee, -1 = aucune
            return _field.value("default", -1);
        if (_type == "color") // "#rrggbb"
            return _field.value("default", std::string("#ffffff"));
        return _field.value("default", 0.0f);
    }

    LayoutNode::Kind parseKind(const nlohmann::json &_node){
        const std::string k = _node.value("kind", std::string("field"));
        if (k == "group")     return LayoutNode::Kind::Group;
        if (k == "separator") return LayoutNode::Kind::Separator;
        if (k == "space")     return LayoutNode::Kind::Space;
        if (k == "label")     return LayoutNode::Kind::Label;
        return LayoutNode::Kind::Field; // les ancien format on pas de kind, et c'est des field;
    }

    std::string joinKey(const std::string &_prefix, const std::string &_name)
    {
        return _prefix.empty() ? _name : _prefix + "." + _name;
    }

    std::vector<LayoutNode> parseNodes(const nlohmann::json &_nodes, const std::string &_prefix,
                                       const std::string &_comp, ComponentCatalog &_out)
    {
        std::vector<LayoutNode> result;
        if (_nodes.is_array() == false)
            return result; // pas de "children" ou pas fermer ect...

        for (const nlohmann::json &j : _nodes)
        {
            LayoutNode node;
            node.kind = parseKind(j);

            switch (node.kind) 
            {
                case LayoutNode::Kind::Separator:
                case LayoutNode::Kind::Space:
                    break;
                
                case LayoutNode::Kind::Label:
                    node.text = j.value("text", std::string{});
                    break;
                case LayoutNode::Kind::Field:
                {
                    node.name = j.value("name", std::string{});
                    if (node.name.empty())
                        continue; // nom invalide;
                    node.key = joinKey(_prefix, node.name);
                    
                    std::string type = j.value("type", "float");
                    _out.defaults[_comp][node.key] = defaultValue(j, type);
                    _out.fieldTypes[_comp][node.key] = type;
                    if (type == "enum")
                        _out.enumOptions[_comp][node.key] = j.value("options", std::vector<std::string>{});
                    else if (type == "entity")
                        _out.entityFields[_comp][node.key] = j.value("requires", std::string{});
                    break;
                }
                case LayoutNode::Kind::Group:
                {
                    node.name = j.value("name", std::string{});
                    if (node.name.empty())
                        continue;
                    node.key = joinKey(_prefix, node.name);
                    node.isArray = j.value("array", false);
                    node.children = parseNodes(j.value("children", nlohmann::json::array()), node.key, _comp,_out);
                    break;
                }
            }
            result.push_back(std::move(node));
                
        }
        return result;
    }

    

    // Un seul parcours du JSON remplit les quatre tables.
    ComponentCatalog parseCatalog(const std::string &_text)
    {
        ComponentCatalog out;
        nlohmann::json data = nlohmann::json::parse(_text, nullptr, false);
        if (!data.is_object() || !data.contains("components"))
            return out; // absent ou malforme : catalogue vide

        for (const nlohmann::json &comp : data["components"])
        {
            const std::string cname = comp.value("name", "");
            if (cname.empty())
                continue;
            out.defaults[cname]; // cree l'entrée vide
            out.layouts[cname] = parseNodes(comp.value("fields", nlohmann::json::array()), "", cname, out);
        }
        return out;
    }

    // Ajoute _project a _base sans ecraser : le standard reste prioritaire.
    void mergeInto(ComponentCatalog &_base, ComponentCatalog _project)
    {
        _base.defaults.merge(_project.defaults);
        for (const auto &kv : _project.defaults) // restes = noms deja pris
            std::cerr << "[Components] nom deja defini par le standard, version projet ignoree : "
                      << kv.first << '\n';
        _base.fieldTypes.merge(_project.fieldTypes);
        _base.enumOptions.merge(_project.enumOptions);
        _base.entityFields.merge(_project.entityFields);
        _base.layouts.merge(_project.layouts);
    }
}

const std::string *ComponentCatalog::requiredComponent(const std::string &_comp,
                                                       const std::string &_field) const
{
    return findField(entityFields, _comp, _field);
}

const std::vector<std::string> *ComponentCatalog::enumValues(const std::string &_comp,
                                                             const std::string &_field) const
{
    return findField(enumOptions, _comp, _field);
}

bool ComponentCatalog::isColor(const std::string &_comp, const std::string &_field) const
{
    const std::string *type = findField(fieldTypes, _comp, _field);
    return type && *type == "color";
}

bool syncSceneWithCatalog(SceneInfo &_scene, const ComponentCatalog &_catalog)
{
    bool changed = false;
    for (EntityInfo &ent : _scene.entities)
        for (ComponentInstance &ci : ent.components)
        {
            auto comp = _catalog.defaults.find(ci.name);
            if (comp == _catalog.defaults.end())
                continue; // inconnu du catalogue (pas encore genere ?) : on garde tout
            const std::map<std::string, FieldValue> &fields = comp->second;

            // Champs disparus.
            for (auto it = ci.values.begin(); it != ci.values.end();)
            {
                if (fields.count(it->first))
                    ++it;
                else
                {
                    it = ci.values.erase(it);
                    changed = true;
                }
            }

            for (const auto &[name, def] : fields)
            {
                auto it = ci.values.find(name);
                if (it == ci.values.end()) // nouveau champ
                {
                    ci.values.emplace(name, def);
                    changed = true;
                    continue;
                }
                FieldValue &v = it->second;
                if (v.index() == def.index())
                    continue;
                // Type change : on garde la valeur si c'est juste int <-> float.
                if (std::holds_alternative<float>(def) && std::holds_alternative<int>(v))
                    v = static_cast<float>(std::get<int>(v));
                else if (std::holds_alternative<int>(def) && std::holds_alternative<float>(v))
                    v = static_cast<int>(std::get<float>(v));
                else
                    v = def;
                changed = true;
            }
        }
    return changed;
}

const ComponentCatalog &componentCatalog(const fs::path &_projectCatalog)
{
    // Standard : integre au binaire, ne change jamais.
    static const ComponentCatalog s_standard = parseCatalog(kStandardComponentsJson);

    static fs::path s_path;
    static fs::file_time_type s_mtime{};
    static bool s_loaded = false;
    static ComponentCatalog s_merged;

    std::error_code ec;
    const fs::file_time_type mtime = fs::last_write_time(_projectCatalog, ec);
    const fs::file_time_type stamp = ec ? fs::file_time_type{} : mtime; // absent = epoque
    if (s_loaded && s_path == _projectCatalog && s_mtime == stamp)
        return s_merged;

    s_merged = s_standard;
    mergeInto(s_merged, parseCatalog(fileio::readFile(_projectCatalog)));
    s_path = _projectCatalog;
    s_mtime = stamp;
    s_loaded = true;
    return s_merged;
}
