#pragma once

#include "visu/helpers/Directions.hpp"

class Project;

struct DockRequest
{
    unsigned int dockTarget = 0;
    unsigned int splitSource = 0;
    Dir splitDir;
    float splitRatio = 0.5f;
};

class Panel
{

protected:
    bool m_locked = false;

public:
    virtual ~Panel() = default;

    virtual const char *name() const = 0;
    virtual void draw(Project *project) = 0;
    // Appele a la place de draw() quand la fenetre existe mais n'est pas
    // affichee (onglet inactif) : sert a garder vivants les dockspaces internes.
    virtual void drawHidden() {}

    DockRequest dock;
    bool visible = true;
    bool removeOnClose = false; // true => détruit quand fermé (ex: ShowCode), pas juste masqué

    bool isLocked() const { return m_locked; }
};
