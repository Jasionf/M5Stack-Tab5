#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <cstdint>
#include "Object.hpp"
#include "Material.hpp"
#include "Light.hpp"
#include "Camera.hpp"
#include "Config.hpp"
#include "Picking.hpp"

/// @def ZBUFFER_STRIDE(w)
/// @brief Z-buffer row stride in pixels for a framebuffer of width @p w.
///
/// The depth buffer is stored at half horizontal resolution only when
/// HALF_WIDTH_BUFFERS is enabled (one z-cell per two output pixels);
/// otherwise it is per-pixel. Frontends should size their z-buffer
/// allocation as `ZBUFFER_STRIDE(w) * h * sizeof(uint16_t)`.
#if HALF_WIDTH_BUFFERS
#define ZBUFFER_STRIDE(w) ((w) / 2)
#else
#define ZBUFFER_STRIDE(w) (w)
#endif

namespace Renderer
{
/// @brief Low-level triangle rasteriser owning a colour and depth buffer.
///
/// Scene drives this class on every render(). External users normally
/// access it through `Scene::getRenderer()` rather than constructing one
/// directly.
class Rasterizer
{
    private:
        uint16_t *framebuffer;
        int screenWidth;
        int screenHeight;
        uint16_t *zBuffer;
        int lastRandom = 0;

    public:
        /// @brief Construct a rasteriser bound to caller-owned buffers.
        /// @param framebuffer RGB565 colour buffer of size screenWidth*screenHeight.
        /// @param screenWidth Width in pixels.
        /// @param screenHeight Height in pixels.
        /// @param zBuffer Depth buffer of size ZBUFFER_STRIDE(screenWidth)*screenHeight, or nullptr if Z_BUFFERING is disabled.
        /// @param camera Optional Camera; can be set later.
        Rasterizer(uint16_t *framebuffer, int screenWidth, int screenHeight, uint16_t *zBuffer, Camera *camera = nullptr)
            : framebuffer(framebuffer), screenWidth(screenWidth), screenHeight(screenHeight), zBuffer(zBuffer), camera(camera) {}

        Camera *camera;                 ///< Camera supplying view/projection state.
        bool interlacedMode = false;    ///< When true, only every other row is drawn each frame.
        bool checkerboardMode = false;  ///< When true, alternates between two complementary checkerboard pixel patterns each frame. Requires double-buffering in the frontend for the reconstruction pass.
        int randomSeed = 255;           ///< Seed for the screen-door / dither random source.

        /// @name Distance-based texture LOD
        /// @brief Beyond `textureLodFar`, textured triangles drop their texture
        /// and render as flat `material->color`, taking the fast simple-span
        /// fill path for free. Between `textureLodNear` and `textureLodFar`
        /// the sampled texel is cross-faded toward the flat colour, using
        /// screen-door stipple under `SCREEN_DOOR_ALPHA` and a per-pixel
        /// alpha blend otherwise. Disabled by default; the host (Scene)
        /// configures it once with whatever distances suit the scene.
        /// Has no effect when `TEXTURE_MAPPING` is compiled out.
        /// @{
        bool textureLodEnabled = false; ///< Master enable for the texture LOD fade.
        int32_t textureLodNear = 0;     ///< Z below which textures render at full detail.
        int32_t textureLodFar  = 0;     ///< Z at and beyond which textures are dropped entirely.
        /// @}

#if MAX_PICK_QUERIES > 0
        /// Pointers to host-owned pick query/result arrays. Storage is owned
        /// by Scene; the rasterizer just reads queries and writes results.
        const PickQuery* pickQueries = nullptr;
        PickResult*      pickResults = nullptr;
        int              pickQueryCount = 0;     ///< Number of slots in use this frame (0..MAX_PICK_QUERIES).
        Object*          currentPickObject = nullptr;       ///< Set by Scene before each drawTriangle for hit attribution.
        int32_t          currentPickTriangleIndex = -1;     ///< Source-mesh triangle index for hit attribution.
#endif

        /// @brief Test whether a pixel at (x, y) should be drawn given a screen-door alpha.
        /// @param x Pixel X.
        /// @param y Pixel Y.
        /// @param alpha Effective alpha (0..255).
        /// @return True if the pixel passes the dither / parity test.
        bool shouldDrawPixel(int x, int y, uint8_t alpha);

        /// @brief Rasterise a single triangle.
        /// @param v1 First vertex.
        /// @param v2 Second vertex.
        /// @param v3 Third vertex.
        /// @param material Material applied to the triangle.
        /// @param directionalLight Active directional light (may be nullptr).
        /// @param ambientLight Active ambient light (may be nullptr).
        /// @param renderEvenLines Used in interlaced mode to select which row parity to draw.
        /// @param ignoreZBuffer Skip the depth test when true.
        /// @param noWriteZBuffer Skip the depth write when true.
        /// @param zBias Per-triangle depth bias in z-buffer units.
        /// @param objAlpha Per-object alpha multiplier (255 = no fade).
        /// @return True if the triangle produced any rasterizer work.
        bool drawTriangle(const Object::Vertex &v1, const Object::Vertex &v2, const Object::Vertex &v3, Material *material, DirectionalLight *directionalLight, AmbientLight *ambientLight, bool renderEvenLines, bool ignoreZBuffer, bool noWriteZBuffer, int zBias, uint8_t objAlpha = 255);

        /// @brief Map an 8-bit grayscale value to RGB565.
        /// @param grayscale 8-bit luminance.
        /// @return RGB565 colour.
        uint16_t grayscaleToRGB565(uint8_t grayscale);

        /// @brief Replace just the colour buffer pointer.
        /// @param newBuffer New caller-owned RGB565 buffer.
        void setFramebuffer(uint16_t *newBuffer) { framebuffer = newBuffer; }

        /// @brief Hot-swap framebuffer, z-buffer and dimensions (e.g. on window resize).
        /// @param newFramebuffer New caller-owned colour buffer.
        /// @param newZBuffer New caller-owned depth buffer.
        /// @param newWidth New width in pixels.
        /// @param newHeight New height in pixels.
        void resize(uint16_t* newFramebuffer, uint16_t* newZBuffer,
                    int newWidth, int newHeight) {
            framebuffer  = newFramebuffer;
            zBuffer      = newZBuffer;
            screenWidth  = newWidth;
            screenHeight = newHeight;
        }
    };
} // namespace Renderer

#endif // RENDERER_HPP