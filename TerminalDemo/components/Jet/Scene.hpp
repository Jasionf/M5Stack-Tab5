#ifndef SCENE_HPP
#define SCENE_HPP

#include <vector>
#include "Object.hpp"
#include "Camera.hpp"
#include "Light.hpp"
#include "Renderer.hpp"
#include "Config.hpp"
#include "PostFX.hpp"

namespace Renderer {

/// @brief Top-level container that owns the scene graph and drives rendering.
///
/// Holds the active camera, lights, object list and per-frame state, and
/// exposes a single `render()` entry point that runs the full
/// transform/cull/raster/post-FX pipeline.
class Scene {
public:
    /// @brief Construct a scene bound to caller-owned framebuffers.
    /// @param framebuffer RGB565 colour buffer of size screenWidth*screenHeight.
    /// @param zBuffer Depth buffer of size ZBUFFER_STRIDE(screenWidth)*screenHeight; pass nullptr when Z_BUFFERING is disabled.
    /// @param screenWidth Output width in pixels.
    /// @param screenHeight Output height in pixels.
    Scene(uint16_t* framebuffer, uint16_t* zBuffer, int screenWidth, int screenHeight);
    ~Scene();

    int frameCounter = 0;       ///< Incremented once per render(); useful for animations and dither parity.

    /// @name Per-frame counters populated by render()
    /// @{
    /// `lastFrameDrawnObjects` is the number of enabled objects that
    /// survived the AABB frustum cull. `lastFrameDrawnTriangles` is the
    /// number of triangles submitted to the rasteriser (renderQueue size
    /// after all culling). `lastFrameRasterizedTriangles` is the subset of
    /// those that produced rasterizer work (drawTriangle returned true).
    int lastFrameDrawnObjects        = 0;
    int lastFrameDrawnTriangles      = 0;
    int lastFrameRasterizedTriangles = 0;
    /// @}

    /// @brief Add an object to the scene.
    /// @param obj Object to add. Pointer is borrowed; caller retains ownership.
    void addObject(Object* obj);

    /// @brief Add a point light to the scene.
    /// @param light Light to add. Pointer is borrowed; caller retains ownership.
    void addPointLight(PointLight* light);

    /// @brief Set the active camera.
    /// @param cam Camera pointer (borrowed).
    void setCamera(Camera* cam);
    /// @brief Get the active camera.
    /// @return Pointer to the current camera, or nullptr if none is set.
    Camera* getCamera() { return camera; }

    /// @brief Set the active directional light.
    /// @param light Directional light (borrowed). May be nullptr.
    void setDirectionalLight(DirectionalLight* light);
    /// @brief Get the active directional light.
    DirectionalLight* getDirectionalLight() { return directionalLight; }

    /// @brief Set the active ambient light.
    /// @param light Ambient light (borrowed). May be nullptr.
    void setAmbientLight(AmbientLight* light);
    /// @brief Get the active ambient light.
    AmbientLight* getAmbientLight() { return ambientLight; }

    /// @brief Run the full pipeline for one frame: cull, transform, rasterise, post-FX.
    void render();

    /// @brief Get total scene statistics (independent of camera position).
    /// @param objectCount Out: number of enabled objects.
    /// @param triangleCount Out: total triangle count across enabled objects.
    /// @param vertexCount Out: total vertex count across enabled objects.
    void getStatistics(int& objectCount, int& triangleCount, int& vertexCount);

    /// @brief Set the colour used to clear the framebuffer.
    /// @param color RGB565 clear colour.
    void setBackcolor(uint16_t color) { backcolor = color; }

    /// @brief Replace the colour buffer pointer (without changing dimensions).
    /// @param framebuffer New caller-owned RGB565 buffer.
    void setFramebuffer(uint16_t *framebuffer);

    /// @brief Enable or disable per-frame framebuffer clearing.
    /// @param clear True to clear before rendering, false to preserve previous content.
    void setClearBuffer(bool clear) { clearRenderBuffer = clear; }

    /// @brief Hot-swap framebuffer, z-buffer and dimensions (e.g. on window resize).
    ///
    /// The caller owns both buffers and is responsible for freeing the old
    /// ones AFTER this call returns. PostFX is recreated internally to
    /// pick up the new dimensions.
    /// @param newFramebuffer New caller-owned colour buffer.
    /// @param newZBuffer New caller-owned depth buffer.
    /// @param newWidth New width in pixels.
    /// @param newHeight New height in pixels.
    void resize(uint16_t* newFramebuffer, uint16_t* newZBuffer,
                int newWidth, int newHeight);

