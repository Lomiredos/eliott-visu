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
    unsigned int render(const SceneInfo &scene, int width, int height, int selected = -1);

    // Picking : rayon depuis la camera a travers (ndcX, ndcY) dans [-1,1].
    // Renvoie l'index de l'entite (sphere) touchee la plus proche, ou -1.
    int pick(const SceneInfo &scene, float ndcX, float ndcY, float aspect);

    // Meme picking depuis une camera quelconque (ex. celle du jeu en pause).
    static int pickFrom(const SceneInfo &scene, const ee::render::Camera &cam,
                        float ndcX, float ndcY, float aspect);

    // Matrices du dernier render (pour ImGuizmo). column-major, 16 floats.
    void getViewMatrix(float out[16]) const;
    void getProjMatrix(float out[16]) const;

    // --- Controle camera libre (mode navigation) ---
    void addYawPitch(float dYaw, float dPitch);           // rotation souris
    void moveLocal(float forward, float right, float up); // deplacement ZQSD
    void dolly(float amount);                             // zoom molette

private:
    void ensureCamInit(); // yaw/pitch initiaux calcules pour viser l'origine

    ee::render::GLRenderer m_gl;

    // Camera : position + orientation (yaw autour de Y, pitch haut/bas).
    ee::math::Vector3<float> m_camPos = {6.0f, 5.0f, 6.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_camInit = false;
};
