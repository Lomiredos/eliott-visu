#include "visu/ui/FileReader.hpp"

#include <fstream>
#include <sstream>

std::string readFile(const std::filesystem::path& _path){
    std::ifstream file(_path);
    if (!file)
        return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
