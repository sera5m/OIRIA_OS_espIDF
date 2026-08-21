#ifndef MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_HPP_
#define MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_HPP_

#include <stdint.h>
#include <string>
#include <memory>
#include <sstream>
#include <algorithm>
#include <variant>
#include <vector>
#include <atomic>
#include <string_view>

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "code_stuff/types.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include "hardware/drivers/lcd/st7789v2/t_shapes.h"
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "hardware/drivers/psram_std/psram_std.hpp"
#include "os_code/core/rShell/enviroment/env_vars.h"
#include "os_code/core/window_env/wenv_basicThemes.h"

#include "PsramBackgroundTile.hpp"
#include "Canvas.hpp"

// ---------------------------------------------------------------------------
// Globals shared with the display-push task
// ---------------------------------------------------------------------------

extern volatile bool     g_display_dirty;
extern volatile uint32_t g_last_display_time;
extern SemaphoreHandle_t g_display_mutex;
extern TaskHandle_t      core2TaskHandle;

// ---------------------------------------------------------------------------
// Rich-text tags / chunks
// ---------------------------------------------------------------------------

struct ColorTag {
    uint16_t value;
};

struct SizeTag {
    uint8_t value;
};

struct PosTag {
    int16_t x;
    int16_t y;
};

struct HighlighterTag {
    uint16_t color;
    bool     enabled;
};

struct PlainTextRef {
    uint32_t offset = 0;
    uint32_t length = 0;
};

enum class TagType : int8_t {
    None = 0,
    PlainText,
    LineBreak,
    UnderlineToggle,
    StrikethroughToggle,
    BoldToggle,
    ItalicToggle,
    ColorChange,
    SizeChange,
    PosChange,
    HighlightChange,
    UnderlineOff     = -UnderlineToggle,
    StrikethroughOff = -StrikethroughToggle,
    BoldOff          = -BoldToggle,
    ItalicOff        = -ItalicToggle,
};

using ChunkContent = std::variant<
    PlainTextRef,
    ColorTag,
    SizeTag,
    PosTag,
    HighlighterTag,
    std::monostate
>;

struct TextChunk {
    TagType      kind    = TagType::None;
    ChunkContent content = std::monostate{};

    TextChunk() = default;
    explicit TextChunk(PlainTextRef ref) : kind(TagType::PlainText), content(ref) {}
    explicit TextChunk(TagType t) : kind(t) {}
    TextChunk(TagType t, ColorTag c)       : kind(t), content(c) {}
    TextChunk(TagType t, SizeTag s)        : kind(t), content(s) {}
    TextChunk(TagType t, PosTag p)         : kind(t), content(p) {}
    TextChunk(TagType t, HighlighterTag h) : kind(t), content(h) {}

    TextChunk(const TextChunk&)            = default;
    TextChunk(TextChunk&&) noexcept        = default;
    TextChunk& operator=(const TextChunk&) = default;
    TextChunk& operator=(TextChunk&&) noexcept = default;
    ~TextChunk() = default;
};

// ---------------------------------------------------------------------------
// Update mode / option flags
// ---------------------------------------------------------------------------

enum e_wenv_updateType {
    manual,
    managed,
    both
};

enum WindowOptionBits : uint16_t {
    WIN_OPT_USE_BORDERGRADIENT   = 1 << 0,
    WIN_OPT_ANIMATED_BORDER      = 1 << 1,
    WIN_OPT_SHOW_TOP_BAR_MENU    = 1 << 2,
    WIN_OPT_ROUNDED_CORNERS      = 1 << 3,
    WIN_OPT_CLIPPED_CORNERS      = 1 << 4,
    WIN_OPT_TRANSPARENCY         = 1 << 5,
    WIN_OPT_INTERIOR_SPECIALFILL = 1 << 6,
    WIN_OPT_CHILDFREE            = 1 << 7,
    WIN_OPT_IS_HEAVY_RENDERING   = 1 << 8,
    WIN_OPT_ALLOW_RAW_VRAM_ACCESS = 1 << 9,
    WIN_OPT_ISMEDIA_WINDOW       = 1 << 10,
};

// ---------------------------------------------------------------------------
// Window configuration
// ---------------------------------------------------------------------------

struct WindowCfg {
    uint16_t Posx = 0;
    uint16_t Posy = 0;
    uint16_t Layer = 0;
    uint16_t renderPriority = 0;
    uint16_t win_width  = 64;
    uint16_t win_height = 64;
    uint8_t  win_rotation = 1;          // quadrant 0–3
    bool     AutoAlignment         = false;
    bool     WrapText              = true;
    bool     borderless            = false;
    bool     ShowNameAtTopOfWindow = false;
    uint8_t  TextSizeMult          = 1;
    char     name[32]              = {0};

