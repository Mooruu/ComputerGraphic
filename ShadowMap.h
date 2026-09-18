#pragma once

#include "Common/d3dUtil.h"

class ShadowMap
{
public:
    static constexpr UINT CascadeCount = 3;
    static constexpr UINT Resolution = 2048;

    void Build(ID3D12Device* device);
    void CreateDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu);
    void Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const;
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv(UINT cascade) const;
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return mSrvGpu; }
    D3D12_VIEWPORT Viewport() const;
    D3D12_RECT ScissorRect() const;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> mResource;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE mSrvGpu = {};
    UINT mDsvSize = 0;
};
