#pragma once
#include "Common/d3dUtil.h"

class GBuffer
{
public:
    static constexpr UINT TargetCount = 3;
    void Build(ID3D12Device* device, UINT width, UINT height);
    void CreateDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu, UINT srvSize);
    void Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvTable() const { return mSrvGpu; }
    ID3D12DescriptorHeap* RtvHeap() const { return mRtvHeap.Get(); }
    static DXGI_FORMAT Format(UINT index);
private:
    UINT mWidth = 0, mHeight = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> mTargets[TargetCount];
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE mSrvGpu = {};
    UINT mRtvSize = 0;
};
