#pragma once
// ---------------------------------------------------------------------------
// html_to_mwenv – constrained HTML → MWenv rich-text markup
//
// Output dialect (what Window::tokenize understands):
//   <|n|>            line break
//   <|size=N|>       1..16
//   <|color=0xXXXX|> RGB565
//   <|u|> / <|/u|>   underline on/off   (also <|underline|>)
//   <|b|> / <|/b|>   bold
//   <|i|> / <|/i|>   italic
//   <|s|> / <|/s|>   strikethrough
//   <|pos=x,y|>      absolute local pos (rarely used by converter)
//
// Supported HTML subset (everything else is stripped or ignored):
//   Document: html, head, title, body, meta, link (ignored)
//   Blocks:   h1–h4, p, div, br, hr, pre, blockquote
//   Inline:   span, b/strong, i/em, u, s/strike, a, code, small, big
//   Lists:    ul, ol, li
//   Tables:   table/tr/td/th → crude row text (no grid)
//   Entities: &amp; &lt; &gt; &quot; &apos; &nbsp; &#NN; &#xHH;
//   Style:    color:#RGB / #RRGGBB / named (red,blue,…)  on style= or color=
//   Scripts/styles/iframes/svg/img → dropped (img alt text kept if present)
//
// Links are collected into HtmlDoc::links for the browser chrome to navigate.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

struct HtmlLink {
    std::string href;
    std::string label;
    // Character index into the produced markup is not stable; browsers
    // match by sequential index when the user activates "follow link N".
    int index = 0;
};

struct HtmlDoc {
    std::string title;
    std::string markup;           // ready for Window::SetText()
    std::vector<HtmlLink> links;
    bool ok = false;
    std::string error;
};

struct HtmlConvertOptions {
    uint16_t default_color = 0xFFFF;   // RGB565 white
    uint16_t link_color    = 0x5D7F;   // soft blue
    uint16_t code_color    = 0x07FF;   // cyan
    uint16_t muted_color   = 0xAD55;   // grey
    int base_size          = 1;        // body text size multiplier
    int max_output_chars   = 12000;    // hard cap for embedded RAM
    bool emit_link_markers = true;     // append [1] after link text
};

// Convert a full HTML string (or fragment) into MWenv markup.
HtmlDoc html_to_mwenv(const char* html, size_t len,
                      const HtmlConvertOptions& opt = {});

inline HtmlDoc html_to_mwenv(const std::string& html,
                             const HtmlConvertOptions& opt = {}) {
    return html_to_mwenv(html.c_str(), html.size(), opt);
}

// Convenience: plain text → escaped markup (no tags).
std::string plain_to_mwenv(const char* text, size_t len);
