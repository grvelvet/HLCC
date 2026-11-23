#pragma once

/**
 * @file overlay_render.hpp
 * @brief OpenGL 1.x overlay renderer for subtitles and localized chapter titles.
 *
 * Pure render layer: reads core::CaptionState and draws via glfont::GlApi / FontAtlas,
 * with zero dependencies on GoldSrc engine headers or hooks.
 */

#include <cstdint>

#include "core/caption_state.hpp"
#include "render/gl_font.hpp"

namespace render {

class OverlayRenderer {
public:
    void Render(glfont::GlApi& gl, core::CaptionState& state, int screenW, int screenH);
    void InvalidateFonts() noexcept;

private:
    void DrawActiveTitles(glfont::GlApi& gl, core::CaptionState& state, int screenW, int screenH, int fontH);
    static void DrawRoundedRect(glfont::GlApi& gl, int x0, int y0, int x1, int y1, int radius,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    glfont::FontAtlas atlas_;
    glfont::FontAtlas voxAtlas_;
    glfont::FontAtlas speakerAtlas_;
    glfont::FontAtlas titleAtlas_;
};

} // namespace render
