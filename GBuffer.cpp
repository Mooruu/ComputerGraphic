#include "GBuffer.h"
using Microsoft::WRL::ComPtr;

DXGI_FORMAT GBuffer::Format(UINT index)
{
    static const DXGI_FORMAT formats[TargetCount] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT, // albedo.rgb, specular power
        DXGI_FORMAT_R16G16B16A16_FLOAT, // world position
        DXGI_FORMAT_R16G16B16A16_FLOAT  // world normal
    };
    return formats[index];
}

void GBuffer::Build(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width; mHeight = height;
    D3D12_DESCRIPTOR_HEAP_DESC heap = {};
    heap.NumDescriptors = TargetCount;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mRtvHeap)));
    mRtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT i = 0; i < TargetCount; ++i)
    {
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(Format(i), width, height, 1, 1,
            1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clear = { Format(i), { 0, 0, 0, 0 } };
        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
            IID_PPV_ARGS(&mTargets[i])));
        auto handle = Rtv(i);
        device->CreateRenderTargetView(mTargets[i].Get(), nullptr, handle);
    }
}

void GBuffer::CreateDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE gpu, UINT size)
{
    mSrvGpu = gpu;
    for (UINT i = 0; i < TargetCount; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Format = Format(i); desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(mTargets[i].Get(), &desc, cpu);
        cpu.ptr += size; gpu.ptr += size;
    }
}

void GBuffer::Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
{
    for (const auto& target : mTargets)
        list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(target.Get(), before, after));
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::Rtv(UINT index) const
{
    auto handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * mRtvSize;
    return handle;
}
