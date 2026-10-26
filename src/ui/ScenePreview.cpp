#include "visu/ui/ScenePreview.hpp"

#include "visu/render/Renderer.hpp"
#include "visu/systems/RenderSystem.hpp"
#include "visu/systems/PickingSystem.hpp"

#include <cmath>
#include <algorithm>

namespace
{
    ee::math::Vector3<float> forwardFrom(float _yaw, float _pitch)
    {
        float cp = std::cos(_pitch), sp = std::sin(_pitch);
        float cy = std::cos(_yaw), sy = std::sin(_yaw);
        return {cp * cy, sp, cp * sy};
    }
}

void ScenePreview::ensureCamInit()
{
    if (m_camInit)
        return;
    ee::math::Vector3<float> d = (-m_camPos).Normalize();
    m_pitch = std::asin(d.y);
    m_yaw = std::atan2(d.z, d.x);
    m_camInit = true;
}

unsigned int ScenePreview::render(const SceneInfo &_scene, int _width, int _height, int _selected)
{
    if (_width <= 0 || _height <= 0)
        return 0;

    ensureCamInit();

    ee::render::Camera cam;
    cam.pos = {m_camPos.x, m_camPos.y, m_camPos.z};
    cam.yaw = m_yaw;
    cam.pitch = m_pitch;

    m_gl.beginScene(cam, _width, _height);

    ee::systems::renderScene(m_gl, _scene, _selected);

    m_gl.endScene();
    return m_gl.texture();
}

void ScenePreview::addYawPitch(float _dYaw, float _dPitch)
{
    m_yaw += _dYaw;
    m_pitch += _dPitch;
    const float lim = 1.55f; // +- 89° on peut pas regarder vers le haut ou le bas extreme pour pas retourner la cam
    if (m_pitch > lim)
        m_pitch = lim;
    if (m_pitch < -lim)
        m_pitch = -lim;
}

void ScenePreview::moveLocal(float _forward, float _right, float _up)
{
    ee::math::Vector3<float> f = forwardFrom(m_yaw, m_pitch);
    ee::math::Vector3<float> wup{0.0f, 1.0f, 0.0f};
    ee::math::Vector3<float> right = f.Cross(wup).Normalize();

    m_camPos.x += f.x * _forward + right.x * _right + wup.x * _up;
    m_camPos.y += f.y * _forward + right.y * _right + wup.y * _up;
    m_camPos.z += f.z * _forward + right.z * _right + wup.z * _up;
}

void ScenePreview::dolly(float _amount)
{
    ee::math::Vector3<float> f = forwardFrom(m_yaw, m_pitch);
    m_camPos.x += f.x * _amount;
    m_camPos.y += f.y * _amount;
    m_camPos.z += f.z * _amount;
}

void ScenePreview::getViewMatrix(float _out[16]) const
{
    m_gl.viewMatrix(_out);
}

void ScenePreview::getProjMatrix(float _out[16]) const
{
    m_gl.projMatrix(_out);
}

int ScenePreview::pick(const SceneInfo &_scene, float _ndcX, float _ndcY, float _aspect)
{
    if (!m_camInit)
        return -1; // la camera est initialisee dans render()

    ee::render::Camera cam; // fovDeg par defaut = celui du render ##FOV
    cam.pos = {m_camPos.x, m_camPos.y, m_camPos.z};
    cam.yaw = m_yaw;
    cam.pitch = m_pitch;
    return pickFrom(_scene, cam, _ndcX, _ndcY, _aspect);
}

int ScenePreview::pickFrom(const SceneInfo &_scene, const ee::render::Camera &_cam,
                           float _ndcX, float _ndcY, float _aspect)
{
    // Repere de la camera : avant, droite, haut.
    ee::math::Vector3<float> forward = forwardFrom(_cam.yaw, _cam.pitch);
    ee::math::Vector3<float> wup{0.0f, 1.0f, 0.0f};
    ee::math::Vector3<float> right = forward.Cross(wup).Normalize();
    ee::math::Vector3<float> camUp = right.Cross(forward);

    float t = std::tan(_cam.fovDeg * 3.14159265f / 180.0f * 0.5f);

    // Rayon a travers le pixel (_ndcX, _ndcY) ; la geometrie est deleguee au
    // systeme de picking (indep. du backend de rendu).
    ee::systems::Ray ray;
    ray.origin = {_cam.pos.x, _cam.pos.y, _cam.pos.z};
    ray.dir = (forward + right * (_ndcX * _aspect * t) + camUp * (_ndcY * t)).Normalize();

    return ee::systems::pickScene(_scene, ray);
}
