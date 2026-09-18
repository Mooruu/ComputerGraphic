#pragma once
#include "Common/d3dUtil.h"

struct DeferredLightConstants
{
    DirectX::XMFLOAT4 CameraPosition;
    DirectX::XMFLOAT4 Ambient;
    DirectX::XMFLOAT4 DirectionalDirection;
    DirectX::XMFLOAT4 DirectionalColor;
    DirectX::XMFLOAT4 PointPositionRange;
    DirectX::XMFLOAT4 PointColor;
    DirectX::XMFLOAT4 SpotPositionRange;
    DirectX::XMFLOAT4 SpotDirectionPower;
    DirectX::XMFLOAT4 SpotColor;
};

class RenderingSystem
{
public:
    DeferredLightConstants BuildLightConstants(const DirectX::XMFLOAT3& camera) const;
};
