#ifndef MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_HPP_
#define MAIN_OS_CODE_CORE_WINDOW_ENV_MWENV_HPP_

#include <stdint.h>
#include "esp_timer.h"
#include "hardware/drivers/lcd/fonts/font_basic_types.h"
#include <string>
#include <memory>
#include <sstream>
#include <algorithm>
#include <variant>//unions for the code
#include "code_stuff/types.h"
#include <memory>
#include <math.h>
#include "hardware/wiring/wiring.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "rom/cache.h"
#include <string.h>
#include <math.h>
#include "hardware/drivers/abstraction_layers/al_scr.h"
#include "code_stuff/types.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "hardware/drivers/lcd/st7789v2/lcDriver.h"
#include "hardware/drivers/lcd/fonts/font_avr_classics.h"
#include "os_code\core\window_env\wenv_basicThemes.h"
#include <vector>

#include "../../../hardware/drivers/psram_std/psram_std.hpp" //my custom work for psram stdd things
#include "hardware/drivers/lcd/st7789v2/lcdriverAddon.hpp"
#include <memory>
#include "os_code/core/rShell/enviroment/env_vars.h"
//get this from psram string and whatnot
// forward declarations
#include "hardware/drivers/lcd/st7789v2/t_shapes.h"
class Window;
class Canvas;
struct CanvasCfg;
struct TextChunk;
struct DrawableElement;

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

struct HighlighterTag {uint16_t color; bool enabled;};



//uint16_t background_color=0x1212;


// Forward declare so variant can use it
using ChunkContent = std::variant<
    stdpsram::String,
    ColorTag,
    SizeTag,
    PosTag,
    HighlighterTag,
    std::monostate
>;



 //if the enum value is negative, it disables the tag as opposed to having tag removal things, so HAH i am goated
    enum class TagType : int8_t {
        None = 0,
    
        PlainText,
    
        LineBreak,           // <n>
        UnderlineToggle,     // <u>
        StrikethroughToggle, // <s>
        BoldToggle,
        ItalicToggle,
    
        ColorChange,
        SizeChange,
        PosChange,
        HighlightChange,
    
        // negative values = disable
        UnderlineOff      = -UnderlineToggle,
        StrikethroughOff  = -StrikethroughToggle,
        BoldOff           = -BoldToggle,
        ItalicOff         = -ItalicToggle,
    };

    struct TextChunk {
        TagType kind = TagType::None;
        ChunkContent content = std::monostate{};
    
        TextChunk() = default;
        explicit TextChunk(stdpsram::String txt)
            : kind(TagType::PlainText), content(std::move(txt)) {}
        explicit TextChunk(TagType t)
            : kind(t), content(std::monostate{}) {}
        TextChunk(TagType t, ColorTag c)       : kind(t), content(c) {}
        TextChunk(TagType t, SizeTag s)        : kind(t), content(s) {}
        TextChunk(TagType t, PosTag p)         : kind(t), content(p) {}
        TextChunk(TagType t, HighlighterTag h) : kind(t), content(h) {}
    };
/* ---------------- update mode ---------------- */

enum e_wenv_updateType {
    manual,
    managed,
    both
};









static int16_t parse_int(const stdpsram::String& str, int base = 10);






/*optionsmapping*/
// ──────────────────────────────────────────────
// Window option flags (bit positions)
// Use 1 << N style so it's impossible to accidentally overlap
// ──────────────────────────────────────────────
enum WindowOptionBits : uint16_t {
    // Bit 0–3 (lowest bits – most commonly changed / cheapest features)-------------------------
    WIN_OPT_USE_BORDERGRADIENT          = 1 << 0,   // 0x0001
    //makes border use Gradient fill
    WIN_OPT_ANIMATED_BORDER       = 1 << 1,   // 0x0002
    //border now has - - - - - animated movement "shimmer"
    WIN_OPT_SHOW_TOP_BAR_MENU     = 1 << 2,   // 0x0004
    //shows -□x on top corner
    WIN_OPT_ROUNDED_CORNERS       = 1 << 3,   // 0x0008
    //corners are now rounded, clipped at 45 deg angle along n pixels (defined in window)

    //------------------------- Bit 4–7---------------------------------------------

    WIN_OPT_CLIPPED_CORNERS       = 1 << 4,   // 0x0010
    //corners are now triangles, clipped at 45 deg angle along n pixels (defined in window)
    WIN_OPT_TRANSPARENCY          = 1 << 5,   // 0x0020
    //enables transparency (0-255) for overlay windows
    WIN_OPT_INTERIOR_SPECIALFILL     = 1 << 6,   // 0x0040  
    //context note: enables interior of window to be filled according to internal pattern or Gradient fill

