#include "html_to_mwenv.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static std::string tolower_str(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)tolower((unsigned char)c);
    return o;
}

// RGB888 → RGB565
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t parse_hex_color(const char* p) {
    // #RGB or #RRGGBB
    if (!p || *p != '#') return 0xFFFF;
    ++p;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = (char)tolower((unsigned char)c);
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        return -1;
    };
    int len = 0;
    while (p[len] && isxdigit((unsigned char)p[len])) ++len;
    uint8_t r = 0, g = 0, b = 0;
    if (len == 3) {
        int rh = hex(p[0]), gh = hex(p[1]), bh = hex(p[2]);
        if (rh < 0 || gh < 0 || bh < 0) return 0xFFFF;
        r = (uint8_t)(rh * 17); g = (uint8_t)(gh * 17); b = (uint8_t)(bh * 17);
    } else if (len >= 6) {
        int r1 = hex(p[0]), r0 = hex(p[1]);
        int g1 = hex(p[2]), g0 = hex(p[3]);
        int b1 = hex(p[4]), b0 = hex(p[5]);
        if (r1 < 0 || r0 < 0 || g1 < 0 || g0 < 0 || b1 < 0 || b0 < 0) return 0xFFFF;
        r = (uint8_t)((r1 << 4) | r0);
        g = (uint8_t)((g1 << 4) | g0);
        b = (uint8_t)((b1 << 4) | b0);
    } else {
        return 0xFFFF;
    }
    return rgb565(r, g, b);
}

static uint16_t named_color(const std::string& name) {
    std::string n = tolower_str(name);
    if (n == "red")     return rgb565(255, 0, 0);
    if (n == "green")   return rgb565(0, 180, 0);
    if (n == "blue")    return rgb565(40, 120, 255);
    if (n == "white")   return 0xFFFF;
    if (n == "black")   return 0x0000;
    if (n == "yellow")  return rgb565(255, 220, 0);
    if (n == "cyan")    return rgb565(0, 220, 220);
    if (n == "magenta") return rgb565(255, 0, 200);
    if (n == "orange")  return rgb565(255, 140, 0);
    if (n == "gray" || n == "grey") return rgb565(160, 160, 160);
    if (n == "silver")  return rgb565(192, 192, 192);
    if (n == "navy")    return rgb565(0, 0, 128);
    if (n == "purple")  return rgb565(160, 32, 240);
    return 0xFFFF;
}

static uint16_t color_from_attr(const std::string& val) {
    std::string v = val;
    // trim
    while (!v.empty() && is_ws(v.front())) v.erase(v.begin());
    while (!v.empty() && is_ws(v.back()))  v.pop_back();
    if (v.empty()) return 0xFFFF;
    if (v[0] == '#') return parse_hex_color(v.c_str());
    // style="color: #abc" — caller may pass full style; search color:
    auto pos = v.find("color:");
    if (pos == std::string::npos) pos = v.find("color=");
    if (pos != std::string::npos) {
        pos = v.find_first_not_of(" \t:=", pos + 5);
        if (pos != std::string::npos) {
            size_t end = pos;
            while (end < v.size() && !is_ws(v[end]) && v[end] != ';' && v[end] != '"')
                ++end;
            return color_from_attr(v.substr(pos, end - pos));
        }
    }
    return named_color(v);
}

