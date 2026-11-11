/* Exercise the actual conversion functions in the exact supported bridge.
 * Build: cc -O2 -Wall -Wextra -rdynamic -o /tmp/ttw-xr-layers \
 *          tests/openxr_layer_conversion.c -ldl
 * Run only against the hash-checked original or its two-byte patched copy.
 * This test does not initialize Wine, OpenXR, a compositor, or a headset.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <openxr/openxr.h>

/* Unused Wine imports required for dlopen; conversion must not call them. */
void NtQueryPerformanceCounter(void) { abort(); }
void __wine_dbg_header(void) { abort(); }
void __wine_dbg_output(void) { abort(); }
void __wine_dbg_strdup(void) { abort(); }

struct pool { char buffer[2048]; uint32_t used; void *next, *prev; };
struct projection32 {
    int32_t type; uint32_t next; uint64_t flags, space;
    uint32_t view_count, views;
};
struct quad32 {
    int32_t type; uint32_t next; uint64_t flags, space;
    int32_t eye; XrSwapchainSubImage image; XrPosef pose; XrExtent2Df size;
};
typedef const XrCompositionLayerBaseHeader *const *(*convert_fn)(
    struct pool *, const void *, uint32_t);

static int check(void *library, unsigned bits)
{
    struct link_map *map = NULL;
    assert(dlinfo(library, RTLD_DI_LINKMAP, &map) == 0);
    convert_fn convert = (convert_fn)(map->l_addr + (bits == 32 ? 0x4e480 : 0x528f0));
    struct pool pool = {0};
    pool.next = pool.prev = &pool.next;
    void *low = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(low != MAP_FAILED && (uintptr_t)low < UINT32_MAX - 4096);
    uint32_t *pointers32 = low;
    struct projection32 *p32 = (void *)((char *)low + 64);
    struct quad32 *q32 = (void *)((char *)low + 128);
    /* wine_swapchain_from_handle reads the native handle in its first word. */
    uint64_t *swapchain = (void *)((char *)low + 512);
    *swapchain = 0x1234;
    XrCompositionLayerProjection p64 = {.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad q64[2] = {0};
    p32->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    pointers32[0] = (uintptr_t)p32;
    const void *pointers64[] = {&p64, &q64[0], &q64[1]};
    for (unsigned i = 0; i < 2; ++i) {
        q32[i].type = q64[i].type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        q32[i].image.swapchain = q64[i].subImage.swapchain = (XrSwapchain)swapchain;
        q32[i].pose.orientation.w = q64[i].pose.orientation.w = 1.0f;
        q32[i].pose.position.x = q64[i].pose.position.x = 0.25f + i;
        q32[i].size.width = q64[i].size.width = 0.5f + i;
        q32[i].size.height = q64[i].size.height = 0.75f + i;
        pointers32[i + 1] = (uintptr_t)&q32[i];
    }
    const XrCompositionLayerBaseHeader *const *out =
        convert(&pool, bits == 32 ? (const void *)pointers32 : (const void *)pointers64, 3);
    int failures = 0;
    for (unsigned i = 0; i < 2; ++i) {
        const XrCompositionLayerQuad *quad = (const void *)out[i + 1];
        int valid = quad->type == XR_TYPE_COMPOSITION_LAYER_QUAD &&
            !memcmp(&quad->pose, &q64[i].pose, sizeof(quad->pose)) &&
            !memcmp(&quad->size, &q64[i].size, sizeof(quad->size));
        printf("%u-bit quad %u: %s (w=%g, x=%g, width=%g)\n", bits, i,
               valid ? "PASS" : "CORRUPTED", quad->pose.orientation.w,
               quad->pose.position.x, quad->size.width);
        failures += !valid;
    }
    assert(pool.used <= sizeof(pool.buffer) && pool.next == &pool.next);
    munmap(low, 4096);
    return failures;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    int failures = check(library, 32) + check(library, 64);
    dlclose(library);
    return failures ? 1 : 0;
}
