#include "visu/helpers/CodeReferences.hpp"

#include "visu/helpers/FileIO.hpp"

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace
{
    bool isIdentifierChar(char _c)
    {
        return std::isalnum(static_cast<unsigned char>(_c)) || _c == '_';
    }

    bool containsWord(const std::string &_text, const std::string &_word)
    {
        for (size_t pos = _text.find(_word); pos != std::string::npos;
             pos = _text.find(_word, pos + 1))
        {
            const bool startOk = pos == 0 || !isIdentifierChar(_text[pos - 1]);
            const size_t end = pos + _word.size();
            const bool endOk = end == _text.size() || !isIdentifierChar(_text[end]);
            if (startOk && endOk)
                return true;
        }
        return false;
    }

    bool isGeneratedRegister(const fs::path &_file)
    {
        const std::string stem = _file.stem().string();
        return stem == "RegisterComponents" || stem == "RegisterWorldComponents" ||
               stem == "RegisterSystems";
    }
}

namespace codereferences
{
    std::vector<fs::path> filesMentioning(const std::vector<fs::path> &_dirs,
                                          const std::string &_identifier,
                                          const std::vector<fs::path> &_ignored)
    {
        std::vector<fs::path> out;
        for (const fs::path &dir : _dirs)
        {
            std::error_code ec;
            for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec;
                 it.increment(ec))
            {
                std::error_code typeEc;
                if (!it->is_regular_file(typeEc))
                    continue;
                const fs::path &file = it->path();
                const std::string ext = file.extension().string();
                if (ext != ".hpp" && ext != ".h" && ext != ".cpp")
                    continue;
                if (isGeneratedRegister(file) ||
                    std::find(_ignored.begin(), _ignored.end(), file) != _ignored.end())
                    continue;
                if (containsWord(fileio::readFile(file), _identifier))
                    out.push_back(file);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }
}