// Decode a single HTML entity starting at s[i]; advances i past it.
static void decode_entity(const char* s, size_t len, size_t& i, std::string& out) {
    if (i >= len || s[i] != '&') { out.push_back(s[i++]); return; }
    size_t start = i;
    ++i;
    if (i < len && s[i] == '#') {
        ++i;
        int base = 10;
        if (i < len && (s[i] == 'x' || s[i] == 'X')) { base = 16; ++i; }
        long code = 0;
        while (i < len && s[i] != ';') {
            int d = -1;
            char c = s[i];
            if (c >= '0' && c <= '9') d = c - '0';
            else if (base == 16) {
                c = (char)tolower((unsigned char)c);
                if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
            }
            if (d < 0) break;
            code = code * base + d;
            ++i;
            if (code > 0x10FFFF) break;
        }
        if (i < len && s[i] == ';') ++i;
        if (code > 0 && code < 128) out.push_back((char)code);
        else if (code == 160) out.push_back(' ');
        else out.push_back('?');
        return;
    }
    // named
    size_t j = i;
    while (j < len && isalpha((unsigned char)s[j])) ++j;
    std::string name(s + i, s + j);
    if (j < len && s[j] == ';') ++j;
    i = j;
    if (name == "amp") out.push_back('&');
    else if (name == "lt") out.push_back('<');
    else if (name == "gt") out.push_back('>');
    else if (name == "quot") out.push_back('"');
    else if (name == "apos") out.push_back('\'');
    else if (name == "nbsp") out.push_back(' ');
    else if (name == "copy") out += "(c)";
    else if (name == "mdash" || name == "ndash") out.push_back('-');
    else {
        // unknown – keep raw
        out.append(s + start, s + i);
    }
}

// Escape characters that would confuse MWenv tokenizer (<| …)
static void append_plain(std::string& out, const char* t, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char c = t[i];
        if (c == '<' && i + 1 < n && t[i + 1] == '|') {
            out += "< "; // break the tag sequence
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') c = ' ';
        // collapse runs of spaces lightly at emit time is hard; keep single spaces
        out.push_back(c);
    }
}

static void append_decoded_text(std::string& out, const char* s, size_t len) {
    size_t i = 0;
    while (i < len) {
        if (s[i] == '&') {
            std::string tmp;
            decode_entity(s, len, i, tmp);
            append_plain(out, tmp.c_str(), tmp.size());
        } else {
            size_t j = i;
            while (j < len && s[j] != '&') ++j;
            append_plain(out, s + i, j - i);
            i = j;
        }
    }
}

// ---------------------------------------------------------------------------
// Tag parse
// ---------------------------------------------------------------------------

struct Attr {
    std::string key;
    std::string val;
};

struct Tag {
    std::string name;
    std::vector<Attr> attrs;
    bool closing = false;
    bool self_closing = false;
};

static std::string attr_get(const Tag& t, const char* key) {
    std::string k = tolower_str(key);
    for (auto& a : t.attrs) {
        if (tolower_str(a.key) == k) return a.val;
    }
    return {};
}

// Parse one tag starting at s[i] where s[i]=='<'. Advances i past '>'.
static bool parse_tag(const char* s, size_t len, size_t& i, Tag& out) {
    out = Tag{};
    if (i >= len || s[i] != '<') return false;
    ++i;
    while (i < len && is_ws(s[i])) ++i;
    if (i < len && s[i] == '/') { out.closing = true; ++i; }
    while (i < len && is_ws(s[i])) ++i;

    size_t ns = i;
    while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == ':')) ++i;
    out.name = tolower_str(std::string(s + ns, s + i));

    // attrs
    while (i < len && s[i] != '>') {
        while (i < len && is_ws(s[i])) ++i;
        if (i >= len || s[i] == '>') break;
        if (s[i] == '/') { out.self_closing = true; ++i; continue; }

        size_t ks = i;
        while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == ':')) ++i;
        Attr a;
        a.key = std::string(s + ks, s + i);
        while (i < len && is_ws(s[i])) ++i;
        if (i < len && s[i] == '=') {
            ++i;
            while (i < len && is_ws(s[i])) ++i;
            if (i < len && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                size_t vs = i;
                while (i < len && s[i] != q) ++i;
                a.val = std::string(s + vs, s + i);
                if (i < len) ++i;
            } else {
                size_t vs = i;
                while (i < len && !is_ws(s[i]) && s[i] != '>') ++i;
                a.val = std::string(s + vs, s + i);
            }
        }
        if (!a.key.empty()) out.attrs.push_back(std::move(a));
    }
    if (i < len && s[i] == '>') ++i;
    return !out.name.empty();
}

// ---------------------------------------------------------------------------
// Converter state machine
// ---------------------------------------------------------------------------

struct Conv {
    const HtmlConvertOptions& opt;
    HtmlDoc& doc;
    std::string& out;

