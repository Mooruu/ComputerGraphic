#include "model.h"
#include <iostream>
#include <fstream>
#include <string>
#include <limits>
#include <algorithm>
#include <cstdlib>

Model::Model(const char* filename)
    : verts_(), faces_(), uvs_(), faces_uv_(), vnorms_(), diffusemap_()
{
    std::ifstream in1(filename);
    if (!in1.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }

    size_t vCount = 0;
    size_t fCount = 0;
    size_t vtCount = 0;
    std::string line;

    while (std::getline(in1, line)) {
        if (line.size() < 2) continue;
        if (!line.compare(0, 2, "v ")) vCount++;
        else if (!line.compare(0, 2, "f ")) fCount++;
        else if (!line.compare(0, 3, "vt ")) vtCount++;
    }

    in1.close();
    verts_.reserve(vCount);
    faces_.reserve(fCount);
    uvs_.reserve(vtCount);
    faces_uv_.reserve(fCount);

    std::ifstream in2(filename);
    if (!in2.is_open()) {
        std::cerr << "Cannot open file on second pass: " << filename << std::endl;
        return;
    }

    while (std::getline(in2, line)) {
        if (line.size() < 2) continue;

        if (!line.compare(0, 2, "v ")) {
            const char* s = line.c_str() + 2;
            char* end = nullptr;

            float x = std::strtof(s, &end); s = end;
            float y = std::strtof(s, &end); s = end;
            float z = std::strtof(s, &end);

            verts_.push_back(Vec3f(x, y, z));
        }
        else if (!line.compare(0, 3, "vt ")) {
            const char* s = line.c_str() + 3;
            char* end = nullptr;

            float u = std::strtof(s, &end); s = end;
            float v = std::strtof(s, &end);

            uvs_.push_back(Vec2f(u, v));
        }
        else if (!line.compare(0, 2, "f ")) {
            const char* s = line.c_str() + 2;
            std::vector<int> f;
            std::vector<int> fuv;

            while (*s) {
                while (*s == ' ' || *s == '\t') s++;
                if (!*s) break;

                char* end = nullptr;
                long vIdx = std::strtol(s, &end, 10);
                if (s == end) break;
                f.push_back((int)vIdx - 1);
                int vtIdx = -1;

                if (*end == '/') {
                    s = end + 1;
                    char* end2 = nullptr;

                    long tmp = std::strtol(s, &end2, 10);
                    if (s != end2) {
                        vtIdx = (int)tmp - 1;
                        s = end2;

                        while (*s && *s != ' ' && *s != '\t') s++;
                    }
                    else {
                        s = end;
                        while (*s && *s != ' ' && *s != '\t') s++;
                    }
                }
                else {
                    s = end;
                    while (*s && *s != ' ' && *s != '\t') s++;
                }

                if (vtIdx >= 0) fuv.push_back(vtIdx);
            }

            if (f.size() >= 3) {
                faces_.push_back(std::move(f));
                if (!fuv.empty()) faces_uv_.push_back(std::move(fuv));
                else faces_uv_.push_back(std::vector<int>());
            }
        }
    }

    std::cout << "v: " << vCount << " f: " << fCount << "\n";
    in2.close();

    if (!verts_.empty()) normalize();

    compute_vertex_normals();

    load_texture(std::string(filename), "_diffuse.tga", diffusemap_);
}

Model::~Model() {}

int Model::nverts() const { return (int)verts_.size(); }
int Model::nfaces() const { return (int)faces_.size(); }

const std::vector<int>& Model::face(int idx) const {
    return faces_[idx];
}

Vec3f Model::vert(int i) const {
    return verts_[i];
}

Vec2f Model::uv(int iface, int nthvert) const {
    if (iface < 0 || iface >= (int)faces_uv_.size()) return Vec2f(0.f, 0.f);
    if (nthvert < 0 || nthvert >= (int)faces_uv_[iface].size()) return Vec2f(0.f, 0.f);
    int idx = faces_uv_[iface][nthvert];
    if (idx < 0 || idx >= (int)uvs_.size()) return Vec2f(0.f, 0.f);
    return uvs_[idx];
}

