#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <limits>

#include "graphics.h"
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"

Vec3f light_dir(0, 0, -1);
Vec3f camera(1, 0, 4);
Vec3f center(0, 0, 0);
Vec3f up(0, 1, 0);

float s = 0.95f;

Vec3f C[8] = {
  {-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s},
  {-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}
};

int F[12][3] = {
  {0,1,2},{0,2,3},
  {4,6,5},{4,7,6},
  {0,4,5},{0,5,1},
  {3,2,6},{3,6,7},
  {0,3,7},{0,7,4},
  {1,5,6},{1,6,2} 
};


static Vec3f m2v(Matrix m) {
    return Vec3f(
        m[0][0] / m[3][0],
        m[1][0] / m[3][0],
        m[2][0] / m[3][0]
    );
}

static Matrix v2m(Vec3f v) {
    Matrix m(4, 1);
    m[0][0] = v.x;
    m[1][0] = v.y;
    m[2][0] = v.z;
    m[3][0] = 1.f;
    return m;
}

int main(int argc, char** argv) {
    if (argc == 2) model = new Model(argv[1]);
    else          model = new Model("obj/african_head.obj");

    if (!model || model->nverts() == 0 || model->nfaces() == 0) {
        std::cerr << "Model is empty or failed to load\n";
        delete model;
        return 1;
    }

    light_dir.normalize();

    zbuffer = new float[width * height];
    for (int i = 0; i < width * height; i++) {
        zbuffer[i] = -std::numeric_limits<float>::infinity();
    }

    Viewport = viewport(0, 0, width, height);


    lookat(camera, center, up);

    Projection = Matrix::identity(4);
    Vec3f dir = camera - center;
    float dist = dir.norm();
    if (dist == 0.f) dist = 1.f;
    Projection[3][2] = -1.f / dist;

    TGAImage image(width, height, TGAImage::RGB);

    bool use_tex = model->has_diffuse();

    for (int i = 0; i < model->nfaces(); i++) {
        const std::vector<int>& face = model->face(i);
        int n = (int)face.size();
        if (n < 3) continue;

        bool face_uv = use_tex && model->face_has_uv(i);

        for (int k = 1; k + 1 < n; k++) {
            int i0 = face[0];
            int i1 = face[k];
            int i2 = face[k + 1];

            Vec3f world[3];
            world[0] = model->vert(i0);
            world[1] = model->vert(i1);
            world[2] = model->vert(i2);

            Vec3f pts[3];
            for (int j = 0; j < 3; j++) {
                Matrix v = v2m(world[j]);
                Matrix vp = Viewport * Projection * ModelView * v;
                pts[j] = m2v(vp);
            }

            Vec3f wpos[3] = { world[0], world[1], world[2] };
            Vec3f norms[3] = { model->normal(i0), model->normal(i1), model->normal(i2) };

            if (face_uv) {
                Vec2f uvs[3];
                uvs[0] = model->uv(i, 0);
                uvs[1] = model->uv(i, k);
                uvs[2] = model->uv(i, k + 1);

                triangle_phong_tex(pts, uvs, norms, wpos, image, zbuffer, light_dir, camera);
            }
            else {
                triangle_phong_flat(pts, norms, wpos, image, zbuffer, light_dir, camera, TGAColor(180, 180, 180, 255));
            }
        }
    }

    TGAColor glass(180, 220, 255, 255);
    float alpha = 0.15f;

    for (int t = 0; t < 12; t++) {
        Vec3f pts[3];
        for (int k = 0; k < 3; k++) {
            Vec3f v = C[F[t][k]];
            Vec3f sv = m2v((Viewport * Projection * ModelView) * v2m(v));
            pts[k] = sv;
        }
        triangle_alpha(pts, image, glass, alpha, zbuffer);
    }


    image.flip_vertically();
    image.write_tga_file("output.tga");

    delete[] zbuffer;
    delete model;
    model = nullptr;
    zbuffer = nullptr;

    return 0;
}
