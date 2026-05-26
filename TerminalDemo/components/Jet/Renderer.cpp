#include "Renderer.hpp"
#include <algorithm>
#include <cmath>
#include "TrigLUT.hpp"
#include "FastMath.hpp"

#if defined(CHECKERBOARD_MODE) && CHECKERBOARD_MODE && defined(FIELD_BUFFERS) && FIELD_BUFFERS
#error "CHECKERBOARD_MODE and FIELD_BUFFERS (interlaced) cannot both be enabled. Each would render only one quarter of pixels per frame and interact destructively. Pick one."
#endif

// JET_FAST_SIMPLE_SPANS: private to this TU. When the rasterizer is
// configured without any per-pixel machinery (z-buffer, lighting,
// brightness, debug overdraw, tile tagging, perspective textures) the inner
// pixel loop degenerates to "maybe write a constant color, gated by a
// triangle-constant dither mask". The scanline-range solver above the inner
// loop already computes the exact [xStart, xEnd] range where the triangle
// is inside, so the per-pixel edge-accumulate/test is pure dead weight.
// Under SCREEN_DOOR_ALPHA the dither threshold reduces to two per-row
// booleans (HALF_WIDTH only steps through x%4 ∈ {0, 2}). That unlocks a
// tight fill loop that the compiler can unroll and the CPU can pipeline.
// Automatic - nothing to turn on in Config.hpp; this simply kicks in when
// none of the heavy features are enabled. Non-qualifying builds fall
// through to the full general-purpose inner loop unchanged.
//
// NOTE: TEXTURE_MAPPING is intentionally NOT in this set. When texture
// mapping is compiled in, untextured triangles still take the fast path
// (decided per-triangle via the runtime `useFastSimpleSpan` flag); only
// triangles that actually carry a diffuse map fall through to the
// general per-pixel loop. This means enabling TEXTURE_MAPPING globally
// no longer penalises scenes that mostly draw flat-shaded geometry.
#define JET_FAST_SIMPLE_SPANS                                             \
    (HALF_WIDTH_BUFFERS && !Z_BUFFERING &&                                   \
     !LIGHTING && !Z_BRIGHTNESS && !DEBUG_OVERDRAW &&                        \
     !RENDER_TILE_BUFFER && FAST_Z && !PERSPECTIVE_CORRECT_TEXTURES)