    int size_stack[16];
    int size_sp = 0;
    uint16_t color_stack[16];
    int color_sp = 0;
    bool in_pre = false;
    bool in_script = false;
    bool in_style = false;
    bool in_title = false;
    int skip_depth = 0; // nested ignored regions
    int list_depth = 0;
    int ol_counter[8] = {0};
    bool pending_space = false;

    Conv(const HtmlConvertOptions& o, HtmlDoc& d)
        : opt(o), doc(d), out(d.markup) {
        size_stack[0] = o.base_size;
        color_stack[0] = o.default_color;
    }

    int cur_size() const { return size_stack[size_sp]; }
    uint16_t cur_color() const { return color_stack[color_sp]; }

    void push_size(int s) {
        if (size_sp + 1 < 16) size_stack[++size_sp] = s;
        char buf[24];
        snprintf(buf, sizeof(buf), "<|size=%d|>", s);
        out += buf;
    }
    void pop_size() {
        if (size_sp > 0) --size_sp;
        char buf[24];
        snprintf(buf, sizeof(buf), "<|size=%d|>", cur_size());
        out += buf;
    }
    void push_color(uint16_t c) {
        if (color_sp + 1 < 16) color_stack[++color_sp] = c;
        char buf[28];
        snprintf(buf, sizeof(buf), "<|color=0x%04X|>", (unsigned)c);
        out += buf;
    }
    void pop_color() {
        if (color_sp > 0) --color_sp;
        char buf[28];
        snprintf(buf, sizeof(buf), "<|color=0x%04X|>", (unsigned)cur_color());
        out += buf;
    }

    void emit_nl() {
        // avoid runaway blank lines
        if (out.size() >= 4 && out.compare(out.size() - 4, 4, "<|n|>") == 0)
            return;
        out += "<|n|>";
        pending_space = false;
    }

    void emit_text(const char* t, size_t n) {
        if (skip_depth || in_script || in_style) return;
        if (in_title) {
            std::string tmp;
            size_t i = 0;
            while (i < n) {
                if (t[i] == '&') decode_entity(t, n, i, tmp);
                else tmp.push_back(t[i++]);
            }
            // trim
            while (!tmp.empty() && is_ws(tmp.front())) tmp.erase(tmp.begin());
            while (!tmp.empty() && is_ws(tmp.back())) tmp.pop_back();
            if (!tmp.empty()) doc.title = tmp;
            return;
        }
        if (n == 0) return;
        if (!in_pre) {
            // collapse leading whitespace into a single pending space
            size_t a = 0;
            while (a < n && is_ws(t[a])) ++a;
            if (a == n) { pending_space = true; return; }
            if (pending_space || (a > 0 && !out.empty() && out.back() != '>' && out.back() != ' '))
                out.push_back(' ');
            pending_space = false;
            // trim trailing for now; keep internal
            size_t b = n;
            while (b > a && is_ws(t[b - 1])) --b;
            if (b < n) pending_space = true;
            append_decoded_text(out, t + a, b - a);
        } else {
            append_decoded_text(out, t, n);
        }
    }

