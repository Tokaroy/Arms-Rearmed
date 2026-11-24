// AMD FSR 3 through the FidelityFX API (fsr3.h). The library is AMD's signed Vulkan build, loaded at run time: the
// zip has the two DLLs (and their license in THIRD_PARTY.txt); without them the setting says FSR 3 is unavailable.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include "vk.h"
#include "record.h"
#include "../../rt.h"
}
#include "fsr3.h"

#include "ffx_api.h"
#include "ffx_upscale.h"
#include "vk/ffx_api_vk.h"

static PfnFfxCreateContext p_create;
static PfnFfxDestroyContext p_destroy;
static PfnFfxConfigure p_configure;
static PfnFfxDispatch p_dispatch;
static PfnFfxQuery p_query;
static ffxContext g_ctxs[FSR3_SLOTS];  // a context per view: each keeps its own history
static uint32_t g_ctx_ws[FSR3_SLOTS], g_ctx_hs[FSR3_SLOTS];

static void ffx_message(uint32_t type, const wchar_t *msg) {
    static int n;
    if (n++ > 50) return;
    char buf[512];
    size_t i = 0;
    for (; msg && msg[i] && i < sizeof buf - 1; i++) buf[i] = msg[i] < 128 ? (char)msg[i] : '?';
    buf[i] = 0;
    LOG("[fsr3] %s: %s\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", buf);
}

extern "C" bool fsr3_load(void) {
    static int state = -1;  // (-1 not tried, 0 no, 1 yes)
    if (state >= 0) return state == 1;
    state = 0;
#ifdef _WIN32
    HMODULE m = LoadLibraryA("amd_fidelityfx_loader_vk.dll");
    if (!m) { LOG("[fsr3] amd_fidelityfx_loader_vk.dll not found next to the game: FSR 3 unavailable\n"); return false; }
    p_create = (PfnFfxCreateContext)(void *)GetProcAddress(m, "ffxCreateContext");
    p_destroy = (PfnFfxDestroyContext)(void *)GetProcAddress(m, "ffxDestroyContext");
    p_configure = (PfnFfxConfigure)(void *)GetProcAddress(m, "ffxConfigure");
    p_dispatch = (PfnFfxDispatch)(void *)GetProcAddress(m, "ffxDispatch");
    p_query = (PfnFfxQuery)(void *)GetProcAddress(m, "ffxQuery");
    if (!p_create || !p_destroy || !p_dispatch || !p_query) { LOG("[fsr3] the loader lacks the API's functions: FSR 3 unavailable\n"); return false; }
    state = 1;
    LOG("[fsr3] AMD FidelityFX loader found\n");
    return true;
#else
    return false;
#endif
}

// One context for every size up to the largest seen (the scene's size changes between menus and fights: a context each
// time stalled for the GPU): the dispatch passes its frame's size. A larger one makes it again, that large.
extern "C" bool fsr3_context(int slot, uint32_t w, uint32_t h) {
    if (!fsr3_load() || slot < 0 || slot >= FSR3_SLOTS) return false;
    ffxContext &g_ctx = g_ctxs[slot];
    uint32_t &g_ctx_w = g_ctx_ws[slot], &g_ctx_h = g_ctx_hs[slot];
    if (g_ctx && w <= g_ctx_w && h <= g_ctx_h) return true;
    if (g_ctx) {  // (larger: the old context's work must be done first)
        if (w < g_ctx_w) w = g_ctx_w;
        if (h < g_ctx_h) h = g_ctx_h;
        rec_drain();
        vkDeviceWaitIdle(g_vk.dev);
        p_destroy(&g_ctx, NULL);
        g_ctx = NULL;
    }
    ffxCreateBackendVKDesc be = {};
    be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    be.deviceContext.vkDevice = g_vk.dev;
    be.deviceContext.vkPhysicalDevice = g_vk.phys;
    be.deviceContext.vkDeviceProcAddr = vkGetDeviceProcAddr;
    ffxCreateContextDescUpscale cd = {};
    cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    cd.header.pNext = &be.header;
    cd.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INFINITE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
               FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;  // (sizes below the context's)
    cd.maxRenderSize = {w, h};
    cd.maxUpscaleSize = {w, h};
    cd.fpMessage = ffx_message;
    if (getenv("LARECOMP_FSR_DEBUG_VIEW")) cd.flags |= FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION;  // (test: FSR's own debug view)
    ffxReturnCode_t r = p_create(&g_ctx, &cd.header, NULL);
    if (r != FFX_API_RETURN_OK) {
        LOG("[fsr3] context for %ux%u failed (%u)\n", w, h, r);
        g_ctx = NULL;
        return false;
    }
    g_ctx_w = w, g_ctx_h = h;
    {   // which version the loader picked
        ffxQueryGetProviderVersion q = {};
        q.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (p_query(&g_ctx, &q.header) == FFX_API_RETURN_OK)
            LOG("[fsr3] context %d: %ux%u (native AA): %s\n", slot + 1, w, h, q.versionName ? q.versionName : "?");
        else
            LOG("[fsr3] context %d: %ux%u (native AA)\n", slot + 1, w, h);
    }
    return true;
}

