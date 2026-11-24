// AMD FSR 3 (FidelityFX upscaler, the signed Vulkan DLLs next to the game: amd_fidelityfx_loader_vk.dll and
// amd_fidelityfx_upscaler_vk.dll, loaded when first asked for). The game's 3D scene is jittered and given motion
// vectors (exec.c fsr_*); this file is the library's side. Native AA: input and output the same size.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "vk.h"

#ifdef __cplusplus
extern "C" {
#endif

// What one dispatch needs, by value (it travels in a recorder packet: rec_CmdCall)
typedef struct Fsr3Args {
    // the scene's HDR colour (B10G11R11), depth (R32F), motion (R16G16F), output (R16G16B16A16F), reactive mask (R8;
    // VK_NULL_HANDLE: none)
    VkImage color, depth, mv, out, reactive;
    uint32_t color_ffx_fmt;          // FFX_API_SURFACE_FORMAT_* of the colour image
    uint32_t w, h;                   // the images' size
    uint32_t rw, rh;                 // the part drawn and anti-aliased (render size = output size; Dynamic resolution: less)
    float jitter_x, jitter_y;        // pixels
    float near_plane, far_plane, fov_y;
    float dt_ms;
    int reset;                       // the history is no good (first frame, a cut, a new size)
    int slot;                        // the view (its own context and history), 0..FSR3_SLOTS-1
    int sharpen;
    float sharpness;
} Fsr3Args;

// The DLLs found and the API's functions there (once; false: FSR 3 can't be used - the log says why)
bool fsr3_load(void);
// View slot's context, for sizes up to w x h (made again, larger, when a larger size comes: waits for the GPU). The
// translating thread.
#define FSR3_SLOTS 4
bool fsr3_context(int slot, uint32_t w, uint32_t h);
// The dispatch, on the recorder thread (rec_CmdCall's function: arg is a Fsr3Args)
void fsr3_dispatch(VkCommandBuffer cb, const void *arg);
// Frame n's sub-pixel jitter, in pixels within [-0.5, 0.5] (Halton 2, 3; FSR's phase count for native: 8)
void fsr3_jitter(uint64_t n, float *x, float *y);
// FSR's surface format for a colour image's Vulkan format (0: not one taken here)
uint32_t fsr3_format_of(VkFormat f);

#ifdef __cplusplus
}
#endif
