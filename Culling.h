#pragma once

#include "Common/d3dUtil.h"
#include <memory>
#include <vector>

struct SceneBounds
{
    DirectX::XMFLOAT3 Center = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 Extents = { 1.0f, 1.0f, 1.0f };
};

class FrustumCuller
{
public:
    static bool IsVisible(const SceneBounds& bounds, DirectX::FXMMATRIX view, DirectX::FXMMATRIX projection);
};

// Static octree for the scattered instanced objects.  Objects that cross a
// child boundary remain in their parent node, so each object is returned once.
class Octree
{
public:
    void Build(const std::vector<SceneBounds>& bounds, const SceneBounds& worldBounds);
    void QueryVisible(DirectX::FXMMATRIX view, DirectX::FXMMATRIX projection, std::vector<UINT>& visibleIndices) const;

private:
    struct Node
    {
        SceneBounds Bounds;
        std::vector<UINT> Objects;
        std::unique_ptr<Node> Children[8];
    };

    static constexpr UINT MaxObjectsPerNode = 32;
    static constexpr UINT MaxDepth = 6;

    std::unique_ptr<Node> mRoot;
    const std::vector<SceneBounds>* mObjectBounds = nullptr;

    void Insert(Node& node, UINT objectIndex, UINT depth);
    void QueryNode(const Node& node, const DirectX::BoundingFrustum& frustum, std::vector<UINT>& visibleIndices) const;
    static int ChildIndex(const SceneBounds& childCandidate, const SceneBounds& parent);
    static SceneBounds MakeChildBounds(const SceneBounds& parent, int childIndex);
};
