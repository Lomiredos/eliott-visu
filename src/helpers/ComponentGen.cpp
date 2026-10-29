#include "visu/helpers/ComponentGen.hpp"

#include "visu/core/Project.hpp"
#include "visu/helpers/CodeReferences.hpp"

#include "visu/helpers/FileIO.hpp"
#include "visu/helpers/NameValidation.hpp"
#include "visu/helpers/ComponentCatalog.hpp"
#include "visu/core/SystemInfo.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace
{

    std::string fieldDecl(const ComponentFieldDef &_f)
    {
        if (_f.type == "int")
            return "    int " + _f.name + " = 0;\n";
        if (_f.type == "bool")
            return "    bool " + _f.name + " = false;\n";
        if (_f.type == "string")
            return "    std::string " + _f.name + ";\n";
        if (_f.type == "entity")
            return _f.required.empty()
                       ? "    ee::EntityRef " + _f.name + ";\n"
                       : "    ee::EntityRefOf<" + _f.required + "> " + _f.name + ";\n";
        return "    float " + _f.name + " = 0.0f;\n";
    }

    void regenerateRegister(const fs::path &_dir)
    {
        const std::vector<std::string> names = componentgen::projectComponentNames(_dir);

        fileio::writeFile(_dir / "RegisterComponents.hpp",
                          "#pragma once\n\n"
                          "#include <nlohmann/json.hpp>\n\n"
                          "// Genere par EE-Visu : emet chaque composant du projet.\n"
                          "void buildComponentCatalog(nlohmann::json &_components);\n");

        std::ostringstream cpp;
        cpp << "#include \"Components/RegisterComponents.hpp\"\n\n";
        cpp << "#include \"visu/reflect/CatalogGen.hpp\"\n\n";
        cpp << "// Genere par EE-Visu -- ne pas editer a la main.\n";
        for (const std::string &n : names)
            cpp << "#include \"Components/" << n << ".hpp\"\n";
        cpp << "\nvoid buildComponentCatalog(nlohmann::json &_components)\n{\n";
        for (const std::string &n : names)
            cpp << "    ee::reflection::emitComponent<" << n << ">(_components);\n";
        cpp << "}\n";
        fileio::writeFile(_dir / "RegisterComponents.cpp", cpp.str());

        // --- Enregistrement des composants dans le World typé (runtime) ---
        fileio::writeFile(
            _dir / "RegisterWorldComponents.hpp",
            "#pragma once\n\n"
            "#include \"visu/scene/WorldLoader.hpp\"\n\n"
            "// Genere par EE-Visu : enregistre les composants du jeu dans le World.\n"
            "void registerGameComponents(ee::scene::WorldRegistry &_reg);\n");

        std::ostringstream world;
        world << "#include \"Components/RegisterWorldComponents.hpp\"\n\n";
        world << "// Genere par EE-Visu -- ne pas editer a la main.\n";
        for (const std::string &n : names)
            world << "#include \"Components/" << n << ".hpp\"\n";
        world << "\nvoid registerGameComponents(ee::scene::WorldRegistry &_reg)\n{\n";
        for (const std::string &n : names)
            world << "    _reg.reg<" << n << ">();\n";
        world << "}\n";
        fileio::writeFile(_dir / "RegisterWorldComponents.cpp", world.str());
    }
}

namespace componentgen
{
    bool isComponentHeader(const fs::path &_hpp)
    {
        return _hpp.extension() == ".hpp" &&
               fileio::readFile(_hpp).find("Reflect<" + _hpp.stem().string() + ">") !=
                   std::string::npos;
    }

