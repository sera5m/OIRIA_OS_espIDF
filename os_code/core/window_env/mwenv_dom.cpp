#include "mwenv_dom.hpp"

#include <string.h>

// ---------------------------------------------------------------------------
// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) – short and fine for frames
// ---------------------------------------------------------------------------
static uint16_t mwdom_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static int wr_need(DomWriter* w, size_t n) {
    if (!w || !w->buf) return -2;
    if (w->len + n > w->cap) { w->error = -1; return -1; }
    return 0;
}

static int wr_u8(DomWriter* w, uint8_t v) {
    if (wr_need(w, 1)) return -1;
    w->buf[w->len++] = v;
    return 0;
}

static int wr_bytes(DomWriter* w, const void* p, size_t n) {
    if (wr_need(w, n)) return -1;
    memcpy(w->buf + w->len, p, n);
    w->len += n;
    return 0;
}

void mwdom_writer_init(DomWriter* w, uint8_t* buf, size_t cap, uint16_t frame_id, uint8_t flags) {
    if (!w) return;
    w->buf = buf;
    w->cap = cap;
    w->frame_id = frame_id;
    w->flags = flags;
    w->error = 0;
    w->len = sizeof(DomFrameHeader); // reserve header
    if (cap < sizeof(DomFrameHeader)) {
        w->error = -1;
        w->len = 0;
    } else {
        memset(buf, 0, sizeof(DomFrameHeader));
    }
}

int mwdom_writer_meta(DomWriter* w, uint16_t screen_w, uint16_t screen_h, uint8_t window_count) {
    if (!w || w->error) return -1;
    DomMeta m{};
    m.frame_id = w->frame_id;
    m.window_count = window_count;
    m.screen_w = screen_w;
    m.screen_h = screen_h;
    if (wr_u8(w, MWDOM_OP_META)) return -1;
    return wr_bytes(w, &m, sizeof(m));
}

int mwdom_writer_clear(DomWriter* w, uint16_t color) {
    if (!w || w->error) return -1;
    if (wr_u8(w, MWDOM_OP_CLEAR_FB)) return -1;
    return wr_bytes(w, &color, sizeof(color));
}

int mwdom_writer_window(DomWriter* w, const DomWindowHeader* win) {
    if (!w || !win || w->error) return -1;
    if (wr_u8(w, MWDOM_OP_WINDOW)) return -1;
    return wr_bytes(w, win, sizeof(*win));
}

int mwdom_writer_window_text(DomWriter* w, uint8_t win_index, const char* utf8, uint16_t len) {
    if (!w || w->error) return -1;
    if (!utf8) { utf8 = ""; len = 0; }
    if (len > MWDOM_MAX_TEXT_BYTES) len = MWDOM_MAX_TEXT_BYTES;
    if (wr_u8(w, MWDOM_OP_WINDOW_TEXT)) return -1;
    if (wr_u8(w, win_index)) return -1;
    if (wr_bytes(w, &len, sizeof(len))) return -1;
    return wr_bytes(w, utf8, len);
}

int mwdom_writer_shape(DomWriter* w, const DomShape* s) {
    if (!w || !s || w->error) return -1;
    if (wr_u8(w, MWDOM_OP_SHAPE)) return -1;
    return wr_bytes(w, s, sizeof(*s));
}

int mwdom_writer_aw_object(DomWriter* w, const DomAwObject* o) {
    if (!w || !o || w->error) return -1;
    if (wr_u8(w, MWDOM_OP_AW_OBJECT)) return -1;
    return wr_bytes(w, o, sizeof(*o));
}

int mwdom_writer_toolbar(DomWriter* w, const DomToolbar* tb) {
    if (!w || !tb || w->error) return -1;
    if (wr_u8(w, MWDOM_OP_TOOLBAR)) return -1;
    return wr_bytes(w, tb, sizeof(*tb));
}