// Standard "over" alpha blend in RGB565. Used when SCREEN_DOOR_ALPHA is
// disabled — the renderer still has to honour material->alpha and the
// per-object fade, just by lerping channels into the framebuffer instead of
// stippling. Uses (256-a) and >>8 (vs (255-a)/255) for a single shift per
// channel; the off-by-one bias is invisible at 5/6-bit channel widths.
static inline uint16_t blendRGB565(uint16_t dst, uint16_t src, uint8_t alpha)
{
    const uint32_t a   = alpha;
    const uint32_t inv = 256u - a;
    const uint32_t sr = (src >> 11) & 0x1F;
    const uint32_t sg = (src >> 5)  & 0x3F;
    const uint32_t sb =  src        & 0x1F;
    const uint32_t dr = (dst >> 11) & 0x1F;
    const uint32_t dg = (dst >> 5)  & 0x3F;
    const uint32_t db =  dst        & 0x1F;
    const uint32_t r = (sr * a + dr * inv) >> 8;
    const uint32_t g = (sg * a + dg * inv) >> 8;
    const uint32_t b = (sb * a + db * inv) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

namespace Renderer
{    
    inline bool Rasterizer::shouldDrawPixel(int x, int y, uint8_t alpha)
    {
        #if NOISE_ALPHA
            //Generate a random number between 0 and 255 using a simple LCG
            uint8_t random = (251 * lastRandom + randomSeed);
            lastRandom = random;
            //If the random number is greater than the alpha value, don't draw the pixel
            return (random & 0xFF) <= alpha;
        #endif
        if (alpha > 240)
        {
            return true; // Draw all pixels
        }
        
        // Precomputed threshold matrix flattened into a 1D array for faster access
        constexpr uint8_t thresholdMatrix[16] = {
            15, 135, 45, 165,
            195, 75, 225, 105,
            60, 180, 30, 150,
            240, 120, 210, 90};

        // Determine the position within the threshold matrix
        uint8_t threshold = thresholdMatrix[(x & 3) | ((y & 3) << 2)];

        // Compare alpha to the threshold to determine if the pixel should be drawn
        return alpha >= threshold;
    }

    uint16_t Rasterizer::grayscaleToRGB565(uint8_t grayscale)
    {
        // Convert the 8-bit grayscale value to 5-bit (for R and B) and 6-bit (for G).
        uint8_t r = grayscale >> 3; // Convert 8-bit to 5-bit
        uint8_t g = grayscale >> 2; // Convert 8-bit to 6-bit
        uint8_t b = grayscale >> 3; // Convert 8-bit to 5-bit

        // Pack RGB into RGB565 format
        uint16_t rgb565 = (r << 11) | (g << 5) | b;

        return rgb565;
    }

    bool PERF_CRITICAL Rasterizer::drawTriangle(
        const Object::Vertex &v1,
        const Object::Vertex &v2,
        const Object::Vertex &v3,
        Material *material,
        DirectionalLight *directionalLight,
        AmbientLight *ambientLight,
        bool renderEvenLines,
        bool ignoreZBuffer,
        bool noWriteZBuffer,
        int zBias,
        uint8_t objAlpha)
    {
        // Fold per-object fade alpha into the material alpha up front so
        // every downstream alpha decision (early-out, depth fog, stipple
        // under SCREEN_DOOR_ALPHA, traditional blend without it) sees the
        // combined value. (objAlpha defaults to 255 for objects that don't
        // use the per-object distance fade.)
        uint8_t alpha = (objAlpha == 255)
                            ? material->alpha
                            : (uint8_t)(((uint16_t)material->alpha * objAlpha) / 255);

        if (alpha == 0) // Don't render fully-transparent triangles
        {
            return false;
        }

#if TEXTURE_MAPPING
        Texture *diffuseMap = material->diffuseMap;
#endif
#if LIGHTING
        bool emissive = material->emissive;
#endif

#if JET_FAST_SIMPLE_SPANS
    #if TEXTURE_MAPPING
        // Per-triangle decision: untextured triangles take the fast simple
        // span path even in builds with TEXTURE_MAPPING enabled. Only
        // triangles that actually have a diffuse map pay for the general
        // per-pixel loop. Non-const because the texture-LOD pass below may
        // promote a fully-flat-LOD textured triangle back onto the fast
        // path (texture is dropped entirely past textureLodFar).
        bool useFastSimpleSpan = (diffuseMap == nullptr);
    #else
        // No textures compiled in the fast path is unconditional and
        // this constexpr lets the optimiser fold the runtime branch out.
        constexpr bool useFastSimpleSpan = true;
    #endif
#endif
        int32_t nearPlane = camera->nearPlane;
        int32_t farPlane = camera->farPlane;

        // Compute bounding box of the triangle
        int32_t minX = std::min({v1.position.x, v2.position.x, v3.position.x}) & ~1;
        int32_t maxX = std::max({v1.position.x, v2.position.x, v3.position.x}) & ~1;
        int32_t minY = std::min({v1.position.y, v2.position.y, v3.position.y}) & ~1;
        int32_t maxY = std::max({v1.position.y, v2.position.y, v3.position.y}) & ~1;

        // If the triangle has no area, skip it
        #if SKIP_ZERO_AREA_TRIANGLES
        if (minX == maxX || minY == maxY)
        {
            return false;
        }
        #endif

#if FAST_Z
#if LAZY_Z
        int32_t z = std::max({v1.position.z, v2.position.z, v3.position.z});
#else
        int32_t z = (v1.position.z + v2.position.z + v3.position.z) / 3;
#endif
        if (z < nearPlane || z > farPlane)
        {
            return false;
        }
        #if Z_BUFFERING
        // Z buffer stores raw int32_t z, narrowed to uint16_t. The project's
        // farPlane (~4096) fits comfortably; we clamp to UINT16_MAX so any
        // future absurdly-far geometry saturates rather than wrapping.
        // Per-object zBias pulls the surface toward the camera in real
        // depth units (no longer divided by 4) so a small bias really is
        // a small distance — 1 unit of bias = 1 unit of world depth.
        int32_t zbRaw = z - zBias;
        if (zbRaw < 0)     zbRaw = 0;
        if (zbRaw > 65535) zbRaw = 65535;
        uint16_t zb = (uint16_t)zbRaw;
        #endif
#endif

        // Clamp to screen bounds
        minX = std::max(minX, static_cast<int32_t>(0));
        maxX = std::min(maxX, static_cast<int32_t>(screenWidth - 1));
        minY = std::max(minY, static_cast<int32_t>(0));
        maxY = std::min(maxY, static_cast<int32_t>(screenHeight - 1));

#if LIGHTING
        const int32_t scaleFactor = FIXED_POINT_SCALE / 64;
        Vector3 normal = {0, 0, 0};
        uint16_t vertexBrightness[3] = {0, 0, 0};

        if (directionalLight)
        {
            if (material->shadingMode == ShadingMode::FLAT)
            {
                // Use the AVERAGE of the three transformed vertex normals as
                // the face normal. v.normal has been rotated by the object
                // and the camera matrices upstream, so it lives in the same
                // camera-space coord system as directionalLight->lightDir.
                //
                // The previous code did a cross product on v1/v2/v3.position
                // deltas — but at this point position.x/y are screen-space
                // (already projected) while position.z is camera-space depth.
                // That mixed-space cross product yields a garbage normal
                // whose direction whips around as the camera pans a pixel,
                // which is what was making every triangle in a lit scene
                // flicker violently between brightnesses every frame.
                normal.assign(v1.normal.x + v2.normal.x + v3.normal.x,
                              v1.normal.y + v2.normal.y + v3.normal.y,
                              v1.normal.z + v2.normal.z + v3.normal.z);
                auto normalLength = normal.length();
                if (normalLength > 0)
                {
                    normal = (normal * static_cast<int32_t>(1024)) / normalLength;
                }
            }
            else if (material->shadingMode == ShadingMode::GOURAUD)
            {
                // Calculate brightness for each vertex
                for (int i = 0; i < 3; i++)
                {
                    const Object::Vertex &v = (i == 0) ? v1 : ((i == 1) ? v2 : v3);
                    int64_t dotProduct = Vector3::dotProduct(v.normal, directionalLight->lightDir) / scaleFactor;
                    dotProduct = std::max(dotProduct, static_cast<int64_t>(-512));
                    int64_t adjustedBrightness = (dotProduct + 255) / 2;
                    vertexBrightness[i] = static_cast<uint16_t>((adjustedBrightness * material->diffuse) / FIXED_POINT_SCALE);
                    if (ambientLight)
                        vertexBrightness[i] += ambientLight->color.r;
                    // Cap vertex brightness based on specular value
                    uint16_t maxBrightness = 255 + ((material->specular * 256) / 255);
                    vertexBrightness[i] = std::min(vertexBrightness[i], maxBrightness);
                }
            }
        }
#endif

        uint16_t color = material->getColor({0, 0});

#if TEXTURE_MAPPING
        // Distance-based texture LOD. textureLodFade goes from 255 (full
        // texture) at textureLodNear down to 0 (fully flat) at
        // textureLodFar. Past textureLodFar we drop the texture entirely:
        // the local `color` is overwritten with material->color and the
        // triangle is promoted onto the fast simple-span path (in builds
        // where it's compiled in), which is the actual perf win - distant
        // scenery falls out of the per-pixel textured loop and into the
        // paired-32-bit fill loop. The fade band still pays the textured
        // loop cost, but composites the texel toward material->color
        // using whichever blend method the build is configured with
        // (stipple under SCREEN_DOOR_ALPHA, alpha lerp otherwise).
        // Triangle-constant: lodZ is taken from the same source as the
        // FAST_Z depth (or the triangle average when FAST_Z is off).
        uint8_t textureLodFade = 255;
        if (textureLodEnabled && diffuseMap && textureLodFar > textureLodNear)
        {
        #if FAST_Z
            const int32_t lodZ = z;
        #else
            const int32_t lodZ = (v1.position.z + v2.position.z + v3.position.z) / 3;
        #endif
            if (lodZ >= textureLodFar)
            {
                textureLodFade = 0;
                color = material->color; // Flat fallback for the fast path.
            #if JET_FAST_SIMPLE_SPANS
                useFastSimpleSpan = true;
            #endif
            }
            else if (lodZ > textureLodNear)
            {
                textureLodFade = (uint8_t)(255 - ((lodZ - textureLodNear) * 255)
                                                  / (textureLodFar - textureLodNear));
            }
        }
#endif

#if TEXTURE_MAPPING || !FAST_Z || LIGHTING
        // Edge-function denominator. Computed in int64 (vertex projected
        // x/y near the camera can blow well past int32 range), then
        // narrowed to int32 for the per-pixel divides. Previously this
        // path had an `if (denom64 > INT32_MAX) return false;` bail-out
        // which silently dropped near-camera triangles at high resolutions
        // / large WORLD_SCALE (triangles popping out of existence as you
        // closed in). We now accept the truncation: the triangle is huge
        // and near-camera so any per-pixel interpolation error is bounded
        // and far less objectionable than the triangle disappearing.
        int64_t denom64 = (int64_t)(v2.position.y - v3.position.y) * (v1.position.x - v3.position.x) +
                          (int64_t)(v3.position.x - v2.position.x) * (v1.position.y - v3.position.y);

        if (denom64 == 0)
            return false; // Degenerate triangle
        int32_t denom = (int32_t)denom64;
        if (denom == 0)
            return false; // Truncation landed on zero; treat as degenerate

        Vector2 uv = {0, 0};
#endif

#if LIGHTING || Z_BRIGHTNESS
        uint16_t brightness = 0;
#endif

#if LIGHTING
        if (!directionalLight && !ambientLight)
        {
            brightness = 255;
        }
        else if (material->shadingMode == ShadingMode::FLAT)
        {
            if (directionalLight)
            {
                const Vector3 &lightDir = directionalLight->lightDir;

                // Simple dot product calculation
                int64_t dotProduct = Vector3::dotProduct(normal, lightDir) / scaleFactor;

                // Map from [-512, 512] to [0, 255] range
                brightness = ((dotProduct + 512) * 255) / 1024;

                // Apply material diffuse property
                brightness = (brightness * material->diffuse) / FIXED_POINT_SCALE;
            }

            if (ambientLight)
            {
                // Add ambient light contribution
                brightness += ambientLight->color.r; // Example value; adjust as needed
            }

            // Cap maximum brightness based on specular value
            uint16_t maxBrightness = 255 + ((material->specular * 256) / 255);
            brightness = std::min(brightness, maxBrightness);
        }
#endif

#if Z_BRIGHTNESS && FAST_Z
        brightness = 255 - ((z - zBrightNear) * zBrightScale) / (zBrightFar - zBrightNear);
        brightness = std::min(brightness, static_cast<uint16_t>(255));
#endif

#if LIGHTING
        // Per-triangle hoist of the channel modulation. For FLAT-shaded
        // (or unlit) materials with no diffuse texture, both `color` and
        // `brightness` are triangle-constants, so the per-pixel multiply +
        // shift dance below is pure repeated work. Pre-modulating once
        // here and skipping the per-pixel block recovers the bulk of the
        // perf cost of enabling LIGHTING.
        bool flatColorPrecomputed = false;
        const bool flatShaded =
            (!directionalLight && !ambientLight) ||
            material->shadingMode == ShadingMode::FLAT;
        const bool diffuseMapSamples =
#if TEXTURE_MAPPING
            (material->diffuseMap != nullptr);
#else
            false;
#endif
        if (flatShaded && !emissive && !diffuseMapSamples)
        {
            if (brightness >= 255)
            {
                const uint16_t blowout = brightness - 255;
                uint8_t r = ((color >> 11) & 0x1F);
                uint8_t g = ((color >>  5) & 0x3F);
                uint8_t b = ( color        & 0x1F);
                r = (uint8_t)(r + ((31 - r) * blowout) / 256);
                g = (uint8_t)(g + ((63 - g) * blowout) / 256);
                b = (uint8_t)(b + ((31 - b) * blowout) / 256);
                color = (uint16_t)((r << 11) | (g << 5) | b);
            }
            else
            {
                const uint16_t br = ((color >> 11) & 0x1F) * brightness;
                const uint16_t bg = ((color >>  5) & 0x3F) * brightness;
                const uint16_t bb = ( color        & 0x1F) * brightness;
                const uint8_t r = (uint8_t)((br + 128 + (br >> 8)) >> 8);
                const uint8_t g = (uint8_t)((bg + 128 + (bg >> 8)) >> 8);
                const uint8_t b = (uint8_t)((bb + 128 + (bb >> 8)) >> 8);
                color = (uint16_t)((r << 11) | (g << 5) | b);
            }
            flatColorPrecomputed = true;
        }
#endif

#if DEPTH_ALPHA_BLEND && FAST_Z
        // Triangle-level depth fog. With FAST_Z the z used here is constant
        // for the whole triangle (it's the triangle average or max), so
        // there is no point re-evaluating this per pixel the way the older
        // path did. We hoist the computation out; the per-pixel version
        // below remains only for the !FAST_Z path where z actually varies.
        // Compose with the existing alpha (material*objAlpha) by taking
        // the minimum, so per-object distance fades and translucent
        // materials are preserved through fog.
        {
            if (z >= depthFogFar) {
                alpha = 0;
            } else if (z > depthFogNear) {
                uint8_t fogA = (uint8_t)(255 - ((z - depthFogNear) * 255) / (depthFogFar - depthFogNear));
                if (fogA < alpha) alpha = fogA;
            }
            if (alpha == 0) return false; // Fully-transparent triangle: nothing to draw.
        }
#endif

#if PERSPECTIVE_CORRECT_TEXTURES
        int32_t oneOverZ1 = (FIXED_POINT_SCALE * FIXED_POINT_SCALE) / v1.position.z;
        int32_t oneOverZ2 = (FIXED_POINT_SCALE * FIXED_POINT_SCALE) / v2.position.z;
        int32_t oneOverZ3 = (FIXED_POINT_SCALE * FIXED_POINT_SCALE) / v3.position.z;

        // Precompute u_over_z and v_over_z at each vertex
        int32_t uOverZ1 = (v1.uv.x * oneOverZ1) / FIXED_POINT_SCALE;
        int32_t vOverZ1 = (v1.uv.y * oneOverZ1) / FIXED_POINT_SCALE;
        int32_t uOverZ2 = (v2.uv.x * oneOverZ2) / FIXED_POINT_SCALE;
        int32_t vOverZ2 = (v2.uv.y * oneOverZ2) / FIXED_POINT_SCALE;
        int32_t uOverZ3 = (v3.uv.x * oneOverZ3) / FIXED_POINT_SCALE;
        int32_t vOverZ3 = (v3.uv.y * oneOverZ3) / FIXED_POINT_SCALE;
#endif

        const int inc = interlacedMode ? 2 : 1;

        // Checkerboard: precompute which (x+y) parity to render this frame.
        // renderEvenLines==true  (even frame) → render where (x+y) is even (parity 0).
        // renderEvenLines==false (odd  frame) → render where (x+y) is odd  (parity 1).
        // Only meaningful when checkerboardMode is true; otherwise unused.
        const int cbFrameParity = renderEvenLines ? 0 : 1;

        // -- Incremental edge function setup --
        // The edge function w_i(x, y) is linear in x and y, so rather than
        // recomputing it from scratch per pixel (which would need int64 to
        // stay safe against huge projected coords near the near plane, and
        // is slow on 32-bit targets), we:
        //   1. Precompute the partial derivatives dw_i/dx and dw_i/dy.
        //      These are just differences of projected vertex coords and
        //      comfortably fit in int32 even when the coords themselves
        //      are huge.
        //   2. Compute the w values at the first pixel of each row once
        //      in int64 (per-triangle setup cost; a few multiplies).
        //   3. In the hot inner loop, just ADD the int32 delta. Running
        //      accumulator is int64 so overflow is impossible, but the
        //      per-pixel work is a few 64-bit adds — much cheaper than
        //      either int64 multiplies or risking int32 overflow.
        const int32_t dw0_dx = v2.position.y - v3.position.y;
        const int32_t dw1_dx = v3.position.y - v1.position.y;
        const int32_t dw2_dx = v1.position.y - v2.position.y;
        const int32_t dw0_dy = v3.position.x - v2.position.x;
        const int32_t dw1_dy = v1.position.x - v3.position.x;
        const int32_t dw2_dy = v2.position.x - v1.position.x;

#if HALF_WIDTH_BUFFERS
        constexpr int xStep = 2;
#else
        constexpr int xStep = 1;
#endif
        const int32_t dw0_dx_step = dw0_dx * xStep;
        const int32_t dw1_dx_step = dw1_dx * xStep;
        const int32_t dw2_dx_step = dw2_dx * xStep;
        const int32_t dw0_dy_step = dw0_dy * inc;
        const int32_t dw1_dy_step = dw1_dy * inc;
        const int32_t dw2_dy_step = dw2_dy * inc;

#if LIGHTING
        // Plane-equation incremental Gouraud brightness. Brightness is
        // linear in (x,y) across the triangle, so the per-pixel and per-row
        // step values are constants. Replacing the original per-pixel
        // (b0*w0 + b1*w1 + b2*w2) / denom with a single int32 add per pixel
        // is roughly 30x cheaper on Xtensa (no divide, no multi-multiply).
        // brightness is tracked in Q16 fixed-point so per-pixel deltas
        // smaller than 1 don't quantise to zero across a wide row.
        int32_t brightness_dx_step_q16 = 0;
        const bool useIncrementalGouraud =
            directionalLight && material->shadingMode == ShadingMode::GOURAUD;
        if (useIncrementalGouraud)
        {
            const int32_t bDx = vertexBrightness[0] * dw0_dx_step
                              + vertexBrightness[1] * dw1_dx_step
                              + vertexBrightness[2] * dw2_dx_step;
            brightness_dx_step_q16 = (int32_t)(((int64_t)bDx << 16) / denom);
        }
#endif

        // Interlaced mode uses renderEvenLines as a row-start offset; checkerboard
        // mode renders every row (the per-pixel column skip happens inside the x-loop).
        const int yStart = interlacedMode ? (minY + (int)renderEvenLines) : minY;
        int64_t w0_row = (int64_t)dw0_dx * (minX - v3.position.x)
                       + (int64_t)dw0_dy * (yStart - v3.position.y);
        int64_t w1_row = (int64_t)dw1_dx * (minX - v1.position.x)
                       + (int64_t)dw1_dy * (yStart - v1.position.y);
        int64_t w2_row = (int64_t)dw2_dx * (minX - v2.position.x)
                       + (int64_t)dw2_dy * (yStart - v2.position.y);

        // Max number of xStep-sized pixel slots in this row.
        const int64_t iMaxRow = (maxX - minX) / xStep;

        for (int y = yStart; y <= maxY;
             y += inc, w0_row += dw0_dy_step, w1_row += dw1_dy_step, w2_row += dw2_dy_step)
        {
            // --- Scanline range: find first and last x inside the triangle.
            //
            // The edge function is linear in x with known int32 slope per
            // xStep, so we can solve for the x-range where all three
            // edges are non-negative without iterating pixel-by-pixel.
            //
            // For each edge with value ew_j at x=minX and slope d_j per
            // xStep:
            //   d_j > 0 : ew_j + i*d_j >= 0 iff i >= ceil(-ew_j / d_j)
            //   d_j < 0 : ew_j + i*d_j >= 0 iff i <= floor(ew_j / -d_j)
            //   d_j == 0: ew_j < 0 kills the whole row; else no constraint
            int64_t iStart = 0;
            int64_t iEnd   = iMaxRow;
            bool skipRow = false;

            #define JET_EDGE_RANGE(EW, D)                                           \
                do {                                                                  \
                    if ((D) > 0) {                                                    \
                        if ((EW) < 0) {                                               \
                            /* ceil(-EW / D) for positive denominator D. */            \
                            int64_t num = -(EW);                                      \
                            int64_t req = (num + (D) - 1) / (D);                      \
                            if (req > iStart) iStart = req;                           \
                        }                                                             \
                    } else if ((D) < 0) {                                             \
                        if ((EW) < 0) { skipRow = true; }                             \
                        else {                                                        \
                            /* floor(EW / -D) for positive divisor (-D). */           \
                            int64_t req = (EW) / -(int64_t)(D);                       \
                            if (req < iEnd) iEnd = req;                               \
                        }                                                             \
                    } else { /* D == 0 */                                             \
                        if ((EW) < 0) skipRow = true;                                 \
                    }                                                                 \
                } while (0)

            JET_EDGE_RANGE(w0_row, dw0_dx_step);
            if (!skipRow) JET_EDGE_RANGE(w1_row, dw1_dx_step);
            if (!skipRow) JET_EDGE_RANGE(w2_row, dw2_dx_step);
            #undef JET_EDGE_RANGE

            if (skipRow || iStart > iEnd) continue;

            // Advance ew accumulators to the first inside x.
            int64_t ew0 = w0_row + (int64_t)dw0_dx_step * iStart;
            int64_t ew1 = w1_row + (int64_t)dw1_dx_step * iStart;
            int64_t ew2 = w2_row + (int64_t)dw2_dx_step * iStart;

            const int xStart = minX + (int)iStart * xStep;
            const int xEnd   = minX + (int)iEnd   * xStep;

#if MAX_PICK_QUERIES > 0
            // Per-row screen-space pick test. Cheap (MAX_PICK_QUERIES is
            // tiny and compile-time bounded) and sees every triangle that
            // covers the queried pixel regardless of which fast/slow
            // span path the rasterizer takes below. We don't try to
            // emulate per-pixel screen-door stipple or texture-key holes
            // picking semantics here are "which surface intersects
            // this screen-space ray", which is what the host actually
            // wants for mouse-over / cursor selection. Z arbitration is
            // strictly closer-wins so the result matches the visible
            // painter-sorted / Z-buffered scene.
            if (pickQueries && pickResults && pickQueryCount > 0)
            {
                // Triangle-effective Z. With FAST_Z this is already
                // the constant `z` set up at the top of drawTriangle;
                // without FAST_Z we don't have per-pixel Z here yet (it
                // would need a barycentric eval per query) so fall back
                // to the triangle average - still good enough for
                // ordering since hit triangles are typically small.
                int32_t pickZ;
            #if FAST_Z
                pickZ = z;
            #else
                pickZ = (v1.position.z + v2.position.z + v3.position.z) / 3;
            #endif
                for (int p = 0; p < pickQueryCount; ++p)
                {
                    const PickQuery& q = pickQueries[p];
                    if (q.x < 0 || q.y < 0) continue;          // disabled slot
                    if (q.y != y) continue;                    // wrong row
                    // HALF_WIDTH_BUFFERS rasterises in 2-pixel xStep; snap
                    // the query x to the same 2-pixel grid before the
                    // range check so an odd query x still hits the cell
                    // the rasterizer actually wrote.
                #if HALF_WIDTH_BUFFERS
                    const int qx = q.x & ~1;
                #else
                    const int qx = q.x;
                #endif
                    if (qx < xStart || qx > xEnd) continue;
                    PickResult& r = pickResults[p];
                    if (!r.hit || pickZ < r.depth)
                    {
                        r.hit           = true;
                        r.object        = currentPickObject;
                        r.triangleIndex = currentPickTriangleIndex;
                        r.depth         = pickZ;
                        r.x             = (int16_t)qx;
                        r.y             = (int16_t)y;
                    }
                }
            }
#endif // MAX_PICK_QUERIES > 0

#if LIGHTING
            // Per-row Gouraud brightness init. Computed in int64 because
            // the numerator can be up to 1533 * |denom| and we shift left
            // by 16 for Q16 precision; divided once per row instead of
            // once per pixel.
            int32_t brightness_q16 = 0;
            if (useIncrementalGouraud)
            {
                const int64_t bRowNum = (int64_t)vertexBrightness[0] * ew0
                                      + (int64_t)vertexBrightness[1] * ew1
                                      + (int64_t)vertexBrightness[2] * ew2;
                brightness_q16 = (int32_t)((bRowNum << 16) / denom);
            }
#endif

#if JET_FAST_SIMPLE_SPANS
#if TEXTURE_MAPPING
            // Untextured triangles take the fast simple-span path even in
            // builds where TEXTURE_MAPPING is compiled in. Textured ones
            // fall through to the general per-pixel loop below.
            if (useFastSimpleSpan)
#endif
            {
            // Fast simple-span path. See JET_FAST_SIMPLE_SPANS comment at
            // the top of the TU. Skips per-pixel edge accumulate/test (the
            // solver above already bounded x to the inside range) and the
            // per-pixel dither lookup (alpha is triangle-constant in this
            // config, so the mask reduces to two per-row booleans).
            (void)ew0; (void)ew1; (void)ew2;  // Unused in this path.
#if FIELD_BUFFERS
            int32_t bufferIndex = (y >> 1) * (screenWidth / 2) + (xStart / 2);
#else
            int32_t bufferIndex = y * (screenWidth / 2) + (xStart / 2);
#endif
#if SCREEN_DOOR_ALPHA
            bool drawP0, drawP2;
            if (alpha > 240) {
                drawP0 = drawP2 = true;
            } else {
                // Same 4x4 Bayer matrix as shouldDrawPixel, but we only
                // need the two phases that HALF_WIDTH actually steps
                // through (x%4 \u2208 {0, 2} when xStart is 2-aligned).
                constexpr uint8_t thresholdMatrix[16] = {
                    15, 135, 45, 165,
                    195, 75, 225, 105,
                    60, 180, 30, 150,
                    240, 120, 210, 90};
                const int yRow = (y & 3) << 2;
                drawP0 = alpha >= thresholdMatrix[0 | yRow];
                drawP2 = alpha >= thresholdMatrix[2 | yRow];
            }
            if (!drawP0 && !drawP2) continue;  // whole row dithered out

            if (drawP0 && drawP2) {
                // Solid fill: pair adjacent uint16 stores into 32-bit
                // writes when alignment permits. Halves the store count on
                // the hot path - this is where most of the per-frame time
                // goes for near-opaque geometry.
                const uint32_t color32 = ((uint32_t)color << 16) | color;
                int idx = bufferIndex;
                const int idxEnd = bufferIndex + ((xEnd - xStart) >> 1); // inclusive
                // Leading single store if the start is misaligned.
                if (idx & 1) {
                    framebuffer[idx++] = color;
                }
                // Middle: paired 32-bit stores.
                const int pairs = (idxEnd - idx + 1) >> 1;
                uint32_t* fb32 = reinterpret_cast<uint32_t*>(&framebuffer[idx]);
                for (int i = 0; i < pairs; ++i) {
                    fb32[i] = color32;
                }
                idx += pairs * 2;
                // Trailing single store if the run length is odd.
                if (idx <= idxEnd) {
                    framebuffer[idx] = color;
                }
                bufferIndex = idxEnd + 1;
            } else {
                // Alternating fill: only one of the two phases draws. Pick
                // the matching phase bool and stride by 4 pixels (2 slots).
                const bool startIsP0 = ((xStart & 3) == 0);
                const bool drawFirst = startIsP0 ? drawP0 : drawP2;
                int x = xStart;
                if (!drawFirst) { x += 2; bufferIndex++; }
                for (; x <= xEnd; x += 4, bufferIndex += 2) {
                    framebuffer[bufferIndex] = color;
                }
            }
#else  // !SCREEN_DOOR_ALPHA
            // Traditional alpha-blend path. For fully-opaque triangles we
            // still emit the paired 32-bit fast stores; sub-opaque ones
            // fall through to a per-pixel "over" blend against the
            // existing framebuffer contents.
            if (alpha == 255) {
                const uint32_t color32 = ((uint32_t)color << 16) | color;
                int idx = bufferIndex;
                const int idxEnd = bufferIndex + ((xEnd - xStart) >> 1);
                if (idx & 1) {
                    framebuffer[idx++] = color;
                }
                const int pairs = (idxEnd - idx + 1) >> 1;
                uint32_t* fb32 = reinterpret_cast<uint32_t*>(&framebuffer[idx]);
                for (int i = 0; i < pairs; ++i) {
                    fb32[i] = color32;
                }
                idx += pairs * 2;
                if (idx <= idxEnd) {
                    framebuffer[idx] = color;
                }
                bufferIndex = idxEnd + 1;
            } else {
                for (int x = xStart; x <= xEnd; x += 2, bufferIndex++) {
                    framebuffer[bufferIndex] = blendRGB565(framebuffer[bufferIndex], color, alpha);
                }
            }
#endif // SCREEN_DOOR_ALPHA
            }  // close fast simple-span block
#if TEXTURE_MAPPING
            else
#endif
#endif // JET_FAST_SIMPLE_SPANS

#if !JET_FAST_SIMPLE_SPANS || TEXTURE_MAPPING
            {  // General per-pixel path. Runs as the textured `else` when
               // the fast path is also compiled (TEXTURE_MAPPING build),
               // or unconditionally when JET_FAST_SIMPLE_SPANS is
               // false (any of the heavy features enabled).
// Per-pixel brightness step appended to the for-loop header below. The
// step has to happen unconditionally per pixel (continues elsewhere in
// the loop body would otherwise desync the running value), so it lives
// in the increment list rather than the body.
#if LIGHTING
#define JET_LIT_STEP , brightness_q16 += brightness_dx_step_q16
#else
#define JET_LIT_STEP
#endif
#if HALF_WIDTH_BUFFERS
#if FIELD_BUFFERS
            int32_t bufferIndex = (y >> 1) * (screenWidth / 2) + (xStart / 2);
#else
            int32_t bufferIndex = y * (screenWidth / 2) + (xStart / 2);
#endif
        for (int x = xStart; x <= xEnd;
             x += 2, ew0 += dw0_dx_step, ew1 += dw1_dx_step, ew2 += dw2_dx_step, bufferIndex++ JET_LIT_STEP)
            {
#else
#if FIELD_BUFFERS
            int32_t bufferIndex = (y >> 1) * screenWidth + xStart;
#else
            int32_t bufferIndex = y * screenWidth + xStart;
#endif
        for (int x = xStart; x <= xEnd;
             x++, ew0 += dw0_dx_step, ew1 += dw1_dx_step, ew2 += dw2_dx_step, bufferIndex++ JET_LIT_STEP)
            {
#endif
            // Z-buffer index. Stride matches the configured depth-buffer
            // layout: half-width (one cell per two output pixels) when
            // HALF_WIDTH_BUFFERS is on, otherwise per-pixel (same stride
            // as the colour buffer). Set per-row.
            #if Z_BUFFERING
            int32_t zBufferIndex = y * ZBUFFER_STRIDE(screenWidth);
            #endif

                #if Z_BUFFERING
                // If the z-buffer position is at its maximum for this pixel (as close to the camera as is possible), skip this pixel
                // since it can't possibly be any closer.
                if (zBuffer[zBufferIndex] == 0 && !ignoreZBuffer)
                {
                    continue;
                }
                #endif

                // Inside test on the incrementally-stepped edge function.
                // Comparing int64 with 0 only examines the high word, so
                // this is cheap. The scanline range above already trimmed
                // the iteration to (mostly) inside-only pixels, but a
                // small shoulder can still be outside due to the integer
                // ceil/floor rounding - keep the test as a safety net.
                if ((ew0 | ew1 | ew2) < 0)
                {
                    continue;
                }

#if !HALF_WIDTH_BUFFERS
                // Checkerboard: skip pixels that belong to the other frame's pattern.
                // Using XOR parity: (x^y)&1 == (x+y)&1 (no carry in the lowest bit).
                if (checkerboardMode && (((x ^ y) & 1) != cbFrameParity))
                {
                    continue;
                }
#endif

                // For any inside pixel, |ew_i| <= |denom|. If any downstream
                // code needs int32 barycentric weights (interpolation paths)
                // they're safe to narrow here. The compiler DCEs these on
                // the FAST_Z / no-texture / no-lighting fast path.
                int32_t w0 = (int32_t)ew0;
                int32_t w1 = (int32_t)ew1;
                int32_t w2 = (int32_t)ew2;

// Pixel is inside the triangle - render it
// Interpolate z, u, v
#if !FAST_Z
                int32_t z = (v1.position.z * w0 + v2.position.z * w1 + v3.position.z * w2) / denom;

                if (z < nearPlane || z > farPlane)
                {
                    continue;
                }

#if Z_BUFFERING
                // Per-pixel Z bias in real depth units. Clamp to uint16
                // for storage (matches the FAST_Z setup path).
                int32_t zbRaw = z - zBias;
                if (zbRaw < 0)     zbRaw = 0;
                if (zbRaw > 65535) zbRaw = 65535;
                uint32_t zb = (uint32_t)zbRaw;
#endif

#if Z_BRIGHTNESS
                brightness = 255 - ((z - nearPlane) * 127) / (farPlane - nearPlane);
                brightness = std::min(brightness, static_cast<uint16_t>(255));
#endif
#endif

#if DEPTH_ALPHA_BLEND && !FAST_Z
                // Per-pixel depth fog. Only used on the !FAST_Z path where
                // z genuinely varies per pixel; on the FAST_Z path this is
                // hoisted to the triangle setup above. Compose with the
                // existing alpha (material*objAlpha) by taking the
                // minimum into a per-pixel local — important: must NOT
                // mutate `alpha`, which is shared across all pixels of
                // this triangle, or each pixel's fade would compound on
                // the previous one's.
                uint8_t pixAlpha = alpha;
                if (z > depthFogNear && z < depthFogFar)
                {
                    uint8_t fogA = (uint8_t)(255 - ((z - depthFogNear) * 255) / (depthFogFar - depthFogNear));
                    if (fogA < pixAlpha) pixAlpha = fogA;
                }
                else if (z >= depthFogFar)
                {
                    pixAlpha = 0;
                }
#else
                uint8_t pixAlpha = alpha;
#endif

#if SCREEN_DOOR_ALPHA
                // Stippling based on alpha value
                if (!shouldDrawPixel(x, y, pixAlpha))
                {
                    continue;
                }
#endif

#if Z_BUFFERING
                if (!ignoreZBuffer && zb > zBuffer[zBufferIndex])
                {
                    continue;
                }
#endif

#if TEXTURE_MAPPING
                if (diffuseMap)
                {
                    if (diffuseMap->screenSpace)
                    {
                        // **Screen-space texture mapping**
                        uv.x = x * diffuseMap->width / screenWidth;
                        uv.y = y * diffuseMap->height / screenHeight;
                    }
                    else if (diffuseMap->reflectionMap)
                    {
                        uv.x = x + z;
                        uv.y = y + z;
                    }
                    else
                    {
#if PERSPECTIVE_CORRECT_TEXTURES
                        // Interpolate 1/z, u/z, and v/z at the current pixel
                        int32_t interpolatedOneOverZ = (oneOverZ1 * w0 + oneOverZ2 * w1 + oneOverZ3 * w2) / denom;
                        int32_t interpolatedUOverZ = (uOverZ1 * w0 + uOverZ2 * w1 + uOverZ3 * w2) / denom;
                        int32_t interpolatedVOverZ = (vOverZ1 * w0 + vOverZ2 * w1 + vOverZ3 * w2) / denom;

                        // Avoid division by zero
                        if (interpolatedOneOverZ == 0)
                            continue;

                        // Compute final texture coordinates
                        uv.x = (interpolatedUOverZ * FIXED_POINT_SCALE) / interpolatedOneOverZ;
                        uv.y = (interpolatedVOverZ * FIXED_POINT_SCALE) / interpolatedOneOverZ;
#else // Affine texture mapping
                        uv.x = (v1.uv.x * w0 + v2.uv.x * w1 + v3.uv.x * w2) / denom;
                        uv.y = (v1.uv.y * w0 + v2.uv.y * w1 + v3.uv.y * w2) / denom;
#endif
                    }

                    // Sample color from material
                    color = material->getColor(uv);

                    // If the material diffuse map has a transparent color and that's what we got, skip drawing this pixel
                    if (diffuseMap->hasAlpha && color == diffuseMap->alphaColor)
                    {
                        continue;
                    }

                    // Distance-based texture LOD cross-fade. textureLodFade
                    // is triangle-constant; outside the fade band it is
                    // 255 (no work). Inside the band, swap or blend the
                    // sampled texel toward material->color using whichever
                    // composite method the build is configured for.
                    if (textureLodFade < 255)
                    {
                    #if SCREEN_DOOR_ALPHA
                        // Stipple between texel (drawn) and flat (drawn).
                        // The same Bayer matrix as material alpha so the
                        // two stipples don't beat against each other in
                        // unpleasant ways.
                        if (!shouldDrawPixel(x, y, textureLodFade))
                            color = material->color;
                    #else
                        // dst = flat, src = texel. blendRGB565 returns
                        // src*alpha + dst*(1-alpha), so alpha=textureLodFade
                        // gives full texel at 255 and full flat at 0.
                        color = blendRGB565(material->color, color, textureLodFade);
                    #endif
                    }
                }
#endif

#if Z_BUFFERING
                if (!noWriteZBuffer)
                {
                    zBuffer[zBufferIndex] = static_cast<uint16_t>(zb);
                }
#endif

#if DEBUG_OVERDRAW
                // Orange color in RGB565 format (0xFDA0)
                color = 0xFDA0;
                // Apply 25% blend
                uint8_t r = ((color >> 11) & 0x1F) / 4;
                uint8_t g = ((color >> 5) & 0x3F) / 4;
                uint8_t b = (color & 0x1F) / 4;
                
                // Get existing color and blend
                uint16_t existing = framebuffer[bufferIndex];
                uint8_t existingR = (existing >> 11) & 0x1F;
                uint8_t existingG = (existing >> 5) & 0x3F;
                uint8_t existingB = existing & 0x1F;
                
                // Add the colors
                r = std::min(static_cast<uint8_t>(31), static_cast<uint8_t>(existingR + r));
                g = std::min(static_cast<uint8_t>(63), static_cast<uint8_t>(existingG + g));
                b = std::min(static_cast<uint8_t>(31), static_cast<uint8_t>(existingB + b));
                
                color = (r << 11) | (g << 5) | b;
                
                #if HALF_WIDTH_BUFFERS
                framebuffer[bufferIndex] = color;
                #else
                uint32_t combinedColor = (static_cast<uint32_t>(color) << 16) | color;
                reinterpret_cast<uint32_t *>(framebuffer)[bufferIndex / 2] = combinedColor;
                #endif                
                continue;
#else
#if LIGHTING || Z_BRIGHTNESS
                if (directionalLight)
                {
                    if (material->shadingMode == ShadingMode::GOURAUD)
                    {
                        // Plane-equation incremental Gouraud brightness:
                        // brightness_q16 was set at the row start and is
                        // stepped by brightness_dx_step_q16 in the for-loop
                        // header below. Mathematically equivalent to the
                        // original (b0*w0 + b1*w1 + b2*w2)/denom but trades
                        // a per-pixel divide + 3 multiplies for a single
                        // int32 add per pixel.
                        int32_t b = brightness_q16 >> 16;
                        if (b < 0) b = 0;
                        if (b > 65535) b = 65535;
                        brightness = (uint16_t)b;
                    }
                    else if (material->shadingMode == ShadingMode::PHONG)
                    {
                        // Interpolate normals and calculate lighting per pixel
                        Vector3 pixelNormal;
                        pixelNormal.x = (v1.normal.x * w0 + v2.normal.x * w1 + v3.normal.x * w2) / denom;
                        pixelNormal.y = (v1.normal.y * w0 + v2.normal.y * w1 + v3.normal.y * w2) / denom;
                        pixelNormal.z = (v1.normal.z * w0 + v2.normal.z * w1 + v3.normal.z * w2) / denom;

                        // Normalize interpolated normal
                        auto normalLength = pixelNormal.length();
                        if (normalLength > 0)
                        {
                            pixelNormal = (pixelNormal * static_cast<int32_t>(1024)) / normalLength;
                        }

                        // Calculate diffuse lighting using the interpolated normal
                        int64_t dotProduct = Vector3::dotProduct(pixelNormal, directionalLight->lightDir) / scaleFactor;
                        dotProduct = std::max(dotProduct, static_cast<int64_t>(-512));
                        int64_t adjustedBrightness = (dotProduct + 255) / 2;
                        brightness = static_cast<uint16_t>((adjustedBrightness * material->diffuse) / FIXED_POINT_SCALE);

                        // Calculate specular reflection if material has specular component
                        if (material->specular > 0)
                        {
                            // Calculate reflection vector R = 2(N·L)N - L
                            int32_t twoNdotL = 2 * (Vector3::dotProduct(pixelNormal, directionalLight->lightDir) / FIXED_POINT_SCALE);
                            Vector3 reflectionVector = (pixelNormal * twoNdotL) - directionalLight->lightDir;
                            
                            // View vector in screen space is approximately (0,0,1)
                            static const Vector3 viewVector = {0, 0, FIXED_POINT_SCALE};
                            
                            // Calculate R·V (reflection · view)
                            int64_t specDot = Vector3::dotProduct(reflectionVector, viewVector) / FIXED_POINT_SCALE;
                            
                            // Only add specular when the angle is less than 90 degrees
                            if (specDot > 0)
                            {
                                // Approximate pow() with multiple multiplications for performance
                                int32_t specPower = (specDot * specDot) / FIXED_POINT_SCALE; // Square it for a basic specular effect
                                int32_t specIntensity = (specPower * material->specular) / FIXED_POINT_SCALE;
                                brightness += specIntensity; // Allow to go above 255 for blow-out effect
                            }
                        }

                        if (ambientLight)
                            brightness += ambientLight->color.r;
                        
                        // Cap maximum brightness based on specular value
                        // 0 specular = cap at 255
                        // 255 specular = cap at 511
                        uint16_t maxBrightness = 255 + ((material->specular * 256) / 255);
                        brightness = std::min(brightness, maxBrightness);
                    }
                }
#endif

#if LIGHTING || Z_BRIGHTNESS
                // If POSTFX_CELLSHADING is enabled, kill off the bottom N bits of the brightness value to create a cell shading effect
                if (POSTFX_CELLSHADING)
                {
                    brightness = brightness >> CELLSHADING_CELL_BITS << CELLSHADING_CELL_BITS;
                }

                // The triangle-setup hoist above already computed the lit
                // color for FLAT/unlit triangles without a diffuse texture
                // - skip the per-pixel modulation in that case. (Z_BRIGHTNESS
                // without LIGHTING never hoists, so guard the flag access
                // behind the same #if it was declared under.)
#if LIGHTING
                if (!emissive && !flatColorPrecomputed)
#else
                if (!emissive)
#endif
                {
                    if (brightness >= 255)
                    {
                        // Blend towards white for brightness values above 255
                        uint16_t blowout = brightness - 255;
                        uint8_t r = ((color >> 11) & 0x1F);
                        uint8_t g = ((color >> 5) & 0x3F);
                        uint8_t b = (color & 0x1F);
                        
                        // Blend each component towards its maximum value
                        r = r + ((31 - r) * blowout) / 256;
                        g = g + ((63 - g) * blowout) / 256;
                        b = b + ((31 - b) * blowout) / 256;
                        
                        color = (r << 11) | (g << 5) | b;
                    }
                    else
                    {
                        // Normal diffuse lighting for brightness 0-255.
                        // Replace the per-channel `(ch * b + 127) / 255` with
                        // the standard divide-by-255 trick: for x in [0..255*63],
                        // (x + 128 + (x >> 8)) >> 8 == round(x/255). One add
                        // and one shift per channel instead of a divide.
                        const uint16_t br = ((color >> 11) & 0x1F) * brightness;
                        const uint16_t bg = ((color >>  5) & 0x3F) * brightness;
                        const uint16_t bb = ( color        & 0x1F) * brightness;
                        const uint8_t r = (uint8_t)((br + 128 + (br >> 8)) >> 8);
                        const uint8_t g = (uint8_t)((bg + 128 + (bg >> 8)) >> 8);
                        const uint8_t b = (uint8_t)((bb + 128 + (bb >> 8)) >> 8);
                        color = (r << 11) | (g << 5) | b;
                    }
                }
#endif
#endif

#if !DEBUG_OVERDRAW
#if SCREEN_DOOR_ALPHA
            // Stippling already accepted/rejected this pixel via
            // shouldDrawPixel(); a straight write is correct here.
            framebuffer[bufferIndex] = color;
#else
            // Traditional alpha blend: lerp toward `color` by pixAlpha.
            // pixAlpha already folds in material*object alpha and any
            // per-pixel depth-fog fade. Skip the blend math when the
            // material is fully opaque — common case.
            if (pixAlpha == 255) {
                framebuffer[bufferIndex] = color;
            } else {
                framebuffer[bufferIndex] = blendRGB565(framebuffer[bufferIndex], color, pixAlpha);
            }
#endif
#endif
#if RENDER_TILE_BUFFER
                // Mark this tile as having been drawn to
                int tileX = x / TILE_WIDTH;
                int tileY = y / TILE_HEIGHT;
                if (tileX >= 0 && tileX < (screenWidth + TILE_WIDTH - 1) / TILE_WIDTH &&
                    tileY >= 0 && tileY < (screenHeight + TILE_HEIGHT - 1) / TILE_HEIGHT)
                {
                    //tileBuffer[tileY * ((screenWidth + TILE_WIDTH - 1) / TILE_WIDTH) + tileX] = 1;
                }

#endif
            }
            }  // close general per-pixel path block
#endif // !JET_FAST_SIMPLE_SPANS || TEXTURE_MAPPING
#undef JET_LIT_STEP
        }

        return true;
    }
} // namespace Renderer

