#include "Scene.hpp"
#include "TrigLUT.hpp"
#include "Renderer.hpp"
#include "Config.hpp"
#include <cstring> // For memset
#include <algorithm> // For std::min, std::max
#include <cmath> // For std::sqrt (per-object distance fade)

namespace Renderer {

// Returns true if the object's AABB is entirely outside the view frustum.
bool Scene::cullObject(Object* obj,
                       int32_t camCosX, int32_t camSinX,
                       int32_t camCosY, int32_t camSinY,
                       int32_t camCosZ, int32_t camSinZ) const {
    if (obj->isBillboard) return false; // keep billboards simple for now

    Vector3 camPos(camera->position);
    Vector3 objPos(obj->position);
    const Vector3& bMin = obj->boundingBoxMin;
    const Vector3& bMax = obj->boundingBoxMax;

    int outLeft = 0, outRight = 0, outTop = 0, outBottom = 0, outNear = 0, outFar = 0;
    int32_t fovFactor = camera->fovFactor;
    int32_t nearPlane = camera->nearPlane;
    int32_t farPlane  = camera->farPlane;

    int32_t objCosX = lookupCosI(obj->rotation.x), objSinX = lookupSinI(obj->rotation.x);
    int32_t objCosY = lookupCosI(obj->rotation.y), objSinY = lookupSinI(obj->rotation.y);
    int32_t objCosZ = lookupCosI(obj->rotation.z), objSinZ = lookupSinI(obj->rotation.z);

    for (int i = 0; i < 8; ++i) {
        Vector3 p(
            (i & 1) ? bMax.x : bMin.x,
            (i & 2) ? bMax.y : bMin.y,
            (i & 4) ? bMax.z : bMin.z);

        // Object rotation (X, Y, Z) — same order as renderObject
        p.assign(p.x,
                 (p.y * objCosX - p.z * objSinX) / FIXED_POINT_SCALE,
                 (p.y * objSinX + p.z * objCosX) / FIXED_POINT_SCALE);
        p.assign((p.x * objCosY + p.z * objSinY) / FIXED_POINT_SCALE,
                  p.y,
                 (-p.x * objSinY + p.z * objCosY) / FIXED_POINT_SCALE);
        p.assign((p.x * objCosZ - p.y * objSinZ) / FIXED_POINT_SCALE,
                 (p.x * objSinZ + p.y * objCosZ) / FIXED_POINT_SCALE,
                  p.z);

        // Translation
        p.add(objPos);
        p.add(camPos.inverse());

        // Camera rotation (Y, X, Z)
        Vector3 r;
        r.assign((p.x * camCosY + p.z * camSinY) / FIXED_POINT_SCALE,
                  p.y,
                 (-p.x * camSinY + p.z * camCosY) / FIXED_POINT_SCALE); p = r;
        r.assign(p.x,
                 (p.y * camCosX - p.z * camSinX) / FIXED_POINT_SCALE,
                 (p.y * camSinX + p.z * camCosX) / FIXED_POINT_SCALE); p = r;
        r.assign((p.x * camCosZ - p.y * camSinZ) / FIXED_POINT_SCALE,
                 (p.x * camSinZ + p.y * camCosZ) / FIXED_POINT_SCALE,
                  p.z); p = r;

        if (p.z < nearPlane) { outNear++; continue; }
        if (p.z > farPlane)  { outFar++;  continue; }
        if (p.z <= 0)        { outNear++; continue; }

        int32_t sx = (p.x * fovFactor) / (p.z * FIXED_POINT_SCALE) + screenWidth / 2;
        int32_t sy = screenHeight / 2 - (p.y * fovFactor) / (p.z * FIXED_POINT_SCALE);
        if (sx < 0)            outLeft++;
        if (sx > screenWidth)  outRight++;
        if (sy < 0)            outTop++;
        if (sy > screenHeight) outBottom++;
    }

    return (outNear == 8 || outFar == 8 ||
            outLeft == 8 || outRight == 8 ||
            outTop  == 8 || outBottom == 8);
}

Scene::Scene(uint16_t* framebuffer, uint16_t* zBuffer, int screenWidth, int screenHeight)
    : camera(nullptr), directionalLight(nullptr), ambientLight(nullptr),
      framebuffer(framebuffer), zBuffer(zBuffer), screenWidth(screenWidth), screenHeight(screenHeight), scanlinesUpdated(nullptr) {
    initializeTrigTables();
    renderer = new Rasterizer(framebuffer, screenWidth, screenHeight, zBuffer);
    postFX = new PostFX(screenWidth, screenHeight);
}
Scene::~Scene() {
    if (renderer) {
        delete renderer;
        renderer = nullptr;
    }
    if (postFX) {
        delete postFX;
        postFX = nullptr;
    }
}

void Scene::setFramebuffer(uint16_t *newBuffer) {
    framebuffer = newBuffer;
    renderer->setFramebuffer(newBuffer);
}

void Scene::resize(uint16_t* newFramebuffer, uint16_t* newZBuffer,
                   int newWidth, int newHeight) {
    framebuffer  = newFramebuffer;
    zBuffer      = newZBuffer;
    screenWidth  = newWidth;
    screenHeight = newHeight;
    if (renderer) {
        renderer->resize(newFramebuffer, newZBuffer, newWidth, newHeight);
    }
    // PostFX caches its own dimensions and (when enabled) owns scratch
    // buffers sized to them. Easiest correct thing is to rebuild it.
    if (postFX) {
        delete postFX;
        postFX = new PostFX(newWidth, newHeight);
    }
}

void Scene::addObject(Object* obj) {
    objects.push_back(obj);
}

void Scene::addPointLight(PointLight* light) {
    pointLights.push_back(light);
}

void Scene::setCamera(Camera* cam) {
    camera = cam;
    renderer->camera = cam;
}

void Scene::setDirectionalLight(DirectionalLight* light) {
    directionalLight = light;
}

void Scene::setAmbientLight(AmbientLight* light) {
    ambientLight = light;
}

#if MAX_PICK_QUERIES > 0
void Scene::setPickQueries(const PickQuery* queries, int count) {
    if (count < 0) count = 0;
    if (count > MAX_PICK_QUERIES) count = MAX_PICK_QUERIES;
    pickQueryCount = count;
    for (int i = 0; i < count; ++i) {
        pickQueries[i] = queries[i];
    }
    // Slots beyond `count` keep their previous contents but are ignored
    // by the rasterizer (it loops 0..pickQueryCount). We don't bother
    // zeroing them.
}
#endif

void Scene::reconstructCheckerboard() {
    // For each pixel that was NOT rendered this frame (opposite parity),
    // approximate it as a 3-tap average of its own previous-frame value and
    // its two freshly-rendered horizontal neighbours. The old value provides
    // temporal stability; the fresh neighbours provide spatial accuracy.
    // At the left/right edges the missing neighbour is omitted (2-tap).
    // This is a separate pass over the completed framebuffer so that painter's
    // algorithm overdraw doesn't cause repeated averaging of the gap pixel.
    const int cbParity = renderEvenLines ? 0 : 1;
    for (int y = 0; y < screenHeight; ++y) {
        uint16_t* row = framebuffer + y * screenWidth;
        for (int x = 0; x < screenWidth; ++x) {
            if (((x ^ y) & 1) == cbParity) continue; // freshly rendered — skip
            const uint16_t old = row[x];
            if (x == 0) {
                // 2-tap: old + right
                const uint16_t r = row[x + 1];
                row[x] = (uint16_t)(
                    ((((old >> 11) & 0x1F) + ((r >> 11) & 0x1F)) >> 1 << 11) |
                    ((((old >>  5) & 0x3F) + ((r >>  5) & 0x3F)) >> 1 <<  5) |
                     (((old        & 0x1F) + ( r        & 0x1F)) >> 1));
            } else if (x == screenWidth - 1) {
                // 2-tap: left + old
                const uint16_t l = row[x - 1];
                row[x] = (uint16_t)(
                    ((((l >> 11) & 0x1F) + ((old >> 11) & 0x1F)) >> 1 << 11) |
                    ((((l >>  5) & 0x3F) + ((old >>  5) & 0x3F)) >> 1 <<  5) |
                     ((( l       & 0x1F) + ( old        & 0x1F)) >> 1));
            } else {
                // 3-tap: left + old + right
                const uint16_t l = row[x - 1];
                const uint16_t r = row[x + 1];
                row[x] = (uint16_t)(
                    ((((l >> 11) & 0x1F) + ((old >> 11) & 0x1F) + ((r >> 11) & 0x1F)) / 3 << 11) |
                    ((((l >>  5) & 0x3F) + ((old >>  5) & 0x3F) + ((r >>  5) & 0x3F)) / 3 <<  5) |
                     ((( l       & 0x1F) + ( old        & 0x1F) + ( r        & 0x1F)) / 3));
            }
        }
    }
}

void PERF_CRITICAL Scene::clearBuffers() {
    //cast the framebuffer to a 32-bit pointer
    uint32_t* framebuffer32 = (uint32_t*)framebuffer;

    if (clearRenderBuffer) {
        if (renderer->checkerboardMode) {
            // Checkerboard clear: only wipe current-parity pixels. Opposite-parity
            // pixels keep last frame's values; when CHECKERBOARD_RECONSTRUCTION
            // is enabled the rasterizer fills them in-situ as it renders each
            // current-parity pixel.
            const int cbParity = renderEvenLines ? 0 : 1;
            for (int y = 0; y < screenHeight; ++y) {
                #if DEBUG_OVERDRAW
                const uint16_t lineColor = 0;
                #else
                const uint16_t lineColor = backgroundGradientColors
                                           ? backgroundGradientColors[y] : backcolor;
                #endif
                uint16_t* row = framebuffer + y * screenWidth;
                for (int x = 0; x < screenWidth; ++x) {
                    if (((x ^ y) & 1) == cbParity) {
                        row[x] = lineColor;
                    }
                }
            }
            #if Z_BUFFERING
            memset(zBuffer, 0xFF, (size_t)screenWidth * screenHeight * sizeof(uint16_t));
            #endif
        } else if (renderer->interlacedMode) {
            for (int y = (int)renderEvenLines; y < screenHeight; y += 2) {
                #if DEBUG_OVERDRAW
                uint16_t lineColor = 0;
                uint32_t lineColor32 = 0;
                #else
                uint16_t lineColor = backgroundGradientColors ? backgroundGradientColors[y] : backcolor;
                uint32_t lineColor32 = (lineColor << 16) | lineColor;
                #endif
                #if HALF_WIDTH_BUFFERS
                const int divisor = 4;
                #else
                const int divisor = 2;
                #endif
                #if FIELD_BUFFERS
                // Field-buffer layout: packed half-height. Row index = y>>1.
                uint32_t* lineStart = framebuffer32 + (y >> 1) * (screenWidth / divisor);
                #else
                uint32_t* lineStart = framebuffer32 + y * (screenWidth / divisor);
                #endif
                for (int x = 0; x < screenWidth / divisor; x++) {
                    lineStart[x] = lineColor32;
                }
                #if Z_BUFFERING
                #if HALF_WIDTH_BUFFERS
                memset(zBuffer + (y / 2) * (screenWidth / 2), 0xFF, (screenWidth / 2) * sizeof(uint16_t));
                #else
                memset(zBuffer + y * screenWidth, 0xFF, screenWidth * sizeof(uint16_t));
                #endif
                #endif
            }
        } else {
            // Non-interlaced clear: fill every row. Honour the per-row
            // background gradient if one is set; otherwise fall back to
            // a solid backcolor. Note that `memset(fb, backcolor, ...)` is
            // wrong for a 16-bit backcolor because memset writes bytes —
            // we'd get backcolor's low byte duplicated. Pack two pixels per
            // 32-bit store and walk rows so the gradient (if any) actually
            // shows up.
            #if HALF_WIDTH_BUFFERS
            const int rowPixels = screenWidth / 2;
            const int rowCount  = screenHeight / 2;
            #else
            const int rowPixels = screenWidth;
            const int rowCount  = screenHeight;
            #endif
            const int row32     = rowPixels / 2; // pairs of pixels per row
            for (int y = 0; y < rowCount; ++y) {
                #if DEBUG_OVERDRAW
                const uint16_t lineColor = 0;
                #else
                // Source row in the gradient table maps 1:1 with the
                // logical screen row (`screenHeight`). When the back buffer
                // is half-height (HALF_WIDTH_BUFFERS implies a y/2-style
                // layout in some configs), index by `y` directly because
                // rowCount already accounts for the halving.
                const uint16_t lineColor = backgroundGradientColors
                                           ? backgroundGradientColors[y * (screenHeight / rowCount)]
                                           : backcolor;
                #endif
                const uint32_t lineColor32 = ((uint32_t)lineColor << 16) | lineColor;
                uint32_t* lineStart = framebuffer32 + y * row32;
                for (int x = 0; x < row32; ++x) lineStart[x] = lineColor32;
            }
            #if Z_BUFFERING
            // Z-buffer stride matches the rasterizer's depth-buffer layout
            // (see ZBUFFER_STRIDE in Renderer.hpp): half-width when
            // HALF_WIDTH_BUFFERS is on, per-pixel otherwise. Height is the
            // full screen height regardless.
            #if HALF_WIDTH_BUFFERS
            memset(zBuffer, 0xFF, (size_t)(screenWidth / 2) * screenHeight * sizeof(uint16_t));
            #else
            memset(zBuffer, 0xFF, (size_t)screenWidth * screenHeight * sizeof(uint16_t));
            #endif
            #endif
        }
    }
    //memset(scanlinesUpdated, 0, screenHeight * sizeof(bool));
}

void Scene::render() {
    if (!camera) return;
    // renderEvenLines drives the frame-parity selection used by both interlaced
    // and checkerboard modes.  In interlaced mode it selects which rows to draw;
    // in checkerboard mode it selects which (x+y) pixel parity to draw.  When
    // neither mode is active we force it to false so drawTriangle's
    // `yStart = interlacedMode ? (minY + renderEvenLines) : minY` never shifts
    // the first scanline by a stray ±1.
    renderEvenLines = (renderer->interlacedMode || renderer->checkerboardMode)
                      ? (frameCounter % 2 == 0)
                      : false;
    clearBuffers();

#if MAX_PICK_QUERIES > 0
    // Reset pick results for this frame and hand the arrays to the
    // rasterizer. With FIELD_BUFFERS the rasterizer only writes one
    // parity of rows per frame, so a query landing on the "off" parity
    // would never be tested by drawTriangle's per-row pick loop. Snap
    // the y to a row this frame's field actually covers so the host's
    // requested pixel still produces a hit on roughly the right
    // location (off by one row at most). The snapped y is what gets
    // stored in PickResult.y so the caller can render their cursor on
    // the same row the renderer inspected.
    for (int i = 0; i < pickQueryCount; ++i) {
        pickResults[i] = PickResult{};
    #if FIELD_BUFFERS
        if (renderer->interlacedMode && pickQueries[i].y >= 0) {
            const int desiredParity = renderEvenLines ? 0 : 1;
            int sy = pickQueries[i].y;
            if ((sy & 1) != desiredParity) {
                // Prefer nudging up; clamp to a valid row at the bottom.
                if (sy + 1 < screenHeight) sy += 1; else if (sy > 0) sy -= 1;
            }
            pickQueries[i].y = (int16_t)sy;
        }
    #endif
    }
    renderer->pickQueries     = pickQueries;
    renderer->pickResults     = pickResults;
    renderer->pickQueryCount  = pickQueryCount;
#endif

#if LIGHTING
    if (directionalLight) directionalLight->updateViewSpaceDirection(camera);
#endif

    int32_t camCosX, camSinX, camCosY, camSinY, camCosZ, camSinZ;
    camera->getRotationMatrix(camCosX, camSinX, camCosY, camSinY, camCosZ, camSinZ);

    renderQueue.clear();
    int drawnObjs = 0;

    for (auto obj : objects) {
        if (!obj->enabled) continue;
        // 1) Object-level AABB frustum cull
        if (cullObject(obj, camCosX, camSinX, camCosY, camSinY, camCosZ, camSinZ))
            continue;
        // 1b) Per-object distance fade (two ramps, multiplied):
        //     - fadeFar > 0:   close=opaque, far=invisible (decor fade-out).
        //     - appearFar > 0: close=invisible, far=opaque (LOD impostor
        //                      that pops in at distance).
        //     Both can be combined on the same object if you want a
        //     visibility "band" — opaque only between two distances.
        //     fadeFar==0 / appearFar==0 disable the respective ramp.
        //     Distance is measured in world space from the camera to the
        //     object's centre (position + centreVolume). Beyond fadeFar
        //     OR closer than appearNear the object is skipped entirely
        //     (no transform, no per-tri work), so this is a real perf
        //     win not just a visual fade.
        uint8_t objAlpha = 255;
        // Distance to object centre. Computed lazily and shared between
        // the fade ramps and the LOD pick below; both want the same
        // value, so we never sqrt() twice.
        int64_t distSq = -1;
        int32_t dist   = -1;
        const bool needsDist = (obj->fadeFar > 0)
                            || (obj->appearFar > 0)
                            || (lodScale > 0);
        if (needsDist) {
            const int32_t dx = (obj->position.x + obj->centreVolume.x) - camera->position.x;
            const int32_t dy = (obj->position.y + obj->centreVolume.y) - camera->position.y;
            const int32_t dz = (obj->position.z + obj->centreVolume.z) - camera->position.z;
            // 64-bit guard: dx/dy/dz can be in the thousands so the
            // squared sum can overflow int32_t.
            distSq = (int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz;
        }
        if (obj->fadeFar > 0 || obj->appearFar > 0) {
            if (obj->fadeFar > 0) {
                const int64_t farSq = (int64_t)obj->fadeFar * obj->fadeFar;
                if (distSq >= farSq) continue; // fully past fade-out — skip
                const int64_t nearSq = (int64_t)obj->fadeNear * obj->fadeNear;
                if (distSq > nearSq && obj->fadeFar > obj->fadeNear) {
                    if (dist < 0) dist = (int32_t)std::sqrt((double)distSq);
                    const int32_t span = obj->fadeFar - obj->fadeNear;
                    const int32_t over = dist - obj->fadeNear;
                    int32_t a = 255 - (over * 255) / span;
                    if (a < 0) a = 0;
                    if (a > 255) a = 255;
                    objAlpha = (uint8_t)((objAlpha * a) / 255);
                }
            }

            // Appear-in ramp (LOD impostor).
            if (obj->appearFar > 0) {
                const int64_t nearSq = (int64_t)obj->appearNear * obj->appearNear;
                if (distSq <= nearSq) continue; // still too close — skip
                const int64_t farSq = (int64_t)obj->appearFar * obj->appearFar;
                if (distSq < farSq && obj->appearFar > obj->appearNear) {
                    if (dist < 0) dist = (int32_t)std::sqrt((double)distSq);
                    const int32_t span = obj->appearFar - obj->appearNear;
                    const int32_t over = dist - obj->appearNear;
                    int32_t a = (over * 255) / span;
                    if (a < 0) a = 0;
                    if (a > 255) a = 255;
                    objAlpha = (uint8_t)((objAlpha * a) / 255);
                }
                // distSq >= farSq: fully appeared, multiplier already 255.
            }

            if (objAlpha == 0) continue;
        }

        // 1c) Global LOD pick. The head Object IS LOD 0; entries in
        //     obj->lodMeshes are LOD 1, 2, ... in order. Beyond the last
        //     available LOD: cull (default) or clamp (`lodPersist`).
        //     The picked Object* contributes ONLY mesh data; the head's
        //     transform / flags / AABB / fade ramps still drive the draw.
        Object* meshSource = obj;
        if (lodScale > 0) {
            if (dist < 0 && distSq >= 0) dist = (int32_t)std::sqrt((double)distSq);
            int32_t level = (dist < 0 ? 0 : dist / lodScale);
            level += (int32_t)lodBias + (int32_t)obj->lodBias;
            if (level < 0) level = 0;

            const int availableLODs = (int)obj->lodMeshes.size();
            if (level == 0) {
                meshSource = obj;
            } else if (level <= availableLODs) {
                Object* candidate = obj->lodMeshes[level - 1];
                meshSource = candidate ? candidate : obj;
            } else if (obj->lodPersist) {
                if (availableLODs > 0) {
                    Object* candidate = obj->lodMeshes[availableLODs - 1];
                    meshSource = candidate ? candidate : obj;
                }
                // else: no LOD chain at all, draw the head as-is.
            } else {
                continue; // ran out of LODs and not persisting → cull.
            }
        }

        // 2) Transform + project + per-triangle cull, push into renderQueue
        renderObject(obj, camCosX, camSinX, camCosY, camSinY, camCosZ, camSinZ, objAlpha, meshSource);
        ++drawnObjs;
    }
    lastFrameDrawnObjects   = drawnObjs;
    lastFrameDrawnTriangles = static_cast<int>(renderQueue.size());

    // 3) Global painter's sort. Three bands:
    //      0. noWriteZBuffer  — drawn first, so later geometry paints over
    //                           them (e.g. skyboxes).
    //      1. Normal          — main scene, back-to-front by effective Z.
    //      2. ignoreZBuffer   — drawn last, unconditionally on top.
    //
    // Within the normal band, the sort key is `avgZ - zBias * zBiasScale`.
    // Bigger zBias pulls a triangle toward the camera in the sort, so it
    // draws later than coplanar geometry without that bias. The scale has
    // to be large enough to beat within-triangle avgZ variation on
    // typical world-scale geometry (~hundreds of units) so decals reliably
    // win coplanar fights, but not so large that a biased decal draws
    // *over* geometry that's genuinely much closer (where avgZ differences
    // are thousands). Tune if the scale of the world changes significantly.
    constexpr int32_t zBiasScale = 256;
    std::sort(renderQueue.begin(), renderQueue.end(),
        [zBiasScale](const RenderTri& a, const RenderTri& b) {
            auto band = [](const RenderTri& t) {
                if (t.noWriteZBuffer) return 0;
                if (t.ignoreZBuffer)  return 2;
                return 1;
            };
            const int ba = band(a), bb = band(b);
            if (ba != bb) return ba < bb;
            const int32_t ka = a.avgZ - (int32_t)a.zBias * zBiasScale;
            const int32_t kb = b.avgZ - (int32_t)b.zBias * zBiasScale;
            return ka > kb; // farther first within a band
        });

    // 4) Flush. Count triangles that actually entered the rasterizer
    // (drawTriangle returned true). Triangles dropped by per-tri checks
    // inside drawTriangle (alpha=0, zero-area, near/far Z, degenerate
    // denom) return false and don't count toward the rasterized total.
    int rasterized = 0;
    for (const auto& t : renderQueue) {
#if MAX_PICK_QUERIES > 0
        renderer->currentPickObject        = t.sourceObject;
        renderer->currentPickTriangleIndex = t.sourceTriangleIndex;
#endif
        if (renderer->drawTriangle(t.v1, t.v2, t.v3, t.material,
                                   directionalLight, ambientLight,
                                   renderEvenLines,
                                   t.ignoreZBuffer, t.noWriteZBuffer,
                                   (int)t.zBias, t.objAlpha)) {
            ++rasterized;
        }
    }
    lastFrameRasterizedTriangles = rasterized;

    // Checkerboard reconstruction: fill in opposite-parity pixels from the
    // freshly-rendered current-parity neighbours so PostFX sees a fully-populated
    // buffer. Done as a separate pass after all triangles are drawn so that
    // painter's algorithm overdraw cannot cause repeated averaging.
    #if defined(CHECKERBOARD_RECONSTRUCTION) && CHECKERBOARD_RECONSTRUCTION
    if (renderer->checkerboardMode) {
        reconstructCheckerboard();
    }
    #endif

    #if POSTFX_ANTIALIASING
    postFX->applyFXAA(framebuffer);
    #endif

    #if POSTFX_BLOOM
    postFX->applyBloom(framebuffer);
    #endif

    #if POSTFX_CRT
    postFX->applyCRT(framebuffer);
    #endif

    #if POSTFX_PIXELATE
    postFX->applyPixelate(framebuffer);
    #endif

    #if POSTFX_CHROMATIC
    postFX->applyChromatic(framebuffer);
    #endif

    #if POSTFX_MOTION_BLUR
    postFX->applyMotionBlur(framebuffer);
    #endif

    frameCounter++;
}

void Scene::getStatistics(int& objectCount, int& triangleCount, int& vertexCount) {
    objectCount = static_cast<int>(objects.size());
    triangleCount = 0;
    vertexCount = 0;

    for (const auto& obj : objects) {
        if (!obj->enabled) continue;
        triangleCount += static_cast<int>(obj->triangles.size());
        vertexCount += static_cast<int>(obj->vertices.size());
    }
}

void PERF_CRITICAL Scene::renderObject(Object* obj,
                                     int32_t camCosX, int32_t camSinX,
                                     int32_t camCosY, int32_t camSinY,
                                     int32_t camCosZ, int32_t camSinZ,
                                     uint8_t objAlpha,
                                     Object* meshSource) {
    // meshSource decouples "which mesh do we rasterise" from "where / how
    // does the object live in the world". Defaults to obj itself, so the
    // non-LOD path is unchanged. When the global LOD system picks a
    // reduced-detail mesh, that Object* is passed in here while obj keeps
    // ownership of the transform, flags, AABB and fade state.
    if (!meshSource) meshSource = obj;

    // Reusable scratch buffers — kept across calls so we don't pay for a
    // heap alloc + copy per object per frame. Renderer is single-threaded
    // (one render task), so plain static is fine here.
    static std::vector<Object::Vertex> transformedVertices;
    static std::vector<Vector3> camSpacePos;
    transformedVertices.assign(meshSource->vertices.begin(), meshSource->vertices.end());
    // Parallel array of camera-space positions (pre-projection). Needed so
    // triangles straddling the near plane can be clipped geometrically —
    // otherwise a single vertex slipping behind the near plane would force
    // the whole triangle to be discarded, leaving a visible hole in the
    // world right under the camera.
    camSpacePos.clear();
    camSpacePos.reserve(transformedVertices.size());

    Vector3 camPos(camera->position);
    #if FLOAT_CAMERA_ANGLES
    Vector3_f camRotF(camera->rotation);
    #endif
    Vector3 objPos(obj->position);

    int32_t fovFactor = camera->fovFactor;
    bool isBillboard = obj->isBillboard;
    CullingMode cullingMode = obj->cullingMode;
    bool ignoreZBuffer = obj->ignoreZBuffer;
    bool noWriteZBuffer = obj->noWriteZBuffer;

    // Hoist object rotation trig out of the per-vertex loop — these are
    // constant for every vertex on the object. Also short-circuit the entire
    // rotation block when the object has zero rotation (true for most static
    // scenery), saving 12 mul + 6 div + 9 add per vertex.
    const bool objHasRotation = !isBillboard &&
        (obj->rotation.x != 0 || obj->rotation.y != 0 || obj->rotation.z != 0);
    int32_t objCosX = 0, objSinX = 0, objCosY = 0, objSinY = 0, objCosZ = 0, objSinZ = 0;
    if (objHasRotation) {
        objCosX = lookupCosI(obj->rotation.x);
        objSinX = lookupSinI(obj->rotation.x);
        objCosY = lookupCosI(obj->rotation.y);
        objSinY = lookupSinI(obj->rotation.y);
        objCosZ = lookupCosI(obj->rotation.z);
        objSinZ = lookupSinI(obj->rotation.z);
    }

    // Transform vertices and normals
    for (auto& vertex : transformedVertices) {
        Vector3 pos(vertex.position);
#if LIGHTING
        Vector3 normal(vertex.normal);
#endif

        // Object rotation
        if (objHasRotation) {
            // X-axis rotation
            pos.assign(pos.x,
                 (pos.y * objCosX - pos.z * objSinX) / FIXED_POINT_SCALE,
                 (pos.y * objSinX + pos.z * objCosX) / FIXED_POINT_SCALE);
#if LIGHTING
            normal.assign(normal.x,
                   (normal.y * objCosX - normal.z * objSinX) / FIXED_POINT_SCALE,
                   (normal.y * objSinX + normal.z * objCosX) / FIXED_POINT_SCALE);
#endif

            // Y-axis rotation
            pos.assign((pos.x * objCosY + pos.z * objSinY) / FIXED_POINT_SCALE,
                  pos.y,
                 (-pos.x * objSinY + pos.z * objCosY) / FIXED_POINT_SCALE);
#if LIGHTING
            normal.assign((normal.x * objCosY + normal.z * objSinY) / FIXED_POINT_SCALE,
                    normal.y,
                   (-normal.x * objSinY + normal.z * objCosY) / FIXED_POINT_SCALE);
#endif

            // Z-axis rotation
            pos.assign((pos.x * objCosZ - pos.y * objSinZ) / FIXED_POINT_SCALE,
                 (pos.x * objSinZ + pos.y * objCosZ) / FIXED_POINT_SCALE,
                  pos.z);
#if LIGHTING
            normal.assign((normal.x * objCosZ - normal.y * objSinZ) / FIXED_POINT_SCALE,
                   (normal.x * objSinZ + normal.y * objCosZ) / FIXED_POINT_SCALE,
                    normal.z);
#endif
        }

        // Translation
        pos.add(objPos);
        pos.add(camPos.inverse());

        // Camera space transformation
        if (!isBillboard) {
            // Apply camera rotation matrix
            Vector3 rotated;
            
            // Apply Y rotation
            rotated.assign((pos.x * camCosY + pos.z * camSinY) / FIXED_POINT_SCALE,
                          pos.y,
                         (-pos.x * camSinY + pos.z * camCosY) / FIXED_POINT_SCALE);
            pos = rotated;

            // Apply X rotation
            rotated.assign(pos.x,
                         (pos.y * camCosX - pos.z * camSinX) / FIXED_POINT_SCALE,
                         (pos.y * camSinX + pos.z * camCosX) / FIXED_POINT_SCALE);
            pos = rotated;

            // Apply Z rotation
            rotated.assign((pos.x * camCosZ - pos.y * camSinZ) / FIXED_POINT_SCALE,
                         (pos.x * camSinZ + pos.y * camCosZ) / FIXED_POINT_SCALE,
                          pos.z);
            pos = rotated;

#if LIGHTING
            // Transform normal using the same rotation matrix. Must apply
            // the rotations in the SAME ORDER as the position transform
            // above (Y -> X -> Z) and as Camera::transformDirection (which
            // transforms the directional light into view space). If the
            // orders disagree, normals and lightDir end up in slightly
            // different frames whenever the camera is both pitched and
            // yawed (i.e. a typical chase camera) and the dot product
            // drifts with camera angle — surfaces appear to change
            // brightness as you turn even though the world hasn't moved.
            Vector3 rotatedNormal;

            // Apply Y rotation to normal
            rotatedNormal.assign((normal.x * camCosY + normal.z * camSinY) / FIXED_POINT_SCALE,
                                normal.y,
                               (-normal.x * camSinY + normal.z * camCosY) / FIXED_POINT_SCALE);
            normal = rotatedNormal;

            // Apply X rotation to normal
            rotatedNormal.assign(normal.x,
                               (normal.y * camCosX - normal.z * camSinX) / FIXED_POINT_SCALE,
                               (normal.y * camSinX + normal.z * camCosX) / FIXED_POINT_SCALE);
            normal = rotatedNormal;

            // Apply Z rotation to normal
            rotatedNormal.assign((normal.x * camCosZ - normal.y * camSinZ) / FIXED_POINT_SCALE,
                               (normal.x * camSinZ + normal.y * camCosZ) / FIXED_POINT_SCALE,
                                normal.z);
            normal = rotatedNormal;
#endif
        }

        // Perspective projection. 64-bit intermediates: cam.x/cam.y can be
        // in the thousands and fovFactor ≈ 1.6e5, so the 32-bit product would
        // overflow for anything sitting close to the camera.
        if (pos.z == 0) pos.z = 1; // avoid divide-by-zero
        // Record camera-space position (pre-projection) for near-plane clipping.
        camSpacePos.push_back(pos);
        int64_t projDenom = (int64_t)pos.z * FIXED_POINT_SCALE;
        if (projDenom == 0) projDenom = 1;
        vertex.position.x = (int32_t)(((int64_t)pos.x * fovFactor) / projDenom) + screenWidth / 2;
        vertex.position.y = screenHeight / 2 - (int32_t)(((int64_t)pos.y * fovFactor) / projDenom);
        vertex.position.z = pos.z;

#if LIGHTING
        // Store transformed normal (only consumed by the lit shading paths).
        vertex.normal.assign(normal);
#endif
    }

#if SORT_TRIANGLES
    // Sort the triangles by depth
    std::sort(meshSource->triangles.begin(), meshSource->triangles.end(), [&](const Object::Triangle& a, const Object::Triangle& b) {
        const auto& v1 = transformedVertices[a.v1];
        const auto& v2 = transformedVertices[a.v2];
        const auto& v3 = transformedVertices[a.v3];
        int32_t z1 = v1.position.z;
        int32_t z2 = v2.position.z;
        int32_t z3 = v3.position.z;
        return (z1 + z2 + z3) / 3 > (transformedVertices[b.v1].position.z + transformedVertices[b.v2].position.z + transformedVertices[b.v3].position.z) / 3;
    });
#endif

    // ------------------------------------------------------------------
    // Near-plane clipping helpers
    // ------------------------------------------------------------------
    // Without these, a triangle with even one vertex behind the near plane
    // is discarded whole (projected x/y would be garbage for that vertex),
    // producing visible holes in geometry right under the camera. We clip
    // straddling triangles to z==nearPlane and project the resulting new
    // vertices fresh, with UVs/normals interpolated along each clipped edge.
    const int32_t nz = camera->nearPlane;

    auto lerpI32 = [](int32_t a, int32_t b, int32_t tFixed) -> int32_t {
        return a + (int32_t)(((int64_t)tFixed * (b - a)) / FIXED_POINT_SCALE);
    };

    // Given a camera-space position, produce an Object::Vertex with screen-
    // space x/y, camera-space z kept in position.z, and the provided normal
    // and uv (both interpolated upstream in camera / texture space).
    // Uses 64-bit intermediates because cam.x * fovFactor can overflow
    // int32_t for vertices generated at the near plane (fovFactor ≈ 1.6e5,
    // cam.x can be in the thousands after clipping).
    auto projectVertex = [&](const Vector3& cam,
                             const Vector3& normalCamSpace,
                             const Vector2& uv) -> Object::Vertex {
        Object::Vertex v;
        int64_t denom = (int64_t)cam.z * FIXED_POINT_SCALE;
        if (denom == 0) denom = 1;
        v.position.x = (int32_t)(((int64_t)cam.x * fovFactor) / denom) + screenWidth / 2;
        v.position.y = screenHeight / 2 - (int32_t)(((int64_t)cam.y * fovFactor) / denom);
        v.position.z = cam.z;
        v.normal = normalCamSpace;
        v.uv = uv;
        return v;
    };

    // Clip edge from A (behind near plane) → B (in front). Returns vertex on
    // the near plane with all attributes interpolated.
    auto clipEdge = [&](const Object::Vertex& A, const Object::Vertex& B,
                        const Vector3& camA, const Vector3& camB) -> Object::Vertex {
        int32_t dz = camB.z - camA.z;
        if (dz == 0) dz = 1;
        int32_t t = (int32_t)(((int64_t)(nz - camA.z) * FIXED_POINT_SCALE) / dz);
        Vector3 camNew(lerpI32(camA.x, camB.x, t),
                       lerpI32(camA.y, camB.y, t),
                       nz);
#if LIGHTING
        Vector3 n(lerpI32(A.normal.x, B.normal.x, t),
                  lerpI32(A.normal.y, B.normal.y, t),
                  lerpI32(A.normal.z, B.normal.z, t));
#else
        Vector3 n(0, 0, 0);
#endif
        Vector2 uv(lerpI32(A.uv.x, B.uv.x, t),
                   lerpI32(A.uv.y, B.uv.y, t));
        return projectVertex(camNew, n, uv);
    };

    // Emit a projected triangle into renderQueue (does screen-bounds + backface
    // cull). Reused by both the fast path and the clipped path.
    auto emitTri = [&](const Object::Vertex& a,
                       const Object::Vertex& b,
                       const Object::Vertex& c,
                       Material* mat
#if MAX_PICK_QUERIES > 0
                       , int32_t srcTriIdx
#endif
                       ) {
        // Per-triangle near/far cull on the average camera-space Z.
        // Object cull rejects entirely-outside boxes; this catches the
        // remaining far-plane tris on objects that straddle it (large
        // ground tiles, big cliff faces). We do this BEFORE the off-
        // screen XY tests + shoelace + queue-push so a doomed tri pays
        // none of those costs (and never enters the painter's-sort).
        // depthFogFar == farPlane in the current build, so this also
        // subsumes the depth-fog alpha=0 early-out that drawTriangle
        // would have done after a full setup.
        const int32_t avgZ = (a.position.z + b.position.z + c.position.z) / 3;
        if (avgZ > camera->farPlane || avgZ < camera->nearPlane) return;

        if (a.position.x < 0 && b.position.x < 0 && c.position.x < 0) return;
        if (a.position.x > screenWidth && b.position.x > screenWidth && c.position.x > screenWidth) return;
        if (a.position.y < 0 && b.position.y < 0 && c.position.y < 0) return;
        if (a.position.y > screenHeight && b.position.y > screenHeight && c.position.y > screenHeight) return;

        // 64-bit shoelace: projected coords from a near-plane-clipped vertex
        // can be tens of thousands of units, which would overflow a 32-bit
        // signed product and accidentally flip the backface-cull sign. That
        // was causing large floor triangles near the camera to vanish
        // (typically in the bottom-left quadrant where cam.x/cam.y are most
        // negative).
        int64_t shoelaceArea = (int64_t)a.position.x * (b.position.y - c.position.y) +
                               (int64_t)b.position.x * (c.position.y - a.position.y) +
                               (int64_t)c.position.x * (a.position.y - b.position.y);

        bool shouldCull = false;
        switch (cullingMode) {
            case CullingMode::CULL_BACKFACES: shouldCull = (shoelaceArea <= 0); break;
            case CullingMode::CULL_FRONTFACES: shouldCull = (shoelaceArea >= 0); break;
            case CullingMode::NO_CULLING: break;
        }
        if (shouldCull) return;

        RenderTri rt;
        if (cullingMode == CullingMode::NO_CULLING && shoelaceArea < 0) {
            rt.v1 = c; rt.v2 = b; rt.v3 = a;
        } else {
            rt.v1 = a; rt.v2 = b; rt.v3 = c;
        }
        rt.material       = mat;
        rt.ignoreZBuffer  = ignoreZBuffer;
        rt.noWriteZBuffer = noWriteZBuffer;
        rt.zBias          = obj->zBias;
        rt.objAlpha       = objAlpha;
        rt.avgZ           = avgZ;
#if MAX_PICK_QUERIES > 0
        rt.sourceObject        = obj;
        rt.sourceTriangleIndex = srcTriIdx;
#endif
        renderQueue.push_back(rt);
    };

    // Render triangles with backface culling and shading
    for (size_t triIdx = 0; triIdx < meshSource->triangles.size(); ++triIdx) {
        const auto& triangle = meshSource->triangles[triIdx];
        const auto& vA = transformedVertices[triangle.v1];
        const auto& vB = transformedVertices[triangle.v2];
        const auto& vC = transformedVertices[triangle.v3];
        const Vector3& cA = camSpacePos[triangle.v1];
        const Vector3& cB = camSpacePos[triangle.v2];
        const Vector3& cC = camSpacePos[triangle.v3];

        // Classify each vertex against the near plane.
        const int outMask = (cA.z < nz ? 1 : 0)
                          | (cB.z < nz ? 2 : 0)
                          | (cC.z < nz ? 4 : 0);

        if (outMask == 7) continue;               // fully behind near plane

#if MAX_PICK_QUERIES > 0
        const int32_t srcTriIdx = (int32_t)triIdx;
        #define JET_EMIT_TRI(A, B, C, M)  emitTri((A), (B), (C), (M), srcTriIdx)
#else
        #define JET_EMIT_TRI(A, B, C, M)  emitTri((A), (B), (C), (M))
#endif

        if (outMask == 0) {                       // fast path: fully in front
            JET_EMIT_TRI(vA, vB, vC, triangle.material);
            continue;
        }

        // Straddling near plane — produce a clipped polygon (3 or 4 verts)
        // while preserving winding order of the original triangle.
        const Object::Vertex* vs[3]  = { &vA, &vB, &vC };
        const Vector3*        cvs[3] = { &cA, &cB, &cC };
        const bool in[3] = { (outMask & 1) == 0,
                             (outMask & 2) == 0,
                             (outMask & 4) == 0 };

        Object::Vertex poly[4];
        int polyN = 0;
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            if (in[i]) poly[polyN++] = *vs[i];
            if (in[i] != in[j]) {
                // One endpoint in, one out — add the near-plane intersection.
                if (in[i])
                    poly[polyN++] = clipEdge(*vs[j], *vs[i], *cvs[j], *cvs[i]);
                else
                    poly[polyN++] = clipEdge(*vs[i], *vs[j], *cvs[i], *cvs[j]);
            }
        }

        if (polyN >= 3) JET_EMIT_TRI(poly[0], poly[1], poly[2], triangle.material);
        if (polyN == 4) JET_EMIT_TRI(poly[0], poly[2], poly[3], triangle.material);
        #undef JET_EMIT_TRI
    }
}

} // namespace Renderer
