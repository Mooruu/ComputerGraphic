#ifndef __MODEL_H__
#define __MODEL_H__

#include <vector>
#include <string>
#include "geometry.h"
#include "tgaimage.h"

class Model {
public:
    Model(const char* filename);
    ~Model();

    int nverts() const;
    int nfaces() const;

    const std::vector<int>& face(int idx) const;
    Vec3f vert(int i) const;

    Vec2f uv(int iface, int nthvert) const;
    bool face_has_uv(int iface) const;


    Vec3f normal(int vidx) const;
    bool has_normals() const;

    void load_texture(std::string filename, const char* suffix, TGAImage& img);
    TGAColor diffuse(Vec2i uv);
    bool has_diffuse();
    int diffuse_width();
    int diffuse_height();

private:
    void normalize();
    void compute_vertex_normals();

private:
    std::vector<Vec3f> verts_;
    std::vector<std::vector<int>> faces_;

    std::vector<Vec2f> uvs_;
    std::vector<std::vector<int>> faces_uv_;

    std::vector<Vec3f> vnorms_;

    TGAImage diffusemap_;
};

#endif