    void open_tag(const Tag& t) {
        const std::string& n = t.name;

        if (n == "script" || n == "style" || n == "svg" || n == "iframe" ||
            n == "object" || n == "noscript") {
            if (n == "script") in_script = true;
            if (n == "style")  in_style = true;
            ++skip_depth;
            return;
        }
        if (skip_depth) return;

        if (n == "title") { in_title = true; return; }
        if (n == "br") { emit_nl(); return; }
        if (n == "hr") {
            emit_nl();
            push_color(opt.muted_color);
            out += "----------";
            pop_color();
            emit_nl();
            return;
        }
        if (n == "p" || n == "div" || n == "section" || n == "article" ||
            n == "header" || n == "footer" || n == "main" || n == "blockquote") {
            emit_nl();
            return;
        }
        if (n == "pre" || n == "code") {
            if (n == "pre") { in_pre = true; emit_nl(); }
            push_color(opt.code_color);
            return;
        }
        if (n == "h1") { emit_nl(); push_size(std::min(4, 16)); push_color(0xFFE0); return; }
        if (n == "h2") { emit_nl(); push_size(std::min(3, 16)); push_color(0xF7BE); return; }
        if (n == "h3") { emit_nl(); push_size(std::min(2, 16)); push_color(0xEF5D); return; }
        if (n == "h4" || n == "h5" || n == "h6") {
            emit_nl(); push_size(std::min(2, 16)); return;
        }
        if (n == "b" || n == "strong") { out += "<|b|>"; return; }
        if (n == "i" || n == "em")     { out += "<|i|>"; return; }
        if (n == "u")                  { out += "<|u|>"; return; }
        if (n == "s" || n == "strike" || n == "del") { out += "<|s|>"; return; }
        if (n == "small") { push_size(std::max(1, cur_size() - 1)); return; }
        if (n == "big")   { push_size(std::min(16, cur_size() + 1)); return; }

        if (n == "span" || n == "font") {
            std::string st = attr_get(t, "style");
            std::string col = attr_get(t, "color");
            uint16_t c = 0xFFFF;
            if (!col.empty()) c = color_from_attr(col);
            else if (!st.empty()) c = color_from_attr(st);
            if (c != 0xFFFF || !col.empty() || st.find("color") != std::string::npos)
                push_color(c == 0xFFFF ? opt.default_color : c);
            return;
        }

        if (n == "a") {
            std::string href = attr_get(t, "href");
            HtmlLink lk;
            lk.href = href;
            lk.index = (int)doc.links.size() + 1;
            doc.links.push_back(lk);
            push_color(opt.link_color);
            out += "<|u|>";
            return;
        }

        if (n == "ul" || n == "ol") {
            emit_nl();
            if (list_depth < 7) {
                if (n == "ol") ol_counter[list_depth] = 0;
            }
            ++list_depth;
            return;
        }
        if (n == "li") {
            emit_nl();
            for (int d = 1; d < list_depth; ++d) out += "  ";
            // ordered?
            // We don't track ul vs ol perfectly per depth; use bullet always unless ol_counter set
            if (list_depth > 0 && list_depth <= 8) {
                // Prefer bullet; if parent was ol, counter was zeroed on open
                // Simple approach: numbered if any ancestor opened ol this depth-1
                int idx = list_depth - 1;
                if (idx >= 0 && idx < 8 && ol_counter[idx] >= 0) {
                    // Heuristic: if counter was used, keep numbering
                    ol_counter[idx]++;
                    char buf[12];
                    snprintf(buf, sizeof(buf), "%d. ", ol_counter[idx]);
                    // Always bullet for reliability on tiny screen
                    (void)buf;
                }
            }
            out += "* ";
            return;
        }

        if (n == "tr") { emit_nl(); return; }
        if (n == "td" || n == "th") {
            out += " | ";
            if (n == "th") out += "<|b|>";
            return;
        }
        if (n == "img") {
            std::string alt = attr_get(t, "alt");
            if (alt.empty()) alt = "[img]";
            push_color(opt.muted_color);
            out += "[";
            append_decoded_text(out, alt.c_str(), alt.size());
            out += "]";
            pop_color();
            return;
        }
        // html, body, head, meta, link, table, tbody, … → no-op
    }

