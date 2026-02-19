#pragma once
#include "common/d3dUtil.h"
#include <string>
#include <vector>
#include <map>

struct ModelVertex
{
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexCoord;
};

struct ModelMaterial
{
    std::string Name;
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::wstring DiffuseTexture;
    bool HasTexture = false;
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