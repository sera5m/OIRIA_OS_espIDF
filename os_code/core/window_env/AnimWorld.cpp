#include "AnimWorld.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <algorithm>

#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"

// Unique name – never "TAG" (avoids collisions if a unity build or mistaken
// #include "*.cpp" merges multiple TUs).
static const char* s_aw_tag = "AnimWorld";

// ---------------------------------------------------------------------------
// Shape builders
// ---------------------------------------------------------------------------

bool AwShape::build_from_verts(const AwVec2* local_pts, uint8_t n) {
    valid = false;
    vert_count = 0;
    tri_count = 0;
    if (!local_pts || n < 3 || n > AW_MAX_SHAPE_VERTS)
        return false;

    for (uint8_t i = 0; i < n; ++i)
        verts[i] = local_pts[i];
    vert_count = n;

    // Fan triangulation (convex polygons). Index 0 is the fan pivot.
    tri_count = 0;
    for (uint8_t i = 1; i + 1 < n && tri_count < AW_MAX_SHAPE_TRIS; ++i) {
        tris[tri_count++] = AwTri{0, i, static_cast<uint8_t>(i + 1)};
    }

    // Local AABB
    aabb_min_x = aabb_max_x = verts[0].x;
    aabb_min_y = aabb_max_y = verts[0].y;
    for (uint8_t i = 1; i < n; ++i) {
        if (verts[i].x < aabb_min_x) aabb_min_x = verts[i].x;
        if (verts[i].x > aabb_max_x) aabb_max_x = verts[i].x;
        if (verts[i].y < aabb_min_y) aabb_min_y = verts[i].y;
        if (verts[i].y > aabb_max_y) aabb_max_y = verts[i].y;
    }

    valid = (tri_count > 0);
    return valid;
}

AwShape AwShape::make_rect(float half_w, float half_h) {
    AwShape s;
    AwVec2 pts[4] = {
        {-half_w, -half_h},
        { half_w, -half_h},
        { half_w,  half_h},
        {-half_w,  half_h}
    };
    s.build_from_verts(pts, 4);
    return s;
}

AwShape AwShape::make_circle_approx(float radius, uint8_t segments) {
    AwShape s;
    if (segments < 3) segments = 3;
    if (segments > AW_MAX_SHAPE_VERTS) segments = AW_MAX_SHAPE_VERTS;
    AwVec2 pts[AW_MAX_SHAPE_VERTS];
    const float step = 6.28318530718f / (float)segments;
    for (uint8_t i = 0; i < segments; ++i) {
        float a = step * (float)i;
        pts[i] = {cosf(a) * radius, sinf(a) * radius};
    }
    s.build_from_verts(pts, segments);
    return s;
}

AwShape AwShape::make_triangle(float half_base, float height) {
    AwShape s;
    AwVec2 pts[3] = {
        {0.f,       -height * 0.5f},
        {-half_base, height * 0.5f},
        { half_base, height * 0.5f}
    };
    s.build_from_verts(pts, 3);
    return s;
}

// ---------------------------------------------------------------------------
// World construction
// ---------------------------------------------------------------------------

AnimWorld::AnimWorld(const AwWorldConfig& cfg)
    : m_cfg(cfg)
{
    // Prefer PSRAM for the object table
    m_objects = (AwObject*)heap_caps_calloc(AW_MAX_OBJECTS, sizeof(AwObject),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m_objects) {
        m_objects = (AwObject*)heap_caps_calloc(AW_MAX_OBJECTS, sizeof(AwObject),
                                                MALLOC_CAP_8BIT);
    }
    if (!m_objects) {
        ESP_LOGE(s_aw_tag, "Failed to allocate object table");
        return;
    }
    ESP_LOGI(s_aw_tag, "AnimWorld ready (%d object slots)", AW_MAX_OBJECTS);
}

AnimWorld::~AnimWorld() {
    if (m_objects) {
        heap_caps_free(m_objects);
        m_objects = nullptr;
    }
}