    std::vector<std::string> projectComponentNames(const fs::path &_componentsDir)
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (fs::directory_iterator it(_componentsDir, ec), end; it != end && !ec; it.increment(ec))
            if (it->is_regular_file() && isComponentHeader(it->path()))
                names.push_back(it->path().stem().string());
        // Ordre du dossier non garanti : registres generes stables d'une fois a l'autre.
        std::sort(names.begin(), names.end());
        return names;
    }

    CreateResult createComponent(const fs::path &_componentsDir, const ComponentInfoCreation &_def)
    {
        // Nom invalide -> refus (identifiant C++ + nom de fichier surs).
        if (!namevalidation::isValidIdentifier(_def.name))
            return {{}, "Nom invalide (identifiant C++ requis)."};

        const std::string &name = _def.name;

        // Collision : ne pas ecraser un composant existant en silence.
        fs::path hppPath = _componentsDir / (name + ".hpp");
        if (fs::exists(hppPath))
            return {{}, "\"" + name + "\" existe deja."};

        std::error_code ec;
        fs::create_directories(_componentsDir, ec);

        // Ne garde que des champs a l'identifiant valide et non duplique :
        // un champ invalide ou en double casserait la struct generee.
        std::vector<ComponentFieldDef> fields;
        std::unordered_set<std::string> seen;
        for (const auto &f : _def.fields)
        {
            if (!namevalidation::isValidIdentifier(f.name))
                continue;
            if (!seen.insert(f.name).second)
                continue;
            fields.push_back(f);
        }

        bool needsString = false;
        bool needsEntityRef = false;
        bool needsEngineComponents = false;
        std::vector<std::string> gameComponentIncludes; // composants requis du projet
        for (const auto &f : fields)
        {
            if (f.type == "string")
                needsString = true;
            if (f.type != "entity")
                continue;
            needsEntityRef = true;
            if (f.required.empty())
                continue;
            // Composant requis : celui du projet s'il existe, sinon du moteur.
            if (fs::exists(_componentsDir / (f.required + ".hpp")))
            {
                std::string inc = "Components/" + f.required + ".hpp";
                if (std::find(gameComponentIncludes.begin(), gameComponentIncludes.end(), inc) ==
                    gameComponentIncludes.end())
                    gameComponentIncludes.push_back(inc);
            }
            else
                needsEngineComponents = true;
        }

        std::ostringstream hpp;
        hpp << "#pragma once\n\n"
            << "#include \"visu/reflect/Reflect.hpp\"\n"
            << "#include \"visu/reflect/FieldVisitor.hpp\"\n";
        if (needsEntityRef)
            hpp << "#include \"visu/core/EntityRef.hpp\"\n";
        if (needsEngineComponents)
            hpp << "#include \"visu/components/Components.hpp\"\n";
        for (const std::string &inc : gameComponentIncludes)
            hpp << "#include \"" << inc << "\"\n";
        if (needsString)
            hpp << "\n#include <string>\n";
        hpp << "\n// Composant genere par EE-Visu. La struct est la verite ;\n"
            << "// Components.json en est genere via Reflect.\n\n"
            << "struct " << name << "\n{\n";
        for (const auto &f : fields)
            hpp << fieldDecl(f);
        hpp << "};\n\n"
            << "template <>\n"
            << "struct ee::reflection::Reflect<" << name << ">\n{\n"
            << "    static constexpr const char *name = \"" << name << "\";\n\n"
            << "    static void visit(" << name << " &_c, ee::reflection::FieldVisitor &_v)\n"
            << "    {\n";
        for (const auto &f : fields)
            hpp << "        _v.visit(\"" << f.name << "\", _c." << f.name << ");\n";
        hpp << "    }\n};\n";

        if (!fileio::writeFile(hppPath, hpp.str()))
            return {{}, "Ecriture impossible (fichier verrouille ?)."};

        regenerateRegister(_componentsDir);

        return {hppPath, {}};
    }

    int removeComponentInstances(SceneInfo &_scene, const std::string &_name)
    {
        int count = 0;
        for (EntityInfo &ent : _scene.entities)
        {
            auto &comps = ent.components;
            const auto it =
                std::remove_if(comps.begin(), comps.end(),
                               [&](const ComponentInstance &_c) { return _c.name == _name; });
            if (it != comps.end())
            {
                comps.erase(it, comps.end());
                ++count;
            }
        }
        return count;
    }

    DeletePlan planDeleteComponent(const Project &_project, const std::string &_name)
    {
        DeletePlan plan;
        plan.name = _name;
        std::error_code ec;
        for (const char *ext : {".hpp", ".cpp"})
            if (fs::path file = _project.componentsDir() / (_name + ext); fs::exists(file, ec))
                plan.files.push_back(file);

        for (fs::directory_iterator it(_project.scenesDataDir(), ec), end; it != end && !ec;
             it.increment(ec))
        {
            if (it->path().extension() != ".json")
                continue;
            if (std::optional<SceneInfo> scene = loadScene(it->path()))
                if (int n = removeComponentInstances(*scene, _name); n > 0)
                {
                    plan.scenes.push_back(it->path());
                    plan.instances += n;
                }
        }

        for (fs::directory_iterator it(_project.systemsDir(), ec), end; it != end && !ec;
             it.increment(ec))
        {
            if (it->path().extension() != ".json")
                continue;
            if (std::optional<SystemInfo> info = loadSystemInfo(it->path()))
                if (std::find(info->components.begin(), info->components.end(), _name) !=
                    info->components.end())
                    plan.systems.push_back(it->path());
        }

        const ComponentCatalog::PerField<FieldValue> &defaults =
            componentCatalog(_project.componentsCatalog()).defaults;
        plan.inCatalog = defaults.count(_name) != 0;

        plan.codeRefs = codereferences::filesMentioning(
            {_project.componentsDir(), _project.systemsDir(), _project.root() / "src"}, _name,
            plan.files);
        return plan;
    }

    std::string deleteComponent(const Project &_project, const DeletePlan &_plan)
    {
        if (_plan.name.empty())
            return "Nom de composant invalide.";
        std::string failed;
        auto fail = [&](const fs::path &_file) { failed += " " + _file.filename().string(); };

        for (const fs::path &file : _plan.files)
        {
            std::error_code ec;
            fs::remove(file, ec);
            if (ec)
                fail(file);
        }
        // Meme en cas d'echec partiel : les registres refletent les headers restants.
        regenerateRegister(_project.componentsDir());

        for (const fs::path &file : _plan.scenes)
        {
            std::optional<SceneInfo> scene = loadScene(file);
            if (!scene ||
                (removeComponentInstances(*scene, _plan.name) > 0 && !saveScene(*scene, file)))
                fail(file);
        }

        for (const fs::path &file : _plan.systems)
        {
            nlohmann::json j = nlohmann::json::parse(fileio::readFile(file), nullptr, false);
            if (!j.is_object() || !j.contains("components") || !j["components"].is_array())
            {
                fail(file);
                continue;
            }
            nlohmann::json &comps = j["components"];
            comps.erase(std::remove(comps.begin(), comps.end(), _plan.name), comps.end());
            if (!fileio::writeFile(file, j.dump(2) + "\n"))
                fail(file);
        }

        // Catalogue : plus d'entree pour lui, ni de "requires" qui le vise.
        if (_plan.inCatalog)
        {
            const fs::path catalogFile = _project.componentsCatalog();
            nlohmann::json j = nlohmann::json::parse(fileio::readFile(catalogFile), nullptr, false);
            if (j.is_object() && j.contains("components") && j["components"].is_array())
            {
                nlohmann::json &comps = j["components"];
                comps.erase(std::remove_if(comps.begin(), comps.end(), [&](const nlohmann::json &_c)
                                           { return _c.value("name", "") == _plan.name; }),
                            comps.end());
                for (nlohmann::json &comp : comps)
                    if (comp.contains("fields") && comp["fields"].is_array())
                        for (nlohmann::json &field : comp["fields"])
                            if (field.value("requires", "") == _plan.name)
                                field["requires"] = "";
                if (!fileio::writeFile(catalogFile, j.dump(2) + "\n"))
                    fail(catalogFile);
            }
        }

        if (!failed.empty())
            return "Suppression incomplete (fichier verrouille ?) :" + failed;
        return {};
    }
}
