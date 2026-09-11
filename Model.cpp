#include "Model.h"

#include <algorithm>
#include <map>
#include <tuple>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

using namespace DirectX;

namespace
{
    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty())
            return std::wstring();

        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (length <= 1)
            return std::wstring();

        std::wstring result(static_cast<size_t>(length - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], length);
        return result;
    }

    UINT SafeMaterialIndex(int materialId, size_t materialCount)
    {
        if (materialId >= 0 && static_cast<size_t>(materialId) < materialCount)
            return static_cast<UINT>(materialId);

        return 0;
    }
}

bool Model::LoadFromOBJ(const std::string& filename)
{
    mMeshes.clear();
    mMaterials.clear();
    mMeshGeo.reset();

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    const std::string basedir = filename.substr(0, filename.find_last_of("/\\") + 1);

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
        filename.c_str(), basedir.c_str(), true);

    if (!warn.empty())
        OutputDebugStringA(warn.c_str());

    if (!err.empty())
        OutputDebugStringA(err.c_str());

    if (!ret)
        return false;

    if (materials.empty())
    {
        mMaterials.push_back(ModelMaterial{});
    }
    else
    {
        mMaterials.resize(materials.size());
        for (size_t i = 0; i < materials.size(); ++i)
        {
            const auto& src = materials[i];
            auto& dst = mMaterials[i];

            dst.Name = src.name.empty() ? ("material_" + std::to_string(i)) : src.name;
            dst.DiffuseAlbedo = XMFLOAT4(
                src.diffuse[0],
                src.diffuse[1],
                src.diffuse[2],
                src.dissolve > 0.0f ? src.dissolve : 1.0f);

            dst.SpecularAlbedo = XMFLOAT3(src.specular[0], src.specular[1], src.specular[2]);
            dst.Shininess = src.shininess > 0.0f ? src.shininess : 32.0f;
            dst.AmbientColor = XMFLOAT3(src.ambient[0], src.ambient[1], src.ambient[2]);
            dst.EmissiveColor = XMFLOAT3(src.emission[0], src.emission[1], src.emission[2]);

            if (!src.diffuse_texname.empty())
            {
                dst.DiffuseTexture = Utf8ToWide(basedir + src.diffuse_texname);
                dst.HasTexture = !dst.DiffuseTexture.empty();
            }

            if (!src.specular_texname.empty())
            {
                dst.SpecularTexture = Utf8ToWide(basedir + src.specular_texname);
                dst.HasSpecularTexture = !dst.SpecularTexture.empty();
            }

            if (!src.normal_texname.empty())
            {
                dst.NormalTexture = Utf8ToWide(basedir + src.normal_texname);
                dst.HasNormalTexture = !dst.NormalTexture.empty();
            }

            if (!src.bump_texname.empty())
            {
                dst.BumpTexture = Utf8ToWide(basedir + src.bump_texname);
                dst.HasBumpTexture = !dst.BumpTexture.empty();
            }

            if (!src.ambient_texname.empty())
            {
                dst.AmbientTexture = Utf8ToWide(basedir + src.ambient_texname);
                dst.HasAmbientTexture = !dst.AmbientTexture.empty();
            }
        }
    }

    for (const auto& shape : shapes)
    {
        std::map<int, ModelMesh> meshesByMaterial;
        std::map<int, std::map<std::tuple<int, int, int>, std::uint32_t>> uniqueVerticesByMaterial;

        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            const int faceVertexCount = shape.mesh.num_face_vertices[f];
            const int rawMaterialId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
            const UINT materialIndex = SafeMaterialIndex(rawMaterialId, mMaterials.size());

            ModelMesh& mesh = meshesByMaterial[static_cast<int>(materialIndex)];
            if (mesh.Name.empty())
            {
                mesh.Name = shape.name + "_mat_" + std::to_string(materialIndex);
                mesh.MaterialIndex = materialIndex;
            }

            auto& uniqueVertices = uniqueVerticesByMaterial[static_cast<int>(materialIndex)];

            for (int v = 0; v < faceVertexCount; ++v)
            {
                const tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                ModelVertex vertex;

                if (idx.vertex_index >= 0)
                {
                    vertex.Position.x = attrib.vertices[3 * idx.vertex_index + 0];
                    vertex.Position.y = attrib.vertices[3 * idx.vertex_index + 1];
                    vertex.Position.z = attrib.vertices[3 * idx.vertex_index + 2];
                }

                if (idx.normal_index >= 0)
                {
                    vertex.Normal.x = attrib.normals[3 * idx.normal_index + 0];
                    vertex.Normal.y = attrib.normals[3 * idx.normal_index + 1];
                    vertex.Normal.z = attrib.normals[3 * idx.normal_index + 2];
                }

                if (idx.texcoord_index >= 0)
                {
                    vertex.TexCoord.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                    vertex.TexCoord.y = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                const auto key = std::make_tuple(idx.vertex_index, idx.normal_index, idx.texcoord_index);
                auto found = uniqueVertices.find(key);
                if (found == uniqueVertices.end())
                {
                    const std::uint32_t newIndex = static_cast<std::uint32_t>(mesh.Vertices.size());
                    uniqueVertices[key] = newIndex;
                    mesh.Vertices.push_back(vertex);
                    mesh.Indices.push_back(newIndex);
                }
                else
                {
                    mesh.Indices.push_back(found->second);
                }
            }

            indexOffset += faceVertexCount;
        }

        for (auto& pair : meshesByMaterial)
        {
            ModelMesh& mesh = pair.second;
            if (!mesh.Indices.empty())
            {
                mesh.IndexCount = static_cast<UINT>(mesh.Indices.size());
                mMeshes.push_back(std::move(mesh));
            }
        }
    }

    return !mMeshes.empty();
}

