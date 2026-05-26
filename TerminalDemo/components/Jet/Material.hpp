#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <cstdint>
#include "Texture.hpp"
#include "Shader.hpp"

namespace Renderer {

/// @brief Per-triangle shading model selection.
enum class ShadingMode {
    FLAT,       ///< Single colour per triangle.
    GOURAUD,    ///< Per-vertex lit, interpolated across the face.
    PHONG,      ///< Per-pixel lit (more expensive).
    WIREFRAME,  ///< Edges only, no fill.
};

/// @brief Surface description: base colour, optional texture/shader and lighting parameters.
class Material {
public:
    uint16_t color;             ///< Flat-shaded base colour (RGB565).
    Texture* diffuseMap;        ///< Optional diffuse texture; nullptr to use `color`.
    Shader* shader;             ///< Optional custom shader; nullptr to use the built-in path.
    bool emissive;              ///< When true, lighting is bypassed and the surface uses its raw colour.
    uint8_t alpha;              ///< Per-material alpha (0=invisible, 255=opaque).
    uint8_t diffuse;            ///< Diffuse reflectance coefficient (0..255).
    uint8_t specular;           ///< Specular reflectance coefficient (0..255).
    ShadingMode shadingMode = ShadingMode::FLAT; ///< Shading model.
    char* name;                 ///< Optional name; used for `usemtl` matching in OBJ loading.

    /// @brief Construct a material.
    /// @param color Base colour in RGB565.
    /// @param diffuseMap Optional diffuse texture.
    /// @param shader Optional custom shader.
    /// @param emissive Bypass lighting if true.
    /// @param alpha Per-material alpha (0..255).
    /// @param diffuse Diffuse coefficient (0..255).
    /// @param specular Specular coefficient (0..255).
    Material(uint16_t color = 0xFFFF, Texture* diffuseMap = nullptr, Shader* shader = nullptr, bool emissive = false, uint8_t alpha = 255, uint8_t diffuse = 255, uint8_t specular = 0);

    /// @brief Convenience constructor for an untextured material with explicit alpha.
    /// @param color Base colour in RGB565.
    /// @param alpha Per-material alpha (0..255).
    Material(uint16_t color, uint8_t alpha);

    /// @brief Sample the material colour at a UV coordinate.
    /// @param u Fixed-point U.
    /// @param v Fixed-point V.
    /// @return Sampled RGB565 colour, or `color` if no diffuse map is bound.
    uint16_t getColor(int u, int v);

    /// @brief Sample the material colour at a UV coordinate.
    /// @param uv UV pair in fixed point.
    /// @return Sampled RGB565 colour, or `color` if no diffuse map is bound.
    uint16_t getColor(Vector2 uv);
};

} // namespace Renderer

#endif // MATERIAL_HPP
