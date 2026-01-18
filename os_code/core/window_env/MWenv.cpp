#ifndef MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_CPP_
#define MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_CPP_

#include "MWenv.hpp"
#include <algorithm>
#include <cstring>
#include <Arduino.h>

// Global graphics state
bool AreGraphicsEnabled = true;
bool IsWindowHandlerAlive = false;

// Forward declarations for framebuffer functions
extern "C" {
    void fb_clear(uint16_t color);
    void fb_rect(bool filled, uint8_t border_thickness, int x, int y, int w, int h, uint16_t color, uint16_t border_color);
    void fb_line(int x0, int y0, int x1, int y1, uint16_t color);
    void fb_draw_text(int angle, int x, int y, const char* text, uint16_t color, uint8_t size, 
                     void* modifiers, const void* font, bool wrap_text, bool draw_background, 
                     uint16_t bg_color, uint8_t max_len, uint8_t font_size);
    void fb_circle(int x, int y, int r, int mode, uint16_t fill_color, uint16_t border_color);
    void fb_triangle(int x0, int y0, int x1, int y1, int x2, int y2, int mode, 
                    uint16_t fill_color, uint16_t border_color);
    void refreshScreen();
}

// Constants
constexpr int MIN_WINDOW_WIDTH = 10;
constexpr int MIN_WINDOW_HEIGHT = 10;
constexpr int DefaultCharWidth = 6;
constexpr int DefaultCharHeight = 8;

/* ======================== WINDOW IMPLEMENTATION ======================== */

Window::Window(const std::string& WindowName,
               const WindowCfg& windowConfiguration,
               const std::string& initialContent)
    : content(initialContent)
{
    // Copy configuration
    Initialcfg = windowConfiguration;
    Currentcfg = windowConfiguration;
    
    // Set window name
    strncpy(Initialcfg.name, WindowName.c_str(), sizeof(Initialcfg.name) - 1);
    strncpy(Currentcfg.name, WindowName.c_str(), sizeof(Currentcfg.name) - 1);
    
    // Initialize sizing
    wi_sizing.Xpos = std::max<int>(MIN_WINDOW_WIDTH, Currentcfg.Posx);
    wi_sizing.Ypos = std::max<int>(MIN_WINDOW_HEIGHT, Currentcfg.Posy);
    wi_sizing.Width = std::max<int>(MIN_WINDOW_WIDTH, Currentcfg.win_width);
    wi_sizing.Height = std::max<int>(MIN_WINDOW_HEIGHT, Currentcfg.win_height);
    
    // Initialize colors
    win_internal_color_background = Currentcfg.BgColor;
    win_internal_color_border = Currentcfg.BorderColor;
    win_internal_color_text = Currentcfg.WinTextColor;
    win_internal_textsize_mult = Currentcfg.TextSizeMult;
    
    // Set update rate
    UpdateTickRate = Currentcfg.UpdateRate;
}

Window::~Window() {
    // Clean up canvases
    canvases.clear();
}

void Window::setWinTextSize(uint8_t t) {
    win_internal_textsize_mult = t;
    dirty = true;
}

void Window::ForceUpdate(bool UpdateSubComps) {
    dirty = true;
    WinDraw();
    
    if (UpdateSubComps) {
        for (auto& canvas : canvases) {
            if (canvas) canvas->CanvasUpdate(true);
        }
    }
}

void Window::ForceUpdateSubComps() {
    for (auto& canvas : canvases) {
        if (canvas) canvas->CanvasUpdate(true);
    }
}

void Window::ApplyTheme(uint16_t BORDER_COLOR, uint16_t BG_COLOR, uint16_t WIN_TEXT_COLOR) {
    win_internal_color_background = BG_COLOR;
    win_internal_color_border = BORDER_COLOR;
    win_internal_color_text = WIN_TEXT_COLOR;
    ForceUpdate(true);
}

void Window::SetBgColor(uint16_t newColor) {
    win_internal_color_background = newColor;
    dirty = true;
}

