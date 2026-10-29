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
            std::map<std::string, FieldValue> &defaults = out.defaults[cname];
            if (!comp.contains("fields"))
                continue;
            for (const nlohmann::json &field : comp["fields"])
            {
                const std::string fname = field.value("name", "");
                if (fname.empty())
                    continue;
                const std::string type = field.value("type", std::string("float"));
                defaults[fname] = defaultValue(field, type);
                out.fieldTypes[cname][fname] = type;
                if (type == "enum")
                    out.enumOptions[cname][fname] =
                        field.value("options", std::vector<std::string>{});
                else if (type == "entity")
                    out.entityFields[cname][fname] = field.value("requires", std::string{});
            }
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
