#include "ShadowMap.h"

void ShadowMap::Build(ID3D12Device* device)
{
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, Resolution, Resolution,
        CascadeCount, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&mResource)));

    D3D12_DESCRIPTOR_HEAP_DESC heap = {};
    heap.NumDescriptors = CascadeCount;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mDsvHeap)));
    mDsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (UINT i = 0; i < CascadeCount; ++i)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.FirstArraySlice = i;
        dsv.Texture2DArray.ArraySize = 1;
        dsv.Texture2DArray.MipSlice = 0;
        device->CreateDepthStencilView(mResource.Get(), &dsv, Dsv(i));
    }
}

void ShadowMap::CreateDescriptors(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
    mSrvGpu = gpu;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MostDetailedMip = 0;
    srv.Texture2DArray.MipLevels = 1;
    srv.Texture2DArray.FirstArraySlice = 0;
    srv.Texture2DArray.ArraySize = CascadeCount;
    device->CreateShaderResourceView(mResource.Get(), &srv, cpu);
}

void ShadowMap::Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
{
    list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mResource.Get(), before, after));
}

D3D12_CPU_DESCRIPTOR_HANDLE ShadowMap::Dsv(UINT cascade) const
{
    auto handle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(cascade) * mDsvSize;
    return handle;
}

D3D12_VIEWPORT ShadowMap::Viewport() const { return { 0.0f, 0.0f, static_cast<float>(Resolution), static_cast<float>(Resolution), 0.0f, 1.0f }; }
D3D12_RECT ShadowMap::ScissorRect() const { return { 0, 0, static_cast<LONG>(Resolution), static_cast<LONG>(Resolution) }; }
