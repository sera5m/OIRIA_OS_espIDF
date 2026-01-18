#ifndef MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_HPP_
#define MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_HPP_

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <cmath>

#include "code_stuff/types.h"
#include "D_st7789v2_s3psram.c"

// forward declarations
class Window;
class Canvas;
struct CanvasCfg;
struct TextChunk;
struct DrawableElement;

/* ---------------- update mode ---------------- */

enum e_wenv_updateType {
    manual,
    managed,
    both
};

/* ---------------- window config ---------------- */

struct WindowCfg {
    uint16_t Posx = 0;
    uint16_t Posy = 0;
    uint16_t win_width  = 64;
    uint16_t win_height = 64;

    bool AutoAlignment        = false;
    bool WrapText             = true;
    bool borderless           = false;
    bool ShowNameAtTopOfWindow = false;

    int  TextSizeMult = 1;
    char name[32] = {0}; // window name

    uint16_t BorderColor  = 0xFFFF;
    uint16_t BgColor      = 0x0000;
    uint16_t WinTextColor = 0xFFFF;

    float UpdateRate = 0.5f;
};

/* ---------------- helpers ---------------- */

struct WinComp_sizing {
    ui16 Xpos   = 0;
    ui16 Ypos   = 0;
    ui16 Zorder = 0;
    ui16 Width  = 0;
    ui16 Height = 0;
};

struct Win_MousePos {
    int scrollOffsetX = 0;
    int scrollOffsetY = 0;
    int accumDX = 0;
    int accumDY = 0;
    int ScrollaccumDX = 0;
    int ScrollaccumDY = 0;
    uint32_t lastScrollTime = 0;
};

/* forward dependency */
void CanvasForceParentUpdate(std::shared_ptr<Window> parent);

/* ======================== WINDOW ======================== */

class Window : public std::enable_shared_from_this<Window> {
public:
    WindowCfg Initialcfg;
    WindowCfg Currentcfg;

    std::string content;
    std::vector<std::shared_ptr<Canvas>> canvases;

    float UpdateTickRate = 0.1f;
    bool dirty = false;

    unsigned long lastUpdateTime = 0;
    unsigned int  lastFrameTime  = 0;

    uint16_t win_internal_color_background = 0x0000;
    uint16_t win_internal_color_border     = 0xFFFF;
    uint16_t win_internal_color_text       = 0xFFFF;

    uint8_t win_internal_textsize_mult = 1;
    WinComp_sizing wi_sizing;

    bool IsWindowShown = true;
    Win_MousePos MousePos_internal;

    static constexpr int scrollLimit    = 3;
    static constexpr int scrollPeriodMs = 100;

    Window(const std::string& WindowName,
           const WindowCfg& windowConfiguration,
           const std::string& initialContent = "");

    ~Window();

    void setWinTextSize(uint8_t t);
    void ForceUpdate(bool UpdateSubComps);
    void ForceUpdateSubComps();

    void ApplyTheme(uint16_t BORDER_COLOR,
                    uint16_t BG_COLOR,
                    uint16_t WIN_TEXT_COLOR);

    void SetBgColor(uint16_t newColor);
    void SetBorderColor(uint16_t newColor);
    void ForceBorderState(bool isShown);

    void ResizeWindow(int newW, int newH, bool fUpdate);
    void MoveWindow(int newX, int newY, bool fUpdate);

    void addCanvas(const CanvasCfg& cfg);

    void WindowScroll(int DX, int DY);
    void animateMove(int targetX, int targetY, int steps);

    void HideWindow();
    void ShowWindow();

    void setUpdateMode(bool manualOnly);
    void updateContent(const std::string& newContent);

    void WinDraw();

    void updateWrappedLinesOptimized();
    void drawVisibleLinesOptimized();

private:
    struct TextState {
        uint16_t color = 0xFFFF;
        uint8_t  size  = 1;
        int16_t  cursorX = 0;
        int16_t  cursorY = 0;
        bool underline = false;
        bool strikethrough = false;
        bool highlighted = false;
    };

    using WrappedLine = std::vector<TextChunk>;
    std::vector<WrappedLine> wrappedLines;

    e_wenv_updateType win_updatemode = both;
    uint32_t lastContentUpdate = 0;

    std::string Delim_LinBreak   = "<n>";
    std::string Delim_Seperator  = "<_>";
    std::string Delim_ColorChange = "<setcolor(";
    std::string Delim_PosChange   = "<pos(";
    std::string Delim_Sizechange  = "<textsize(";
    std::string Delim_Strikethr   = "<s>";
    std::string Delim_Underline   = "<u>";

