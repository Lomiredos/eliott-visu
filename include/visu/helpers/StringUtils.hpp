#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace stringutils
{
    // Minuscules ASCII (recherche insensible a la casse, extensions).
    inline std::string toLower(std::string _s)
    {
        std::transform(_s.begin(), _s.end(), _s.begin(),
                       [](unsigned char _c) { return static_cast<char>(std::tolower(_c)); });
        return _s;
    }
}