size_t mwdom_writer_finish(DomWriter* w) {
    if (!w || !w->buf || w->cap < sizeof(DomFrameHeader)) return 0;
    if (w->error) return 0;

    // terminator
    if (wr_u8(w, MWDOM_OP_END)) return 0;

    const size_t total = w->len;
    const size_t body_off = sizeof(DomFrameHeader);
    const size_t body_len = total - body_off;

    DomFrameHeader* h = (DomFrameHeader*)w->buf;
    h->magic[0] = MWDOM_MAGIC0;
    h->magic[1] = MWDOM_MAGIC1;
    h->magic[2] = MWDOM_MAGIC2;
    h->magic[3] = MWDOM_MAGIC3;
    h->version  = MWDOM_VERSION;
    h->flags    = w->flags;
    h->frame_id = w->frame_id;
    h->body_bytes = (uint16_t)body_len;
    h->crc16 = mwdom_crc16(w->buf + body_off, body_len);
    return total;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

static int rd_need(DomReader* r, size_t n) {
    if (!r || r->error) return -1;
    if (r->pos + n > r->body_len) { r->error = -1; return -1; }
    return 0;
}

int mwdom_reader_open(DomReader* r, const uint8_t* frame, size_t frame_len) {
    if (!r || !frame || frame_len < sizeof(DomFrameHeader)) return -1;
    memset(r, 0, sizeof(*r));
    memcpy(&r->hdr, frame, sizeof(DomFrameHeader));
    if (r->hdr.magic[0] != MWDOM_MAGIC0 || r->hdr.magic[1] != MWDOM_MAGIC1 ||
        r->hdr.magic[2] != MWDOM_MAGIC2 || r->hdr.magic[3] != MWDOM_MAGIC3)
        return -2;
    if (r->hdr.version != MWDOM_VERSION) return -3;
    if (sizeof(DomFrameHeader) + r->hdr.body_bytes > frame_len) return -4;

    r->body = frame + sizeof(DomFrameHeader);
    r->body_len = r->hdr.body_bytes;
    r->pos = 0;

    if (r->hdr.crc16) {
        uint16_t c = mwdom_crc16(r->body, r->body_len);
        if (c != r->hdr.crc16) return -5;
    }
    return 0;
}

int mwdom_reader_next_op(DomReader* r) {
    if (!r || r->error) return -1;
    if (r->pos >= r->body_len) return MWDOM_OP_END;
    return (int)r->body[r->pos++];
}

static int rd_bytes(DomReader* r, void* out, size_t n) {
    if (rd_need(r, n)) return -1;
    memcpy(out, r->body + r->pos, n);
    r->pos += n;
    return 0;
}

int mwdom_reader_meta(DomReader* r, DomMeta* out) {
    return rd_bytes(r, out, sizeof(*out));
}

int mwdom_reader_clear(DomReader* r, uint16_t* color) {
    return rd_bytes(r, color, sizeof(*color));
}

int mwdom_reader_window(DomReader* r, DomWindowHeader* out) {
    return rd_bytes(r, out, sizeof(*out));
}

int mwdom_reader_window_text(DomReader* r, uint8_t* win_index, char* text_buf,
                             uint16_t text_cap, uint16_t* out_len) {
    if (rd_need(r, 1 + 2)) return -1;
    uint8_t idx = r->body[r->pos++];
    uint16_t len = 0;
    memcpy(&len, r->body + r->pos, 2);
    r->pos += 2;
    if (rd_need(r, len)) return -1;
    if (win_index) *win_index = idx;
    if (out_len) *out_len = len;
    if (text_buf && text_cap) {
        uint16_t copy = len;
        if (copy >= text_cap) copy = text_cap - 1;
        memcpy(text_buf, r->body + r->pos, copy);
        text_buf[copy] = '\0';
    }
    r->pos += len;
    return 0;
}

int mwdom_reader_shape(DomReader* r, DomShape* out) {
    return rd_bytes(r, out, sizeof(*out));
}

int mwdom_reader_aw_object(DomReader* r, DomAwObject* out) {
    return rd_bytes(r, out, sizeof(*out));
}

int mwdom_reader_toolbar(DomReader* r, DomToolbar* out) {
    return rd_bytes(r, out, sizeof(*out));
}
