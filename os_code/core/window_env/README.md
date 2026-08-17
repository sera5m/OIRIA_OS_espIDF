# Window environment split

Original monolithic `MWenv.cpp` / `MWenv.hpp` split into three units:

| Unit | Header | Source | Contents |
|------|--------|--------|----------|
| **Psram tile** | `PsramBackgroundTile.hpp` | `PsramBackgroundTile.cpp` | `BgFillType`, `p_bgTile_cfg`, `PsramBackgroundTile`, `blit_tile` / `blit_tile_clipped` |
| **Canvas** | `Canvas.hpp` | `Canvas.cpp` | `CanvasCfg`, `Canvas` |
| **Window / WindowManager** | `MWenv.hpp` | `MWenv.cpp` | rich-text tags, `WindowCfg`, `Window`, toolbar types, `WindowManager`, display-push task |

`wenv_basicThemes.h` is unchanged and lives beside these files.

## Include order / dependencies

```
PsramBackgroundTile.hpp   (standalone)
Canvas.hpp                → needs Window forward-decl (provided)
MWenv.hpp                 → includes both of the above
```

`Canvas.cpp` includes `MWenv.hpp` for the full `Window` definition (LocalToScreen, dirty flags, etc.).

## ESP-IDF integration

Point `COMPONENT_SRCS` / `CMakeLists.txt` at all three `.cpp` files in the same component that previously only listed `MWenv.cpp`. Keep the include path covering this directory (or `os_code/core/window_env/`).

Example CMake fragment:

```cmake
set(srcs
    "os_code/core/window_env/MWenv.cpp"
    "os_code/core/window_env/Canvas.cpp"
    "os_code/core/window_env/PsramBackgroundTile.cpp"
)
```

Existing `#include "os_code/core/window_env/MWenv.hpp"` continues to work for code that only needs Window / WindowManager; pull in `Canvas.hpp` or `PsramBackgroundTile.hpp` directly when you only need those types.
