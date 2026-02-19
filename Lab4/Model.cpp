#include "Model.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

using namespace DirectX;

bool Model::LoadFromOBJ(const std::string& filename)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string inputfile = filename;
    std::string basedir = inputfile.substr(0, inputfile.find_last_of("/\\") + 1);

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
        inputfile.c_str(), basedir.c_str(), true);

    if (!warn.empty()) {
        OutputDebugStringA(warn.c_str());
    }
    if (!err.empty()) {
        OutputDebugStringA(err.c_str());
    }
    if (!ret) {
        return false;
    }

    // Convert materials
    mMaterials.resize(materials.size());
    for (size_t i = 0; i < materials.size(); i++) {
        mMaterials[i].Name = materials[i].name;
        mMaterials[i].DiffuseAlbedo = XMFLOAT4(
            materials[i].diffuse[0],
            materials[i].diffuse[1],
            materials[i].diffuse[2],
            1.0f
        );

        if (!materials[i].diffuse_texname.empty()) {
            std::string texname = basedir + materials[i].diffuse_texname;
            int len = MultiByteToWideChar(CP_UTF8, 0, texname.c_str(), -1, nullptr, 0);
            mMaterials[i].DiffuseTexture.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, texname.c_str(), -1, &mMaterials[i].DiffuseTexture[0], len);
            mMaterials[i].HasTexture = true;
        }
    }

    // Process each shape
    for (const auto& shape : shapes) {
        ModelMesh mesh;
        mesh.Name = shape.name;

        size_t index_offset = 0;
        std::map<std::tuple<int, int, int>, uint32_t> uniqueVertices;

        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];

            for (size_t v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];

                ModelVertex vertex;

                // Position
                vertex.Position.x = attrib.vertices[3 * idx.vertex_index + 0];
                vertex.Position.y = attrib.vertices[3 * idx.vertex_index + 1];
                vertex.Position.z = attrib.vertices[3 * idx.vertex_index + 2];

                // Normal
                if (idx.normal_index >= 0) {
                    vertex.Normal.x = attrib.normals[3 * idx.normal_index + 0];
                    vertex.Normal.y = attrib.normals[3 * idx.normal_index + 1];
                    vertex.Normal.z = attrib.normals[3 * idx.normal_index + 2];
                }

                // TexCoord
                if (idx.texcoord_index >= 0) {
                    vertex.TexCoord.x = attrib.texcoords[2 * idx.texcoord_index + 0];
                    vertex.TexCoord.y = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                auto key = std::make_tuple(idx.vertex_index, idx.normal_index, idx.texcoord_index);
                if (uniqueVertices.count(key) == 0) {
                    uniqueVertices[key] = static_cast<uint32_t>(mesh.Vertices.size());
                    mesh.Vertices.push_back(vertex);
                }
                mesh.Indices.push_back(uniqueVertices[key]);
            }

            mesh.MaterialIndex = shape.mesh.material_ids[f];
            index_offset += fv;
        }

        mesh.IndexCount = static_cast<UINT>(mesh.Indices.size());
        mMeshes.push_back(mesh);
    }

    return true;
}

void Model::CreateBuffers(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    UINT totalVertices = 0;
    UINT totalIndices = 0;

    for (auto& mesh : mMeshes) {
        mesh.VertexOffset = totalVertices;
        mesh.IndexOffset = totalIndices;

        totalVertices += static_cast<UINT>(mesh.Vertices.size());
        totalIndices += static_cast<UINT>(mesh.Indices.size());
    }

    std::vector<ModelVertex> allVertices;
    std::vector<std::uint32_t> allIndices;

    allVertices.reserve(totalVertices);
    allIndices.reserve(totalIndices);

    for (const auto& mesh : mMeshes) {
        allVertices.insert(allVertices.end(), mesh.Vertices.begin(), mesh.Vertices.end());
        allIndices.insert(allIndices.end(), mesh.Indices.begin(), mesh.Indices.end());
    }

    mMeshGeo = std::make_unique<MeshGeometry>();
    mMeshGeo->Name = "sponzaGeo";

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

    for (size_t i = 0; i < mMeshes.size(); i++) {
        SubmeshGeometry submesh;
        submesh.IndexCount = mMeshes[i].IndexCount;
        submesh.StartIndexLocation = mMeshes[i].IndexOffset;
        submesh.BaseVertexLocation = mMeshes[i].VertexOffset;

        mMeshGeo->DrawArgs[std::to_string(i)] = submesh;
    }
}