bool Model::face_has_uv(int iface) const {
    if (iface < 0 || iface >= (int)faces_uv_.size()) return false;
    return faces_uv_[iface].size() >= 3;
}

void Model::load_texture(std::string filename, const char* suffix, TGAImage& img) {
    std::string texfile(filename);
    size_t dot = texfile.find_last_of(".");
    if (dot != std::string::npos) {
        texfile = texfile.substr(0, dot) + std::string(suffix);
        std::cerr << "texture file " << texfile << " loading " << (img.read_tga_file(texfile.c_str()) ? "ok" : "failed") << std::endl;
        if (img.get_width() > 0 && img.get_height() > 0) img.flip_vertically();
    }
}

TGAColor Model::diffuse(Vec2i uv) {
    return diffusemap_.get(uv.x, uv.y);
}

bool Model::has_diffuse() {
    return diffusemap_.get_width() > 0 && diffusemap_.get_height() > 0;
}

int Model::diffuse_width() {
    return diffusemap_.get_width();
}

int Model::diffuse_height() {
    return diffusemap_.get_height();
}

Vec3f Model::normal(int vidx) const {
    if (vidx < 0 || vidx >= (int)vnorms_.size()) return Vec3f(0.f, 0.f, 1.f);
    return vnorms_[vidx];
}

bool Model::has_normals() const {
    return (int)vnorms_.size() == (int)verts_.size();
}

void Model::compute_vertex_normals() {
    vnorms_.assign(verts_.size(), Vec3f(0.f, 0.f, 0.f));

    for (int i = 0; i < (int)faces_.size(); i++) {
        const std::vector<int>& f = faces_[i];
        int n = (int)f.size();
        if (n < 3) continue;

        Vec3f v0 = verts_[f[0]];
        for (int k = 1; k + 1 < n; k++) {
            int i1 = f[k];
            int i2 = f[k + 1];

            Vec3f v1 = verts_[i1];
            Vec3f v2 = verts_[i2];

            Vec3f e1 = v2 - v0;
            Vec3f e2 = v1 - v0;
            Vec3f fn = e1 ^ e2;

            vnorms_[f[0]] = vnorms_[f[0]] + fn;
            vnorms_[i1] = vnorms_[i1] + fn;
            vnorms_[i2] = vnorms_[i2] + fn;

        }
    }

    for (int i = 0; i < (int)vnorms_.size(); i++) {
        float len = vnorms_[i].norm();
        if (len > 1e-8f) vnorms_[i] = vnorms_[i] * (1.f / len);
        else vnorms_[i] = Vec3f(0.f, 0.f, 1.f);
    }
}

void Model::normalize() {
    Vec3f minv(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    );

    Vec3f maxv(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()
    );

    for (const auto& v : verts_) {
        minv.x = std::min(minv.x, v.x);
        minv.y = std::min(minv.y, v.y);
        minv.z = std::min(minv.z, v.z);

        maxv.x = std::max(maxv.x, v.x);
        maxv.y = std::max(maxv.y, v.y);
        maxv.z = std::max(maxv.z, v.z);
    }

    Vec3f center(
        (minv.x + maxv.x) * 0.5f,
        (minv.y + maxv.y) * 0.5f,
        (minv.z + maxv.z) * 0.5f
    );

    Vec3f size(
        maxv.x - minv.x,
        maxv.y - minv.y,
        maxv.z - minv.z
    );

    float maxExtentXY = std::max(size.x, size.y);
    if (maxExtentXY == 0.f) maxExtentXY = 1.f;

    float scale = 1.8f / maxExtentXY;

    for (auto& v : verts_) {
        v.x = (v.x - center.x) * scale;
        v.y = (v.y - center.y) * scale;
        v.z = (v.z - center.z) * scale;
    }
}
