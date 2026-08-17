#pragma once
// =============================================================================
// mwenv_dom – serializable "solved UI" DOM for split render (ESP1 logic → ESP2 pixels)
// =============================================================================
// Pipeline (local):
//   app → SetText / Canvas / AnimWorld → WinDraw → framebuffer → LCD
//
// Pipeline (collaborative):
//   ESP1 app → build DomFrame → UART @ 2Mbps → ESP2 apply DomFrame → WinDraw/LCD
//
// Design goals:
//   - Compact binary (not JSON)
//   - Fixed-endian little-endian
//   - Idempotent full frames + optional dirty patches
//   - No heap required on the wire decode path if caller supplies a scratch buffer
// =============================================================================

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Magic / version
// ---------------------------------------------------------------------------
#define MWDOM_MAGIC0        'M'
#define MWDOM_MAGIC1        'W'
#define MWDOM_MAGIC2        'D'
#define MWDOM_MAGIC3        '1'
#define MWDOM_VERSION       1

// Practical limits for one frame (raise if PSRAM TX buffer is large)
#ifndef MWDOM_MAX_WINDOWS
#define MWDOM_MAX_WINDOWS   4
#endif
#ifndef MWDOM_MAX_TEXT_BYTES
#define MWDOM_MAX_TEXT_BYTES  2048
#endif
#ifndef MWDOM_MAX_SHAPES
#define MWDOM_MAX_SHAPES      48
#endif
#ifndef MWDOM_MAX_AW_OBJECTS
#define MWDOM_MAX_AW_OBJECTS  40
#endif
#ifndef MWDOM_MAX_FRAME_BYTES
#define MWDOM_MAX_FRAME_BYTES 4096
#endif

// ---------------------------------------------------------------------------
// Node opcodes (tag byte before each payload)
// ---------------------------------------------------------------------------
typedef enum {
    MWDOM_OP_END          = 0x00,  // end of frame body
    MWDOM_OP_CLEAR_FB     = 0x01,  // uint16 color
    MWDOM_OP_WINDOW       = 0x10,  // DomWindowHeader + optional trailing text
    MWDOM_OP_WINDOW_TEXT  = 0x11,  // uint8 win_index, uint16 len, bytes[len]
    MWDOM_OP_SHAPE        = 0x20,  // DomShape
    MWDOM_OP_AW_OBJECT    = 0x30,  // DomAwObject (solved pose + color + optional label)
    MWDOM_OP_TOOLBAR      = 0x40,  // DomToolbar
    MWDOM_OP_META         = 0x7F,  // DomMeta (frame id, flags)
} mwdom_op_t;

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------
typedef enum {
    MWDOM_FRAME_FULL      = 1 << 0,  // complete scene; receiver may clear first
    MWDOM_FRAME_DELTA     = 1 << 1,  // only changed nodes
    MWDOM_FRAME_NEEDS_ACK = 1 << 2,
} mwdom_frame_flags_t;

typedef enum {
    MWDOM_WIN_BORDERLESS  = 1 << 0,
    MWDOM_WIN_SHOWN       = 1 << 1,
    MWDOM_WIN_FULLSCREEN  = 1 << 2,
    MWDOM_WIN_HAS_TEXT    = 1 << 3,
    MWDOM_WIN_HAS_CANVAS  = 1 << 4,
} mwdom_win_flags_t;

// ---------------------------------------------------------------------------
// Packed structs (wire layout) – all LE
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

typedef struct {
    char     magic[4];       // "MWD1"
    uint8_t  version;        // MWDOM_VERSION
    uint8_t  flags;          // mwdom_frame_flags_t
    uint16_t frame_id;       // monotonic
    uint16_t body_bytes;     // size of payload after this header
    uint16_t crc16;          // CRC of body (0 = unused)
} DomFrameHeader;

typedef struct {
    uint16_t frame_id;
    uint8_t  window_count;
    uint8_t  reserved;
    uint16_t screen_w;
    uint16_t screen_h;
} DomMeta;

typedef struct {
    uint8_t  index;          // slot 0..MWDOM_MAX_WINDOWS-1
    uint8_t  flags;          // mwdom_win_flags_t
    uint8_t  rotation;       // 0..3
    uint8_t  text_size_mult;
    int16_t  x, y;
    uint16_t w, h;
    uint16_t bg_color;
    uint16_t border_color;
    uint16_t text_color;
    uint8_t  bg_fill_type;   // BgFillType as uint8
    uint8_t  layer;
    // If MWDOM_WIN_HAS_TEXT, next OP is WINDOW_TEXT for this index
} DomWindowHeader;