void Window::SetBorderColor(uint16_t newColor) {
    if (win_internal_color_border != newColor) {
        win_internal_color_border = newColor;
        dirty = true;
    }
}

void Window::ForceBorderState(bool isShown) {
    if (Currentcfg.borderless != !isShown) {
        Currentcfg.borderless = !isShown;
        dirty = true;
    }
}

void Window::ResizeWindow(int newW, int newH, bool fUpdate) {
    if (wi_sizing.Width == newW && wi_sizing.Height == newH)
        return;

    // Clear old area (using framebuffer)
    fb_rect(true, 0, wi_sizing.Xpos, wi_sizing.Ypos, wi_sizing.Width, wi_sizing.Height, 
            0x0000, 0x0000); // Using black as background
    
    // Update sizing
    wi_sizing.Width = newW;
    wi_sizing.Height = newH;
    
    ForceUpdate(fUpdate);
}

void Window::MoveWindow(int newX, int newY, bool fUpdate) {
    if (wi_sizing.Xpos == newX && wi_sizing.Ypos == newY) 
        return;
        
    // Clear old area
    fb_rect(true, 0, wi_sizing.Xpos, wi_sizing.Ypos, wi_sizing.Width, wi_sizing.Height, 
            0x0000, 0x0000);
    
    // Update position
    wi_sizing.Xpos = newX;
    wi_sizing.Ypos = newY;
    
    ForceUpdate(fUpdate);
}

void Window::addCanvas(const CanvasCfg& cfg) {
    if (cfg.parentWindow != this) {
        Serial.print("Error: Canvas parent mismatch in Window: ");
        Serial.println(Currentcfg.name);
        return;
    }
    
    auto newCanvas = std::make_shared<Canvas>(cfg, shared_from_this());
    canvases.push_back(newCanvas);
}

int Window::calculateTotalTextWidth() {
    int charWidth = 6 * win_internal_textsize_mult;
    int maxWidth = 0;
    for (const auto& line : wrappedLines) {
        int lineWidth = line.size() * charWidth;
        if (lineWidth > maxWidth) {
            maxWidth = lineWidth;
        }
    }
    return maxWidth;
}

void Window::WindowScroll(int DX, int DY) {
    // Accumulate scroll deltas
    MousePos_internal.accumDX += DX;
    MousePos_internal.accumDY += DY;
    
    unsigned long now = millis();
    if ((now - MousePos_internal.lastScrollTime) >= scrollPeriodMs || 
        abs(MousePos_internal.accumDX) >= scrollLimit || 
        abs(MousePos_internal.accumDY) >= scrollLimit) {
        
        // Update scroll offsets
        MousePos_internal.scrollOffsetX += MousePos_internal.accumDX;
        MousePos_internal.scrollOffsetY += MousePos_internal.accumDY;
        
        // Clamp vertical scroll
        int totalTextHeight = wrappedLines.size() * (8 * win_internal_textsize_mult);
        int maxOffsetY = (totalTextHeight > wi_sizing.Height - 4) ? 
                        totalTextHeight - (wi_sizing.Height - 4) : 0;
        MousePos_internal.scrollOffsetY = std::max(0, std::min(MousePos_internal.scrollOffsetY, maxOffsetY));
        
        // Clamp horizontal scroll
        int totalTextWidth = calculateTotalTextWidth();
        int maxOffsetX = (totalTextWidth > wi_sizing.Width - 4) ? 
                        totalTextWidth - (wi_sizing.Width - 4) : 0;
        MousePos_internal.scrollOffsetX = std::max(0, std::min(MousePos_internal.scrollOffsetX, maxOffsetX));
        
        // Reset accumulators
        MousePos_internal.lastScrollTime = now;
        MousePos_internal.accumDX = 0;
        MousePos_internal.accumDY = 0;
        
        ForceUpdate(true);
    }
}

