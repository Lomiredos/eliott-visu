#include "visu/helpers/ComponentGen.hpp"

#include "visu/helpers/NameValidation.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    bool writeFile(const fs::path &_path, const std::string &_content)
    {
        std::ofstream f(_path, std::ios::binary);
        if (!f)
            return false; // fichier verrouille / lecture seule / chemin invalide
        f << _content;
        return f.good();
    }

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

        writeFile(_dir / "RegisterComponents.hpp",
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
        writeFile(_dir / "RegisterComponents.cpp", cpp.str());

        // --- Enregistrement des composants dans le World typé (runtime) ---
        writeFile(_dir / "RegisterWorldComponents.hpp",
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
        writeFile(_dir / "RegisterWorldComponents.cpp", world.str());
    }
}

namespace componentgen
{
    bool isComponentHeader(const fs::path &_hpp)
    {
        if (_hpp.extension() != ".hpp")
            return false;
        std::ifstream f(_hpp, std::ios::binary);
        if (!f)
            return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str().find("Reflect<" + _hpp.stem().string() + ">") != std::string::npos;
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

        if (!writeFile(hppPath, hpp.str()))
            return {{}, "Ecriture impossible (fichier verrouille ?)."};

        regenerateRegister(_componentsDir);

        return {hppPath, {}};
    }
}
