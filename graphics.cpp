#include <algorithm>
#include <limits>
#include <cmath>
#include "graphics.h"

const int width = 1920;
const int height = 1920;
const int depth = 255;

Model* model = nullptr;
float* zbuffer = nullptr;

Matrix ModelView;
Matrix Viewport;
Matrix Projection;

static const float shininess = 64.0f;
static const float ambientStrength = 0.30f;
static const float diffuseStrength = 0.70f;
static const float specularStrength = 0.20f;

static inline float clamp01(float x) {
    return std::max(0.f, std::min(1.f, x));
}

static TGAColor phongColor(const Vec3f& N_in,
    const Vec3f& fragPos,
    const Vec3f& lightDir_in,
    const Vec3f& eyePos,
    const TGAColor& albedo)
{
    Vec3f N = N_in; N.normalize();
    Vec3f L = lightDir_in; L.normalize();
    Vec3f V = (eyePos - fragPos); V.normalize();

    float diff = std::max(0.f, N * L);

    Vec3f minusL = L * -1.f;
    Vec3f R = minusL - N * (2.f * (minusL * N));
    R.normalize();

    float spec = std::pow(std::max(0.f, R * V), shininess);

    float ar = albedo.r / 255.f;
    float ag = albedo.g / 255.f;
    float ab = albedo.b / 255.f;

    float a = ambientStrength;
    float d = diffuseStrength * diff;
    float s = specularStrength * spec;

    float r = ar * (a + d) + s;
    float g = ag * (a + d) + s;
    float b = ab * (a + d) + s;

    r = clamp01(r); g = clamp01(g); b = clamp01(b);

    return TGAColor((unsigned char)(r * 255.f),
        (unsigned char)(g * 255.f),
        (unsigned char)(b * 255.f),
        albedo.a);
}

void lookat(const Vec3f& eye, const Vec3f& center, const Vec3f& up) {
    Vec3f z = (eye - center).normalize();
    Vec3f x = (up ^ z).normalize();
    Vec3f y = (z ^ x).normalize();

    Matrix Minv = Matrix::identity(4);
    Matrix Tr = Matrix::identity(4);

    for (int i = 0; i < 3; i++) {
        Minv[0][i] = x[i];
        Minv[1][i] = y[i];
        Minv[2][i] = z[i];
        Tr[i][3] = -eye[i];
    }
    ModelView = Minv * Tr;
}

Matrix viewport(int x, int y, int w, int h) {
    Matrix m = Matrix::identity(4);
    m[0][3] = x + w / 2.f;
    m[1][3] = y + h / 2.f;
    m[2][3] = depth / 2.f;

    m[0][0] = w / 2.f;
    m[1][1] = h / 2.f;
    m[2][2] = depth / 2.f;
    return m;
}

Vec3f barycentric(const Vec3f* pts, const Vec2i& P) {
    float x0 = pts[0].x, y0 = pts[0].y;
    float x1 = pts[1].x, y1 = pts[1].y;
    float x2 = pts[2].x, y2 = pts[2].y;

    float denom = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::abs(denom) < 1e-2f) return Vec3f(-1.f, 1.f, 1.f);

    float u = ((P.x - x0) * (y2 - y0) - (x2 - x0) * (P.y - y0)) / denom;
    float v = ((x1 - x0) * (P.y - y0) - (P.x - x0) * (y1 - y0)) / denom;
    float w = 1.f - u - v;

    return Vec3f(w, u, v);
}

void triangle_flat(Vec3f* pts, TGAImage& image, TGAColor color, float* zb) {
    Vec2i bboxmin(width - 1, height - 1);
    Vec2i bboxmax(0, 0);
    Vec2i clampv(width - 1, height - 1);

    for (int i = 0; i < 3; i++) {
        bboxmin.x = std::max(0, std::min(bboxmin.x, (int)pts[i].x));
        bboxmin.y = std::max(0, std::min(bboxmin.y, (int)pts[i].y));
        bboxmax.x = std::min(clampv.x, std::max(bboxmax.x, (int)pts[i].x));
        bboxmax.y = std::min(clampv.y, std::max(bboxmax.y, (int)pts[i].y));
    }

    Vec2i P;
    for (P.x = bboxmin.x; P.x <= bboxmax.x; P.x++) {
        for (P.y = bboxmin.y; P.y <= bboxmax.y; P.y++) {
            Vec3f bc = barycentric(pts, P);
            if (bc.x < 0.f || bc.y < 0.f || bc.z < 0.f) continue;

            float z = pts[0].z * bc.x + pts[1].z * bc.y + pts[2].z * bc.z;
            int idx = P.x + P.y * width;
            if (idx < 0 || idx >= width * height) continue;

            if (zb[idx] < z) {
                zb[idx] = z;
                image.set(P.x, P.y, color);
            }
        }
    }
}