    std::vector<TextChunk> tokenize(const std::string& input);
    void wrapTextIntoLines(const std::string& text,
                           std::vector<TextChunk>& currentLine,
                           int maxCharsPerLine);

    void applyTextState(const TextState& state);
    void handleTextTag(const std::string& tag, TextState& state);
    void renderTextLine(const std::string& line, int yPos, TextState initialState);
    void renderTextChunk(const std::string& text, TextState& state);
    int  calculateTotalTextWidth();
};

/* ======================== CANVAS ======================== */

enum class DrawType {
    Text, Line, Pixel, FRect, Rect, RFRect, RRect,
    Triangle, Circle, Bitmap, otherImage
};

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
    float UpdateTickRateS = 0.1f;
};

class Canvas {
public:
    Canvas(const CanvasCfg& cfg, std::shared_ptr<Window> parent);
    ~Canvas();

    void clear();

    void AddTextLine(int posX, int posY, const String& text,
                     uint8_t txtsize, uint16_t color, int layer = 0);

    void AddLine(int x0, int y0, int x1, int y1,
                 uint16_t color, int layer = 0);

    void AddPixel(int x, int y, uint16_t color, int layer = 0);

    void AddFRect(int x, int y, int w, int h,
                  uint16_t color, int layer = 0);

    void AddRect(int x, int y, int w, int h,
                 uint16_t color, int layer = 0);

    void AddRFRect(int x, int y, int w, int h, int r,
                   uint16_t color, int layer = 0);

    void AddRRect(int x, int y, int w, int h, int r,
                  uint16_t color, int layer = 0);

    void AddTriangle(uint16_t x0, uint16_t y0,
                     uint16_t x1, uint16_t y1,
                     uint16_t x2, uint16_t y2,
                     uint16_t color, int layer = 0);

    void AddFTriangle(uint16_t x0, uint16_t y0,
                      uint16_t x1, uint16_t y1,
                      uint16_t x2, uint16_t y2,
                      uint16_t color, int layer = 0);

    void AddFCircle(int x, int y, int r,
                     uint16_t color, int layer = 0);

    void AddCircle(int x, int y, int r,
                   uint16_t color, int layer = 0);

    void AddBitmap(int x, int y, const uint16_t* bitmap,
                   int w, int h, int layer = 0);

    void CanvasDraw();
    void CanvasUpdate(bool force);
    void ClearAll();

private:
    int x = 0;
    int y = 0;
    int width  = 0;
    int height = 0;

    bool borderless = true;
    bool DrawBG     = true;

    uint16_t bgColor     = 0x0000;
    uint16_t BorderColor = 0xFFFF;

    std::weak_ptr<Window> parentWindow;
    std::vector<DrawableElement> drawElements;

    bool canvasDirty = true;
    unsigned long lastUpdateTime = 0;
    unsigned int UpdateTickRate = 100;
};

/* ======================== WINDOW MANAGER ======================== */

struct WindowAndUpdateInterval {
    std::weak_ptr<Window> windowWeakPtr;
    int UpdateTickRate = 0;

    explicit WindowAndUpdateInterval(std::shared_ptr<Window> Win)
        : windowWeakPtr(Win),
          UpdateTickRate(static_cast<int>(Win->UpdateTickRate * 1000)) {}

    void updateIfValid() {
        if (auto WinPtr = windowWeakPtr.lock()) {
            WinPtr->WinDraw();
        }
    }
};

class WindowManager {
public:
    static WindowManager& getInstance();

    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    void registerWindow(std::shared_ptr<Window> Win);
    void unregisterWindow(Window* Win);
    void clearAllWindows();

    std::shared_ptr<Window> GetWindowByName(const std::string& WindowName);

    void UpdateAllWindows(bool Force, bool AndSubComps);
    void ApplyThemeAllWindows(uint16_t secondary,
                              uint16_t background,
                              uint16_t primary);

    void notifyUpdateTickRateChange(Window* targetWindow,
                                    int newUpdateTickRate);

    bool initialize(bool graphicsEnabled);

    bool throttleLowPower = false;
    bool isScreenCastExternalDevice = false;

private:
    WindowManager();

    bool AreGraphicsEnabled = false;
    std::vector<WindowAndUpdateInterval> WindowRegistry;
};

void clearScreenEveryXCalls(uint16_t x);

#endif
