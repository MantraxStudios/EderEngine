#pragma once
#include <string>
#include <vector>

namespace Krayon {

// ─────────────────────────────────────────────────────────────────────────────
//  PostProcessEffect
//  Describes one custom full-screen post-process effect.
//
//  fragShaderPath  : relative to the content root, e.g. "shaders/blur.frag.spv"
//  params          : up to 16 floats uploaded to the shader as
//                    layout(binding=2) uniform PPParams { vec4 p[4]; };
//  paramCount      : how many of the 16 floats the shader actually uses
//                    (editor shows this many sliders)
// ─────────────────────────────────────────────────────────────────────────────
struct PostProcessEffect
{
    std::string name           = "Effect";
    std::string fragShaderPath = "";      // e.g. "shaders/my_effect.frag.spv"
    bool        enabled        = true;
    float       params[16]     = {};
    int         paramCount     = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  PostProcessGraph
//  Ordered list of custom post-process effects applied after the built-in
//  passes (volumetric light/fog, sun shafts).
//  Stored and loaded as a [postprocess] block in the .scene file.
// ─────────────────────────────────────────────────────────────────────────────
struct PostProcessGraph
{
    std::vector<PostProcessEffect> effects;
};

} // namespace Krayon