void Window::animateMove(int targetX, int targetY, int steps) {
    int stepX = (targetX - wi_sizing.Xpos) / steps;
    int stepY = (targetY - wi_sizing.Ypos) / steps;
    
    for (int i = 0; i < steps; i++) {
        wi_sizing.Xpos += stepX;
        wi_sizing.Ypos += stepY;
        ForceUpdate(false);
        delay(45);
    }
    
    // Ensure final position is exact
    wi_sizing.Xpos = targetX;
    wi_sizing.Ypos = targetY;
    ForceUpdate(true);
}

void Window::HideWindow() {
    IsWindowShown = false;
    // Clear window area
    fb_rect(true, 0, wi_sizing.Xpos, wi_sizing.Ypos, 
            wi_sizing.Width, wi_sizing.Height, 0x0000, 0x0000);
}

void Window::ShowWindow() {
    IsWindowShown = true;
    ForceUpdate(true);
}

void Window::setUpdateMode(bool manualOnly) {
    win_updatemode = manualOnly ? e_wenv_updateType::manual : e_wenv_updateType::both;
    Serial.printf("[Config] Update mode set to %s\n", manualOnly ? "MANUAL" : "AUTO");
}

void Window::updateContent(const std::string& newContent) {
    if (content == newContent) return;
    content = newContent;
    dirty = true;
    lastContentUpdate = millis();
}

std::vector<TextChunk> Window::tokenize(const std::string& input) {
    std::vector<TextChunk> chunks;
    size_t pos = 0, len = input.size();

    while (pos < len) {
        if (input[pos] == '<') {
            size_t end = input.find('>', pos);
            if (end == std::string::npos) {
                chunks.push_back({false, input.substr(pos, 1)});
                pos++;
            } else {
                chunks.push_back({true, input.substr(pos, end - pos + 1)});
                pos = end + 1;
            }
        } else {
            size_t next = input.find('<', pos);
            if (next == std::string::npos) next = len;
            chunks.push_back({false, input.substr(pos, next - pos)});
            pos = next;
        }
    }
    return chunks;
}

void Window::WinDraw() {
    if (!IsWindowShown) return;

    // 1. Clear window area
    fb_rect(true, 0, wi_sizing.Xpos, wi_sizing.Ypos, 
            wi_sizing.Width, wi_sizing.Height, 
            win_internal_color_background, win_internal_color_border);
    
    // 2. Draw border if needed
    if (!Currentcfg.borderless) {
        fb_rect(false, 1, wi_sizing.Xpos, wi_sizing.Ypos, 
                wi_sizing.Width, wi_sizing.Height, 
                0x0000, win_internal_color_border);
    }
    
    // 3. Tokenize and render content
    auto chunks = tokenize(content);
    
    // Initial text state
    TextState state;
    state.color = win_internal_color_text;
    state.size = win_internal_textsize_mult;
    state.cursorX = wi_sizing.Xpos + 2;
    state.cursorY = wi_sizing.Ypos + 2;
    
    // Font selection (avrclassic_font6x8 needs to be defined elsewhere)
    extern const void* avrclassic_font6x8;
    
    for (auto& chunk : chunks) {
        if (chunk.isTag) {
            handleTextTag(chunk.text, state);
        } else {
            // Draw text using framebuffer
            fb_draw_text(0, state.cursorX, state.cursorY, chunk.text.c_str(), 
                        state.color, state.size, nullptr, avrclassic_font6x8,
                        false, false, win_internal_color_background, 32, 8);
            
            // Update cursor position
            state.cursorX += chunk.text.length() * DefaultCharWidth * state.size;
        }
    }
    
    dirty = false;
    lastUpdateTime = millis();
}

void Window::applyTextState(const TextState& state) {
    // Text state is applied through fb_draw_text parameters
}

