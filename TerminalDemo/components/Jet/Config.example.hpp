// Jet configuration EXAMPLE / TEMPLATE.
//
// Each frontend (firmware / desktop / etc.) is expected to keep its OWN copy
// of this file as `Config.hpp`, sitting somewhere on the include path so the
// Jet headers resolve `#include "Config.hpp"` to the per-frontend version.
// This way the renderer can be tuned per platform without forking the
// library or relying on global build flags.
//
// Existing frontend copies in this repo:
//   * ESP32 firmware : main/Config.hpp
//   * SDL3 desktop   : desktop/Config.hpp
//
// To start a new frontend, copy this file to <frontend>/Config.hpp and add
// that directory to the Jet target's include path (see the existing
// CMakeLists.txt files for the pattern).

// Jet configuration file

// RENDER_TILE_BUFFER: Exposes information about what tiles are being rendered for each frame. Can be used to implement tile-based display output.
#define RENDER_TILE_BUFFER 0
#define TILE_WIDTH 32
#define TILE_HEIGHT 32
// FAST_Z: Calculate Z per triangle instead of per pixel by taking the average.
#define FAST_Z 1
// LAZY_Z: Use the max Z value of the triangle instead of the average. Requires FAST_Z to be enabled
#define LAZY_Z 0

// SCREEN_DOOR_ALPHA: Enable stippling based on alpha value. Minimal performance impact.
#define SCREEN_DOOR_ALPHA 1
// SKIP_ZERO_AREA_TRIANGLES: Skip rendering triangles with no area (e.g. a line). Can improve performance.
#define SKIP_ZERO_AREA_TRIANGLES 1
// NOISE_ALPHA: Enable dithering based on alpha value. Only looks decent at high frame rates.
#define NOISE_ALPHA 0
// Z_BUFFERING: Enable Z-buffering.
#define Z_BUFFERING 0
// SORT_TRIANGLES: Sort triangles by depth before rendering, recommended if not using Z-buffering
#define SORT_TRIANGLES 1
// SORT_SCENE_OBJECTS: Sort objects by distance to camera before rendering, recommended if using Z-buffering
#define SORT_SCENE_OBJECTS 0
// SORT_SCENE_REVERSE: Sorts objects in reverse order (farthest first, painters algorithm) - only used if SORT_SCENE_OBJECTS is enabled
#define SORT_SCENE_REVERSE 0
// DEPTH_ALPHA_BLEND: A pseudo-fog effect based on distance - requires SCREEN_DOOR_ALPHA.
#define DEPTH_ALPHA_BLEND 1
// TEXTURE_MAPPING: Enable texture mapping (affine by default - fast, but can cause warping)
#define TEXTURE_MAPPING 0
// PERSPECTIVE_CORRECT_TEXTURES: Enable perspective-correct texture mapping (slower, more accurate)
#define PERSPECTIVE_CORRECT_TEXTURES 0
// BILINEAR_FILTER: Enable bilinear filtering for textures (slower)
#define BILINEAR_FILTER 0
// LIGHTING: Enable ambient and directional lighting
#define LIGHTING 0
// Z_BRIGHTNESS: Make distant objects darker - if FAST_Z is enabled, can cause warping unless LAZY_Z is also enabled
#define Z_BRIGHTNESS 0
// FLOAT_CAMERA_ANGLES: Use floating point values for the camera rotation
#define FLOAT_CAMERA_ANGLES 1
// FLOAT_SIN_CACHE_SCALE: Scale for sin/cos lookup (10 = 1dp, 100 = 2dp, etc.) - in practice setting this to 10 is sufficient.
#define FLOAT_SIN_CACHE_SCALE 10
// FLOAT_TAN_CACHE_SCALE: Scale for tan lookup (1 = use integer table, 10 = 1dp, etc.) - in practice setting this to 1 is sufficient.
#define FLOAT_TAN_CACHE_SCALE 1