    uint16_t optionsbitmask        = 0;

    uint16_t BorderColor           = 0xFFFF;
    uint16_t BgColor               = 0x0000;
    uint16_t Bg_secondaryColor     = 0x4040;
    uint16_t WinTextColor          = 0xFFFF;
    BgFillType backgroundType      = BgFillType::Solid;
    float    UpdateRate            = 0.5f;
};

struct WinComp_sizing {
    uint16_t Xpos   = 0;
    uint16_t Ypos   = 0;
    uint16_t Zorder = 0;
    uint16_t Width  = 0;
    uint16_t Height = 0;
    uint8_t  rotation = 0;
};

struct Win_MousePos {
    int scrollOffsetX = 0;
    int scrollOffsetY = 0;
    uint16_t accumDX = 0;
    uint16_t accumDY = 0;
    int ScrollaccumDX = 0;
    int ScrollaccumDY = 0;
    uint32_t lastScrollTime = 0;
};

// Helpers declared here, defined in MWenv.cpp
int      safe_parse_int(std::string_view str, int default_val = 0);
uint16_t safe_parse_color(std::string_view str, uint16_t default_val = 0xFFFF);

void CanvasForceParentUpdate(std::shared_ptr<Window> parent);
void clearScreenEveryXCalls(uint16_t x);

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

class Window : public std::enable_shared_from_this<Window> {
public:
    bool content_dirty = true;
    SemaphoreHandle_t text_mtx = nullptr;

    void HaltDrawing();
    void ResumeDrawing();

    void set_position(uint16_t x, uint16_t y, bool interpolate = false);
    void set_layer(uint8_t layer);
    void set_size(uint16_t width, uint16_t height);

    bool window_highlighted = false;

    void get_physical_bounds(int& out_x, int& out_y, int& out_w, int& out_h);

    explicit Window(const WindowCfg& cfg, const std::string& initialContent = "");

    void WinDraw();

    void SetText(const char* newText);
    void SetText(std::string_view text);
    void SetText(const std::string& newText);
    void SetText(const stdpsram::String& newText);

    void AppendText(const std::string& moreText);
    void AppendText(const stdpsram::String& moreText);
    void calculateLogicalDimensions();
    void ClearText();
    void LocalToScreen(int lx, int ly, int& sx, int& sy);

    stdpsram::String content;
    stdpsram::String last_content;
    stdpsram::Vector<TextChunk> cachedChunks;

    WindowCfg Initialcfg;
    WindowCfg Currentcfg;

    uint16_t logicalW = 0;
    uint16_t logicalH = 0;
    fontdata w_font_info;

    std::shared_ptr<PsramBackgroundTile> bgTile;
    BgFillType win_backgroundpattern = BgFillType::Solid;

    std::shared_ptr<Canvas> AddCanvas(const CanvasCfg& cfg);
    void RemoveCanvas();
    std::shared_ptr<Canvas> GetCanvas() const { return m_canvas; }
    void DrawCanvas();

    struct {
        uint16_t Xpos   = 0;
        uint16_t Ypos   = 0;
        uint16_t Zorder = 0;
        uint16_t Width  = 0;
        uint16_t Height = 0;
        uint8_t  rotation = 0;
    } wi_sizing;

    uint16_t win_internal_color_background = 0;
    uint16_t win_internal_color_border     = 0xFFFF;
    uint16_t win_internal_color_text       = 0xFFFF;
    int      win_internal_textsize_mult    = 1;
    uint16_t win_internal_optionsBitmask   = 0;
    float    UpdateTickRate = 0.5f;

    bool     IsWindowShown = true;
    bool     dirty         = true;
    uint64_t lastUpdateTime = 0;
    uint16_t bgPrimaryColor   = 0;
    uint16_t bgSecondaryColor = 0;

    void setupBackgroundTile();

    bool enable_refresh_override = false;

    uint16_t currentPhysX = 0;
    uint16_t currentPhysY = 0;

    stdpsram::Vector<TextChunk> tokenize(const stdpsram::String& s);
    stdpsram::Vector<TextChunk> tokenize(const std::string& s);

    ~Window();

private:
    std::atomic<bool> drawing_halted{false};
    std::shared_ptr<Canvas> m_canvas;
    bool TenthTick = false;

    BgFillType lastBackgroundPattern = BgFillType::Solid;
    uint16_t   lastPrimaryColor      = 0;
    uint16_t   lastSecondaryColor    = 0;