    WIN_OPT_CHILDFREE      = 1 << 7,   // 0x0080   
    //context note: optimization step keeping it to skip update checks, and blocks this from having children added

    // -------------------------Bit 8–11---------------------------------------------------
    WIN_OPT_IS_HEAVY_RENDERING    = 1 << 8,   // 0x0100   (games, animated graphs, etc.)
    WIN_OPT_ALLOW_RAW_VRAM_ACCESS = 1 << 9,   // 0x0200   (dangerous – only if really needed for 2d/3d)
	WIN_OPT_ISMEDIA_WINDOW=1<<10, //0x???? is this media playing a video or music or something that reqires it to display frames
    // Bits 11–15 still free (6 left)
    // WIN_OPT_???                = 1 << 10,
    // ...
};

 

enum class BgFillType : uint8_t {
 Solid,GradientVertical,GradientHorizontal,Checkerboard,Noise,
 Diagonal_lines,Transparent,triangles,waves,dots,count
};

struct HasDelta {
    bool dx;
    bool dy;
};
//const lookup table to describe rules for tiling on x and y axis. 
//true means that it repeats on this axis and can be copied or referenced in rendering
constexpr HasDelta fillTypeRules[] = {
    /* Solid */              {1, 1},
    /* GradientVertical */   {true,  false},
    /* GradientHorizontal */ {false, true },
    /* Checkerboard */       {true,  true },
    /* Noise */              {false, false},
    /* Diagonal_lines */     {true,  true },
    /* Transparent */        {false, false},
    /* Waves */              {false, true },
    /*dots*/                {false, false }
};

struct p_bgTile_cfg {
    uint8_t  win_rotation = 1; 
    BgFillType fill_type = BgFillType::Solid;
    uint16_t tileSize_x = 32;
    uint16_t tileSize_y = 32;
};


static void blit_tile(uint16_t targetX, uint16_t targetY,
    uint16_t* framebuffer,
    uint16_t* tileBuffer,
    uint16_t tileW, uint16_t tileH);
   //where to put it and what rotation. usually under text
   //we are putting transfer tile outside of the background tile object so that we can use it for other thingslike emojis or bitmaps


   //it's pretty simple lol
   class PsramBackgroundTile : public std::enable_shared_from_this<PsramBackgroundTile> {
    public:
        bool allocated = false;
        uint16_t* pseudoframebuffer = nullptr;
        p_bgTile_cfg pbt_cfg;           // your existing cfg struct
        uint16_t primaryColor = 0xFFFF;
        uint16_t secondaryColor = 0x0000;
    
        // Constructor – fixed size 32×32 (you can make it configurable later)
        explicit PsramBackgroundTile(uint16_t tileSizeX = 32, uint16_t tileSizeY = 32);
    
        // Generate pattern ONCE into PSRAM
        void generate_pattern(BgFillType type, uint16_t primary, uint16_t secondary);
    
        ~PsramBackgroundTile();
    };







struct WindowCfg {
    uint16_t Posx = 0;
    uint16_t Posy = 0;
    uint16_t Layer = 0; //0 is the top
    uint16_t renderPriority = 0; // does nothing so far
    uint16_t win_width  = 64;
    uint16_t win_height = 64;
    uint8_t  win_rotation = 1;          // quadrant only! holy fucking hell (I,II,III,IV)
    bool     AutoAlignment         = false;
    bool     WrapText              = true;
    bool     borderless            = false;
    bool     ShowNameAtTopOfWindow = false;
    uint8_t  TextSizeMult          = 1;
    char     name[32]              = {0};

    uint16_t optionsbitmask        = 0;   

    uint16_t BorderColor           = 0xFFFF;
    uint16_t BgColor               = 0x0000;
    uint16_t Bg_secondaryColor=0x4040;
    uint16_t WinTextColor          = 0xFFFF;
    BgFillType backgroundType=BgFillType::Solid;
    float    UpdateRate            = 0.5f;
    };
/* ---------------- helpers ---------------- */