void AnimWorld::set_border(const AwWorldBorder& b) { m_cfg.border = b; }
void AnimWorld::set_gravity(AwVec2 g) { m_cfg.gravity = g; }

// ---------------------------------------------------------------------------
// Slot helpers
// ---------------------------------------------------------------------------

AwObject* AnimWorld::alloc_slot() {
    if (!m_objects) return nullptr;
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        if (!m_objects[i].alive || m_objects[i].id == 0) {
            AwObject* o = &m_objects[i];
            *o = AwObject{};   // zero / default
            o->alive = true;
            o->id = m_next_id++;
            if (m_next_id == 0) m_next_id = 1; // skip 0
            if (i + 1 > m_count) m_count = i + 1;
            return o;
        }
    }
    ESP_LOGW(s_aw_tag, "Object table full");
    return nullptr;
}

AwObject* AnimWorld::find_slot(uint16_t id) {
    if (!m_objects || id == 0) return nullptr;
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        if (m_objects[i].alive && m_objects[i].id == id)
            return &m_objects[i];
    }
    return nullptr;
}

const AwObject* AnimWorld::find_slot(uint16_t id) const {
    return const_cast<AnimWorld*>(this)->find_slot(id);
}

AwObject* AnimWorld::get(uint16_t id) { return find_slot(id); }
const AwObject* AnimWorld::get(uint16_t id) const { return find_slot(id); }

uint16_t AnimWorld::find_by_name(const char* name) const {
    if (!name || !m_objects) return 0;
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        if (m_objects[i].alive && m_objects[i].id != 0 &&
            strncmp(m_objects[i].name, name, AW_NAME_LEN) == 0)
            return m_objects[i].id;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// AABB
// ---------------------------------------------------------------------------

void AnimWorld::update_world_aabb(AwObject& o) {
    if (!o.shape.valid) {
        o.world_aabb_min_x = o.pos.x;
        o.world_aabb_min_y = o.pos.y;
        o.world_aabb_max_x = o.pos.x;
        o.world_aabb_max_y = o.pos.y;
        return;
    }

    // Rotate local AABB corners into world (cheap OBB->AABB)
    const float corners[4][2] = {
        {o.shape.aabb_min_x, o.shape.aabb_min_y},
        {o.shape.aabb_max_x, o.shape.aabb_min_y},
        {o.shape.aabb_max_x, o.shape.aabb_max_y},
        {o.shape.aabb_min_x, o.shape.aabb_max_y}
    };

    float minx =  1e9f, miny =  1e9f;
    float maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < 4; ++i) {
        AwVec2 r = aw_rotate({corners[i][0], corners[i][1]}, o.rotation);
        float wx = o.pos.x + r.x;
        float wy = o.pos.y + r.y;
        if (wx < minx) minx = wx;
        if (wy < miny) miny = wy;
        if (wx > maxx) maxx = wx;
        if (wy > maxy) maxy = wy;
    }
    o.world_aabb_min_x = minx;
    o.world_aabb_min_y = miny;
    o.world_aabb_max_x = maxx;
    o.world_aabb_max_y = maxy;
}

// ---------------------------------------------------------------------------
// Create / destroy
// ---------------------------------------------------------------------------

uint16_t AnimWorld::create_object(const char* name,
                                  AwVec2 pos,
                                  const AwShape& shape,
                                  AwBodyType body,
                                  int16_t z_layer)
{
    AwObject* o = alloc_slot();
    if (!o) return 0;

    if (name) {
        strncpy(o->name, name, AW_NAME_LEN - 1);
        o->name[AW_NAME_LEN - 1] = '\0';
    }
    o->pos = pos;
    o->shape = shape;
    o->body_type = body;
    o->z_layer = z_layer;
    o->sim_physics = (body == AwBodyType::Dynamic);
    update_world_aabb(*o);
    return o->id;
}

