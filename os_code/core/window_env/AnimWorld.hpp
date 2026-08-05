#ifndef OS_CODE_CORE_WINDOW_ENV_ANIM_WORLD_HPP_
#define OS_CODE_CORE_WINDOW_ENV_ANIM_WORLD_HPP_

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <memory>
#include <vector>

#include "esp_log.h"
#include "esp_heap_caps.h"

// ---------------------------------------------------------------------------
// Limits (embedded-friendly; raise if you have PSRAM headroom)
// ---------------------------------------------------------------------------
#ifndef AW_MAX_OBJECTS
#define AW_MAX_OBJECTS          64
#endif
#ifndef AW_MAX_SHAPE_VERTS
#define AW_MAX_SHAPE_VERTS      16
#endif
#ifndef AW_MAX_SHAPE_TRIS
#define AW_MAX_SHAPE_TRIS       14   // fan triangulation: n-2 for convex
#endif
#ifndef AW_MAX_CHILDREN
#define AW_MAX_CHILDREN         8
#endif
#ifndef AW_NAME_LEN
#define AW_NAME_LEN             24
#endif
#ifndef AW_TEXT_LEN
#define AW_TEXT_LEN             16
#endif

// Keep this header free of MWenv / Window includes so callers can include it
// without the full window stack. Draw uses an optional LocalToScreen callback.
class Canvas;

using AwLocalToScreenFn = void (*)(void* ctx, int lx, int ly, int& sx, int& sy);

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------

struct AwVec2 {
    float x = 0.f;
    float y = 0.f;

    AwVec2() = default;
    AwVec2(float x_, float y_) : x(x_), y(y_) {}

    AwVec2 operator+(const AwVec2& o) const { return {x + o.x, y + o.y}; }
    AwVec2 operator-(const AwVec2& o) const { return {x - o.x, y - o.y}; }
    AwVec2 operator*(float s) const { return {x * s, y * s}; }
    AwVec2& operator+=(const AwVec2& o) { x += o.x; y += o.y; return *this; }
    AwVec2& operator-=(const AwVec2& o) { x -= o.x; y -= o.y; return *this; }

    float length() const { return sqrtf(x * x + y * y); }
    float length_sq() const { return x * x + y * y; }
};

inline float aw_dot(const AwVec2& a, const AwVec2& b) { return a.x * b.x + a.y * b.y; }
inline float aw_cross(const AwVec2& a, const AwVec2& b) { return a.x * b.y - a.y * b.x; }

