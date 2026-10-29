#pragma once

class Modal
{
public:
    virtual ~Modal() = default;
    virtual const char *id() const = 0;
    // Dessine la modale ; true quand elle est terminee (a detruire).
    virtual bool draw() = 0;
};