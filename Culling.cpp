#include "Culling.h"

using namespace DirectX;

namespace
{
    BoundingBox ToBoundingBox(const SceneBounds& bounds)
    {
        BoundingBox box;
        box.Center = bounds.Center;
        box.Extents = bounds.Extents;
        return box;
    }

    BoundingFrustum MakeWorldFrustum(FXMMATRIX view, FXMMATRIX projection)
    {
        BoundingFrustum frustum;
        BoundingFrustum::CreateFromMatrix(frustum, projection);
        XMVECTOR determinant;
        const XMMATRIX inverseView = XMMatrixInverse(&determinant, view);
        BoundingFrustum worldFrustum;
        frustum.Transform(worldFrustum, inverseView);
        return worldFrustum;
    }
}

bool FrustumCuller::IsVisible(const SceneBounds& bounds, FXMMATRIX view, FXMMATRIX projection)
{
    const BoundingFrustum frustum = MakeWorldFrustum(view, projection);
    return frustum.Intersects(ToBoundingBox(bounds));
}

void Octree::Build(const std::vector<SceneBounds>& bounds, const SceneBounds& worldBounds)
{
    mObjectBounds = &bounds;
    mRoot = std::make_unique<Node>();
    mRoot->Bounds = worldBounds;
    for (UINT i = 0; i < bounds.size(); ++i)
        Insert(*mRoot, i, 0);
}

void Octree::QueryVisible(FXMMATRIX view, FXMMATRIX projection, std::vector<UINT>& visibleIndices) const
{
    visibleIndices.clear();
    if (!mRoot)
        return;

    const BoundingFrustum frustum = MakeWorldFrustum(view, projection);
    QueryNode(*mRoot, frustum, visibleIndices);
}

void Octree::Insert(Node& node, UINT objectIndex, UINT depth)
{
    const SceneBounds& objectBounds = (*mObjectBounds)[objectIndex];
    if (depth == MaxDepth)
    {
        node.Objects.push_back(objectIndex);
        return;
    }

    const int childIndex = ChildIndex(objectBounds, node.Bounds);
    if (childIndex < 0)
    {
        node.Objects.push_back(objectIndex);
        return;
    }

    if (!node.Children[childIndex])
    {
        node.Children[childIndex] = std::make_unique<Node>();
        node.Children[childIndex]->Bounds = MakeChildBounds(node.Bounds, childIndex);
    }

    Node& child = *node.Children[childIndex];
    if (child.Objects.size() < MaxObjectsPerNode || depth + 1 == MaxDepth)
        child.Objects.push_back(objectIndex);
    else
        Insert(child, objectIndex, depth + 1);
}

void Octree::QueryNode(const Node& node, const BoundingFrustum& frustum, std::vector<UINT>& visibleIndices) const
{
    if (!frustum.Intersects(ToBoundingBox(node.Bounds)))
        return;

    for (UINT objectIndex : node.Objects)
    {
        if (frustum.Intersects(ToBoundingBox((*mObjectBounds)[objectIndex])))
            visibleIndices.push_back(objectIndex);
    }

    for (const auto& child : node.Children)
    {
        if (child)
            QueryNode(*child, frustum, visibleIndices);
    }
}

int Octree::ChildIndex(const SceneBounds& objectBounds, const SceneBounds& parent)
{
    int child = 0;
    const float objectMin[3] = {
        objectBounds.Center.x - objectBounds.Extents.x,
        objectBounds.Center.y - objectBounds.Extents.y,
        objectBounds.Center.z - objectBounds.Extents.z };
    const float objectMax[3] = {
        objectBounds.Center.x + objectBounds.Extents.x,
        objectBounds.Center.y + objectBounds.Extents.y,
        objectBounds.Center.z + objectBounds.Extents.z };
    const float center[3] = { parent.Center.x, parent.Center.y, parent.Center.z };

    for (int axis = 0; axis < 3; ++axis)
    {
        if (objectMax[axis] <= center[axis])
            continue;
        if (objectMin[axis] >= center[axis])
            child |= 1 << axis;
        else
            return -1;
    }
    return child;
}

SceneBounds Octree::MakeChildBounds(const SceneBounds& parent, int childIndex)
{
    SceneBounds child;
    child.Extents = XMFLOAT3(parent.Extents.x * 0.5f, parent.Extents.y * 0.5f, parent.Extents.z * 0.5f);
    child.Center = parent.Center;
    child.Center.x += (childIndex & 1) ? child.Extents.x : -child.Extents.x;
    child.Center.y += (childIndex & 2) ? child.Extents.y : -child.Extents.y;
    child.Center.z += (childIndex & 4) ? child.Extents.z : -child.Extents.z;
    return child;
}
