#include "visu/helpers/FileIO.hpp"

#include <fstream>
#include <sstream>

namespace fileio
{
    std::string readFile(const std::filesystem::path &_path)
    {
        std::ifstream file(_path, std::ios::binary);
        if (!file)
            return {};
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    bool writeFile(const std::filesystem::path &_path, const std::string &_content)
    {
        std::ofstream file(_path, std::ios::binary);
        if (!file)
            return false;
        file << _content;
        return file.good();
    }
}
