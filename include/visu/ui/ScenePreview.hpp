#pragma once

#include "visu/core/SceneInfo.hpp"
#include "visu/render/GLRenderer.hpp"
#include "math/Vector3.hpp"

// Host editeur de la preview 3D.
//
// Ne dessine plus lui-meme : il possede un GLRenderer (le backend), tient
// l'etat de camera (navigation libre) et fait le picking. Le rendu proprement
// dit est delegue au systeme de rendu + au GLRenderer.
//
// API publique inchangee : MainPanel n'a pas a bouger.
class ScenePreview
{
public:
    // Rend la scene a la taille demandee. Renvoie l'id de texture GL (0 si echec).
    // selected = index de l'entite a mettre en surbrillance (glow), -1 = aucune.
    unsigned int render(const SceneInfo &_scene, int _width, int _height, int _selected = -1);

    // Ce que voit une camera du jeu (sans gizmos editeur), dans une 2e image :
    // n'ecrase pas celle de render(). Renvoie l'id de texture GL (0 si echec).
    unsigned int renderCameraView(const SceneInfo &_scene, const ee::render::Camera &_cam,
                                  int _width, int _height);

    // Picking : rayon depuis la camera a travers (ndcX, ndcY) dans [-1,1].
    // Renvoie l'index de l'entite (sphere) touchee la plus proche, ou -1.
    int pick(const SceneInfo &_scene, float _ndcX, float _ndcY, float _aspect);

    // Meme picking depuis une camera quelconque (ex. celle du jeu en pause).
    static int pickFrom(const SceneInfo &_scene, const ee::render::Camera &_cam, float _ndcX,
                        float _ndcY, float _aspect);

    // Matrices du dernier render (pour ImGuizmo). column-major, 16 floats.
    void getViewMatrix(float _out[16]) const;
    void getProjMatrix(float _out[16]) const;

    // --- Controle camera libre (mode navigation) ---
    void addYawPitch(float _dYaw, float _dPitch);            // rotation souris
    void moveLocal(float _forward, float _right, float _up); // deplacement ZQSD
    void dolly(float _amount);                               // zoom molette

private:
    void ensureCamInit(); // yaw/pitch initiaux calcules pour viser l'origine

    ee::render::GLRenderer m_gl;

    // Matrices du dernier render() (vue editeur), gardees a part : un
    // renderCameraView ensuite ne doit pas fausser le gizmo.
    float m_view[16] = {0};
    float m_proj[16] = {0};

    // Camera : position + orientation (yaw autour de Y, pitch haut/bas).
    ee::math::Vector3<float> m_camPos = {6.0f, 5.0f, 6.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_camInit = false;
};
