#include "visu/ui/DeletionSummary.hpp"

namespace fs = std::filesystem;

void DeletionSummary::section(const std::string &_title, const std::vector<fs::path> &_files)
{
    if (_files.empty())
        return;
    m_text += _title + "\n";
    for (const fs::path &file : _files)
    {
        std::error_code ec;
        fs::path rel = fs::relative(file, m_root, ec);
        m_text += "  " + (ec || rel.empty() ? file : rel).generic_string() + "\n";
    }
    m_text += "\n";
}