struct WinComp_sizing {
    ui16 Xpos   = 0;
    ui16 Ypos   = 0;
    ui16 Zorder = 0;
    ui16 Width  = 0;
    ui16 Height = 0;
    uint8_t rotation = 0;
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

/* forward dependency */
void CanvasForceParentUpdate(std::shared_ptr<Window> parent);
int         safe_parse_int(const stdpsram::String& str, int default_val);
uint16_t    safe_parse_color(const stdpsram::String& str, uint16_t default_val);


/* ======================== CANVAS ======================== */



  //  s_bounds_16u c_bounds; //xywh bounds

class Window;//forward declare

struct CanvasCfg {
    int x = 0;
    int y = 0;
    int width = 32;
    int height = 32;
    
    bool borderless = true;
    bool DrawBG = true;
    
    uint16_t bgColor = 0x0000;
    uint16_t BorderColor = 0xFFFF;
    
    Window* parentWindow = nullptr;
    float UpdateTickRateS = 0.1f;
};

class Canvas : public std::enable_shared_from_this<Canvas> {
public:
    static std::shared_ptr<Canvas> Create(const CanvasCfg& cfg) {
        return std::shared_ptr<Canvas>(new Canvas(cfg));
    }
    
    ~Canvas();
    
    void Update(float deltaTime);
    void Draw();
    
    // Shape management helpers
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
    int GetWidth() const { return m_cfg.width; }
    int GetHeight() const { return m_cfg.height; }
    Window* GetParentWindow() const { return m_parentWindow; }
    
    // Setters with bounds checking
    void SetPosition(int x, int y);
    void SetSize(int width, int height);
    void SetParentWindow(Window* window) { m_parentWindow = window; }
    
private:
    explicit Canvas(const CanvasCfg& cfg);
    
    s_bounds_16u ClampBoundsToParent(s_bounds_16u bounds, s_bounds_16u parentBounds);
    
    CanvasCfg m_cfg;
    Window* m_parentWindow;
    fb_shape_buffer_t* m_shapeBuffer;
    uint16_t m_maxShapes;
    bool m_dirty;
};






/* ======================== WINDOW MANAGER ======================== */



void clearScreenEveryXCalls(uint16_t x);

class Window : public std::enable_shared_from_this<Window> {
    public:

    //why the fuck did i not have this
void set_position(uint16_t x, uint16_t y, bool interpolate = false);
void set_layer(uint8_t layer);
void set_size(uint16_t width, uint16_t height);


    bool window_highlighted=0; //default-init 0

    void get_physical_bounds(int& out_x, int& out_y, int& out_w, int& out_h);
        explicit Window(const WindowCfg& cfg, const std::string& initialContent = "");
    
        void WinDraw();
        
        // Overloads 
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
        stdpsram::Vector<TextChunk> cachedChunks;
        
        // Members you are using
       // std::string content;
       // In Window class definition:

        WindowCfg Initialcfg;
        WindowCfg Currentcfg;

        uint16_t logicalW;uint16_t logicalH; 
        //phisical dims
        fontdata w_font_info; //ffont info incl size
        // Background tile (PSRAM)
std::shared_ptr<PsramBackgroundTile> bgTile;

// Background configuration
BgFillType win_backgroundpattern;

//canvas thing we add on to the window here....
std::shared_ptr<Canvas> AddCanvas(const CanvasCfg& cfg);
void RemoveCanvas();
std::shared_ptr<Canvas> GetCanvas() const { return m_canvas; }
void DrawCanvas();  // Call this in WinDraw()
////// now the rest of the bullshit

// Call this once after changing pattern/colors


        struct {
            uint16_t Xpos   = 0;
            uint16_t Ypos   = 0;
            uint16_t Zorder = 0;
            uint16_t Width  = 0;
            uint16_t Height = 0;
            uint8_t rotation = 0;
        } wi_sizing;
    
        uint16_t win_internal_color_background = 0;
        uint16_t win_internal_color_border     = 0xFFFF;
        uint16_t win_internal_color_text       = 0xFFFF;
        int      win_internal_textsize_mult    = 1;
        uint16_t win_internal_optionsBitmask=0;
        float    UpdateTickRate = 0.5f;
    
        bool     IsWindowShown = true;
        bool     dirty         = true;
        uint64_t lastUpdateTime = 0;
        uint16_t   bgPrimaryColor = win_internal_color_background;   // your window BgColor by default
        uint16_t   bgSecondaryColor = Currentcfg.Bg_secondaryColor;
        
        
        void setupBackgroundTile();
        // Optional: if i want to hide implementation details later,
        // move tokenize() and TextState to private
        bool enable_refresh_override=0; //by default no need to enable, but ok if you want

