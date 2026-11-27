// screenspace configuration for touch input
#include "code_stuff/types.h" //for bounds

enum TouchscreenDebugVisualizationMode : uint8_t {
    // ---- Visual element flags (low bits) ----
    TSD_NONE                  = 0,           // no touch visualization
    TSD_CROSSHAIR             = 1u << 0,     // draw a crosshair at each touch point
    TSD_CIRCLE                = 1u << 1,     // draw a circle at each touch point
    TSD_COLOR_CODED           = 1u << 2,     // color-code touchpoints by instantiation/id
    TSD_LABEL                 = 1u << 3,     // number each touchpoint to the side

    // ---- Label content flags (combinable, only meaningful if TSD_LABEL set) ----
    TSD_LABEL_XY              = 1u << 4,     // include position XY in label
    TSD_LABEL_TIME_HELD       = 1u << 5,     // include time held down (seconds)

    // ---- Output / debug transport flags (upper bits) ----
    TSD_SEND_DEBUG_TTY        = 1u << 6,     // send debug info over tty

    // ---- Convenience combinations ----
    TSD_CROSSHAIR_AND_CIRCLE  = TSD_CROSSHAIR | TSD_CIRCLE,
    TSD_CIRCLE_AND_LABEL      = TSD_CIRCLE | TSD_LABEL | TSD_LABEL_XY | TSD_LABEL_TIME_HELD,
    TSD_ALL_VISUALS           = TSD_CROSSHAIR | TSD_CIRCLE | TSD_COLOR_CODED |
                                TSD_LABEL | TSD_LABEL_XY | TSD_LABEL_TIME_HELD,
};

inline TouchscreenDebugVisualizationMode operator|(TouchscreenDebugVisualizationMode a,
                                                   TouchscreenDebugVisualizationMode b) {
    return static_cast<TouchscreenDebugVisualizationMode>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline TouchscreenDebugVisualizationMode operator&(TouchscreenDebugVisualizationMode a,
                                                   TouchscreenDebugVisualizationMode b) {
    return static_cast<TouchscreenDebugVisualizationMode>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline TouchscreenDebugVisualizationMode& operator|=(TouchscreenDebugVisualizationMode& a,
                                                     TouchscreenDebugVisualizationMode b) {
    a = a | b;
    return a;
}

inline bool tsd_has(TouchscreenDebugVisualizationMode mode,
                    TouchscreenDebugVisualizationMode flag) {
    return (static_cast<uint8_t>(mode) & static_cast<uint8_t>(flag)) != 0;
}

//should instance this per device driver, as this is just an abstraction layer config
struct TouchConf {

    // absolute, for the local compositor
    struct TouchscreenMode {
        //area covered by the touch absolute in pixels. (equivalent to screen resolution)
        uint16_t ScreenWidth;
        uint16_t ScreenHeight;

        //scaling factors for touch coordinates,
        //touchscreen may have different resolution than the screen
        // we need to scale the touch coordinates to match the screen coordinates.
        float TouchScalar_x = 1.0f;
        float TouchScalar_y = 1.0f;

        uint8_t CompositorInputEntryLayer = 0; //config var. comspotitor usually is 0 at first layer.
        //if we want to use RESERVED last (os panel config), we can access it by setting this to 255.
    } touchscreen;

    // relative, for a connected computer
    //for when we want to use the touchscreen as a mousepad/touchpad instead of a touchscreen normally
    //see if you THINK of it, a touchscreen is just a touchpad with a screen behind it.
    struct TouchpadMode {
        //the owning computer screen resolution is not needed; touchpads report relative movement (Δx, Δy) like a mouse
        uint16_t SensitivityMultiplier = 10; //multiplier of "pixels" on the touchpad to Δpos on the "mouse" on a connected computer

        bool ReserveFractionalScreenAreaForTouchpad = false; //if true, the touchpad will only use a fraction of the screen area, and the rest will be reserved for other things (like an on-screen keyboard or stream deck hotkeys)
        s_bounds_16u FractionalReserveBounds = {0, 0, 128, 128};
        //bounds xywh , startxy, add wh to get endxy.
        //used for physical bounds on watch touchscreen, enabling a reserved area of a zone to use this on, meaning you can have an on screen keyboard or "stream deck hotkeys",
        //and still use a "touchpad" on the connected other device
        //the compositor still needs to be configured to add a window to the reserved area.
        TouchscreenDebugVisualizationMode visualizationMode = TSD_CROSSHAIR; //default render crosshair in this space
    } touchpad;

    // remaining unified configuration items
    int16_t deadzone_x = 0; //deadzone for touch input in pixels.
    int16_t deadzone_y = 0; //deadzone? more like dead name. i can't think of a joke

    int debounce_time_ms = 0;
    uint8_t max_touch_points = 5; //max fingers to track. 5 is a good number, but you can change it if you want to be a rebel
    TouchscreenDebugVisualizationMode debugVisMode = TSD_NONE;

    uint32_t touchscreen_PollInterval_us = 20000; //guess what? micros. because what if i want one zillion hertz
    //default 50 fps idk
    uint32_t touchpad_PollInterval_us = 20000;
};


