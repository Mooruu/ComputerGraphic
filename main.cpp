#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/Camera.h"
#include "Common/DDSTextureLoader.h"
#include "Common/GeometryGenerator.h"
#include "Culling.h"
#include "ShadowMap.h"
#include "GBuffer.h"
#include "Model.h"
#include "RenderingSystem.h"

#include <wincodec.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cfloat>
#include <fstream>
#include <random>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4 CameraPosition = { 0, 0, 0, 1 };
};

struct MaterialRootConstants
{
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 SpecularAlbedo = { 1.0f, 1.0f, 1.0f, 32.0f };
    DirectX::XMFLOAT4 AmbientColor = { 0.1f, 0.1f, 0.1f, 0.0f };
    DirectX::XMFLOAT4 EmissiveColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    // xy = tiling, zw = animated UV offset.
    DirectX::XMFLOAT4 UVTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
    // x = has diffuse texture, y = has specular texture, z = has normal texture.
    DirectX::XMFLOAT4 Flags = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct InstanceData
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct ShadowPassConstants
{
    DirectX::XMFLOAT4X4 LightViewProj = MathHelper::Identity4x4();
};

struct CascadedShadowConstants
{
    DirectX::XMFLOAT4X4 ShadowTransform[ShadowMap::CascadeCount];
    DirectX::XMFLOAT4 CascadeSplits = { 0, 0, 0, 0 };
};

namespace
{
    std::wstring FindExistingFileW(const std::vector<std::wstring>& candidates)
    {
        for (const auto& path : candidates)
        {
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                return path;
        }

        return candidates.empty() ? std::wstring() : candidates.front();
    }

    std::string FindExistingFileA(const std::vector<std::string>& candidates)
    {
        for (const auto& path : candidates)
        {
            std::ifstream fin(path, std::ios::binary);
            if (fin.good())
                return path;
        }

        return candidates.empty() ? std::string() : candidates.front();
    }

    void CreateTextureFromRGBA8(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::uint8_t* pixels,
        UINT width,
        UINT height,
        Texture& texture)
    {
        const auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            width,
            height,
            1,
            1);

        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(texture.Resource.GetAddressOf())));

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Resource.Get(), 0, 1);

        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(texture.UploadHeap.GetAddressOf())));

        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = pixels;
        textureData.RowPitch = static_cast<LONG_PTR>(width) * 4;
        textureData.SlicePitch = textureData.RowPitch * height;

        UpdateSubresources(cmdList, texture.Resource.Get(), texture.UploadHeap.Get(), 0, 0, 1, &textureData);
        cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }

    void CreateWhiteTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, Texture& texture)
    {
        const std::array<std::uint8_t, 4> whitePixel = { 255, 255, 255, 255 };
        CreateTextureFromRGBA8(device, cmdList, whitePixel.data(), 1, 1, texture);
    }

    bool LoadWICTextureFromFile(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& filename,
        Texture& texture)
    {
        ComPtr<IWICImagingFactory> wicFactory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wicFactory.GetAddressOf()));

        if (FAILED(hr))
            return false;

        ComPtr<IWICBitmapDecoder> decoder;
        hr = wicFactory->CreateDecoderFromFilename(
            filename.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            decoder.GetAddressOf());

        if (FAILED(hr))
            return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr))
            return false;

        UINT width = 0;
        UINT height = 0;
        frame->GetSize(&width, &height);

        WICPixelFormatGUID pixelFormat;
        frame->GetPixelFormat(&pixelFormat);

        ComPtr<IWICBitmapSource> bitmapSource;
        ComPtr<IWICFormatConverter> converter;

        if (IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppRGBA))
        {
            hr = frame.As(&bitmapSource);
            if (FAILED(hr))
                return false;
        }
        else
        {
            hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
            if (FAILED(hr))
                return false;

            hr = converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0f,
                WICBitmapPaletteTypeCustom);

            if (FAILED(hr))
                return false;

            hr = converter.As(&bitmapSource);
            if (FAILED(hr))
                return false;
        }

        std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * height * 4);
        hr = bitmapSource->CopyPixels(
            nullptr,
            width * 4,
            static_cast<UINT>(pixels.size()),
            pixels.data());

        if (FAILED(hr))
            return false;

        CreateTextureFromRGBA8(device, cmdList, pixels.data(), width, height, texture);
        texture.Filename = filename;
        return true;
    }

    bool LoadDDSTextureCustom(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& filename,
        Texture& texture)
    {
        HRESULT hr = DirectX::CreateDDSTextureFromFile12(
            device,
            cmdList,
            filename.c_str(),
            texture.Resource,
            texture.UploadHeap);

        if (FAILED(hr))
            return false;

        texture.Filename = filename;
        return true;
    }

    bool LoadTGATextureFromFile(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& filename,
        Texture& texture)
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
            return false;

        // TGA Header
        std::uint8_t header[18];
        file.read(reinterpret_cast<char*>(header), 18);

        if (!file.good())
            return false;

        const UINT width = header[12] | (header[13] << 8);
        const UINT height = header[14] | (header[15] << 8);
        const std::uint8_t bpp = header[16];
        const std::uint8_t imageType = header[2];

        if (width == 0 || height == 0 || (bpp != 24 && bpp != 32))
            return false;

        const UINT bytesPerPixel = bpp / 8;
        const size_t imageSize = static_cast<size_t>(width) * height * bytesPerPixel;
        std::vector<std::uint8_t> imageData(imageSize);

        if (imageType == 2) // Uncompressed RGB/RGBA
        {
            file.read(reinterpret_cast<char*>(imageData.data()), imageSize);
        }
        else if (imageType == 10) // RLE compressed RGB/RGBA
        {
            size_t currentByte = 0;
            size_t currentPixel = 0;
            const size_t pixelCount = width * height;

            while (currentPixel < pixelCount)
            {
                std::uint8_t chunkHeader = 0;
                file.read(reinterpret_cast<char*>(&chunkHeader), 1);

                if (chunkHeader < 128) // Raw chunk
                {
                    chunkHeader++;
                    for (int i = 0; i < chunkHeader; i++)
                    {
                        file.read(reinterpret_cast<char*>(&imageData[currentByte]), bytesPerPixel);
                        currentByte += bytesPerPixel;
                        currentPixel++;
                        if (currentPixel >= pixelCount)
                            break;
                    }
                }
                else // RLE chunk
                {
                    chunkHeader -= 127;
                    std::vector<std::uint8_t> colorBuffer(bytesPerPixel);
                    file.read(reinterpret_cast<char*>(colorBuffer.data()), bytesPerPixel);

                    for (int i = 0; i < chunkHeader; i++)
                    {
                        std::memcpy(&imageData[currentByte], colorBuffer.data(), bytesPerPixel);
                        currentByte += bytesPerPixel;
                        currentPixel++;
                        if (currentPixel >= pixelCount)
                            break;
                    }
                }
            }
        }
        else
        {
            return false;
        }

        if (!file.good())
            return false;

        // Convert BGR(A) to RGBA
        std::vector<std::uint8_t> rgba(static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < width * height; ++i)
        {
            if (bpp == 32)
            {
                rgba[i * 4 + 0] = imageData[i * 4 + 2]; // R
                rgba[i * 4 + 1] = imageData[i * 4 + 1]; // G
                rgba[i * 4 + 2] = imageData[i * 4 + 0]; // B
                rgba[i * 4 + 3] = imageData[i * 4 + 3]; // A
            }
            else // 24 bpp
            {
                rgba[i * 4 + 0] = imageData[i * 3 + 2]; // R
                rgba[i * 4 + 1] = imageData[i * 3 + 1]; // G
                rgba[i * 4 + 2] = imageData[i * 3 + 0]; // B
                rgba[i * 4 + 3] = 255;                  // A
            }
        }

        // Flip vertically if needed (TGA is usually bottom-up)
        const bool isBottomUp = (header[17] & 0x20) == 0;
        if (isBottomUp)
        {
            std::vector<std::uint8_t> flipped(rgba.size());
            const size_t rowSize = width * 4;
            for (UINT y = 0; y < height; ++y)
            {
                std::memcpy(
                    &flipped[y * rowSize],
                    &rgba[(height - 1 - y) * rowSize],
                    rowSize);
            }
            rgba = std::move(flipped);
        }

        CreateTextureFromRGBA8(device, cmdList, rgba.data(), width, height, texture);
        texture.Filename = filename;
        return true;
    }

    bool LoadTextureFromFile(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        const std::wstring& filename,
        Texture& texture)
    {
        std::wstring extension = filename.substr(filename.find_last_of(L".") + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);

        if (extension == L"dds")
        {
            return LoadDDSTextureCustom(device, cmdList, filename, texture);
        }
        else if (extension == L"tga")
        {
            return LoadTGATextureFromFile(device, cmdList, filename, texture);
        }
        else
        {
            return LoadWICTextureFromFile(device, cmdList, filename, texture);
        }
    }
}