// Post-processing effects (No extra buffer required)
#define POSTFX_CRT 0         // CRT scanline effect
#define POSTFX_CELLSHADING 0 // Cell shading effect

// Post-processing effects (Extra buffer required, generally not usable on ESP32 due to memory constraints)
#define POSTFX_ANTIALIASING 0 // FXAA anti-aliasing
#define POSTFX_BLOOM 0       // Bloom effect (requires additional buffer)
#define POSTFX_MOTION_BLUR 0 // Motion blur effect (requires additional buffer)
#define POSTFX_CHROMATIC 0   // Chromatic aberration
#define POSTFX_PIXELATE 0    // Pixelation effect

// Debug visualization options
#define DEBUG_OVERDRAW 0     // Visualize overdraw using orange blending

// Effect parameters
#define CRT_SCANLINE_INTENSITY 48    // Intensity of CRT scanlines (0-255)
#define MOTION_BLUR_STRENGTH 50      // Strength of motion blur (0-100)
#define CHROMATIC_OFFSET 2           // Pixel offset for chromatic aberration
#define PIXELATE_SIZE 4              // Size of pixelation blocks
#define CELLSHADING_CELL_BITS 4      // Reduce precision of lighting to create a cell shading effect (0-8)

// Brightness settings - used for Z_BRIGHTNESS
#define zBrightFar (1600 * 8)
#define zBrightNear (200  * 8)
#define zBrightScale 48

// Depth fog settings - used for DEPTH_ALPHA_BLEND
#define depthFogFar  (8192 * 8)
#define depthFogNear (6144 * 8)

// HALF_WIDTH_BUFFERS: Use a half-width buffer with half the horizontal resolution.
#define HALF_WIDTH_BUFFERS 1

// FIELD_BUFFERS: Split the framebuffer into two half-height buffers — one for
// even screen rows, one for odd. Each frame, the rasterizer writes into
// whichever matches the current interlaced field, while display DMA reads the
// OTHER one. Eliminates CPU/DMA SRAM bank contention and lets render + display
// truly run in parallel. Requires interlacedMode = true. Host code is
// responsible for allocating two buffers and calling setFramebuffer() with the
// active one each frame.
#define FIELD_BUFFERS 1

// CHECKERBOARD_MODE: Enable the checkerboard rendering mode on the desktop.
// Each frame renders only half the pixels in an alternating checker pattern;
// the SDL frontend reconstructs the missing pixels from the previous frame.
// Requires double-buffering (an extra prevFrame buffer allocated in main_sdl.cpp).
// Set to 0 to render every pixel every frame (default full-quality mode).
#define CHECKERBOARD_MODE 0

// CHECKERBOARD_RECONSTRUCTION: When CHECKERBOARD_MODE is enabled, this controls 
// whether the renderer reconstructs pixels that were not drawn in the current frame
// from the previous frame's buffer.
#define CHECKERBOARD_RECONSTRUCTION 0

// MAX_PICK_QUERIES: Compile-time upper bound on the number of
// simultaneous screen-space picks the renderer will service per frame.
// Set to 0 to compile out ALL picking machinery (zero cost — the
// per-triangle scanline pick test, the per-RenderTri object/triangle
// tags, and the Scene/Rasterizer pick state all disappear). When > 0,
// host code may call Scene::setPickQueries() with up to this many
// (x, y) screen-space points; after Scene::render() returns, the
// matching slots in Scene::getPickResults() report whether anything
// was rendered at that pixel, the closest object, and the index of the
// triangle that covered it. When FIELD_BUFFERS is on the queried Y is
// snapped to a row that this frame's interlaced field actually drew,
// since the other rows belong to the previous frame's buffer.
#define MAX_PICK_QUERIES 0

// Define ESP32 only when actually building for an Espressif target. Detected
// via ESP_PLATFORM (set by the ESP-IDF build system) so desktop builds (e.g.
// the SDL3 frontend) don't pull in esp_attr.h / IRAM_ATTR.
#if defined(ESP_PLATFORM)
#define ESP32
#endif