static FfxApiResource res(VkImage img, uint32_t fmt, uint32_t w, uint32_t h, uint32_t usage) {
    FfxApiResource r = {};
    r.resource = (void *)img;
    r.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    r.description.format = fmt;
    r.description.width = w;
    r.description.height = h;
    r.description.depth = 1;
    r.description.mipCount = 1;
    r.description.usage = usage;
    r.state = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;  // (every image here is in GENERAL)
    return r;
}

extern "C" void fsr3_dispatch(VkCommandBuffer cb, const void *arg) {
    const Fsr3Args *a = (const Fsr3Args *)arg;
    if (a->slot < 0 || a->slot >= FSR3_SLOTS) return;
    ffxContext &g_ctx = g_ctxs[a->slot];
    if (!g_ctx || a->w > g_ctx_ws[a->slot] || a->h > g_ctx_hs[a->slot]) return;
    ffxDispatchDescUpscale d = {};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = (void *)cb;
    d.color = res(a->color, a->color_ffx_fmt, a->w, a->h, FFX_API_RESOURCE_USAGE_READ_ONLY);
    d.depth = res(a->depth, FFX_API_SURFACE_FORMAT_R32_FLOAT, a->w, a->h, FFX_API_RESOURCE_USAGE_READ_ONLY);
    d.motionVectors = res(a->mv, FFX_API_SURFACE_FORMAT_R16G16_FLOAT, a->w, a->h, FFX_API_RESOURCE_USAGE_READ_ONLY);
    if (a->reactive) d.reactive = res(a->reactive, FFX_API_SURFACE_FORMAT_R8_UNORM, a->w, a->h, FFX_API_RESOURCE_USAGE_READ_ONLY);
    d.output = res(a->out, FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT, a->w, a->h, FFX_API_RESOURCE_USAGE_UAV);
    d.jitterOffset = {a->jitter_x, a->jitter_y};
    d.motionVectorScale = {(float)a->w, (float)a->h};  // (UV-space motion, in the images' uv)
    d.renderSize = {a->rw, a->rh};   // (the images' top-left part: Dynamic resolution draws there)
    d.upscaleSize = {a->rw, a->rh};
    d.enableSharpening = a->sharpen != 0;
    d.sharpness = a->sharpness;
    d.frameTimeDelta = a->dt_ms;
    d.preExposure = 1.0f;
    d.reset = a->reset != 0;
    d.cameraNear = a->near_plane;
    d.cameraFar = a->far_plane;
    d.cameraFovAngleVertical = a->fov_y;
    d.viewSpaceToMetersFactor = 1.0f;
    if (getenv("LARECOMP_FSR_DEBUG_VIEW")) d.flags |= FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;
    ffxReturnCode_t r = p_dispatch(&g_ctx, &d.header);
    static int nerr;
    if (r != FFX_API_RETURN_OK && nerr++ < 10) LOG("[fsr3] dispatch failed (%u)\n", r);
}

static float halton(uint64_t i, int b) {
    float f = 1, r = 0;
    for (; i; i /= (uint64_t)b) { f /= (float)b; r += f * (float)(i % (uint64_t)b); }
    return r;
}
extern "C" void fsr3_jitter(uint64_t n, float *x, float *y) {
    uint64_t i = n % 8 + 1;  // (FSR's phase count for native AA: 8 x ratio^2)
    *x = halton(i, 2) - 0.5f;
    *y = halton(i, 3) - 0.5f;
}

extern "C" uint32_t fsr3_format_of(VkFormat f) {
    switch (f) {
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT;
    default: return 0;
    }
}