class SponzaApp : public D3DApp
{
public:
    SponzaApp(HINSTANCE hInstance);
    SponzaApp(const SponzaApp& rhs) = delete;
    SponzaApp& operator=(const SponzaApp& rhs) = delete;
    ~SponzaApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
    void OnKeyboardInput(const GameTimer& gt);

    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildModel();
    void BuildTextures();
    void BuildGBuffer();
    void BuildShadowMap();
    void BuildPSO();
    void BuildInstancedScene();
    void UpdateVisibleInstances();
    void UpdateCullingMode();
    void UpdateShadowTransforms();
    void DrawShadowMap();

private:
    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;
    UINT mCbvSrvUavDescriptorSize = 0;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<DeferredLightConstants>> mLightCB = nullptr;
    std::unique_ptr<UploadBuffer<InstanceData>> mInstanceBuffer = nullptr;
    std::unique_ptr<UploadBuffer<ShadowPassConstants>> mShadowPassCB = nullptr;
    std::unique_ptr<Model> mSponza = nullptr;
    std::unique_ptr<MeshGeometry> mSponzaGeo = nullptr;
    std::vector<Texture> mTextures;
    std::unique_ptr<MeshGeometry> mInstanceGeo = nullptr;
    std::vector<InstanceData> mInstances;
    std::vector<SceneBounds> mInstanceBounds;
    std::vector<UINT> mVisibleInstanceIndices;
    Octree mInstanceOctree;
    UINT mVisibleInstanceCount = 0;
    bool mFrustumCullingEnabled = true;
    bool mUseOctree = true;
    GBuffer mGBuffer;
    ShadowMap mShadowMap;
    RenderingSystem mRenderingSystem;
    UINT mGBufferSrvStart = 0;
    UINT mShadowSrvStart = 0;
    bool mDeferredReady = false;

    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;
    ComPtr<ID3DBlob> mGBufferVS = nullptr;
    ComPtr<ID3DBlob> mGBufferPS = nullptr;
    ComPtr<ID3DBlob> mGBufferHS = nullptr;
    ComPtr<ID3DBlob> mGBufferDS = nullptr;
    ComPtr<ID3DBlob> mLightingVS = nullptr;
    ComPtr<ID3DBlob> mLightingPS = nullptr;
    ComPtr<ID3DBlob> mInstanceVS = nullptr;
    ComPtr<ID3DBlob> mInstancePS = nullptr;
    ComPtr<ID3DBlob> mShadowVS = nullptr;
    ComPtr<ID3DBlob> mShadowInstanceVS = nullptr;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInstanceInputLayout;
    ComPtr<ID3D12PipelineState> mPSO = nullptr;
    ComPtr<ID3D12PipelineState> mLightingPSO = nullptr;
    ComPtr<ID3D12PipelineState> mInstancePSO = nullptr;
    ComPtr<ID3D12PipelineState> mShadowPSO = nullptr;
    ComPtr<ID3D12PipelineState> mShadowInstancePSO = nullptr;

    CascadedShadowConstants mCascadedShadowConstants;

    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();

    Camera mCamera;

    float mTheta = 1.3f * XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 20.0f;
    POINT mLastMousePos;