static void bbox_of_triangle(Vec3f* pts, Vec2i& bboxmin, Vec2i& bboxmax) {
    bboxmin = Vec2i(width - 1, height - 1);
    bboxmax = Vec2i(0, 0);
    Vec2i clampv(width - 1, height - 1);

    for (int i = 0; i < 3; i++) {
        bboxmin.x = std::max(0, std::min(bboxmin.x, (int)pts[i].x));
        bboxmin.y = std::max(0, std::min(bboxmin.y, (int)pts[i].y));
        bboxmax.x = std::min(clampv.x, std::max(bboxmax.x, (int)pts[i].x));
        bboxmax.y = std::min(clampv.y, std::max(bboxmax.y, (int)pts[i].y));
    }
}

void triangle_phong_flat(Vec3f* pts, Vec3f* norms, Vec3f* worldPos,
    TGAImage& image, float* zb,
    const Vec3f& light_dir, const Vec3f& eyePos,
    const TGAColor& albedo)
{
    Vec2i bboxmin, bboxmax;
    bbox_of_triangle(pts, bboxmin, bboxmax);

    Vec2i P;
    for (P.x = bboxmin.x; P.x <= bboxmax.x; P.x++) {
        for (P.y = bboxmin.y; P.y <= bboxmax.y; P.y++) {
            Vec3f bc = barycentric(pts, P);
            if (bc.x < 0.f || bc.y < 0.f || bc.z < 0.f) continue;

            float z = pts[0].z * bc.x + pts[1].z * bc.y + pts[2].z * bc.z;
            int idx = P.x + P.y * width;
            if (idx < 0 || idx >= width * height) continue;

            if (zb[idx] < z) {
                zb[idx] = z;

                Vec3f N = norms[0] * bc.x + norms[1] * bc.y + norms[2] * bc.z;
                N.normalize();

                Vec3f fragPos = worldPos[0] * bc.x + worldPos[1] * bc.y + worldPos[2] * bc.z;

                image.set(P.x, P.y, phongColor(N, fragPos, light_dir, eyePos, albedo));
            }
        }
    }
}

void triangle_phong_tex(Vec3f* pts, Vec2f* uvs, Vec3f* norms, Vec3f* worldPos,
    TGAImage& image, float* zb,
    const Vec3f& light_dir, const Vec3f& eyePos)
{
    Vec2i bboxmin, bboxmax;
    bbox_of_triangle(pts, bboxmin, bboxmax);

    int texW = model->diffuse_width();
    int texH = model->diffuse_height();

    Vec2i P;
    for (P.x = bboxmin.x; P.x <= bboxmax.x; P.x++) {
        for (P.y = bboxmin.y; P.y <= bboxmax.y; P.y++) {
            Vec3f bc = barycentric(pts, P);
            if (bc.x < 0.f || bc.y < 0.f || bc.z < 0.f) continue;

            float z = pts[0].z * bc.x + pts[1].z * bc.y + pts[2].z * bc.z;
            int idx = P.x + P.y * width;
            if (idx < 0 || idx >= width * height) continue;

            if (zb[idx] < z) {
                zb[idx] = z;

                float u = uvs[0].x * bc.x + uvs[1].x * bc.y + uvs[2].x * bc.z;
                float v = uvs[0].y * bc.x + uvs[1].y * bc.y + uvs[2].y * bc.z;

                int tx = std::max(0, std::min(texW - 1, (int)(u * texW)));
                int ty = std::max(0, std::min(texH - 1, (int)(v * texH)));

                TGAColor albedo = model->diffuse(Vec2i(tx, ty));

                Vec3f N = norms[0] * bc.x + norms[1] * bc.y + norms[2] * bc.z;
                N.normalize();

                Vec3f fragPos = worldPos[0] * bc.x + worldPos[1] * bc.y + worldPos[2] * bc.z;

                image.set(P.x, P.y, phongColor(N, fragPos, light_dir, eyePos, albedo));
            }
        }
    }
}