#ifndef OS_CODE_CORE_WINDOW_ENV_CANVAS_HPP_
#define OS_CODE_CORE_WINDOW_ENV_CANVAS_HPP_

#include <stdint.h>
#include <memory>
#include <atomic>

#include "hardware/drivers/lcd/st7789v2/t_shapes.h"
#include "code_stuff/types.h" // s_bounds_16u

class Window; // forward

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
// Canvas – shape buffer owned by a Window
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
};

#endif // OS_CODE_CORE_WINDOW_ENV_CANVAS_HPP_