    void ConfigureMaterialAnimations();
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        SponzaApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;
        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

SponzaApp::SponzaApp(HINSTANCE hInstance) : D3DApp(hInstance)
{
    mMainWndCaption = L"Sponza Deferred - WASD move, Q/E up/down, mouse look, F1/F2/F3 culling modes";
    mClientWidth = 1280;
    mClientHeight = 720;

    mCamera.SetPosition(0.0f, 5.0f, -20.0f);
    mCamera.LookAt(
        XMFLOAT3(0.0f, 5.0f, -20.0f),
        XMFLOAT3(0.0f, 3.0f, 0.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f)
    );
    mCamera.SetLens(0.25f * MathHelper::Pi, 1.0f, 1.0f, 100000.0f);
}

SponzaApp::~SponzaApp() {}

bool SponzaApp::Initialize()
{
    HRESULT coHr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE)
        ThrowIfFailed(coHr);

    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildModel();
    BuildInstancedScene();
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildTextures();
    BuildGBuffer();
    BuildShadowMap();
    BuildPSO();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    FlushCommandQueue();

    return true;
}

void SponzaApp::OnResize()
{
    D3DApp::OnResize();
    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 100000.0f);
    if (mDeferredReady)
        BuildGBuffer();
}

void SponzaApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
    mCamera.UpdateViewMatrix();
    UpdateCullingMode();
    UpdateVisibleInstances();
    UpdateShadowTransforms();

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
    const auto cameraPosition = mCamera.GetPosition3f();
    objConstants.CameraPosition = XMFLOAT4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);
    mObjectCB->CopyData(0, objConstants);
}

void SponzaApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    DrawShadowMap();
    mCommandList->SetPipelineState(mPSO.Get());
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mGBuffer.Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRtvs[GBuffer::TargetCount] = { mGBuffer.Rtv(0), mGBuffer.Rtv(1), mGBuffer.Rtv(2) };
    for (UINT i = 0; i < GBuffer::TargetCount; ++i)
        mCommandList->ClearRenderTargetView(gbufferRtvs[i], Colors::Black, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    mCommandList->OMSetRenderTargets(GBuffer::TargetCount, gbufferRtvs, false, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    if (mSponzaGeo && mSponza)
    {
        mCommandList->IASetVertexBuffers(0, 1, &mSponzaGeo->VertexBufferView());
        mCommandList->IASetIndexBuffer(&mSponzaGeo->IndexBufferView());
        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

        const auto& meshes = mSponza->GetMeshes();
        const auto& materials = mSponza->GetMaterials();
        const float time = gt.TotalTime();

        for (size_t i = 0; i < meshes.size(); ++i)
        {
            const auto& mesh = meshes[i];
            const UINT materialIndex = (mesh.MaterialIndex < materials.size()) ? mesh.MaterialIndex : 0;
            const auto& material = materials[materialIndex];

            MaterialRootConstants materialConstants;
            materialConstants.DiffuseAlbedo = material.DiffuseAlbedo;
            materialConstants.SpecularAlbedo = XMFLOAT4(
                material.SpecularAlbedo.x,
                material.SpecularAlbedo.y,
                material.SpecularAlbedo.z,
                material.Shininess);
            materialConstants.AmbientColor = XMFLOAT4(
                material.AmbientColor.x,
                material.AmbientColor.y,
                material.AmbientColor.z,
                0.0f);
            materialConstants.EmissiveColor = XMFLOAT4(
                material.EmissiveColor.x,
                material.EmissiveColor.y,
                material.EmissiveColor.z,
                0.0f);
            materialConstants.UVTilingOffset = XMFLOAT4(
                material.TextureTiling.x,
                material.TextureTiling.y,
                time * material.TextureScrollSpeed.x,
                time * material.TextureScrollSpeed.y);
            materialConstants.Flags = XMFLOAT4(
                material.HasTexture ? 1.0f : 0.0f,
                material.HasSpecularTexture ? 1.0f : 0.0f,
                material.HasNormalTexture ? 1.0f : 0.0f,
                material.HasBumpTexture ? 1.0f : 0.0f);

            mCommandList->SetGraphicsRoot32BitConstants(
                2,
                sizeof(MaterialRootConstants) / sizeof(UINT32),
                &materialConstants,
                0);

            CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
            textureHandle.Offset(1 + materialIndex * 3, mCbvSrvUavDescriptorSize);
            mCommandList->SetGraphicsRootDescriptorTable(1, textureHandle);

            auto submeshIt = mSponzaGeo->DrawArgs.find(std::to_string(i));
            if (submeshIt == mSponzaGeo->DrawArgs.end())
                continue;

            const auto& submesh = submeshIt->second;
            mCommandList->DrawIndexedInstanced(
                submesh.IndexCount,
                1,
                submesh.StartIndexLocation,
                submesh.BaseVertexLocation,
                0);
        }
    }

    if (mInstanceGeo && mInstanceBuffer && mVisibleInstanceCount > 0)
    {
        mCommandList->SetPipelineState(mInstancePSO.Get());
        const D3D12_VERTEX_BUFFER_VIEW instanceBufferView = {
            mInstanceBuffer->Resource()->GetGPUVirtualAddress(),
            mVisibleInstanceCount * static_cast<UINT>(sizeof(InstanceData)),
            static_cast<UINT>(sizeof(InstanceData)) };
        const D3D12_VERTEX_BUFFER_VIEW vertexViews[] = {
            mInstanceGeo->VertexBufferView(), instanceBufferView };
        mCommandList->IASetVertexBuffers(0, _countof(vertexViews), vertexViews);
        mCommandList->IASetIndexBuffer(&mInstanceGeo->IndexBufferView());
        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        MaterialRootConstants instanceMaterial;
        instanceMaterial.DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        instanceMaterial.SpecularAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 24.0f);
        mCommandList->SetGraphicsRoot32BitConstants(
            2, sizeof(MaterialRootConstants) / sizeof(UINT32), &instanceMaterial, 0);

        const auto& cube = mInstanceGeo->DrawArgs.at("cube");
        mCommandList->DrawIndexedInstanced(
            cube.IndexCount, mVisibleInstanceCount, cube.StartIndexLocation, cube.BaseVertexLocation, 0);
    }

    mGBuffer.Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, nullptr);
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);
    mCommandList->SetPipelineState(mLightingPSO.Get());
    mCommandList->SetGraphicsRootDescriptorTable(3, mGBuffer.SrvTable());
    mCommandList->SetGraphicsRootDescriptorTable(5, mShadowMap.Srv());
    auto lights = mRenderingSystem.BuildLightConstants(mCamera.GetPosition3f());
    for (UINT i = 0; i < ShadowMap::CascadeCount; ++i)
        lights.ShadowTransform[i] = mCascadedShadowConstants.ShadowTransform[i];
    lights.CascadeSplits = mCascadedShadowConstants.CascadeSplits;
    mLightCB->CopyData(0, lights);
    mCommandList->SetGraphicsRootConstantBufferView(4, mLightCB->Resource()->GetGPUVirtualAddress());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->DrawInstanced(3, 1, 0, 0);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
    FlushCommandQueue();
}

void SponzaApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void SponzaApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void SponzaApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.5f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.5f * static_cast<float>(y - mLastMousePos.y));
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void SponzaApp::OnKeyboardInput(const GameTimer& gt)
{
    const float speed = (GetAsyncKeyState(VK_SHIFT) & 0x8000 ? 80.0f : 30.0f) * gt.DeltaTime();
    if (GetAsyncKeyState('W') & 0x8000) mCamera.Walk(speed);
    if (GetAsyncKeyState('S') & 0x8000) mCamera.Walk(-speed);
    if (GetAsyncKeyState('A') & 0x8000) mCamera.Strafe(-speed);
    if (GetAsyncKeyState('D') & 0x8000) mCamera.Strafe(speed);
    if (GetAsyncKeyState('Q') & 0x8000 || GetAsyncKeyState('E') & 0x8000)
    {
        auto position = mCamera.GetPosition3f();
        position.y += (GetAsyncKeyState('E') & 0x8000 ? speed : -speed);
        mCamera.SetPosition(position);
    }

    if (!mSponza) return;

    auto& materials = const_cast<std::vector<ModelMaterial>&>(mSponza->GetMaterials());

    if (GetAsyncKeyState('1') & 0x8000)
    {
        for (auto& mat : materials)
        {
            mat.TextureTiling.x += 0.5f * gt.DeltaTime();
            mat.TextureTiling.y += 0.5f * gt.DeltaTime();
            mat.TextureTiling.x = MathHelper::Clamp(mat.TextureTiling.x, 0.1f, 20.0f);
            mat.TextureTiling.y = MathHelper::Clamp(mat.TextureTiling.y, 0.1f, 20.0f);
        }
    }

    if (GetAsyncKeyState('2') & 0x8000)
    {
        for (auto& mat : materials)
        {
            mat.TextureTiling.x -= 0.5f * gt.DeltaTime();
            mat.TextureTiling.y -= 0.5f * gt.DeltaTime();
            mat.TextureTiling.x = MathHelper::Clamp(mat.TextureTiling.x, 0.1f, 20.0f);
            mat.TextureTiling.y = MathHelper::Clamp(mat.TextureTiling.y, 0.1f, 20.0f);
        }
    }

    if (GetAsyncKeyState('3') & 0x8000)
    {
        for (auto& mat : materials)
        {
            mat.TextureScrollSpeed.x += 0.01f * gt.DeltaTime();
            mat.TextureScrollSpeed.x = MathHelper::Clamp(mat.TextureScrollSpeed.x, -0.5f, 0.5f);
        }
    }

    if (GetAsyncKeyState('4') & 0x8000)
    {
        for (auto& mat : materials)
        {
            mat.TextureScrollSpeed.x -= 0.01f * gt.DeltaTime();
            mat.TextureScrollSpeed.x = MathHelper::Clamp(mat.TextureScrollSpeed.x, -0.5f, 0.5f);
        }
    }

    if (GetAsyncKeyState('5') & 0x8000)
    {
        for (auto& mat : materials)
        {
            mat.TextureScrollSpeed.y += 0.01f * gt.DeltaTime();
            mat.TextureScrollSpeed.y = MathHelper::Clamp(mat.TextureScrollSpeed.y, -0.5f, 0.5f);
        }
    }

    if (GetAsyncKeyState('6') & 0x8000)
    {
        for (auto& mat : materials)
        {
            mat.TextureScrollSpeed.y -= 0.01f * gt.DeltaTime();
            mat.TextureScrollSpeed.y = MathHelper::Clamp(mat.TextureScrollSpeed.y, -0.5f, 0.5f);
        }
    }

    if (GetAsyncKeyState('0') & 0x8000)
    {
        ConfigureMaterialAnimations();
    }
}

