#pragma once
#include "visu/ui/Modal.hpp"

#include <functional>
#include <vector>
#include <string>

struct SystemInfoCreation
{
    std::string name;
    std::vector<std::string> requiredComponentName;
};

class CreateSystemModal : public Modal
{
private:
    std::vector<std::string> m_componentsNames;
    std::vector<bool> m_valide;
    std::string m_sysName;

    std::function<void(const SystemInfoCreation &)> m_onClose;

    SystemInfoCreation buildResult();

public:
    CreateSystemModal(std::vector<std::string> _components,
                      std::function<void(const SystemInfoCreation &)> _cb);
    const char *id() const override { return "CreateSystemModal"; }
    bool draw() override;
};