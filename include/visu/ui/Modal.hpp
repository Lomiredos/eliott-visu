#pragma once

class Modal
{
public:
    virtual ~Modal() = default;
    virtual const char *Id() const = 0;
    virtual bool Draw() = 0;
};