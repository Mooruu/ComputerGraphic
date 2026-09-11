#pragma once

#include "Common/d3dUtil.h"
#include <cstdint>
#include <string>
#include <vector>

struct ModelVertex
{
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 Normal = { 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT2 TexCoord = { 0.0f, 0.0f };
};

struct ModelMaterial
{
    std::string Name = "default";
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 SpecularAlbedo = { 1.0f, 1.0f, 1.0f };
    float Shininess = 32.0f;
    DirectX::XMFLOAT3 AmbientColor = { 0.1f, 0.1f, 0.1f };
    DirectX::XMFLOAT3 EmissiveColor = { 0.0f, 0.0f, 0.0f };

    std::wstring DiffuseTexture;
    std::wstring SpecularTexture;
    std::wstring NormalTexture;
    std::wstring BumpTexture;
    std::wstring AmbientTexture;

    bool HasTexture = false;
    bool HasSpecularTexture = false;
    bool HasNormalTexture = false;
    bool HasBumpTexture = false;
    bool HasAmbientTexture = false;

    DirectX::XMFLOAT2 TextureTiling = { 1.0f, 1.0f };
    DirectX::XMFLOAT2 TextureScrollSpeed = { 0.0f, 0.0f };
};

struct ModelMesh
{
    std::string Name;
    std::vector<ModelVertex> Vertices;
    std::vector<std::uint32_t> Indices;
    UINT MaterialIndex = 0;
    UINT VertexOffset = 0;
    UINT IndexOffset = 0;
    UINT IndexCount = 0;
};

class Model
{
public:
    bool LoadFromOBJ(const std::string& filename);
    void CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

    std::unique_ptr<MeshGeometry> GetMeshGeometry() { return std::move(mMeshGeo); }
    const std::vector<ModelMesh>& GetMeshes() const { return mMeshes; }
    const std::vector<ModelMaterial>& GetMaterials() const { return mMaterials; }

private:
    std::vector<ModelMesh> mMeshes;
    std::vector<ModelMaterial> mMaterials;
    std::unique_ptr<MeshGeometry> mMeshGeo;
};
