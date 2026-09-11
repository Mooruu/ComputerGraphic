#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/Camera.h"
#include "Common/DDSTextureLoader.h"
#include "Model.h"

#include <wincodec.h>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
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
    void BuildPSO();

private:
    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;
    UINT mCbvSrvUavDescriptorSize = 0;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
    std::unique_ptr<Model> mSponza = nullptr;
    std::unique_ptr<MeshGeometry> mSponzaGeo = nullptr;
    std::vector<Texture> mTextures;

    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    ComPtr<ID3D12PipelineState> mPSO = nullptr;

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
    mMainWndCaption = L"Sponza Demo - Keys: 1/2 Tiling +/-, 3/4 Scroll X +/-, 5/6 Scroll Y +/-, 0 Reset";
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
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildTextures();
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
}

void SponzaApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    float x = mRadius * sinf(mPhi) * cosf(mTheta);
    float z = mRadius * sinf(mPhi) * sinf(mTheta);
    float y = mRadius * cosf(mPhi);

    XMFLOAT3 pos(x, y, z);
    XMFLOAT3 target(0.0f, 3.0f, 0.0f);

    mCamera.LookAt(pos, target, XMFLOAT3(0.0f, 1.0f, 0.0f));
    mCamera.UpdateViewMatrix();

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
    XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
    mObjectCB->CopyData(0, objConstants);
}

void SponzaApp::Draw(const GameTimer& gt)
{
    ThrowIfFailed(mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    if (mSponzaGeo && mSponza)
    {
        mCommandList->IASetVertexBuffers(0, 1, &mSponzaGeo->VertexBufferView());
        mCommandList->IASetIndexBuffer(&mSponzaGeo->IndexBufferView());
        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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
                0.0f);

            mCommandList->SetGraphicsRoot32BitConstants(
                2,
                sizeof(MaterialRootConstants) / sizeof(UINT32),
                &materialConstants,
                0);

            CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
            textureHandle.Offset(1 + materialIndex, mCbvSrvUavDescriptorSize);
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

        mTheta += dx;
        mPhi += dy;
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        float dx = 0.01f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.01f * static_cast<float>(y - mLastMousePos.y);
        mRadius += dx - dy;
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 100.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void SponzaApp::OnKeyboardInput(const GameTimer& gt)
{
    if (!mSponza)
        return;

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
    cbvSrvHeapDesc.NumDescriptors = 1 + materialCount; // 1 CBV + one SRV per material.
    cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvSrvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&mCbvHeap)));

    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void SponzaApp::BuildConstantBuffers()
{
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    md3dDevice->CreateConstantBufferView(&cbvDesc, mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void SponzaApp::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[3];

    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    slotRootParameter[1].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    slotRootParameter[2].InitAsConstants(
        sizeof(MaterialRootConstants) / sizeof(UINT32),
        1,
        0,
        D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_STATIC_SAMPLER_DESC linearWrapSampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        3,
        slotRootParameter,
        1,
        &linearWrapSampler,
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

    OutputDebugString(L"Shaders compiled successfully!\n");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
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

void SponzaApp::BuildTextures()
{
    const auto& materials = mSponza->GetMaterials();
    mTextures.resize(materials.size());

    char debugBuffer[512];
    sprintf_s(debugBuffer, "=== LOADING %zu MATERIALS ===\n", materials.size());
    OutputDebugStringA(debugBuffer);

    for (size_t i = 0; i < materials.size(); ++i)
    {
        const auto& material = materials[i];
        Texture& texture = mTextures[i];
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

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = texture.Resource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = -1;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
        srvHandle.Offset(1 + static_cast<INT>(i), mCbvSrvUavDescriptorSize);
        md3dDevice->CreateShaderResourceView(texture.Resource.Get(), &srvDesc, srvHandle);
    }

    OutputDebugStringA("=== TEXTURE LOADING COMPLETE ===\n");
}

void SponzaApp::BuildPSO()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()), mvsByteCode->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()), mpsByteCode->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
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