void SponzaApp::BuildDescriptorHeaps()
{
    const UINT materialCount = (mSponza && !mSponza->GetMaterials().empty())
        ? static_cast<UINT>(mSponza->GetMaterials().size())
        : 1;

    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
    cbvSrvHeapDesc.NumDescriptors = 1 + materialCount * 3 + GBuffer::TargetCount + 1;
    cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvSrvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));

    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void SponzaApp::BuildConstantBuffers()
{
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);
    mLightCB = std::make_unique<UploadBuffer<DeferredLightConstants>>(md3dDevice.Get(), 1, true);
    mShadowPassCB = std::make_unique<UploadBuffer<ShadowPassConstants>>(md3dDevice.Get(), ShadowMap::CascadeCount, true);
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    md3dDevice->CreateConstantBufferView(&cbvDesc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void SponzaApp::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[7];

    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
    // Displacement is sampled by the domain shader, while diffuse and normal
    // maps are sampled by the pixel shader.  The table must therefore be
    // visible to every shader stage that uses it.
    slotRootParameter[1].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_ALL);

    slotRootParameter[2].InitAsConstants(
        sizeof(MaterialRootConstants) / sizeof(UINT32),
        1,
        0,
        D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_DESCRIPTOR_RANGE gbufferTable;
    gbufferTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBuffer::TargetCount, 3);
    slotRootParameter[3].InitAsDescriptorTable(1, &gbufferTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[4].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_DESCRIPTOR_RANGE shadowTable;
    shadowTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
    slotRootParameter[5].InitAsDescriptorTable(1, &shadowTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[6].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_STATIC_SAMPLER_DESC linearWrapSampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_STATIC_SAMPLER_DESC shadowSampler(
        1,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f,
        16,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

    CD3DX12_STATIC_SAMPLER_DESC samplers[] = { linearWrapSampler, shadowSampler };
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        7,
        slotRootParameter,
        _countof(samplers),
        samplers,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}

void SponzaApp::BuildShadersAndInputLayout()
{
    std::wstring shaderPath = FindExistingFileW({
        L"Shaders\\sponza.hlsl",
        L"..\\Shaders\\sponza.hlsl",
        L"..\\..\\Shaders\\sponza.hlsl",
        L"sponza.hlsl"
    });

    OutputDebugString(L"Compiling VS with entrypoint 'VS'...\n");
    mvsByteCode = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");

    OutputDebugString(L"Compiling PS with entrypoint 'PS'...\n");
    mpsByteCode = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");
    const std::wstring gbufferPath = FindExistingFileW({
        L"Shaders\\gbuffer.hlsl",
        L"..\\Shaders\\gbuffer.hlsl",
        L"..\\..\\Shaders\\gbuffer.hlsl",
        L"gbuffer.hlsl"
    });
    const std::wstring lightingPath = FindExistingFileW({
        L"Shaders\\deferred_lighting.hlsl",
        L"..\\Shaders\\deferred_lighting.hlsl",
        L"..\\..\\Shaders\\deferred_lighting.hlsl",
        L"deferred_lighting.hlsl"
    });
    const std::wstring instancePath = FindExistingFileW({
        L"Shaders\\instances.hlsl",
        L"..\\Shaders\\instances.hlsl",
        L"..\\..\\Shaders\\instances.hlsl",
        L"instances.hlsl"
    });
    const std::wstring shadowPath = FindExistingFileW({ L"Shaders\\shadow.hlsl", L"..\\Shaders\\shadow.hlsl", L"..\\..\\Shaders\\shadow.hlsl", L"shadow.hlsl" });
    const std::wstring shadowInstancePath = FindExistingFileW({ L"Shaders\\shadow_instances.hlsl", L"..\\Shaders\\shadow_instances.hlsl", L"..\\..\\Shaders\\shadow_instances.hlsl", L"shadow_instances.hlsl" });

    mGBufferVS = d3dUtil::CompileShader(gbufferPath, nullptr, "VS", "vs_5_0");
    mGBufferPS = d3dUtil::CompileShader(gbufferPath, nullptr, "PS", "ps_5_0");
    mGBufferHS = d3dUtil::CompileShader(gbufferPath, nullptr, "HS", "hs_5_0");
    mGBufferDS = d3dUtil::CompileShader(gbufferPath, nullptr, "DS", "ds_5_0");
    mLightingVS = d3dUtil::CompileShader(lightingPath, nullptr, "VS", "vs_5_0");
    mLightingPS = d3dUtil::CompileShader(lightingPath, nullptr, "PS", "ps_5_0");
    mInstanceVS = d3dUtil::CompileShader(instancePath, nullptr, "VS", "vs_5_0");
    mInstancePS = d3dUtil::CompileShader(instancePath, nullptr, "PS", "ps_5_0");
    mShadowVS = d3dUtil::CompileShader(shadowPath, nullptr, "VS", "vs_5_0");
    mShadowInstanceVS = d3dUtil::CompileShader(shadowInstancePath, nullptr, "VS", "vs_5_0");

    OutputDebugString(L"Shaders compiled successfully!\n");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    mInstanceInputLayout = mInputLayout;
    mInstanceInputLayout.push_back({ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 });
    mInstanceInputLayout.push_back({ "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 });
    mInstanceInputLayout.push_back({ "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 });
    mInstanceInputLayout.push_back({ "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 });
    mInstanceInputLayout.push_back({ "TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 });
}

void SponzaApp::BuildModel()
{
    mSponza = std::make_unique<Model>();

    std::string modelPath = FindExistingFileA({
        "Models/sponza.obj",
        "../Models/sponza.obj",
        "../../Models/sponza.obj"
    });

    if (!mSponza->LoadFromOBJ(modelPath))
    {
        MessageBox(0, L"Failed to load Sponza model!\nCheck if model exists in Models/sponza.obj", L"Error", MB_OK);
        return;
    }

    ConfigureMaterialAnimations();

    mSponza->CreateBuffers(md3dDevice.Get(), mCommandList.Get());
    mSponzaGeo = mSponza->GetMeshGeometry();

    OutputDebugString(L"Model loaded successfully!\n");
}

void SponzaApp::BuildInstancedScene()
{
    constexpr UINT gridSize = 64;
    constexpr UINT instanceCount = gridSize * gridSize;

    GeometryGenerator generator;
    const auto cube = generator.CreateBox(1.5f, 1.5f, 1.5f, 0);
    std::vector<ModelVertex> vertices;
    vertices.reserve(cube.Vertices.size());
    for (const auto& vertex : cube.Vertices)
        vertices.push_back({ vertex.Position, vertex.Normal, vertex.TexC, vertex.TangentU });

    mInstanceGeo = std::make_unique<MeshGeometry>();
    mInstanceGeo->Name = "cullingInstanceCube";
    const UINT vertexBytes = static_cast<UINT>(vertices.size() * sizeof(ModelVertex));
    const UINT indexBytes = static_cast<UINT>(cube.Indices32.size() * sizeof(std::uint32_t));
    mInstanceGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        vertices.data(), vertexBytes, mInstanceGeo->VertexBufferUploader);
    mInstanceGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        cube.Indices32.data(), indexBytes, mInstanceGeo->IndexBufferUploader);
    mInstanceGeo->VertexByteStride = sizeof(ModelVertex);
    mInstanceGeo->VertexBufferByteSize = vertexBytes;
    mInstanceGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
    mInstanceGeo->IndexBufferByteSize = indexBytes;
    mInstanceGeo->DrawArgs["cube"] = { static_cast<UINT>(cube.Indices32.size()), 0, 0 };

    mInstances.resize(instanceCount);
    mInstanceBounds.resize(instanceCount);
    std::mt19937 random(20260918);
    std::uniform_real_distribution<float> color(0.35f, 1.0f);
    std::uniform_real_distribution<float> height(0.7f, 2.3f);

    for (UINT z = 0; z < gridSize; ++z)
    {
        for (UINT x = 0; x < gridSize; ++x)
        {
            const UINT index = z * gridSize + x;
            const float worldX = (static_cast<float>(x) - 31.5f) * 5.0f;
            const float worldZ = (static_cast<float>(z) - 31.5f) * 5.0f;
            const float scaleY = height(random);
            XMMATRIX world = XMMatrixScaling(1.0f, scaleY, 1.0f) * XMMatrixTranslation(worldX, 0.75f * scaleY, worldZ);
            XMStoreFloat4x4(&mInstances[index].World, world);
            mInstances[index].Color = XMFLOAT4(color(random), color(random), color(random), 1.0f);
            mInstanceBounds[index].Center = XMFLOAT3(worldX, 0.75f * scaleY, worldZ);
            mInstanceBounds[index].Extents = XMFLOAT3(0.75f, 0.75f * scaleY, 0.75f);
        }
    }

    mInstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(md3dDevice.Get(), instanceCount, false);
    mInstanceOctree.Build(mInstanceBounds, { XMFLOAT3(0.0f, 4.0f, 0.0f), XMFLOAT3(170.0f, 12.0f, 170.0f) });
}

void SponzaApp::UpdateCullingMode()
{
    if (GetAsyncKeyState(VK_F1) & 0x1)
    {
        mFrustumCullingEnabled = false;
        SetWindowText(mhMainWnd, L"Sponza Deferred - F1: culling OFF");
    }
    if (GetAsyncKeyState(VK_F2) & 0x1)
    {
        mFrustumCullingEnabled = true;
        mUseOctree = false;
        SetWindowText(mhMainWnd, L"Sponza Deferred - F2: direct frustum culling");
    }
    if (GetAsyncKeyState(VK_F3) & 0x1)
    {
        mFrustumCullingEnabled = true;
        mUseOctree = true;
        SetWindowText(mhMainWnd, L"Sponza Deferred - F3: octree frustum culling");
    }
}

void SponzaApp::UpdateVisibleInstances()
{
    if (!mInstanceBuffer)
        return;

    mVisibleInstanceIndices.clear();
    if (!mFrustumCullingEnabled)
    {
        mVisibleInstanceIndices.resize(static_cast<UINT>(mInstances.size()));
        for (UINT i = 0; i < mVisibleInstanceIndices.size(); ++i)
            mVisibleInstanceIndices[i] = i;
    }
    else if (mUseOctree)
    {
        mInstanceOctree.QueryVisible(mCamera.GetView(), mCamera.GetProj(), mVisibleInstanceIndices);
    }
    else
    {
        for (UINT i = 0; i < mInstanceBounds.size(); ++i)
        {
            if (FrustumCuller::IsVisible(mInstanceBounds[i], mCamera.GetView(), mCamera.GetProj()))
                mVisibleInstanceIndices.push_back(i);
        }
    }

    mVisibleInstanceCount = static_cast<UINT>(mVisibleInstanceIndices.size());
    for (UINT i = 0; i < mVisibleInstanceCount; ++i)
        mInstanceBuffer->CopyData(i, mInstances[mVisibleInstanceIndices[i]]);
}

void SponzaApp::UpdateShadowTransforms()
{
    constexpr float shadowNear = 1.0f;
    constexpr float shadowFar = 180.0f;
    constexpr float lambda = 0.78f;
    float cascadeNear = shadowNear;
    const XMFLOAT3 cameraPosition = mCamera.GetPosition3f();
    const XMVECTOR position = XMLoadFloat3(&cameraPosition);
    const XMVECTOR look = XMVector3Normalize(mCamera.GetLook());
    const XMVECTOR right = XMVector3Normalize(mCamera.GetRight());
    const XMVECTOR up = XMVector3Normalize(mCamera.GetUp());
    const XMVECTOR lightDirection = XMVector3Normalize(XMVectorSet(-0.35f, -0.8f, 0.25f, 0.0f));
    const XMMATRIX textureTransform = XMMatrixScaling(0.5f, -0.5f, 1.0f) * XMMatrixTranslation(0.5f, 0.5f, 0.0f);

    for (UINT cascade = 0; cascade < ShadowMap::CascadeCount; ++cascade)
    {
        const float p = static_cast<float>(cascade + 1) / ShadowMap::CascadeCount;
        const float logarithmic = shadowNear * powf(shadowFar / shadowNear, p);
        const float uniform = shadowNear + (shadowFar - shadowNear) * p;
        const float cascadeFar = lambda * logarithmic + (1.0f - lambda) * uniform;

        XMVECTOR corners[8];
        UINT cornerIndex = 0;
        for (float distance : { cascadeNear, cascadeFar })
        {
            const float halfHeight = tanf(0.5f * mCamera.GetFovY()) * distance;
            const float halfWidth = halfHeight * mCamera.GetAspect();
            const XMVECTOR center = position + look * distance;
            corners[cornerIndex++] = center - right * halfWidth - up * halfHeight;
            corners[cornerIndex++] = center - right * halfWidth + up * halfHeight;
            corners[cornerIndex++] = center + right * halfWidth - up * halfHeight;
            corners[cornerIndex++] = center + right * halfWidth + up * halfHeight;
        }

        XMVECTOR center = XMVectorZero();
        for (const auto& corner : corners) center += corner;
        center /= 8.0f;
        const XMMATRIX lightView = XMMatrixLookAtLH(center - lightDirection * 220.0f, center, XMVectorSet(0, 1, 0, 0));
        XMFLOAT3 minPoint(FLT_MAX, FLT_MAX, FLT_MAX);
        XMFLOAT3 maxPoint(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const auto& corner : corners)
        {
            XMFLOAT3 point;
            XMStoreFloat3(&point, XMVector3TransformCoord(corner, lightView));
            minPoint.x = (std::min)(minPoint.x, point.x); minPoint.y = (std::min)(minPoint.y, point.y); minPoint.z = (std::min)(minPoint.z, point.z);
            maxPoint.x = (std::max)(maxPoint.x, point.x); maxPoint.y = (std::max)(maxPoint.y, point.y); maxPoint.z = (std::max)(maxPoint.z, point.z);
        }
        const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(minPoint.x, maxPoint.x, minPoint.y, maxPoint.y,
            minPoint.z - 80.0f, maxPoint.z + 80.0f);
        const XMMATRIX lightViewProj = lightView * lightProj;
        XMStoreFloat4x4(&mCascadedShadowConstants.ShadowTransform[cascade], XMMatrixTranspose(lightViewProj * textureTransform));
        ShadowPassConstants shadowPass;
        XMStoreFloat4x4(&shadowPass.LightViewProj, XMMatrixTranspose(lightViewProj));
        mShadowPassCB->CopyData(cascade, shadowPass);
        (&mCascadedShadowConstants.CascadeSplits.x)[cascade] = cascadeFar;
        cascadeNear = cascadeFar;
    }
}

void SponzaApp::DrawShadowMap()
{
    mShadowMap.Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    const D3D12_VIEWPORT viewport = mShadowMap.Viewport();
    const D3D12_RECT scissor = mShadowMap.ScissorRect();
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissor);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    for (UINT cascade = 0; cascade < ShadowMap::CascadeCount; ++cascade)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = mShadowMap.Dsv(cascade);
        mCommandList->OMSetRenderTargets(0, nullptr, false, &dsv);
        mCommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        mCommandList->SetGraphicsRootConstantBufferView(6,
            mShadowPassCB->Resource()->GetGPUVirtualAddress() + cascade * d3dUtil::CalcConstantBufferByteSize(sizeof(ShadowPassConstants)));

        if (mSponzaGeo && mSponza)
        {
            mCommandList->SetPipelineState(mShadowPSO.Get());
            mCommandList->IASetVertexBuffers(0, 1, &mSponzaGeo->VertexBufferView());
            mCommandList->IASetIndexBuffer(&mSponzaGeo->IndexBufferView());
            mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            for (size_t i = 0; i < mSponza->GetMeshes().size(); ++i)
            {
                const auto& submesh = mSponzaGeo->DrawArgs.at(std::to_string(i));
                mCommandList->DrawIndexedInstanced(submesh.IndexCount, 1, submesh.StartIndexLocation, submesh.BaseVertexLocation, 0);
            }
        }

        if (mInstanceGeo && mInstanceBuffer && mVisibleInstanceCount > 0)
        {
            mCommandList->SetPipelineState(mShadowInstancePSO.Get());
            const D3D12_VERTEX_BUFFER_VIEW instanceBufferView = { mInstanceBuffer->Resource()->GetGPUVirtualAddress(), mVisibleInstanceCount * static_cast<UINT>(sizeof(InstanceData)), static_cast<UINT>(sizeof(InstanceData)) };
            const D3D12_VERTEX_BUFFER_VIEW vertexViews[] = { mInstanceGeo->VertexBufferView(), instanceBufferView };
            mCommandList->IASetVertexBuffers(0, _countof(vertexViews), vertexViews);
            mCommandList->IASetIndexBuffer(&mInstanceGeo->IndexBufferView());
            mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            const auto& cube = mInstanceGeo->DrawArgs.at("cube");
            mCommandList->DrawIndexedInstanced(cube.IndexCount, mVisibleInstanceCount, cube.StartIndexLocation, cube.BaseVertexLocation, 0);
        }
    }
    mShadowMap.Transition(mCommandList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SponzaApp::BuildTextures()
{
    const auto& materials = mSponza->GetMaterials();
    mTextures.resize(materials.size() * 3);

    char debugBuffer[512];
    sprintf_s(debugBuffer, "=== LOADING %zu MATERIALS ===\n", materials.size());
    OutputDebugStringA(debugBuffer);

    for (size_t i = 0; i < materials.size(); ++i)
    {
        const auto& material = materials[i];
        Texture& texture = mTextures[i * 3];
        texture.Name = material.Name;
        texture.Filename = material.DiffuseTexture;

        sprintf_s(debugBuffer, "Material %zu: %s, HasTexture=%d\n",
            i, material.Name.c_str(), material.HasTexture ? 1 : 0);
        OutputDebugStringA(debugBuffer);

        bool textureLoaded = false;
        if (material.HasTexture && !material.DiffuseTexture.empty())
        {
            std::wstring message = L"Trying to load: " + material.DiffuseTexture + L"\n";
            OutputDebugStringW(message.c_str());

            // Check if file exists
            DWORD attrs = GetFileAttributesW(material.DiffuseTexture.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
            {
                message = L"FILE NOT FOUND: " + material.DiffuseTexture + L"\n";
                OutputDebugStringW(message.c_str());
            }
            else
            {
                OutputDebugStringW(L"File exists, loading...\n");
            }

            textureLoaded = LoadTextureFromFile(
                md3dDevice.Get(),
                mCommandList.Get(),
                material.DiffuseTexture,
                texture);

            if (!textureLoaded)
            {
                std::wstring message = L"❌ Texture load FAILED: " + material.DiffuseTexture + L"\n";
                OutputDebugStringW(message.c_str());
            }
            else
            {
                std::wstring message = L"✅ Texture loaded OK: " + material.DiffuseTexture + L"\n";
                OutputDebugStringW(message.c_str());
            }
        }
        else
        {
            OutputDebugStringA("No texture for this material, using white\n");
        }

        if (!textureLoaded)
            CreateWhiteTexture(md3dDevice.Get(), mCommandList.Get(), texture);

        Texture& normalTexture = mTextures[i * 3 + 1];
        bool normalLoaded = material.HasNormalTexture && !material.NormalTexture.empty() &&
            LoadTextureFromFile(md3dDevice.Get(), mCommandList.Get(), material.NormalTexture, normalTexture);
        if (!normalLoaded)
        {
            const std::array<std::uint8_t, 4> flatNormal = { 128, 128, 255, 255 };
            CreateTextureFromRGBA8(md3dDevice.Get(), mCommandList.Get(), flatNormal.data(), 1, 1, normalTexture);
        }

        Texture& displacementTexture = mTextures[i * 3 + 2];
        bool displacementLoaded = material.HasBumpTexture && !material.BumpTexture.empty() &&
            LoadTextureFromFile(md3dDevice.Get(), mCommandList.Get(), material.BumpTexture, displacementTexture);
        if (!displacementLoaded)
        {
            const std::array<std::uint8_t, 4> flatDisplacement = { 0, 0, 0, 255 };
            CreateTextureFromRGBA8(md3dDevice.Get(), mCommandList.Get(), flatDisplacement.data(), 1, 1, displacementTexture);
        }

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
        srvHandle.Offset(1 + static_cast<INT>(i * 3), mCbvSrvUavDescriptorSize);
        for (Texture* current : { &texture, &normalTexture, &displacementTexture })
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = current->Resource->GetDesc().Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = -1;
            md3dDevice->CreateShaderResourceView(current->Resource.Get(), &srvDesc, srvHandle);
            srvHandle.Offset(1, mCbvSrvUavDescriptorSize);
        }
    }

    OutputDebugStringA("=== TEXTURE LOADING COMPLETE ===\n");
}

void SponzaApp::BuildGBuffer()
{
    mGBuffer.Build(md3dDevice.Get(), mClientWidth, mClientHeight);
    const UINT materialCount = static_cast<UINT>(mSponza->GetMaterials().size());
    mGBufferSrvStart = 1 + materialCount * 3;
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
    cpu.Offset(mGBufferSrvStart, mCbvSrvUavDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpu(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    gpu.Offset(mGBufferSrvStart, mCbvSrvUavDescriptorSize);
    mGBuffer.CreateDescriptors(md3dDevice.Get(), cpu, gpu, mCbvSrvUavDescriptorSize);
    mDeferredReady = true;
}

void SponzaApp::BuildShadowMap()
{
    mShadowMap.Build(md3dDevice.Get());
    mShadowSrvStart = mGBufferSrvStart + GBuffer::TargetCount;
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
    cpu.Offset(mShadowSrvStart, mCbvSrvUavDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpu(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    gpu.Offset(mShadowSrvStart, mCbvSrvUavDescriptorSize);
    mShadowMap.CreateDescriptors(md3dDevice.Get(), cpu, gpu);
}

void SponzaApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mGBufferVS->GetBufferPointer()), mGBufferVS->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mGBufferPS->GetBufferPointer()), mGBufferPS->GetBufferSize() };
    psoDesc.HS = { reinterpret_cast<BYTE*>(mGBufferHS->GetBufferPointer()), mGBufferHS->GetBufferSize() };
    psoDesc.DS = { reinterpret_cast<BYTE*>(mGBufferDS->GetBufferPointer()), mGBufferDS->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    psoDesc.NumRenderTargets = GBuffer::TargetCount;
    for (UINT i = 0; i < GBuffer::TargetCount; ++i) psoDesc.RTVFormats[i] = GBuffer::Format(i);
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC instanceDesc = psoDesc;
    instanceDesc.InputLayout = { mInstanceInputLayout.data(), static_cast<UINT>(mInstanceInputLayout.size()) };
    instanceDesc.VS = { reinterpret_cast<BYTE*>(mInstanceVS->GetBufferPointer()), mInstanceVS->GetBufferSize() };
    instanceDesc.PS = { reinterpret_cast<BYTE*>(mInstancePS->GetBufferPointer()), mInstancePS->GetBufferSize() };
    instanceDesc.HS = { nullptr, 0 };
    instanceDesc.DS = { nullptr, 0 };
    instanceDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&instanceDesc, IID_PPV_ARGS(&mInstancePSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowDesc = {};
    shadowDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
    shadowDesc.pRootSignature = mRootSignature.Get();
    shadowDesc.VS = { reinterpret_cast<BYTE*>(mShadowVS->GetBufferPointer()), mShadowVS->GetBufferSize() };
    shadowDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    shadowDesc.RasterizerState.DepthBias = 1000;
    shadowDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowDesc.RasterizerState.DepthBiasClamp = 0.01f;
    shadowDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowDesc.SampleMask = UINT_MAX;
    shadowDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowDesc.NumRenderTargets = 0;
    shadowDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowDesc.SampleDesc.Count = 1;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowDesc, IID_PPV_ARGS(&mShadowPSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowInstanceDesc = shadowDesc;
    shadowInstanceDesc.InputLayout = { mInstanceInputLayout.data(), static_cast<UINT>(mInstanceInputLayout.size()) };
    shadowInstanceDesc.VS = { reinterpret_cast<BYTE*>(mShadowInstanceVS->GetBufferPointer()), mShadowInstanceVS->GetBufferSize() };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowInstanceDesc, IID_PPV_ARGS(&mShadowInstancePSO)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lightingDesc = psoDesc;
    lightingDesc.InputLayout = { nullptr, 0 };
    lightingDesc.HS = { nullptr, 0 };
    lightingDesc.DS = { nullptr, 0 };
    lightingDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lightingDesc.VS = { reinterpret_cast<BYTE*>(mLightingVS->GetBufferPointer()), mLightingVS->GetBufferSize() };
    lightingDesc.PS = { reinterpret_cast<BYTE*>(mLightingPS->GetBufferPointer()), mLightingPS->GetBufferSize() };
    lightingDesc.NumRenderTargets = 1;
    lightingDesc.RTVFormats[0] = mBackBufferFormat;
    for (UINT i = 1; i < _countof(lightingDesc.RTVFormats); ++i)
        lightingDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    lightingDesc.DepthStencilState.DepthEnable = FALSE;
    lightingDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    lightingDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&lightingDesc, IID_PPV_ARGS(&mLightingPSO)));
}

void SponzaApp::ConfigureMaterialAnimations()
{
    if (!mSponza)
        return;

    auto& materials = const_cast<std::vector<ModelMaterial>&>(mSponza->GetMaterials());

    for (size_t i = 0; i < materials.size(); ++i)
    {
        auto& mat = materials[i];

        if (mat.Name.find("water") != std::string::npos ||
            mat.Name.find("Water") != std::string::npos)
        {
            mat.TextureTiling = XMFLOAT2(4.0f, 4.0f);
            mat.TextureScrollSpeed = XMFLOAT2(0.05f, 0.02f);
        }
        else if (mat.Name.find("fabric") != std::string::npos ||
                 mat.Name.find("Fabric") != std::string::npos ||
                 mat.Name.find("curtain") != std::string::npos)
        {
            mat.TextureTiling = XMFLOAT2(3.0f, 3.0f);
            mat.TextureScrollSpeed = XMFLOAT2(0.0f, 0.01f);
        }
        else if (mat.Name.find("floor") != std::string::npos ||
                 mat.Name.find("Floor") != std::string::npos)
        {
            mat.TextureTiling = XMFLOAT2(8.0f, 8.0f);
            mat.TextureScrollSpeed = XMFLOAT2(0.0f, 0.0f);
        }
        else if (mat.Name.find("wall") != std::string::npos ||
                 mat.Name.find("Wall") != std::string::npos)
        {
            mat.TextureTiling = XMFLOAT2(2.0f, 2.0f);
            mat.TextureScrollSpeed = XMFLOAT2(0.0f, 0.0f);
        }
        else
        {
            mat.TextureTiling = XMFLOAT2(1.0f, 1.0f);
            mat.TextureScrollSpeed = XMFLOAT2(0.0f, 0.0f);
        }
    }
}