void AnimWorld::destroy_recursive(uint16_t id) {
    AwObject* o = find_slot(id);
    if (!o) return;

    // Destroy children first
    for (uint8_t c = 0; c < o->child_count; ++c) {
        uint16_t cid = o->child_ids[c];
        if (cid) destroy_recursive(cid);
    }
    o->child_count = 0;

    // Detach from parent
    if (o->parent_id) {
        AwObject* p = find_slot(o->parent_id);
        if (p) {
            for (uint8_t c = 0; c < p->child_count; ++c) {
                if (p->child_ids[c] == id) {
                    // shift left
                    for (uint8_t k = c; k + 1 < p->child_count; ++k)
                        p->child_ids[k] = p->child_ids[k + 1];
                    p->child_count--;
                    break;
                }
            }
        }
    }

    *o = AwObject{};  // clear slot
}

bool AnimWorld::destroy_object(uint16_t id) {
    if (!find_slot(id)) return false;
    destroy_recursive(id);
    return true;
}

// ---------------------------------------------------------------------------
// Motion setters
// ---------------------------------------------------------------------------

void AnimWorld::set_velocity(uint16_t id, AwVec2 v) {
    if (AwObject* o = find_slot(id)) o->velocity = v;
}

void AnimWorld::set_position(uint16_t id, AwVec2 p) {
    if (AwObject* o = find_slot(id)) {
        o->pos = p;
        update_world_aabb(*o);
        sync_welded_children(*o);
    }
}

void AnimWorld::set_rotation(uint16_t id, float radians) {
    if (AwObject* o = find_slot(id)) {
        o->rotation = radians;
        update_world_aabb(*o);
        sync_welded_children(*o);
    }
}

void AnimWorld::apply_impulse(uint16_t id, AwVec2 impulse) {
    AwObject* o = find_slot(id);
    if (!o || !o->sim_physics || o->body_type != AwBodyType::Dynamic) return;
    if (o->mass <= 0.f) return;
    o->velocity += impulse * (1.f / o->mass);
}

void AnimWorld::set_phys_check(uint16_t id, bool enabled) {
    if (AwObject* o = find_slot(id)) o->needs_phys_check = enabled;
}

// ---------------------------------------------------------------------------
// Weld
// ---------------------------------------------------------------------------

bool AnimWorld::weld(uint16_t parent_id, uint16_t child_id,
                     AwVec2 local_offset,
                     float local_rot_offset,
                     bool inherit_rotation)
{
    if (parent_id == 0 || child_id == 0 || parent_id == child_id)
        return false;

    AwObject* parent = find_slot(parent_id);
    AwObject* child  = find_slot(child_id);
    if (!parent || !child) return false;

    // Already welded to someone else?
    if (child->parent_id != 0 && child->parent_id != parent_id)
        unweld(child_id);

    if (parent->child_count >= AW_MAX_CHILDREN) {
        ESP_LOGW(s_aw_tag, "Parent '%s' child list full", parent->name);
        return false;
    }

    // Prevent cycles: walk up from parent
    uint16_t walk = parent->parent_id;
    while (walk) {
        if (walk == child_id) {
            ESP_LOGW(s_aw_tag, "weld would create cycle");
            return false;
        }
        AwObject* w = find_slot(walk);
        if (!w) break;
        walk = w->parent_id;
    }

    child->parent_id = parent_id;
    child->weld_offset = local_offset;
    child->weld_rot_offset = local_rot_offset;
    child->weld_inherit_rot = inherit_rotation;

    // Children never simulate physics while welded
    child->sim_physics = false;
    child->velocity = {0.f, 0.f};
    child->angular_velocity = 0.f;

    parent->child_ids[parent->child_count++] = child_id;

    // Snap child into place immediately
    sync_welded_children(*parent);
    ESP_LOGI(s_aw_tag, "Welded '%s' -> '%s' offset=(%.1f,%.1f)",
             child->name, parent->name, local_offset.x, local_offset.y);
    return true;
}

