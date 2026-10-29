#include "visu/helpers/ComponentGetter.hpp"
#include "visu/helpers/StandardComponents.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

#include <iostream>

// Lit le catalogue depuis un flux ou une chaine (tout ce que json::parse accepte).
template <typename Source>
static std::map<std::string, std::map<std::string, FieldValue>>
parseComponentDefaults(Source &&_source) {
  std::map<std::string, std::map<std::string, FieldValue>> out;

  try {
    json data = json::parse(std::forward<Source>(_source));
    for (const auto &comp : data["components"]) {
      std::string cname = comp.value("name", "");
      if (cname.empty())
        continue;

      std::map<std::string, FieldValue> fields;
      if (comp.contains("fields"))
        for (const auto &fld : comp["fields"]) {
          std::string fname = fld.value("name", "");
          if (fname.empty())
            continue;

          const std::string ftype = fld.value("type", "float");
          if (ftype == "bool")
            fields[fname] = fld.value("default", false);
          else if (ftype == "int")
            fields[fname] = fld.value("default", 0);
          else if (ftype == "string")
            fields[fname] = fld.value("default", std::string{});
          else if (ftype == "enum")
            fields[fname] = fld.value("default", std::string{});
          else if (ftype == "entity") // index de l'entite visee, -1 = aucune
            fields[fname] = fld.value("default", -1);
          else if (ftype == "color") // "#rrggbb"
            fields[fname] = fld.value("default", std::string("#ffffff"));
          else
            fields[fname] = fld.value("default", 0.0f);
        }
      out[cname] = std::move(fields);
    }
  } catch (const std::exception &) {
    // catalogue absent/malforme -> map vide
  }

  return out;
}

// Lit, depuis un flux ou une chaine, la liste des valeurs possibles
// ("options") des champs de type "enum" d'un catalogue.
template <typename Source>
static std::map<std::string, std::map<std::string, std::vector<std::string>>>
parseComponentEnumOptions(Source &&_source) {
  std::map<std::string, std::map<std::string, std::vector<std::string>>> out;

  try {
    json data = json::parse(std::forward<Source>(_source));
    for (const auto &comp : data["components"]) {
      std::string cname = comp.value("name", "");
      if (cname.empty())
        continue;

      std::map<std::string, std::vector<std::string>> fields;
      if (comp.contains("fields"))
        for (const auto &fld : comp["fields"]) {
          std::string fname = fld.value("name", "");
          if (fname.empty() || fld.value("type", "") != "enum")
            continue;

          fields[fname] = fld.value("options", std::vector<std::string>{});
        }
      if (!fields.empty())
        out[cname] = std::move(fields);
    }
  } catch (const std::exception &) {
    // catalogue absent/malforme -> map vide
  }

  return out;
}

std::map<std::string, std::map<std::string, FieldValue>>
getComponentDefaults(const fs::path &_catalog) {
  std::ifstream f(_catalog);
  if (!f)
    return {};

  return parseComponentDefaults(f);
}

std::map<std::string, std::map<std::string, FieldValue>>
getAllComponentDefaults(const fs::path &_projectCatalog) {
  // Catalogue standard integre au binaire (voir cmake/StandardComponents.hpp.in).
  auto all = parseComponentDefaults(std::string(kStandardComponentsJson));

  auto project = getComponentDefaults(_projectCatalog);

  all.merge(project);
  for (const auto &kv : project)
    std::cerr << "[Components] nom deja defini par le standard, version projet "
                 "ignoree : "
              << kv.first << '\n';

  return all;
}

std::map<std::string, std::map<std::string, std::vector<std::string>>>
getComponentEnumOptions(const fs::path &_catalog) {
  std::ifstream f(_catalog);
  if (!f)
    return {};

  return parseComponentEnumOptions(f);
}

std::map<std::string, std::map<std::string, std::vector<std::string>>>
getAllComponentEnumOptions(const fs::path &_projectCatalog) {
  // Catalogue standard integre au binaire (voir cmake/StandardComponents.hpp.in).
  auto all = parseComponentEnumOptions(std::string(kStandardComponentsJson));

  auto project = getComponentEnumOptions(_projectCatalog);
  all.merge(project);

  return all;
}

// Lit, depuis un flux ou une chaine, les champs de type "entity" d'un
// catalogue et le composant requis ("requires", vide = aucun).
template <typename Source>
static std::map<std::string, std::map<std::string, std::string>>
parseComponentEntityFields(Source &&_source) {
  std::map<std::string, std::map<std::string, std::string>> out;

  try {
    json data = json::parse(std::forward<Source>(_source));
    for (const auto &comp : data["components"]) {
      std::string cname = comp.value("name", "");
      if (cname.empty() || !comp.contains("fields"))
        continue;

      std::map<std::string, std::string> fields;
      for (const auto &fld : comp["fields"]) {
        std::string fname = fld.value("name", "");
        if (fname.empty() || fld.value("type", "") != "entity")
          continue;
        fields[fname] = fld.value("requires", std::string{});
      }
      if (!fields.empty())
        out[cname] = std::move(fields);
    }
  } catch (const std::exception &) {
    // catalogue absent/malforme -> map vide
  }

  return out;
}

std::map<std::string, std::map<std::string, std::string>>
getAllComponentEntityFields(const fs::path &_projectCatalog) {
  auto all = parseComponentEntityFields(std::string(kStandardComponentsJson));

  std::ifstream f(_projectCatalog);
  if (f) {
    auto project = parseComponentEntityFields(f);
    all.merge(project);
  }
  return all;
}

// Type declare ("float", "color", "entity"...) de chaque champ d'un catalogue.
template <typename Source>
static std::map<std::string, std::map<std::string, std::string>>
parseComponentFieldTypes(Source &&_source) {
  std::map<std::string, std::map<std::string, std::string>> out;
  try {
    json data = json::parse(std::forward<Source>(_source));
    for (const auto &comp : data["components"]) {
      std::string cname = comp.value("name", "");
      if (cname.empty() || !comp.contains("fields"))
        continue;
      for (const auto &fld : comp["fields"]) {
        std::string fname = fld.value("name", "");
        if (!fname.empty())
          out[cname][fname] = fld.value("type", std::string("float"));
      }
    }
  } catch (const std::exception &) {
    // catalogue absent/malforme -> map vide
  }
  return out;
}

std::map<std::string, std::map<std::string, std::string>>
getAllComponentFieldTypes(const fs::path &_projectCatalog) {
  auto all = parseComponentFieldTypes(std::string(kStandardComponentsJson));
  std::ifstream f(_projectCatalog);
  if (f) {
    auto project = parseComponentFieldTypes(f);
    all.merge(project);
  }
  return all;
}
