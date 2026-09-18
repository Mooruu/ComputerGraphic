#pragma once
#include "Common/d3dUtil.h"
#include "Common/UploadBuffer.h"

struct ParticleRenderConstants
{
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 CameraRight = { 1, 0, 0 }; float DeltaTime = 0;
    DirectX::XMFLOAT3 CameraUp = { 0, 1, 0 }; float TotalTime = 0;
};

class ParticleSystem
{
public:
    static constexpr UINT MaxParticles = 2048;
    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* list);
    void BuildPipelineStates(ID3D12Device* device, ID3DBlob* computeShader, ID3DBlob* vs, ID3DBlob* gs, ID3DBlob* ps, DXGI_FORMAT targetFormat, DXGI_FORMAT depthFormat);
    void Update(ID3D12GraphicsCommandList* list, float deltaTime, float totalTime);
    void Draw(ID3D12GraphicsCommandList* list, const ParticleRenderConstants& constants, D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);

private:
    struct Particle { DirectX::XMFLOAT3 Position; float Age; DirectX::XMFLOAT3 Velocity; float Lifetime; DirectX::XMFLOAT4 Color; float Size; DirectX::XMFLOAT3 Padding; };
    Microsoft::WRL::ComPtr<ID3D12Resource> mBuffers[2], mCounters[2], mResetUpload;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mComputeRoot, mRenderRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mComputePSO, mRenderPSO;
    std::unique_ptr<UploadBuffer<ParticleRenderConstants>> mConstants;
    UINT mInput = 0;
    ID3D12Device* mDevice = nullptr;
    UINT mDescriptorSize = 0;
    bool mInputReadable = false;
    void CreateViews(ID3D12Device* device);
};