inline AwVec2 aw_rotate(const AwVec2& v, float radians) {
    float c = cosf(radians), s = sinf(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// ---------------------------------------------------------------------------
// World dimensionality & borders
// ---------------------------------------------------------------------------

enum class AwWorldDim : uint8_t {
    Dim2D = 0,
    Dim3D = 1   // reserved; engine is 2D-first
};

enum class AwBorderType : uint8_t {
    None = 0,
    Square,     // axis-aligned box [x_low..x_high] x [y_low..y_high]
    Circle      // center + radius
};

struct AwWorldBorder {
    AwBorderType type = AwBorderType::None;
    AwVec2       center{0.f, 0.f};
    float        x_low  = 0.f;
    float        x_high = 0.f;
    float        y_low  = 0.f;
    float        y_high = 0.f;
    float        radius = 0.f;   // Circle only

    // Bounce restitution when objects hit the border (0 = stick, 1 = elastic)
    float        restitution = 0.85f;
};

// ---------------------------------------------------------------------------
// Shape (local polygon, origin at 0,0)
// ---------------------------------------------------------------------------
// Vertices are LOCAL to the shape origin (typically the object's center).
// On create, a helper fan-triangulates (assumes convex) for draw, and builds
// an AABB used as the broad-phase / physics outline.

struct AwTri {
    uint8_t i0, i1, i2;
};

struct AwShape {
    AwVec2  verts[AW_MAX_SHAPE_VERTS];
    uint8_t vert_count = 0;

    AwTri   tris[AW_MAX_SHAPE_TRIS];
    uint8_t tri_count = 0;

    // Local AABB (relative to shape origin)
    float aabb_min_x = 0.f, aabb_min_y = 0.f;
    float aabb_max_x = 0.f, aabb_max_y = 0.f;

    bool    valid = false;

    // Build from local vertices. Evaluates triangulation + AABB ONCE.
    // Returns false if n < 3 or n > AW_MAX_SHAPE_VERTS, or non-simple enough.
    bool build_from_verts(const AwVec2* local_pts, uint8_t n);

    // Helpers for common primitives (local space, centered at 0,0)
    static AwShape make_rect(float half_w, float half_h);
    static AwShape make_circle_approx(float radius, uint8_t segments = 8);
    static AwShape make_triangle(float half_base, float height);
};

// ---------------------------------------------------------------------------
// Body / object
// ---------------------------------------------------------------------------

enum class AwBodyType : uint8_t {
    Static = 0,     // never moves (walls, board)
    Kinematic,      // moved by code, not by forces (paddles if you prefer)
    Dynamic         // integrates velocity / gravity
};

struct AwObject {
    char        name[AW_NAME_LEN] = {0};
    uint16_t    id = 0;                 // unique within world; 0 = free slot

    AwVec2      pos{0.f, 0.f};          // world position of local origin
    float       rotation = 0.f;         // radians about local origin
    int16_t     z_layer = 0;

    AwBodyType  body_type = AwBodyType::Dynamic;
    bool        sim_physics = true;     // master switch; weld can force false
    bool        alive = true;

    AwVec2      velocity{0.f, 0.f};
    float       angular_velocity = 0.f; // rad/s
    float       mass = 1.f;
    float       restitution = 0.8f;     // for object-object / border bounce
    float       friction = 0.05f;

    AwShape     shape;

    uint16_t    color = 0xFFFF;         // RGB565 draw hint
    bool        filled = true;

    // --- hierarchy / weld ---
    // Child does NOT run physics when parent_id != 0.
    // World position is derived each frame: parent_world + R(parent) * weld_offset
    uint16_t    parent_id = 0;          // 0 = no parent
    uint16_t    child_ids[AW_MAX_CHILDREN] = {0};
    uint8_t     child_count = 0;

    AwVec2      weld_offset{0.f, 0.f};  // in parent's local space
    float       weld_rot_offset = 0.f;  // added to parent rotation
    bool        weld_inherit_rot = true;

    // Optional text payload (e.g. "2048" tile value). Not simulated.
    // When welded as a child of a tile square, this rides along with parent.
    char        text[AW_TEXT_LEN] = {0};
    uint16_t    text_color = 0x0000;
    uint8_t     text_size  = 1;         // fb_draw_text size mult

    // Cached world-space AABB (updated during step / after weld sync)
    float       world_aabb_min_x = 0.f, world_aabb_min_y = 0.f;
    float       world_aabb_max_x = 0.f, world_aabb_max_y = 0.f;

    bool        needs_phys_check = true; // can throttle; false skips collision this frame
    bool        visible = true;          // draw toggle independent of alive
};

// ---------------------------------------------------------------------------
// Camera: maps world space → canvas / window-local pixels
// ---------------------------------------------------------------------------
// world point W becomes pixel P = (W - origin) * scale + screen_offset
struct AwCamera {
    AwVec2 origin{0.f, 0.f};       // world point mapped to pixel (0,0) before offset
    float  scale = 1.f;            // world units → pixels
    int    offset_x = 0;           // extra pixel shift (canvas origin in window)
    int    offset_y = 0;
};

// ---------------------------------------------------------------------------
// Collision event (optional callback)
// ---------------------------------------------------------------------------

struct AwContact {
    uint16_t a_id;
    uint16_t b_id;          // 0 = world border
    AwVec2   normal;        // from a toward b, or inward for border
    float    penetration;
};

using AwContactCallback = void (*)(const AwContact& c, void* user);

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

struct AwWorldConfig {
    AwWorldDim    dim = AwWorldDim::Dim2D;
    AwWorldBorder border;
    AwVec2        gravity{0.f, 0.f};     // e.g. {0, 200} for downward
    float         fixed_dt = 1.f / 60.f;
    uint8_t       velocity_iterations = 2;
    bool          enable_object_collisions = true;
    bool          enable_border_collisions = true;
};

class AnimWorld {
public:
    explicit AnimWorld(const AwWorldConfig& cfg = AwWorldConfig{});
    ~AnimWorld();

    AnimWorld(const AnimWorld&) = delete;
    AnimWorld& operator=(const AnimWorld&) = delete;

    // --- config ---
    void set_border(const AwWorldBorder& b);
    void set_gravity(AwVec2 g);
    const AwWorldConfig& config() const { return m_cfg; }

    // --- object lifecycle ---
    // Returns object id (>0) or 0 on failure.
    uint16_t create_object(const char* name,
                           AwVec2 pos,
                           const AwShape& shape,
                           AwBodyType body = AwBodyType::Dynamic,
                           int16_t z_layer = 0);

    bool destroy_object(uint16_t id);   // also destroys welded children recursively
    AwObject* get(uint16_t id);
    const AwObject* get(uint16_t id) const;

    // Find by name (first match). Returns 0 if none.
    uint16_t find_by_name(const char* name) const;

    // --- motion / forces ---
    void set_velocity(uint16_t id, AwVec2 v);
    void set_position(uint16_t id, AwVec2 p);
    void set_rotation(uint16_t id, float radians);
    void apply_impulse(uint16_t id, AwVec2 impulse);

    // --- weld hierarchy ---
    // Weld child onto parent at offset in parent's LOCAL space.
    // Disables child's physics simulation for as long as welded.
    // If parent is destroyed, children are destroyed with it.
    bool weld(uint16_t parent_id, uint16_t child_id,
              AwVec2 local_offset,
              float local_rot_offset = 0.f,
              bool inherit_rotation = true);

    bool unweld(uint16_t child_id);     // re-enables child's sim_physics

    // Convenience for 2048-style tiles: create a rect + text child, welded.
    // text_local_offset is in the square's local space (usually {0,0} for center).
    // Deleting the returned tile id destroys the text child automatically.
    uint16_t create_welded_tile(const char* name,
                                AwVec2 pos,
                                float half_w, float half_h,
                                const char* label,
                                uint16_t tile_color,
                                uint16_t text_color,
                                AwVec2 text_local_offset = {0.f, 0.f});

    // Update label on a welded text child (or any object that carries text).
    bool set_object_text(uint16_t id, const char* label, uint16_t color = 0xFFFF);
    // Find the first text-bearing child of a tile (for 2048 value updates).
    uint16_t find_text_child(uint16_t parent_id) const;

    // --- simulation ---
    // Call once per frame with real delta (seconds). Internally substeps
    // with fixed_dt if needed.
    void step(float dt);

    // Toggle whether an object participates in collision this frame / until set again.
    void set_phys_check(uint16_t id, bool enabled);

    // --- iteration / draw helpers ---
    uint16_t object_count() const { return m_count; }
    // Visits alive objects sorted by z_layer ascending (back to front).
    // fn signature: void(const AwObject& obj)
    template <typename Fn>
    void for_each_sorted(Fn&& fn) const;

    // Fill out array of up to max_out alive object pointers (unsorted).
    uint16_t gather_alive(AwObject** out, uint16_t max_out);

    void set_contact_callback(AwContactCallback cb, void* user = nullptr);

    // Recompute world AABB for one object (after manual pos change).
    void update_world_aabb(AwObject& o);

    // --- drawing ---
    // Draw all visible objects. World AABB → pixels via camera; filled shapes
    // use AABB rects (fast path for pong / 2048). Objects with non-empty text[]
    // get a centered label.
    //
    // If l2s is non-null, each local pixel is passed through it (use this to
    // apply Window::LocalToScreen without AnimWorld depending on Window).
    // If l2s is null, cam.offset_* are treated as absolute screen pixels.
    void draw(const AwCamera& cam,
              AwLocalToScreenFn l2s = nullptr,
              void* l2s_ctx = nullptr) const;

    // World → pixel helper (public so Canvas / games can share the same mapping).
    static void world_to_pixel(const AwCamera& cam, float wx, float wy, int& px, int& py);

private:
    AwWorldConfig m_cfg;
    AwObject*     m_objects = nullptr;  // array of AW_MAX_OBJECTS in PSRAM if available
    uint16_t      m_count = 0;          // highest used slot (not sparse count)
    uint16_t      m_next_id = 1;
    float         m_accum = 0.f;

    AwContactCallback m_contact_cb = nullptr;
    void*             m_contact_user = nullptr;

    AwObject* alloc_slot();
    AwObject* find_slot(uint16_t id);
    const AwObject* find_slot(uint16_t id) const;
    void destroy_recursive(uint16_t id);
    void sync_welded_children(AwObject& parent);
    void integrate(AwObject& o, float dt);
    void resolve_border(AwObject& o);
    void resolve_pair(AwObject& a, AwObject& b);
    void fixed_step(float dt);
};

// ---------------------------------------------------------------------------
// Template out-of-line
// ---------------------------------------------------------------------------
template <typename Fn>
void AnimWorld::for_each_sorted(Fn&& fn) const {
    // Small fixed stack sort by z (insertion sort; N is small)
    const AwObject* order[AW_MAX_OBJECTS];
    uint16_t n = 0;
    for (uint16_t i = 0; i < AW_MAX_OBJECTS; ++i) {
        if (m_objects[i].alive && m_objects[i].id != 0)
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
    for (uint16_t i = 0; i < n; ++i)
        fn(*order[i]);
}

#endif // OS_CODE_CORE_WINDOW_ENV_ANIM_WORLD_HPP_