bool AnimWorld::unweld(uint16_t child_id) {
    AwObject* child = find_slot(child_id);
    if (!child || child->parent_id == 0) return false;

    AwObject* parent = find_slot(child->parent_id);
    if (parent) {
        for (uint8_t c = 0; c < parent->child_count; ++c) {
            if (parent->child_ids[c] == child_id) {
                for (uint8_t k = c; k + 1 < parent->child_count; ++k)
                    parent->child_ids[k] = parent->child_ids[k + 1];
                parent->child_count--;
                break;
            }
        }
    }

    child->parent_id = 0;
    // Restore physics only if body type allows it
    child->sim_physics = (child->body_type == AwBodyType::Dynamic);
    return true;
}

void AnimWorld::sync_welded_children(AwObject& parent) {
    for (uint8_t c = 0; c < parent.child_count; ++c) {
        AwObject* child = find_slot(parent.child_ids[c]);
        if (!child || !child->alive) continue;

        AwVec2 off = child->weld_offset;
        if (child->weld_inherit_rot)
            off = aw_rotate(off, parent.rotation);

        child->pos = parent.pos + off;
        if (child->weld_inherit_rot)
            child->rotation = parent.rotation + child->weld_rot_offset;
        else
            child->rotation = child->weld_rot_offset;

        update_world_aabb(*child);

        // Nested welds
        if (child->child_count > 0)
            sync_welded_children(*child);
    }
}

uint16_t AnimWorld::create_welded_tile(const char* name,
                                       AwVec2 pos,
                                       float half_w, float half_h,
                                       const char* label,
                                       uint16_t tile_color,
                                       uint16_t text_color,
                                       AwVec2 text_local_offset)
{
    AwShape rect = AwShape::make_rect(half_w, half_h);
    uint16_t tile_id = create_object(name, pos, rect, AwBodyType::Kinematic, 0);
    if (!tile_id) return 0;

    AwObject* tile = find_slot(tile_id);
    tile->color = tile_color;
    tile->filled = true;
    tile->sim_physics = false; // board tiles usually kinematic / static

    // Text is a zero-size "marker" child (draw layer reads obj.text)
    char text_name[AW_NAME_LEN];
    snprintf(text_name, sizeof(text_name), "%s_txt", name ? name : "t");

    AwShape empty{};
    empty.valid = false; // no geometry; pure label

    uint16_t text_id = create_object(text_name, pos, empty, AwBodyType::Static, 1);
    if (!text_id) {
        destroy_object(tile_id);
        return 0;
    }

    AwObject* text = find_slot(text_id);
    text->color = text_color;
    text->text_color = text_color;
    text->text_size = 1;
    if (label) {
        strncpy(text->text, label, AW_TEXT_LEN - 1);
        text->text[AW_TEXT_LEN - 1] = '\0';
    }

    if (!weld(tile_id, text_id, text_local_offset, 0.f, true)) {
        destroy_object(tile_id); // cascades to text
        return 0;
    }

    return tile_id;
}

bool AnimWorld::set_object_text(uint16_t id, const char* label, uint16_t color) {
    AwObject* o = find_slot(id);
    if (!o) return false;
    if (label) {
        strncpy(o->text, label, AW_TEXT_LEN - 1);
        o->text[AW_TEXT_LEN - 1] = '\0';
    } else {
        o->text[0] = '\0';
    }
    o->text_color = color;
    return true;
}