void Window::handleTextTag(const std::string& tag, TextState& state) {
    if (tag == Delim_LinBreak) {
        state.cursorY += DefaultCharHeight * state.size;
        state.cursorX = wi_sizing.Xpos + 2;
    } else if (tag == Delim_Underline) {
        state.underline = !state.underline;
    } else if (tag == Delim_Strikethr) {
        state.strikethrough = !state.strikethrough;
    } else if (tag.starts_with(Delim_ColorChange)) {
        size_t start = Delim_ColorChange.size();
        size_t end = tag.find(')', start);
        if (end != std::string::npos) {
            uint16_t color = std::stoul(tag.substr(start, end - start), nullptr, 16);
            state.color = color;
        }
    } else if (tag.starts_with(Delim_Sizechange)) {
        size_t start = Delim_Sizechange.size();
        size_t end = tag.find(')', start);
        if (end != std::string::npos) {
            uint8_t size = std::stoul(tag.substr(start, end - start));
            state.size = size;
        }
    } else if (tag.starts_with(Delim_PosChange)) {
        size_t start = Delim_PosChange.size();
        size_t end = tag.find(')', start);
        if (end != std::string::npos) {
            size_t comma = tag.find(',', start);
            if (comma != std::string::npos) {
                int x = std::stoi(tag.substr(start, comma - start));
                int y = std::stoi(tag.substr(comma + 1, end - comma - 1));
                state.cursorX = wi_sizing.Xpos + x;
                state.cursorY = wi_sizing.Ypos + y;
            }
        }
    }
}

void Window::renderTextLine(const std::string& line, int yPos, TextState initialState) {
    // Simplified rendering - actual implementation would use fb_draw_text
    TextState state = initialState;
    state.cursorY = yPos;
    
    fb_draw_text(0, state.cursorX, state.cursorY, line.c_str(), 
                state.color, state.size, nullptr, avrclassic_font6x8,
                false, false, win_internal_color_background, 32, 8);
}

void Window::updateWrappedLinesOptimized() {
    // Implementation for text wrapping
    wrappedLines.clear();
    if (content.empty()) return;

    const int maxCharsPerLine = (wi_sizing.Width - 4) / (DefaultCharWidth * win_internal_textsize_mult);
    
    // Simplified implementation - actual would parse tags and wrap
    std::stringstream ss(content);
    std::string line;
    
    while (std::getline(ss, line)) {
        if (line.length() > maxCharsPerLine) {
            // Wrap the line
            size_t pos = 0;
            while (pos < line.length()) {
                size_t chunkSize = std::min(line.length() - pos, (size_t)maxCharsPerLine);
                std::string chunk = line.substr(pos, chunkSize);
                pos += chunkSize;
                
                // Add as wrapped line
                wrappedLines.push_back({{false, chunk}});
            }
        } else {
            wrappedLines.push_back({{false, line}});
        }
    }
}

void Window::wrapTextIntoLines(const std::string& text, 
                              std::vector<TextChunk>& currentLine, 
                              int maxCharsPerLine) {
    // Implementation for word wrapping
    std::stringstream ss(text);
    std::string word;
    
    while (ss >> word) {
        if (currentLine.size() + word.length() > maxCharsPerLine) {
            wrappedLines.push_back(currentLine);
            currentLine.clear();
        }
        currentLine.push_back({false, word + " "});
    }
}

void Window::drawVisibleLinesOptimized() {
    if (!IsWindowShown) return;

    const int charHeight = DefaultCharHeight * win_internal_textsize_mult;
    const int startLine = MousePos_internal.scrollOffsetY / charHeight;
    const int visibleLines = (wi_sizing.Height - 4) / charHeight;
    const int endLine = std::min(startLine + visibleLines, (int)wrappedLines.size());

    // Clear window area
    fb_rect(true, 0, wi_sizing.Xpos, wi_sizing.Ypos, 
            wi_sizing.Width, wi_sizing.Height, 
            win_internal_color_background, 0x0000);

    TextState state;
    state.color = win_internal_color_text;
    state.size = win_internal_textsize_mult;
    state.cursorX = wi_sizing.Xpos + 2;
    state.cursorY = wi_sizing.Ypos + 2 - (MousePos_internal.scrollOffsetY % charHeight);

    for (int i = startLine; i < endLine; i++) {
        for (const auto& chunk : wrappedLines[i]) {
            if (chunk.isTag) {
                handleTextTag(chunk.text, state);
            } else {
                renderTextChunk(chunk.text, state);
            }
        }
        state.cursorY += charHeight;
        state.cursorX = wi_sizing.Xpos + 2;
    }
}

