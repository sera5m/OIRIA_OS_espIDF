#ifndef OS_CODE_CORE_WINDOW_ENV_CANVAS_HPP_
#define OS_CODE_CORE_WINDOW_ENV_CANVAS_HPP_

#include <stdint.h>
#include <memory>
#include <atomic>

#include "hardware/drivers/lcd/st7789v2/t_shapes.h"
#include "code_stuff/types.h" // s_bounds_16u
#include "AnimWorld.hpp"

class Window; // forward

// ---------------------------------------------------------------------------
// Input zones. A touch inside a zone emits a key, a mouse delta, or a macro.
// ---------------------------------------------------------------------------
enum class ZoneKind : uint8_t { Key, Mouse, Macro };

struct InputZone {
    s_bounds_16u bounds{};     /* canvas-local */
    ZoneKind     kind = ZoneKind::Key;
    uint16_t     key = 0;      /* Key: the key. Mouse: unused. Macro: first key */
    int16_t      dx = 0;       /* Mouse: delta per touch */
    int16_t      dy = 0;
    uint16_t     macro[4] = {};/* Macro: up to 4 keys */
    uint8_t      macro_n = 0;
    bool         enabled = true;
};



// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

struct CanvasCfg {
    int x = 0;
    int y = 0;
    int width  = 32;
    int height = 32;

    bool borderless = true;
    bool DrawBG     = true;

    uint16_t bgColor     = 0x0000;
    uint16_t BorderColor = 0xFFFF;

    Window* parentWindow = nullptr;
    float   UpdateTickRateS = 0.1f;
};

// ---------------------------------------------------------------------------
// Canvas – shape buffer owned by a Window, optional AnimWorld attach
// ---------------------------------------------------------------------------

class Canvas : public std::enable_shared_from_this<Canvas> {
public:
    static std::shared_ptr<Canvas> Create(const CanvasCfg& cfg) {
        return std::shared_ptr<Canvas>(new Canvas(cfg));
    }

    ~Canvas();

    void Update(float deltaTime);
    void Draw();

    // Shape management
    fb_shape_t* AddShape(fb_shape_type type, s_bounds_16u bounds,
                         uint16_t color, uint8_t layer);

    void RemoveShape(uint16_t index);
    void ClearShapes();
    void SortShapes();
    void SetShapeVisible(uint16_t index, bool visible);

    fb_shape_buffer_t* GetShapeBuffer() { return m_shapeBuffer; }

    // --- AnimWorld integration ---
    // Non-owning pointer; caller keeps the world alive.
    // When attached, Draw() will step-draw the world after static shapes.
    void AttachWorld(AnimWorld* world);
    void DetachWorld();
    AnimWorld* GetWorld() const { return m_world; }

    // Camera maps world units into this canvas's local pixel space.
    // offset defaults to canvas (x,y); origin/scale are world mapping.
    void SetWorldCamera(const AwCamera& cam);
    const AwCamera& GetWorldCamera() const { return m_cam; }

    // If true (default), Draw() calls world->draw(). Set false if you
    // step/draw the world yourself from game code.
    void SetAutoDrawWorld(bool on) { m_auto_draw_world = on; }

    /* Input zones. Touch inside a zone emits the mapped input. */
    int  AddZone(const InputZone& z);          /* returns index, -1 on full */
    void ClearZones();
    void SetZoneEnabled(int index, bool on);
    /* Test a canvas-local point. Returns the zone index or -1. */
    int  ZoneAt(int lx, int ly) const;
    const InputZone* GetZone(int index) const;



    // Getters
    int GetX() const { return m_cfg.x; }
    int GetY() const { return m_cfg.y; }
    int GetWidth()  const { return m_cfg.width; }
    int GetHeight() const { return m_cfg.height; }
    Window* GetParentWindow() const { return m_parentWindow; }

    // Setters
    void SetPosition(int x, int y);
    void SetSize(int width, int height);
    void SetParentWindow(Window* window) { m_parentWindow = window; }

private:
    explicit Canvas(const CanvasCfg& cfg);

    s_bounds_16u ClampBoundsToParent(s_bounds_16u bounds, s_bounds_16u parentBounds);

    std::atomic<bool> drawing_halted{false};

    CanvasCfg          m_cfg;
    Window*            m_parentWindow;
    fb_shape_buffer_t* m_shapeBuffer;
    uint16_t           m_maxShapes;
    bool               m_dirty;

    AnimWorld*         m_world = nullptr;
    AwCamera           m_cam{};
    bool               m_auto_draw_world = true;
};

#endif // OS_CODE_CORE_WINDOW_ENV_CANVAS_HPP_