uint16_t AnimWorld::find_text_child(uint16_t parent_id) const {
    const AwObject* p = find_slot(parent_id);
    if (!p) return 0;
    for (uint8_t c = 0; c < p->child_count; ++c) {
        const AwObject* ch = find_slot(p->child_ids[c]);
        if (ch && ch->text[0] != '\0')
            return ch->id;
    }
    // Also accept any child with empty shape (label marker)
    for (uint8_t c = 0; c < p->child_count; ++c) {
        const AwObject* ch = find_slot(p->child_ids[c]);
        if (ch && !ch->shape.valid)
            return ch->id;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Integration & collision
// ---------------------------------------------------------------------------

void AnimWorld::integrate(AwObject& o, float dt) {
    if (!o.sim_physics || o.body_type != AwBodyType::Dynamic)
        return;
    if (o.parent_id != 0)
        return; // welded children never integrate

    o.velocity += m_cfg.gravity * dt;
    o.pos += o.velocity * dt;
    o.rotation += o.angular_velocity * dt;

    // light damping
    o.velocity = o.velocity * (1.f - o.friction * dt);
    o.angular_velocity *= (1.f - o.friction * dt);

    update_world_aabb(o);
}

void AnimWorld::resolve_border(AwObject& o) {
    if (!m_cfg.enable_border_collisions) return;
    if (!o.sim_physics || o.body_type == AwBodyType::Static) return;
    if (o.parent_id != 0) return;
    if (!o.needs_phys_check) return;

    const AwWorldBorder& b = m_cfg.border;
    if (b.type == AwBorderType::None) return;

    bool hit = false;
    AwVec2 normal{0.f, 0.f};
    float pen = 0.f;

    if (b.type == AwBorderType::Square) {
        // Push AABB back inside the box
        if (o.world_aabb_min_x < b.x_low) {
            float d = b.x_low - o.world_aabb_min_x;
            o.pos.x += d;
            if (o.velocity.x < 0.f) o.velocity.x = -o.velocity.x * b.restitution;
            normal = {1.f, 0.f}; pen = d; hit = true;
        }
        if (o.world_aabb_max_x > b.x_high) {
            float d = o.world_aabb_max_x - b.x_high;
            o.pos.x -= d;
            if (o.velocity.x > 0.f) o.velocity.x = -o.velocity.x * b.restitution;
            normal = {-1.f, 0.f}; pen = d; hit = true;
        }
        if (o.world_aabb_min_y < b.y_low) {
            float d = b.y_low - o.world_aabb_min_y;
            o.pos.y += d;
            if (o.velocity.y < 0.f) o.velocity.y = -o.velocity.y * b.restitution;
            normal = {0.f, 1.f}; pen = d; hit = true;
        }
        if (o.world_aabb_max_y > b.y_high) {
            float d = o.world_aabb_max_y - b.y_high;
            o.pos.y -= d;
            if (o.velocity.y > 0.f) o.velocity.y = -o.velocity.y * b.restitution;
            normal = {0.f, -1.f}; pen = d; hit = true;
        }
    } else if (b.type == AwBorderType::Circle) {
        // Approximate with object center vs circle
        AwVec2 d = o.pos - b.center;
        float dist = d.length();
        // Use max extent of AABB as crude radius
        float obj_r = 0.5f * fmaxf(o.world_aabb_max_x - o.world_aabb_min_x,
                                   o.world_aabb_max_y - o.world_aabb_min_y);
        float max_r = b.radius - obj_r;
        if (max_r < 0.f) max_r = 0.f;
        if (dist > max_r && dist > 1e-5f) {
            AwVec2 n = d * (1.f / dist);
            float overflow = dist - max_r;
            o.pos -= n * overflow;
            // Reflect velocity
            float vn = aw_dot(o.velocity, n);
            if (vn > 0.f)
                o.velocity -= n * (vn * (1.f + b.restitution));
            normal = n * -1.f;
            pen = overflow;
            hit = true;
        }
    }

    if (hit) {
        update_world_aabb(o);
        if (m_contact_cb) {
            AwContact c{o.id, 0, normal, pen};
            m_contact_cb(c, m_contact_user);
        }
    }
}

static bool aabb_overlap(const AwObject& a, const AwObject& b) {
    return !(a.world_aabb_max_x < b.world_aabb_min_x ||
             a.world_aabb_min_x > b.world_aabb_max_x ||
             a.world_aabb_max_y < b.world_aabb_min_y ||
             a.world_aabb_min_y > b.world_aabb_max_y);
}

void AnimWorld::resolve_pair(AwObject& a, AwObject& b) {
    // Skip if either is a welded child without its own physics interest
    if (a.parent_id || b.parent_id) return;
    if (!a.needs_phys_check && !b.needs_phys_check) return;
    if (!aabb_overlap(a, b)) return;

    // Minimal translation vector along principal axes
    float overlap_x = fminf(a.world_aabb_max_x - b.world_aabb_min_x,
                            b.world_aabb_max_x - a.world_aabb_min_x);
    float overlap_y = fminf(a.world_aabb_max_y - b.world_aabb_min_y,
                            b.world_aabb_max_y - a.world_aabb_min_y);

    if (overlap_x <= 0.f || overlap_y <= 0.f) return;

    AwVec2 normal;
    float pen;
    if (overlap_x < overlap_y) {
        pen = overlap_x;
        normal = (a.pos.x < b.pos.x) ? AwVec2{-1.f, 0.f} : AwVec2{1.f, 0.f};
    } else {
        pen = overlap_y;
        normal = (a.pos.y < b.pos.y) ? AwVec2{0.f, -1.f} : AwVec2{0.f, 1.f};
    }

    bool a_dyn = a.sim_physics && a.body_type == AwBodyType::Dynamic;
    bool b_dyn = b.sim_physics && b.body_type == AwBodyType::Dynamic;

    if (!a_dyn && !b_dyn) return;

    float inv_a = a_dyn ? (1.f / fmaxf(a.mass, 1e-4f)) : 0.f;
    float inv_b = b_dyn ? (1.f / fmaxf(b.mass, 1e-4f)) : 0.f;
    float inv_sum = inv_a + inv_b;
    if (inv_sum <= 0.f) return;

    // Positional correction
    AwVec2 corr = normal * (pen / inv_sum);
    if (a_dyn) { a.pos += corr * inv_a; update_world_aabb(a); }
    if (b_dyn) { b.pos -= corr * inv_b; update_world_aabb(b); }

    // Velocity impulse along normal
    AwVec2 rel = a.velocity - b.velocity;
    float vel_n = aw_dot(rel, normal);
    if (vel_n > 0.f) return; // separating

    float e = fminf(a.restitution, b.restitution);
    float j = -(1.f + e) * vel_n / inv_sum;
    AwVec2 impulse = normal * j;

    if (a_dyn) a.velocity += impulse * inv_a;
    if (b_dyn) b.velocity -= impulse * inv_b;

    if (m_contact_cb) {
        AwContact c{a.id, b.id, normal, pen};
        m_contact_cb(c, m_contact_user);
    }
}

void AnimWorld::fixed_step(float dt) {
    if (!m_objects) return;

    // 1) Integrate dynamics
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        if (!m_objects[i].alive || m_objects[i].id == 0) continue;
        integrate(m_objects[i], dt);
    }

    // 2) Border
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        if (!m_objects[i].alive || m_objects[i].id == 0) continue;
        resolve_border(m_objects[i]);
    }

    // 3) Object-object (pairs)
    if (m_cfg.enable_object_collisions) {
        for (uint8_t it = 0; it < m_cfg.velocity_iterations; ++it) {
            for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
                if (!m_objects[i].alive || m_objects[i].id == 0) continue;
                for (uint16_t j = i + 1; j < AW_MAX_OBJECTS; ++j) {
                    if (!m_objects[j].alive || m_objects[j].id == 0) continue;
                    resolve_pair(m_objects[i], m_objects[j]);
                }
            }
        }
    }

    // 4) Propagate welds (parents may have moved)
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        AwObject& o = m_objects[i];
        if (!o.alive || o.id == 0) continue;
        if (o.parent_id == 0 && o.child_count > 0)
            sync_welded_children(o);
    }
}