    private:
    std::shared_ptr<Canvas> m_canvas;
    bool OtherTick=0; //true every OTHER update

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
    bool     bold          = false;        // ← add
    bool     italic        = false;        // ← add
    uint16_t highlight_bg  = 0;            // ← add
};
TextState Tstate ; //new var should default to defaults



    public:
    uint16_t currentPhysX = 0;
uint16_t currentPhysY = 0;


    // One tokenize – takes the native type
    stdpsram::Vector<TextChunk> tokenize(const stdpsram::String& s);
    // Optionally keep a std::string wrapper if needed
    stdpsram::Vector<TextChunk> tokenize(const std::string& s);
};


    struct WindowAndUpdateInterval {
        std::weak_ptr<Window> windowWeakPtr;
        int UpdateTickRate = 0;
    
        explicit WindowAndUpdateInterval(std::shared_ptr<Window> Win)
            : windowWeakPtr(Win),
              UpdateTickRate(static_cast<int>(Win->UpdateTickRate * 1000))
        {
        }
    
        void updateIfValid() {
            if (auto WinPtr = windowWeakPtr.lock()) {
                WinPtr->WinDraw();
            }
        }
    };

    typedef enum {
        TB_BLUETOOTH   = (1 << 0),
        TB_WIFI        = (1 << 1),
        TB_RF          = (1 << 2),
        TB_OPTICAL     = (1 << 3),
        TB_FLASHLIGHT  = (1 << 4),
        TB_BATT        = (1 << 5),
        TB_GYRO        = (1 << 6),
        TB_TEMP        = (1 << 7),
        TB_KEYS        = (1 << 8),
        TB_SDCARD      = (1 << 9),
        TB_SILENT      = (1<<10),
    } toolbar_items_t;

    

    typedef struct {
        bool tb_overlay; //if disabled,pushes other windows off to the bottom 
        uint8_t tb_update_hz; //target update rate for the toolbar
        uint8_t tb_rot; //0: top, 1 left, 2 bottom, 3 right
        bool showToolbar; //fake up polybar idgaf, we'll throw it here to manage it
        bool disableTouch; //touching it with the touchscreen doesn't do anything if this is enabled
        bool expandsDownOnTap; //much like the android context menu thing, this will expand down
        s_bmp_t* ref_iconptrs[16]; //god please tell me this is 16 pointers to my icons for that one enum
        toolbar_items_t icons_shown; //the above reference to the bitmaps for icon pointers
        uint16_t color; //glerp
    }toolbarconfig; //i'm doing a little c style pseudo oop here instead of attatching a new object

    

    class WindowManager {
        public:
            static WindowManager& getInstance() {
                static WindowManager instance;  // created once
                return instance;
            }
        
            void UpdateAll(bool force = false, bool ToolbarUpdate = true, bool repositionWindows = true, bool draw_toolbar_ontop = true);
            bool PruneDeadWindows();   
            bool registerWindow(std::shared_ptr<Window> window);
            void ClampToArea(s_bounds_16u bounds, bool is_universal); 
            //change clamping behavior for registering and drawing windows
            void ClampToArea(s_bounds_16u bounds, std::shared_ptr<Window> target); //clamp this window to this target
                
            // prevent copying
            WindowManager(const WindowManager&) = delete;
            WindowManager& operator=(const WindowManager&) = delete;


            //toolbar segment------------------
            void SetToolbarActive(bool on);
            void UpdateToolbar();
            void setToolbarRot(uint8_t new_rot);
            void addToolbarIco(s_bmp_t& icon); //reference of new icon by direct ref, no copy because icons are static single assets
            void RepositionAllWindows();
            void SortWindowsByZOrder();
            void SortWindowsByLayer();
            void SetToolbarText(const char* text);  // NEW: set time/date text
            void DrawToolBar();
            void ResetRepositioning() { windows_repositioned = false; }
              //--------------------------------

                //Get the space available for windows (screen minus toolbar)
              uint16_t GetAvailableWidth();
              uint16_t GetAvailableHeight();
              uint16_t GetToolbarOffset();  // offset from top/left where windows should start  
        
              private:
              bool windows_repositioned = false;
              WindowManager(); 
              ~WindowManager();
              
              std::vector<std::shared_ptr<Window>> windows;
          
              // Toolbar member variables
              toolbarconfig m_toolbarConfig;
              std::string toolbar_text;
              bool tb_dirty;
              uint64_t last_toolbar_update;

        };

#endif
