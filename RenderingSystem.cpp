#include "RenderingSystem.h"
using namespace DirectX;

DeferredLightConstants RenderingSystem::BuildLightConstants(const XMFLOAT3& camera) const
{
    DeferredLightConstants c = {};
    c.CameraPosition = XMFLOAT4(camera.x, camera.y, camera.z, 1.0f);
    c.Ambient = XMFLOAT4(0.04f, 0.04f, 0.06f, 1.0f);
    c.DirectionalDirection = XMFLOAT4(-0.35f, -0.8f, 0.25f, 0.0f);
    c.DirectionalColor = XMFLOAT4(0.12f, 0.22f, 0.95f, 1.0f);
    c.PointPositionRange = XMFLOAT4(0.0f, 14.0f, -8.0f, 42.0f);
    c.PointColor = XMFLOAT4(1.0f, 0.24f, 0.08f, 10.0f);
    c.SpotPositionRange = XMFLOAT4(15.0f, 16.0f, -4.0f, 60.0f);
    c.SpotDirectionPower = XMFLOAT4(-0.72f, -0.58f, 0.38f, 20.0f);
    c.SpotColor = XMFLOAT4(0.18f, 0.85f, 1.0f, 16.0f);
    return c;
}