void Window::renderTextChunk(const std::string& text, TextState& state) {
    fb_draw_text(0, state.cursorX, state.cursorY, text.c_str(), 
                state.color, state.size, nullptr, avrclassic_font6x8,
                false, false, win_internal_color_background, 32, 8);
    
    state.cursorX += text.length() * DefaultCharWidth * state.size;
}

/* ======================== CANVAS IMPLEMENTATION ======================== */

Canvas::Canvas(const CanvasCfg& cfg, std::shared_ptr<Window> parent)
    : x(cfg.x), y(cfg.y), width(cfg.width), height(cfg.height),
      borderless(cfg.borderless), DrawBG(cfg.DrawBG),
      bgColor(cfg.bgColor), BorderColor(cfg.BorderColor),
      parentWindow(parent), UpdateTickRate(static_cast<unsigned int>(cfg.UpdateTickRateS * 1000))
{
    clear();
}

Canvas::~Canvas() {
    drawElements.clear();
}

void Canvas::clear() {
    drawElements.clear();
    canvasDirty = true;
}

void Canvas::AddTextLine(int posX, int posY, const String& text,
                         uint8_t txtsize, uint16_t color, int layer) {
    // Implementation would add a draw element
    canvasDirty = true;
}

void Canvas::AddLine(int x0, int y0, int x1, int y1,
                     uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddPixel(int x, int y, uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddFRect(int x, int y, int w, int h,
                      uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddRect(int x, int y, int w, int h,
                     uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddRFRect(int x, int y, int w, int h, int r,
                       uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddRRect(int x, int y, int w, int h, int r,
                      uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddTriangle(uint16_t x0, uint16_t y0,
                         uint16_t x1, uint16_t y1,
                         uint16_t x2, uint16_t y2,
                         uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddFTriangle(uint16_t x0, uint16_t y0,
                          uint16_t x1, uint16_t y1,
                          uint16_t x2, uint16_t y2,
                          uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddFCircle(int x, int y, int r,
                        uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddCircle(int x, int y, int r,
                       uint16_t color, int layer) {
    canvasDirty = true;
}

void Canvas::AddBitmap(int x, int y, const uint16_t* bitmap,
                       int w, int h, int layer) {
    canvasDirty = true;
}

void Canvas::CanvasDraw() {
    if (!canvasDirty) return;
    
    // Draw background if needed
    if (DrawBG) {
        fb_rect(true, 0, x, y, width, height, bgColor, BorderColor);
    }
    
    // Draw border if needed
    if (!borderless) {
        fb_rect(false, 1, x, y, width, height, 0x0000, BorderColor);
    }
    
    // Draw all elements
    for (const auto& element : drawElements) {
        // Implementation would draw each element based on its type
    }
    
    canvasDirty = false;
}

void Canvas::CanvasUpdate(bool force) {
    unsigned long currentTime = millis();
    if (force || (currentTime - lastUpdateTime >= UpdateTickRate)) {
        CanvasDraw();
        lastUpdateTime = currentTime;
    }
}

void Canvas::ClearAll() {
    drawElements.clear();
    CanvasDraw();
}

/* ======================== WINDOW MANAGER IMPLEMENTATION ======================== */

WindowManager& WindowManager::getInstance() {
    static WindowManager instance;
    return instance;
}

WindowManager::WindowManager() {
    IsWindowHandlerAlive = true;
    Serial.print("WindowManager created.\n");
}

WindowManager::~WindowManager() {
    clearAllWindows();
    IsWindowHandlerAlive = false;
    Serial.print("WindowManager destroyed.\n");
}

void WindowManager::registerWindow(std::shared_ptr<Window> Win) {
    WindowRegistry.emplace_back(Win);
}

void WindowManager::unregisterWindow(Window* Win) {
    if (!Win) return;

    auto it = std::remove_if(WindowRegistry.begin(), WindowRegistry.end(),
        [&](const WindowAndUpdateInterval& entry) {
            if (auto winPtr = entry.windowWeakPtr.lock()) {
                return winPtr.get() == Win;
            }
            return false;
        });

    if (it != WindowRegistry.end()) {
        WindowRegistry.erase(it, WindowRegistry.end());
    }

    // Clear window area
    fb_rect(true, 0, Win->wi_sizing.Xpos, Win->wi_sizing.Ypos, 
            Win->wi_sizing.Width, Win->wi_sizing.Height, 0x0000, 0x0000);
}

void WindowManager::clearAllWindows() {
    for (auto& entry : WindowRegistry) {
        if (auto winPtr = entry.windowWeakPtr.lock()) {
            fb_rect(true, 0, winPtr->wi_sizing.Xpos, winPtr->wi_sizing.Ypos, 
                    winPtr->wi_sizing.Width, winPtr->wi_sizing.Height, 0x0000, 0x0000);
        }
    }
    WindowRegistry.clear();
    fb_clear(0x0000); // Clear entire screen
}

std::shared_ptr<Window> WindowManager::GetWindowByName(const std::string& WindowName) {
    for (auto& entry : WindowRegistry) {
        if (auto winPtr = entry.windowWeakPtr.lock()) {
            if (strcmp(winPtr->Currentcfg.name, WindowName.c_str()) == 0) {
                return winPtr;
            }
        }
    }
    return nullptr;
}

void WindowManager::UpdateAllWindows(bool Force, bool AndSubComps) {
    unsigned long currentTime = millis();
    for (auto it = WindowRegistry.begin(); it != WindowRegistry.end(); ) {
        if (auto winPtr = it->windowWeakPtr.lock()) {
            if (Force || (currentTime - winPtr->lastUpdateTime >= winPtr->UpdateTickRate * 1000)) {
                if (winPtr->dirty || Force) {
                    winPtr->WinDraw();
                    if (AndSubComps) winPtr->ForceUpdateSubComps();
                    winPtr->lastUpdateTime = currentTime;
                    winPtr->dirty = false;
                }
            }
            ++it;
        } else {
            it = WindowRegistry.erase(it);
        }
    }
}

void WindowManager::ApplyThemeAllWindows(uint16_t secondary, uint16_t background, uint16_t primary) {
    for (auto it = WindowRegistry.begin(); it != WindowRegistry.end(); ) {
        if (auto winPtr = it->windowWeakPtr.lock()) {
            winPtr->ApplyTheme(secondary, background, primary);
            ++it;
        } else {
            it = WindowRegistry.erase(it);
        }
    }
    UpdateAllWindows(true, true);
}

void WindowManager::notifyUpdateTickRateChange(Window* targetWindow,
                                               int newUpdateTickRate) {
    for (auto& entry : WindowRegistry) {
        if (auto winPtr = entry.windowWeakPtr.lock()) {
            if (winPtr.get() == targetWindow) {
                winPtr->UpdateTickRate = newUpdateTickRate / 1000.0f; // Convert ms to seconds
                Serial.printf("Update rate changed for Window: %s to %f seconds\n", 
                             winPtr->Currentcfg.name, winPtr->UpdateTickRate);
                return;
            }
        }
    }
    Serial.print("Error: Window not found in registry!");
}

bool WindowManager::initialize(bool graphicsEnabled) {
    AreGraphicsEnabled = graphicsEnabled;
    return AreGraphicsEnabled;
}

/* ======================== HELPER FUNCTIONS ======================== */

void CanvasForceParentUpdate(std::shared_ptr<Window> parent) {
    if (parent) {
        parent->ForceUpdate(false);
    }
}

void clearScreenEveryXCalls(uint16_t x) {
    static uint16_t callCount = 0;
    callCount++;
    if (callCount >= x) {
        fb_clear(0x0000);
        callCount = 0;
    }
}

#endif // MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_CPP_