void AnimWorld::step(float dt) {
    if (dt <= 0.f || !m_objects) return;

    // Clamp crazy frames (e.g. after blocking flash)
    if (dt > 0.1f) dt = 0.1f;

    const float h = m_cfg.fixed_dt > 0.f ? m_cfg.fixed_dt : (1.f / 60.f);
    m_accum += dt;

    int guard = 0;
    while (m_accum >= h && guard < 8) {
        fixed_step(h);
        m_accum -= h;
        ++guard;
    }
}

// ---------------------------------------------------------------------------
// Iteration helpers
// ---------------------------------------------------------------------------

uint16_t AnimWorld::gather_alive(AwObject** out, uint16_t max_out) {
    if (!out || !m_objects || max_out == 0) return 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < AW_MAX_OBJECTS && n < max_out; ++i) {
        if (m_objects[i].alive && m_objects[i].id != 0)
            out[n++] = &m_objects[i];
    }
    return n;
}

void AnimWorld::set_contact_callback(AwContactCallback cb, void* user) {
    m_contact_cb = cb;
    m_contact_user = user;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void AnimWorld::world_to_pixel(const AwCamera& cam, float wx, float wy, int& px, int& py) {
    px = (int)((wx - cam.origin.x) * cam.scale) + cam.offset_x;
    py = (int)((wy - cam.origin.y) * cam.scale) + cam.offset_y;
}

void AnimWorld::draw(const AwCamera& cam,
                     AwLocalToScreenFn l2s,
                     void* l2s_ctx) const {
    if (!m_objects) return;

    // Back-to-front via z_layer
    const AwObject* order[AW_MAX_OBJECTS];
    uint16_t n = 0;
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        if (m_objects[i].alive && m_objects[i].id != 0 && m_objects[i].visible)
            order[n++] = &m_objects[i];
    }
    for (uint16_t i = 1; i < n; ++i) {
        const AwObject* key = order[i];
        int j = (int)i - 1;
        while (j >= 0 && order[j]->z_layer > key->z_layer) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = key;
    }

    auto to_screen = [&](int lx, int ly, int& sx, int& sy) {
        if (l2s) {
            l2s(l2s_ctx, lx, ly, sx, sy);
        } else {
            sx = lx;
            sy = ly;
        }
    };

    for (uint16_t i = 0; i < n; ++i) {
        const AwObject& o = *order[i];

        // Geometry: fast AABB → screen rect (covers pong paddles/ball & 2048 tiles)
        if (o.shape.valid) {
            int x0, y0, x1, y1;
            world_to_pixel(cam, o.world_aabb_min_x, o.world_aabb_min_y, x0, y0);
            world_to_pixel(cam, o.world_aabb_max_x, o.world_aabb_max_y, x1, y1);

            if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
            if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }

            int sx = x0, sy = y0, sw = x1 - x0, sh = y1 - y0;
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;

            int sxa, sya, sxb, syb;
            to_screen(sx, sy, sxa, sya);
            to_screen(sx + sw, sy + sh, sxb, syb);
            int rx = sxa < sxb ? sxa : sxb;
            int ry = sya < syb ? sya : syb;
            int rw = (sxa > sxb ? sxa : sxb) - rx;
            int rh = (sya > syb ? sya : syb) - ry;
            if (rw > 0 && rh > 0) {
                if (o.filled)
                    fb_rect(true, 1, rx, ry, rw, rh, o.color, o.color);
                else
                    fb_rect(false, 1, rx, ry, rw, rh, o.color, o.color);
            }
        }

        // Label (welded text children, or text on the object itself)
        if (o.text[0] != '\0') {
            int cx, cy;
            float midx = 0.5f * (o.world_aabb_min_x + o.world_aabb_max_x);
            float midy = 0.5f * (o.world_aabb_min_y + o.world_aabb_max_y);
            if (!o.shape.valid) {
                midx = o.pos.x;
                midy = o.pos.y;
            }
            world_to_pixel(cam, midx, midy, cx, cy);

            const int glen = (int)strlen(o.text);
            const int gw = 6 * (o.text_size > 0 ? o.text_size : 1);
            const int gh = 8 * (o.text_size > 0 ? o.text_size : 1);
            cx -= (glen * gw) / 2;
            cy -= gh / 2;

            int tx, ty;
            to_screen(cx, cy, tx, ty);
            fb_draw_text(0, tx, ty, o.text,
                         o.text_color ? o.text_color : o.color,
                         o.text_size > 0 ? o.text_size : 1,
                         0, true, 0x0000, 0, ft_AVR_classic_6x8);
        }
    }
}