typedef struct {
    uint8_t  win_index;
    uint8_t  type;           // fb_shape_type
    uint8_t  layer;
    uint8_t  shown;
    int16_t  x, y;
    uint16_t w, h;
    uint16_t color;
} DomShape;

// Solved AnimWorld object – already integrated (no velocity needed on ESP2)
typedef struct {
    uint8_t  win_index;      // which window/canvas owns this
    uint8_t  flags;          // bit0 filled, bit1 has_text
    int16_t  px, py;         // pixel-space top-left of AABB (canvas local)
    uint16_t pw, ph;         // pixel size
    uint16_t color;
    uint16_t text_color;
    uint8_t  text_size;
    uint8_t  z_layer;
    char     text[12];       // short label; empty if none
} DomAwObject;

typedef struct {
    uint8_t  show;
    uint8_t  rot;
    uint16_t color;
    char     text[24];
} DomToolbar;

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Writer (ESP1 – logic side)
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t* buf;
    size_t   cap;
    size_t   len;
    uint16_t frame_id;
    uint8_t  flags;
    int      error;          // 0 ok, -1 overflow, -2 bad arg
} DomWriter;

void     mwdom_writer_init(DomWriter* w, uint8_t* buf, size_t cap, uint16_t frame_id, uint8_t flags);
int      mwdom_writer_meta(DomWriter* w, uint16_t screen_w, uint16_t screen_h, uint8_t window_count);
int      mwdom_writer_clear(DomWriter* w, uint16_t color);
int      mwdom_writer_window(DomWriter* w, const DomWindowHeader* win);
int      mwdom_writer_window_text(DomWriter* w, uint8_t win_index, const char* utf8, uint16_t len);
int      mwdom_writer_shape(DomWriter* w, const DomShape* s);
int      mwdom_writer_aw_object(DomWriter* w, const DomAwObject* o);
int      mwdom_writer_toolbar(DomWriter* w, const DomToolbar* tb);
// Finalize: writes OP_END, fills header body_bytes + optional crc. Returns total bytes.
size_t   mwdom_writer_finish(DomWriter* w);

// ---------------------------------------------------------------------------
// Reader (ESP2 – pixel side)
// ---------------------------------------------------------------------------

typedef struct {
    const uint8_t* body;
    size_t         body_len;
    size_t         pos;
    DomFrameHeader hdr;
    int            error;
} DomReader;

// Validates magic/version; sets up body cursor. Returns 0 on ok.
int  mwdom_reader_open(DomReader* r, const uint8_t* frame, size_t frame_len);
// Returns next opcode, or MWDOM_OP_END / negative on error.
int  mwdom_reader_next_op(DomReader* r);
// Payload readers – call immediately after next_op matches.
int  mwdom_reader_meta(DomReader* r, DomMeta* out);
int  mwdom_reader_clear(DomReader* r, uint16_t* color);
int  mwdom_reader_window(DomReader* r, DomWindowHeader* out);
// text_buf receives up to text_cap-1 bytes + NUL; *out_len = raw length
int  mwdom_reader_window_text(DomReader* r, uint8_t* win_index, char* text_buf, uint16_t text_cap, uint16_t* out_len);
int  mwdom_reader_shape(DomReader* r, DomShape* out);
int  mwdom_reader_aw_object(DomReader* r, DomAwObject* out);
int  mwdom_reader_toolbar(DomReader* r, DomToolbar* out);

#ifdef __cplusplus
}

// C++ helpers that know about Window / Canvas / AnimWorld (optional include)
#if defined(MWDOM_HAVE_MWENV) && MWDOM_HAVE_MWENV
#include <memory>
class Window;
class AnimWorld;
struct AwCamera;

// Pack one window's "solved" state into an existing DomWriter.
// - text markup is sent as WINDOW_TEXT
// - canvas shapes as SHAPE ops
// - AnimWorld visible objects projected to pixel AABB as AW_OBJECT ops
int mwdom_pack_window(DomWriter* w, uint8_t index, const Window& win,
                      const AnimWorld* world, const AwCamera* cam);
#endif

#endif // __cplusplus