    /// @brief Get the underlying rasteriser.
    Rasterizer* getRenderer() { return renderer; }
    /// @brief Get the mutable list of scene objects.
    std::vector<Object*>& getObjects() { return objects; }
    /// @brief Get the mutable list of point lights.
    std::vector<PointLight*>& getPointLights() { return pointLights; }
    /// @brief Get the mutable list of materials owned by the scene.
    std::vector<Material*>& getMaterials() { return materials; }

#if MAX_PICK_QUERIES > 0
    /// @brief Submit screen-space pick points to be tested during the next render().
    ///
    /// Excess queries beyond MAX_PICK_QUERIES are silently dropped. Pass
    /// count == 0 (or just don't call this) to disable picking. The renderer
    /// reads queries during render() and writes the matching slots in the
    /// pick result array. Both arrays are owned by Scene; the host should
    /// copy queries in by value and read results back after render().
    /// @param queries Caller-owned array of pick queries.
    /// @param count Number of valid entries in @p queries.
    void setPickQueries(const PickQuery* queries, int count);

    /// @brief Get the pick results from the most recent render() call.
    /// @return Pointer to an internal array of MAX_PICK_QUERIES results.
    const PickResult* getPickResults() const { return pickResults; }

    /// @brief Get the number of active pick queries set for the next render().
    int getPickQueryCount() const { return pickQueryCount; }
#endif


    uint16_t* backgroundGradientColors = nullptr;   ///< Optional per-row background gradient (screenHeight entries) used during clear.

    /// @name Distance-based level of detail (LOD)
    /// @brief Global LOD selection driven by camera-to-object distance.
    ///
    /// `lodScale` is the world-units-per-LOD-step. Setting it to 0 (the
    /// default) disables global LOD and every Object renders its own
    /// mesh as before. With `lodScale = 4096`, an Object with two LOD
    /// meshes attached renders LOD 0 below 4096 units, LOD 1 from 4096
    /// to 8191, LOD 2 from 8192 onward (or fades out / persists past
    /// the last LOD depending on the Object's `lodPersist` flag).
    ///
    /// `lodBias` is added to the computed level globally — a bias of -1
    /// pushes everything one LOD step higher in detail, +1 cheaper. This
    /// composes with the per-Object `lodBias` and is intended for runtime
    /// quality knobs (e.g. perf-driven dynamic adjustment).
    /// @{
    int32_t lodScale = 0;   ///< World units per LOD step; 0 disables global LOD.
    int8_t  lodBias  = 0;   ///< Scene-wide LOD level offset (added to each object's choice).
    /// @}

private:
    struct RenderTri {
        Object::Vertex v1, v2, v3;
        Material* material;
        int32_t avgZ;
        bool ignoreZBuffer;
        bool noWriteZBuffer;
        int8_t zBias;
        // Per-object alpha multiplier (255 = no per-object fade); folded
        // into the per-pixel screen-door alpha at raster time.
        uint8_t objAlpha;
#if MAX_PICK_QUERIES > 0
        // Source object + ORIGINAL triangle index (in obj->triangles) for
        // pick attribution. Carried through the painter sort.
        Object* sourceObject;
        int32_t sourceTriangleIndex;
#endif
    };
    std::vector<RenderTri> renderQueue;

    Camera* camera;
    DirectionalLight* directionalLight;
    AmbientLight* ambientLight;
    Rasterizer* renderer = nullptr;
    PostFX* postFX = nullptr;

    uint16_t* framebuffer;
    uint16_t* zBuffer;
    int screenWidth;
    int screenHeight;
    bool* scanlinesUpdated;

    std::vector<Object*> objects;
    std::vector<PointLight*> pointLights;
    std::vector<Material*> materials;

    uint16_t backcolor = 0;
    bool clearRenderBuffer = true;
    bool renderEvenLines = false;

    bool cullObject(Object* obj,
                    int32_t camCosX, int32_t camSinX,
                    int32_t camCosY, int32_t camSinY,
                    int32_t camCosZ, int32_t camSinZ) const;

    void renderObject(Object* obj,
                      int32_t camCosX, int32_t camSinX,
                      int32_t camCosY, int32_t camSinY,
                      int32_t camCosZ, int32_t camSinZ,
                      uint8_t objAlpha,
                      Object* meshSource = nullptr);
    void reconstructCheckerboard();
    void clearBuffers();

#if MAX_PICK_QUERIES > 0
    PickQuery  pickQueries[MAX_PICK_QUERIES];
    PickResult pickResults[MAX_PICK_QUERIES];
    int        pickQueryCount = 0;
#endif
};

} // namespace Renderer

#endif // SCENE_HPP
