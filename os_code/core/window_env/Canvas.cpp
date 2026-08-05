#include "Canvas.hpp"
#include "MWenv.hpp" // Window

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"

static const char* TAG = "Canvas";

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Canvas::Canvas(const CanvasCfg& cfg)
    : m_cfg(cfg)
    , m_parentWindow(cfg.parentWindow)
    , m_shapeBuffer(nullptr)
    , m_maxShapes(FB_MAX_SHAPES)
    , m_dirty(true)
    , m_world(nullptr)
    , m_auto_draw_world(true)
{
    // Default camera: world (0,0) → canvas top-left, 1:1 pixels
    m_cam.origin = {0.f, 0.f};
    m_cam.scale = 1.f;
    m_cam.offset_x = cfg.x;
    m_cam.offset_y = cfg.y;

    m_shapeBuffer = (fb_shape_buffer_t*)heap_caps_malloc(
        sizeof(fb_shape_buffer_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!m_shapeBuffer) {
        ESP_LOGE(TAG, "Failed to allocate shape buffer!");
        return;
    }

    if (!fb_shapes_init(m_shapeBuffer, m_maxShapes)) {
        ESP_LOGE(TAG, "Failed to initialize shapes!");
        heap_caps_free(m_shapeBuffer);
        m_shapeBuffer = nullptr;
        return;
    }

    ESP_LOGI(TAG, "Canvas created with parent window at %p", m_parentWindow);
}

Canvas::~Canvas() {
    if (m_shapeBuffer) {
        fb_shapes_free(m_shapeBuffer);
        heap_caps_free(m_shapeBuffer);
        m_shapeBuffer = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Update / Draw
// ---------------------------------------------------------------------------

void Canvas::Update(float deltaTime) {
    (void)deltaTime;
    if (m_dirty) {
        SortShapes();
        if (m_parentWindow) {
            m_parentWindow->dirty = true;
        }
        m_dirty = false;
    }
}

void Canvas::Draw() {
    if (!m_parentWindow || !m_parentWindow->IsWindowShown) {
        return;
    }

    // 1) Static shape buffer (UI chrome, overlays, etc.)
    if (m_shapeBuffer) {
        for (uint16_t i = 0; i < m_shapeBuffer->count; i++) {
            fb_shape_t* shape = &m_shapeBuffer->shapes[i];
            if (!shape->shown) continue;

            s_bounds_16u screenBounds;
            int sx1, sy1, sx2, sy2;

            m_parentWindow->LocalToScreen(shape->bounds.x, shape->bounds.y, sx1, sy1);
            m_parentWindow->LocalToScreen(
                shape->bounds.x + shape->bounds.w,
                shape->bounds.y + shape->bounds.h,
                sx2, sy2
            );

            screenBounds.x = sx1;
            screenBounds.y = sy1;
            screenBounds.w = sx2 - sx1;
            screenBounds.h = sy2 - sy1;

            s_bounds_16u windowBounds = {
                .x = m_parentWindow->currentPhysX,
                .y = m_parentWindow->currentPhysY,
                .w = m_parentWindow->logicalW,
                .h = m_parentWindow->logicalH
            };

            screenBounds = ClampBoundsToParent(screenBounds, windowBounds);
            if (screenBounds.w <= 0 || screenBounds.h <= 0) continue;

            switch ((fb_shape_type)shape->type) {
                case SHAPE_RECT:
                    fb_rect(true, 1,
                            screenBounds.x, screenBounds.y,
                            screenBounds.w, screenBounds.h,
                            shape->color, shape->color);
                    break;

                case SHAPE_LINE:
                    fb_line(screenBounds.x, screenBounds.y,
                            screenBounds.x + screenBounds.w,
                            screenBounds.y + screenBounds.h,
                            shape->color);
                    break;

                case SHAPE_CIRCLE: {
                    int radius = screenBounds.w / 2;
                    fb_circle(screenBounds.x + radius,
                              screenBounds.y + radius,
                              radius, plain, shape->color, shape->color);
                    break;
                }

                case SHAPE_BITMAP:
                    if (shape->data) {
                        fb_draw_bitmap(screenBounds.x, screenBounds.y,
                                       screenBounds.w, screenBounds.h,
                                       (const uint16_t*)shape->data);
                    }
                    break;

                case SHAPE_TEXT:
                    if (shape->data) {
                        fb_draw_text(0, screenBounds.x, screenBounds.y,
                                     (const char*)shape->data,
                                     shape->color, 1, 0, true, 0x0000,
                                     screenBounds.w, ft_AVR_classic_6x8);
                    }
                    break;

                default:
                    break;
            }
        }
    }

    // 2) AnimWorld layer (pong / 2048 / etc.)
        // 2) AnimWorld layer (pong / 2048 / etc.)
    if (m_auto_draw_world && m_world) {
        // Keep camera offset in sync with canvas position unless caller overrode
        AwCamera cam = m_cam;
        if (cam.offset_x == 0 && cam.offset_y == 0) {
            cam.offset_x = m_cfg.x;
            cam.offset_y = m_cfg.y;
        }
        // Trampoline so AnimWorld never needs a complete Window type.
        auto l2s = [](void* ctx, int lx, int ly, int& sx, int& sy) {
            static_cast<Window*>(ctx)->LocalToScreen(lx, ly, sx, sy);
        };
        m_world->draw(cam, l2s, m_parentWindow);
    }
}

void Canvas::AttachWorld(AnimWorld* world) {
    m_world = world;
    m_dirty = true;
    if (m_parentWindow) m_parentWindow->dirty = true;
}

void Canvas::DetachWorld() {
    m_world = nullptr;
    m_dirty = true;
}

void Canvas::SetWorldCamera(const AwCamera& cam) {
    m_cam = cam;
    m_dirty = true;
}

// ---------------------------------------------------------------------------
// Shape management
// ---------------------------------------------------------------------------

fb_shape_t* Canvas::AddShape(fb_shape_type type, s_bounds_16u bounds,
                              uint16_t color, uint8_t layer) {
    if (!m_shapeBuffer) return nullptr;

    s_bounds_16u clampedBounds = bounds;
    if (clampedBounds.x < 0) {
        clampedBounds.w += clampedBounds.x;
        clampedBounds.x = 0;
    }
    if (clampedBounds.y < 0) {
        clampedBounds.h += clampedBounds.y;
        clampedBounds.y = 0;
    }
    if (clampedBounds.x + clampedBounds.w > m_cfg.width) {
        clampedBounds.w = m_cfg.width - clampedBounds.x;
    }
    if (clampedBounds.y + clampedBounds.h > m_cfg.height) {
        clampedBounds.h = m_cfg.height - clampedBounds.y;
    }

    if (clampedBounds.w <= 0 || clampedBounds.h <= 0) {
        ESP_LOGW(TAG, "Shape bounds invalid after clamping");
        return nullptr;
    }

    fb_shape_t* shape = fb_shape_add(m_shapeBuffer, type, clampedBounds, color, layer);
    if (shape) {
        m_dirty = true;
        ESP_LOGI(TAG, "Added shape type=%d at (%d,%d) size=%dx%d",
                 type, bounds.x, bounds.y, bounds.w, bounds.h);
    }

    return shape;
}

void Canvas::RemoveShape(uint16_t index) {
    if (!m_shapeBuffer || index >= m_shapeBuffer->count) return;

    for (uint16_t i = index; i < m_shapeBuffer->count - 1; i++) {
        m_shapeBuffer->shapes[i] = m_shapeBuffer->shapes[i + 1];
    }

    m_shapeBuffer->count--;
    m_dirty = true;
}

void Canvas::ClearShapes() {
    if (!m_shapeBuffer) return;
    m_shapeBuffer->count = 0;
    for (int i = 0; i < FB_MAX_LAYERS; i++) {
        m_shapeBuffer->layer_counts[i]  = 0;
        m_shapeBuffer->layer_offsets[i] = 0;
    }
    m_dirty = true;
}

void Canvas::SortShapes() {
    if (!m_shapeBuffer) return;
    fb_shapes_Fsort_by_layer(m_shapeBuffer);
}

void Canvas::SetShapeVisible(uint16_t index, bool visible) {
    if (!m_shapeBuffer || index >= m_shapeBuffer->count) return;
    m_shapeBuffer->shapes[index].shown = visible;
    m_dirty = true;
}

s_bounds_16u Canvas::ClampBoundsToParent(s_bounds_16u bounds, s_bounds_16u parentBounds) {
    s_bounds_16u result = bounds;

    if (result.x < parentBounds.x) {
        result.w -= (parentBounds.x - result.x);
        result.x  = parentBounds.x;
    }
    if (result.y < parentBounds.y) {
        result.h -= (parentBounds.y - result.y);
        result.y  = parentBounds.y;
    }
    if (result.x + result.w > parentBounds.x + parentBounds.w) {
        result.w = (parentBounds.x + parentBounds.w) - result.x;
    }
    if (result.y + result.h > parentBounds.y + parentBounds.h) {
        result.h = (parentBounds.y + parentBounds.h) - result.y;
    }

    return result;
}

void Canvas::SetPosition(int x, int y) {
    m_cfg.x = x;
    m_cfg.y = y;
    m_dirty = true;
}

void Canvas::SetSize(int width, int height) {
    m_cfg.width  = width;
    m_cfg.height = height;
    m_dirty = true;
}
