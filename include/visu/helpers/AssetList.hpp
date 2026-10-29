#pragma once

#include "visu/helpers/StringUtils.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Fichiers du projet ayant l'une des extensions (".mat", ".png"...), cherches
// dans <racine>/Assets (sous-dossiers compris). Chemins RELATIFS a la racine,
// avec des '/', tries : c'est la forme stockee dans les scenes et les .mat.
inline std::vector<std::string> listAssets(const std::filesystem::path &_root,
                                           const std::vector<std::string> &_exts)
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    fs::path dir = _root / "Assets";
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
    {
        if (!it->is_regular_file(ec) || ec)
        {
            ec.clear(); // lien casse, droits... : on saute ce fichier
            continue;
        }
        const std::string ext = stringutils::toLower(it->path().extension().string());
        if (std::find(_exts.begin(), _exts.end(), ext) == _exts.end())
            continue;
        std::string rel = fs::relative(it->path(), _root, ec).generic_string();
        if (!ec && !rel.empty())
            out.push_back(std::move(rel));
        ec.clear();
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Meme liste, gardee en cache et relue au plus une fois par seconde : pour
// les combos dessines a chaque frame (parcourir Assets/ coute cher).
inline const std::vector<std::string> &cachedAssets(const std::filesystem::path &_root,
                                                    const std::vector<std::string> &_exts)
{
    struct Entry
    {
        std::vector<std::string> files;
        std::chrono::steady_clock::time_point time;
    };
    static std::map<std::string, Entry> s_cache; // cle : racine + extensions

    std::string key = _root.string();
    for (const std::string &ext : _exts)
        key += '|' + ext;
    const auto now = std::chrono::steady_clock::now();
    auto it = s_cache.find(key);
    if (it == s_cache.end() || now - it->second.time >= std::chrono::seconds(1))
        it = s_cache.insert_or_assign(key, Entry{listAssets(_root, _exts), now}).first;
    return it->second.files;
}

inline const std::vector<std::string> &imageExtensions()
{
    static const std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    return exts;
}

// Glisser-deposer d'un asset (payload = chemin relatif, texte termine par 0).
inline constexpr const char *kAssetPayload = "EE_ASSET";
