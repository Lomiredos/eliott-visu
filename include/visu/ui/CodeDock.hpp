#pragma once

#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// Zone de lecture de code integree a un panel (System, Component).
//
// C'est un dockspace interne au panel : les lecteurs (ShowCodePanel) s'y
// dockent, donc ils disparaissent avec le panel quand on change d'onglet.
// Un lecteur sorti a la main (tire hors du dockspace) devient une fenetre
// independante : il reste affiche et n'est plus reutilise par les clics.
// ---------------------------------------------------------------------------
class CodeDock
{
public:
    explicit CodeDock(std::string _id) : m_id(std::move(_id)) {}

    // Dessine le dockspace sur la place restante de la fenetre courante.
    // A appeler au niveau de la fenetre du panel (pas dans un child).
    void draw();

    // Panel masque (onglet inactif) : garde le dockspace vivant sans
    // l'afficher -> les lecteurs dockes dedans sont masques, pas detaches.
    void keepAlive();

    // Affiche _path dans un lecteur de cette zone : reutilise un lecteur non
    // verrouille deja docke ici, sinon en ouvre un nouveau docke ici.
    // Un .hpp qui a un .cpp a cote ouvre aussi le .cpp, a sa droite.
    void open(const std::filesystem::path &_path);

private:
    std::string m_id;
    unsigned int m_dockId = 0;

    // .cpp a ouvrir a droite du lecteur de m_pendingFor, des qu'il est docke.
    std::filesystem::path m_pendingCpp;
    std::filesystem::path m_pendingFor;
};
