#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
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
        if (!it->is_regular_file())
            continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char _c) { return static_cast<char>(std::tolower(_c)); });
        if (std::find(_exts.begin(), _exts.end(), ext) != _exts.end())
            out.push_back(fs::relative(it->path(), _root, ec).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline const std::vector<std::string> &imageExtensions()
{
    static const std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
    return exts;
}

// Glisser-deposer d'un asset (payload = chemin relatif, texte termine par 0).
inline constexpr const char *kAssetPayload = "EE_ASSET";