    bool isTokenized = false;

    struct TextState {
        uint16_t color = 0xFFFF;
        int      size  = 1;
        int      cursorX = 0;
        int      cursorY = 0;
        bool     underline     = false;
        bool     strikethrough = false;
        bool     bold          = false;
        bool     italic        = false;
        uint16_t highlight_bg  = 0;
    };
    TextState Tstate;
};

// ---------------------------------------------------------------------------
// WindowAndUpdateInterval helper
// ---------------------------------------------------------------------------

struct WindowAndUpdateInterval {
    std::weak_ptr<Window> windowWeakPtr;
    int UpdateTickRate = 0;

    explicit WindowAndUpdateInterval(std::shared_ptr<Window> Win)
        : windowWeakPtr(Win),
          UpdateTickRate(static_cast<int>(Win->UpdateTickRate * 1000))
    {}

    void updateIfValid() {
        if (auto WinPtr = windowWeakPtr.lock()) {
            WinPtr->WinDraw();
        }
    }
};

// ---------------------------------------------------------------------------
// Toolbar types
// ---------------------------------------------------------------------------

typedef enum {
    TB_BLUETOOTH  = (1 << 0),
    TB_WIFI       = (1 << 1),
    TB_RF         = (1 << 2),
    TB_OPTICAL    = (1 << 3),
    TB_FLASHLIGHT = (1 << 4),
    TB_BATT       = (1 << 5),
    TB_GYRO       = (1 << 6),
    TB_TEMP       = (1 << 7),
    TB_KEYS       = (1 << 8),
    TB_SDCARD     = (1 << 9),
    TB_SILENT     = (1 << 10),
} toolbar_items_t;

typedef struct {
    bool tb_overlay;
    uint8_t tb_update_hz;
    uint8_t tb_rot;          // 0 top, 1 left, 2 bottom, 3 right
    bool showToolbar;
    bool disableTouch;
    bool expandsDownOnTap;
    s_bmp_t* ref_iconptrs[16];
    toolbar_items_t icons_shown;
    uint16_t color;
} toolbarconfig;

extern toolbarconfig g_defaultToolbarConfig;

// ---------------------------------------------------------------------------
// WindowManager (singleton)
// ---------------------------------------------------------------------------

class WindowManager {
public:
    static WindowManager& getInstance() {
        static WindowManager instance;
        return instance;
    }

    void DebugPrintWindowDOM() const;
    void UpdateAll(bool force = false,
                   bool ToolbarUpdate = true,
                   bool repositionWindows = true,
                   bool draw_toolbar_ontop = true);

    bool PruneDeadWindows();
    bool registerWindow(std::shared_ptr<Window> window);
    bool unregisterWindow(std::shared_ptr<Window> window);

    void ClampWinToArea(s_bounds_16u bounds, std::shared_ptr<Window> target);
    void ClampToArea(s_bounds_16u bounds, std::shared_ptr<Window> window, bool is_universal);

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    // Toolbar
    void SetToolbarActive(bool on);
    void UpdateToolbar();
    void setToolbarRot(uint8_t new_rot);
    void addToolbarIco(s_bmp_t& icon);
    void RepositionAllWindows();
    void SortWindowsByZOrder();
    void SortWindowsByLayer();
    void SetToolbarText(const char* text);
    void DrawToolBar();
    void ResetRepositioning() { windows_repositioned = false; }

    uint16_t GetAvailableWidth();
    uint16_t GetAvailableHeight();
    uint16_t GetToolbarOffset();

    // Fullscreen
    void make_window_fullscreen(std::shared_ptr<Window> win);
    void restore_from_fullscreen();
    void ResetTheRepositioning();

private:
    struct FullscreenState {
        bool was_toolbar_active = true;
        uint16_t old_clamped_w = 0;
        uint16_t old_clamped_h = 0;
        uint16_t old_toolbar_offset = 0;
        std::shared_ptr<Window> fullscreen_win = nullptr;
    };
    FullscreenState fs_state;

    bool windows_repositioned = false;

    WindowManager();
    ~WindowManager();

    std::vector<std::shared_ptr<Window>> windows;

    toolbarconfig m_toolbarConfig;
    std::string toolbar_text;
    bool tb_dirty = true;
    uint64_t last_toolbar_update = 0;
};

// Display-push task entry
void launchTHESTUPIDMOTHERFUCKINGPEICEOFSHITDISPLAYPUSHTASKFUCKYOU();

#endif // MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_HPP_
