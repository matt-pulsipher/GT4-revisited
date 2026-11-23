#pragma once

// What the menus' 3D faces (cars, models) share: the face's camera and lights as the engine's
// view takes them. docs/formats/menu.md, "Cars" and "Models".

#include "gt4r/menu/faces_car.h"
#include "render/view3d.h"

#include <SDL3/SDL_rect.h>

#include <algorithm>
#include <cmath>

namespace gt4r::menu_face3d {

namespace render = gt3r::render;
constexpr float kDeg = 3.14159265f / 180.0f;

// The view matrix of a face's camera (0x21AA30, mode 0): Ry(-ry) Rx(-rx) Rz(-rz) T(-t).
inline render::Mat4 view_of(const menu::FaceView& v) {
    return render::rotate_y(-v.camera_rotate[1] * kDeg) * render::rotate_x(-v.camera_rotate[0] * kDeg) * render::rotate_z(-v.camera_rotate[2] * kDeg) *
           render::translate({-v.camera_trans[0], -v.camera_trans[1], -v.camera_trans[2]});
}

// The projection of a face over the part `part` of the page (page units): the program's, whose
// picture is centred on the face's rectangle `face` and spans the whole page.
inline render::Mat4 projection_of(const menu::FaceView& v, const menu::Rect& face, const menu::Rect& part) {
    const menu::FaceProjection p = menu::FaceProjection::of(v.view_angle, face);
    const float left = (part.x - p.cx) / p.fx, right = (part.x + part.w - p.cx) / p.fx;
    const float up = (p.cy - part.y) / p.fy, down = (p.cy - part.y - part.h) / p.fy;
    return render::frustum(left, right, down, up, v.near, v.far);
}

// The face's lights in the engine's terms: light 0 the key, 1.. the fills (directions toward the
// light in world space, as GL's); every light's ambient adds to the world's. ❓ How the program's
// renderer sums them with the materials is not read: the engine's car lighting is used.
inline void light(const menu::FaceView& v, render::DrawOptions& o) {
    o.gt3_lighting = true;
    for (int i = 0; i < 3; ++i) {
        o.light_ambient[i] = v.perfect_dark ? 0 : v.world_ambient[i];
        o.light_diffuse[i] = o.light_specular[i] = 0;
    }
    o.fill_count = 0;
    if (v.perfect_dark) return;
    bool key = false;
    for (int li = 0; li < 3; ++li) {
        const menu::FaceLight& L = v.lights[li];
        if (!L.on) continue;
        for (int i = 0; i < 3; ++i) o.light_ambient[i] += L.amb[i];
        const render::Vec3 dir = render::normalize({L.pos[0], L.pos[1], L.pos[2]});
        if (!key) {
            key = true;
            o.light_dir = dir;
            for (int i = 0; i < 3; ++i) o.light_diffuse[i] = L.dif[i], o.light_specular[i] = L.spe[i];
        } else if (o.fill_count < 3) {
            const int k = o.fill_count++;
            o.fill_dir[k][0] = dir.x, o.fill_dir[k][1] = dir.y, o.fill_dir[k][2] = dir.z;
            for (int i = 0; i < 3; ++i) o.fill_diffuse[k][i] = L.dif[i], o.fill_specular[k][i] = L.spe[i];
        }
    }
}

// A part of the page on the screen: `at` is the face's rectangle in the page, `dst` the same on
// the screen.
inline SDL_FRect on_screen(const menu::Rect& part, const menu::Rect& at, const SDL_FRect& dst) {
    const float scale = at.w > 0 ? dst.w / at.w : dst.h / std::max(at.h, 1.0f);
    const float ox = dst.x - at.x * scale, oy = dst.y - at.y * scale;
    return {ox + part.x * scale, oy + part.y * scale, part.w * scale, part.h * scale};
}

} // namespace gt4r::menu_face3d