void Model::CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    UINT totalVertices = 0;
    UINT totalIndices = 0;

    for (auto& mesh : mMeshes)
    {
        mesh.VertexOffset = totalVertices;
        mesh.IndexOffset = totalIndices;

        totalVertices += static_cast<UINT>(mesh.Vertices.size());
        totalIndices += static_cast<UINT>(mesh.Indices.size());
    }

    std::vector<ModelVertex> allVertices;
    std::vector<std::uint32_t> allIndices;

    allVertices.reserve(totalVertices);
    allIndices.reserve(totalIndices);

    for (const auto& mesh : mMeshes)
    {
        allVertices.insert(allVertices.end(), mesh.Vertices.begin(), mesh.Vertices.end());
        allIndices.insert(allIndices.end(), mesh.Indices.begin(), mesh.Indices.end());
    }

    mMeshGeo = std::make_unique<MeshGeometry>();
    mMeshGeo->Name = "objModelGeo";

    const UINT vbByteSize = static_cast<UINT>(allVertices.size() * sizeof(ModelVertex));
    const UINT ibByteSize = static_cast<UINT>(allIndices.size() * sizeof(std::uint32_t));

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mMeshGeo->VertexBufferCPU));
    CopyMemory(mMeshGeo->VertexBufferCPU->GetBufferPointer(), allVertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mMeshGeo->IndexBufferCPU));
    CopyMemory(mMeshGeo->IndexBufferCPU->GetBufferPointer(), allIndices.data(), ibByteSize);

    mMeshGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList,
        allVertices.data(), vbByteSize, mMeshGeo->VertexBufferUploader);

    mMeshGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList,
        allIndices.data(), ibByteSize, mMeshGeo->IndexBufferUploader);

    mMeshGeo->VertexByteStride = sizeof(ModelVertex);
    mMeshGeo->VertexBufferByteSize = vbByteSize;
    mMeshGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
    mMeshGeo->IndexBufferByteSize = ibByteSize;

    for (size_t i = 0; i < mMeshes.size(); ++i)
    {
        SubmeshGeometry submesh;
        submesh.IndexCount = mMeshes[i].IndexCount;
        submesh.StartIndexLocation = mMeshes[i].IndexOffset;
        submesh.BaseVertexLocation = mMeshes[i].VertexOffset;

        mMeshGeo->DrawArgs[std::to_string(i)] = submesh;
    }
}
