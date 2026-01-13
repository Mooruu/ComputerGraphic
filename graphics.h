#ifndef __GRAPHICS_H__
#define __GRAPHICS_H__
#include "geometry.h"
#include "tgaimage.h"
#include "model.h"

extern const int width;
extern const int height;
extern const int depth;
extern Model* model;
extern float* zbuffer;
extern Matrix ModelView;
extern Matrix Viewport;
extern Matrix Projection;

void lookat(const Vec3f& eye, const Vec3f& center, const Vec3f& up);
Matrix viewport(int x, int y, int w, int h);
Vec3f barycentric(const Vec3f* pts, const Vec2i& P);

void triangle_flat(Vec3f* pts, TGAImage& image, TGAColor color, float* zb);

void triangle_phong_flat(Vec3f* pts, Vec3f* norms, Vec3f* worldPos,
    TGAImage& image, float* zb,
    const Vec3f& light_dir, const Vec3f& eyePos,
    const TGAColor& albedo);

void triangle_phong_tex(Vec3f* pts, Vec2f* uvs, Vec3f* norms, Vec3f* worldPos,
    TGAImage& image, float* zb,
    const Vec3f& light_dir, const Vec3f& eyePos);

#endif