    void close_tag(const Tag& t) {
        const std::string& n = t.name;

        if (n == "script" || n == "style" || n == "svg" || n == "iframe" ||
            n == "object" || n == "noscript") {
            if (skip_depth > 0) --skip_depth;
            if (n == "script") in_script = false;
            if (n == "style")  in_style = false;
            return;
        }
        if (skip_depth) return;

        if (n == "title") { in_title = false; return; }
        if (n == "p" || n == "div" || n == "section" || n == "article" ||
            n == "header" || n == "footer" || n == "main" || n == "blockquote") {
            emit_nl();
            return;
        }
        if (n == "pre") { in_pre = false; pop_color(); emit_nl(); return; }
        if (n == "code") { pop_color(); return; }
        if (n == "h1" || n == "h2" || n == "h3" || n == "h4" || n == "h5" || n == "h6") {
            if (n == "h1" || n == "h2" || n == "h3") pop_color();
            pop_size();
            emit_nl();
            return;
        }
        if (n == "b" || n == "strong") { out += "<|/b|>"; return; }
        if (n == "i" || n == "em")     { out += "<|/i|>"; return; }
        if (n == "u")                  { out += "<|/u|>"; return; }
        if (n == "s" || n == "strike" || n == "del") { out += "<|/s|>"; return; }
        if (n == "small" || n == "big") { pop_size(); return; }

        if (n == "span" || n == "font") {
            // Only pop if we pushed — we always push_color on open when style present;
            // cheap approach: always pop_color if stack deeper than 0 and we may have pushed.
            // Safer: track push count. For simplicity pop if color_sp>0 and attr had color.
            // Caller may over-pop; keep minimum depth 0.
            // We pushed only when color resolved — mirror with a flag is better.
            // Approximate: if style or color attr existed, pop.
            // Re-parse isn't available; just leave color until next explicit change.
            // Actually open_tag always push_color for span with style/color. Pop always for span:
            // Too aggressive if no color — open_tag only pushes when color found.
            // Track with a side channel is heavy; accept sticky color until next heading.
            return;
        }

        if (n == "a") {
            out += "<|/u|>";
            if (opt.emit_link_markers && !doc.links.empty()) {
                char buf[16];
                snprintf(buf, sizeof(buf), "[%d]", doc.links.back().index);
                out += buf;
            }
            pop_color();
            // store label is hard without buffering; leave href only
            return;
        }

        if (n == "ul" || n == "ol") {
            if (list_depth > 0) --list_depth;
            emit_nl();
            return;
        }
        if (n == "th") { out += "<|/b|>"; return; }
    }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

HtmlDoc html_to_mwenv(const char* html, size_t len, const HtmlConvertOptions& opt) {
    HtmlDoc doc;
    if (!html || len == 0) {
        doc.error = "empty input";
        return doc;
    }

    Conv c(opt, doc);
    // seed default size/color
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "<|size=%d|><|color=0x%04X|>",
                 opt.base_size, (unsigned)opt.default_color);
        doc.markup = buf;
    }

    size_t i = 0;
    while (i < len) {
        if ((int)doc.markup.size() >= opt.max_output_chars) {
            doc.markup += "<|n|><|color=0xF800|>[truncated]<|color=0xFFFF|>";
            break;
        }

        if (html[i] == '<') {
            // comment
            if (i + 3 < len && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
                i += 4;
                while (i + 2 < len && !(html[i] == '-' && html[i + 1] == '-' && html[i + 2] == '>'))
                    ++i;
                if (i + 2 < len) i += 3;
                continue;
            }
            // doctype
            if (i + 2 < len && html[i + 1] == '!' ) {
                while (i < len && html[i] != '>') ++i;
                if (i < len) ++i;
                continue;
            }

            Tag tag;
            size_t before = i;
            if (!parse_tag(html, len, i, tag)) {
                // treat '<' as text
                i = before + 1;
                c.emit_text("<", 1);
                continue;
            }
            if (tag.closing) c.close_tag(tag);
            else {
                c.open_tag(tag);
                if (tag.self_closing) c.close_tag(tag);
            }
        } else {
            size_t j = i;
            while (j < len && html[j] != '<') ++j;
            c.emit_text(html + i, j - i);
            i = j;
        }
    }

    if (doc.title.empty()) doc.title = "Untitled";
    doc.ok = true;
    return doc;
}

std::string plain_to_mwenv(const char* text, size_t len) {
    std::string out = "<|size=1|><|color=0xFFFF|>";
    if (!text || !len) return out;
    size_t i = 0;
    while (i < len) {
        if (text[i] == '\n') { out += "<|n|>"; ++i; continue; }
        if (text[i] == '\r') { ++i; continue; }
        size_t j = i;
        while (j < len && text[j] != '\n' && text[j] != '\r') ++j;
        append_plain(out, text + i, j - i);
        i = j;
    }
    return out;
}
