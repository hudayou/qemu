/* Copyright (C) 2011 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include <stdint.h>
#include "hw/android/opengles.h"
#include <assert.h>

#include "ui/console.h"

int  android_gles_fast_pipes = 1;

static void android_opengles_update(void *opaque);

typedef struct {
    QemuConsole *console;
#if 0
    bool invalidate;
#endif
} OpenglesState;

static const GraphicHwOps android_opengles_ops = {
#if 0
    .invalidate  = android_opengles_invalidate,
#endif
    .gfx_update  = android_opengles_update,
};

/* HOST_LONG_BITS is the size of a native pointer in bits. */
#if UINTPTR_MAX == UINT32_MAX
# define HOST_LONG_BITS 32
#elif UINTPTR_MAX == UINT64_MAX
# define HOST_LONG_BITS 64
#else
# error Unknown pointer size
#endif

#define CONFIG_ANDROID_OPENGLES 1

#ifdef CONFIG_ANDROID_OPENGLES

#include "hw/android/utils/debug.h"
#include "hw/android/utils/path.h"
#include "hw/android/utils/bufprint.h"
#include "hw/android/utils/dll.h"

#define RENDER_API_NO_PROTOTYPES 1
#include "libOpenglRender/render_api.h"

#include <stdio.h>
#include <stdlib.h>

#define D(...)  VERBOSE_PRINT(init,__VA_ARGS__)
#define DD(...) VERBOSE_PRINT(gles,__VA_ARGS__)

/* Name of the GLES rendering library we're going to use */
#if HOST_LONG_BITS == 32
#define RENDERER_LIB_NAME  "libOpenglRender"
#elif HOST_LONG_BITS == 64
#define RENDERER_LIB_NAME  "lib64OpenglRender"
#else
#error Unknown HOST_LONG_BITS
#endif

#define DYNLINK_FUNCTIONS  \
  DYNLINK_FUNC(initLibrary) \
  DYNLINK_FUNC(setStreamMode) \
  DYNLINK_FUNC(initOpenGLRenderer) \
  DYNLINK_FUNC(setPostCallback) \
  DYNLINK_FUNC(getHardwareStrings) \
  DYNLINK_FUNC(createOpenGLSubwindow) \
  DYNLINK_FUNC(destroyOpenGLSubwindow) \
  DYNLINK_FUNC(repaintOpenGLDisplay) \
  DYNLINK_FUNC(stopOpenGLRenderer) \
  DYNLINK_FUNC(readFrameBuffer)

#ifndef CONFIG_STANDALONE_UI
/* Defined in android/hw-pipe-net.c */
extern int android_init_opengles_pipes(void);
#endif

static ADynamicLibrary*  rendererLib;
static int               rendererStarted;
static char              rendererAddress[256];

/* Define the function pointers */
#define DYNLINK_FUNC(name) \
    static name##Fn name = NULL;
DYNLINK_FUNCTIONS
#undef DYNLINK_FUNC

static int
initOpenglesEmulationFuncs(ADynamicLibrary* rendererLib)
{
    void*  symbol;
    char*  error;

#define DYNLINK_FUNC(name) \
    symbol = adynamicLibrary_findSymbol(rendererLib, #name, &error); \
    if (symbol != NULL) { \
        name = symbol; \
    } else { \
        derror("GLES emulation: Could not find required symbol (%s): %s", #name, error); \
        free(error); \
        return -1; \
    }
DYNLINK_FUNCTIONS
#undef DYNLINK_FUNC

    return 0;
}

int
android_initOpenglesEmulation(void)
{
    char* error = NULL;
    OpenglesState *s = NULL;

    if (rendererLib != NULL)
        return 0;

    D("Initializing hardware OpenGLES emulation support");

    rendererLib = adynamicLibrary_open(RENDERER_LIB_NAME, &error);
    if (rendererLib == NULL) {
        derror("Could not load OpenGLES emulation library: %s", error);
        return -1;
    }

#ifndef CONFIG_STANDALONE_UI
    android_init_opengles_pipes();
#endif


    /* Resolve the functions */
    if (initOpenglesEmulationFuncs(rendererLib) < 0) {
        derror("OpenGLES emulation library mismatch. Be sure to use the correct version!");
        goto BAD_EXIT;
    }

    if (!initLibrary()) {
        derror("OpenGLES initialization failed!");
        goto BAD_EXIT;
    }

    if (android_gles_fast_pipes) {
#ifdef _WIN32
        /* XXX: NEED Win32 pipe implementation */
        setStreamMode(STREAM_MODE_TCP);
#else
	    setStreamMode(STREAM_MODE_UNIX);
#endif
    } else {
	    setStreamMode(STREAM_MODE_TCP);
    }

    s = (OpenglesState *)g_malloc0(sizeof(*s));
    s->console = graphic_console_init(NULL, &android_opengles_ops, s);
    qemu_console_resize(s->console, 320, 480);
    return 0;

BAD_EXIT:
    derror("OpenGLES emulation library could not be initialized!");
    adynamicLibrary_close(rendererLib);
    rendererLib = NULL;
    return -1;
}

#if 0
static void android_opengles_invalidate(void *opaque)
{
    OpenglesState *s = (OpenglesState *)opaque;
    s->invalidate = true;
}
#endif

static void android_opengles_update(void *opaque)
{
    OpenglesState *s = (OpenglesState *)opaque;
    DisplaySurface *surface;
    uint8_t *data;
    int width;
    int height;
    void *fb = NULL;
    unsigned int i;
    uint8_t r, g, b;
    uint32_t pixel;

    if (!s || !s->console ||
        surface_bits_per_pixel(qemu_console_surface(s->console)) == 0) {
        return;
    }
    surface = qemu_console_surface(s->console);
    data = surface_data(surface);
    readFrameBuffer(&width, &height, &fb);
    for(i = 0; i < width * height; i ++) {
        // RGBA -> BGRA
        r = *(uint8_t *)fb;
        g = *(uint8_t *)(fb + 1);
        b = *(uint8_t *)(fb + 2);
        pixel = (r << 16) | (g << 8) | b;
        *(uint32_t *)data = pixel;
        fb += 4;
        data += 4;
    }

    // fetch image from framebuffer, copy it to surface
    dpy_gfx_update(s->console, 0, 0, width, height);
}

int
android_startOpenglesRenderer(int width, int height)
{
    if (!rendererLib) {
        D("Can't start OpenGLES renderer without support libraries");
        return -1;
    }

    if (rendererStarted) {
        return 0;
    }

    if (!initOpenGLRenderer(width, height, rendererAddress, sizeof(rendererAddress))) {
        D("Can't start OpenGLES renderer?");
        return -1;
    }

    rendererStarted = 1;
    return 0;
}

void
android_setPostCallback(OnPostFunc onPost, void* onPostContext)
{
    if (rendererLib) {
        setPostCallback(onPost, onPostContext);
    }
}

static void strncpy_safe(char* dst, const char* src, size_t n)
{
    strncpy(dst, src, n);
    dst[n-1] = '\0';
}

static void extractBaseString(char* dst, const char* src, size_t dstSize)
{
    const char* begin = strchr(src, '(');
    const char* end = strrchr(src, ')');

    if (!begin || !end) {
        strncpy_safe(dst, src, dstSize);
        return;
    }
    begin += 1;

    // "foo (bar)"
    //       ^  ^
    //       b  e
    //     = 5  8
    // substring with NUL-terminator is end-begin+1 bytes
    if (end - begin + 1 > dstSize) {
        end = begin + dstSize - 1;
    }

    strncpy_safe(dst, begin, end - begin + 1);
}

void
android_getOpenglesHardwareStrings(char* vendor, size_t vendorBufSize,
                                   char* renderer, size_t rendererBufSize,
                                   char* version, size_t versionBufSize)
{
    const char *vendorSrc, *rendererSrc, *versionSrc;

    assert(vendorBufSize > 0 && rendererBufSize > 0 && versionBufSize > 0);
    assert(vendor != NULL && renderer != NULL && version != NULL);

    if (!rendererStarted) {
        D("Can't get OpenGL ES hardware strings when renderer not started");
        vendor[0] = renderer[0] = version[0] = '\0';
        return;
    }

    getHardwareStrings(&vendorSrc, &rendererSrc, &versionSrc);
    if (!vendorSrc) vendorSrc = "";
    if (!rendererSrc) rendererSrc = "";
    if (!versionSrc) versionSrc = "";

    /* Special case for the default ES to GL translators: extract the strings
     * of the underlying OpenGL implementation. */
    if (strncmp(vendorSrc, "Google", 6) == 0 &&
            strncmp(rendererSrc, "Android Emulator OpenGL ES Translator", 37) == 0) {
        extractBaseString(vendor, vendorSrc, vendorBufSize);
        extractBaseString(renderer, rendererSrc, rendererBufSize);
        extractBaseString(version, versionSrc, versionBufSize);
    } else {
        strncpy_safe(vendor, vendorSrc, vendorBufSize);
        strncpy_safe(renderer, rendererSrc, rendererBufSize);
        strncpy_safe(version, versionSrc, versionBufSize);
    }
}

void
android_stopOpenglesRenderer(void)
{
    if (rendererStarted) {
        stopOpenGLRenderer();
        rendererStarted = 0;
    }
}

int
android_showOpenglesWindow(void* window, int x, int y, int width, int height, float rotation)
{
    if (rendererStarted) {
        int success = createOpenGLSubwindow((FBNativeWindowType)window, x, y, width, height, rotation);
        return success ? 0 : -1;
    } else {
        return -1;
    }
}

int
android_hideOpenglesWindow(void)
{
    if (rendererStarted) {
        int success = destroyOpenGLSubwindow();
        return success ? 0 : -1;
    } else {
        return -1;
    }
}

void
android_redrawOpenglesWindow(void)
{
    if (rendererStarted) {
        repaintOpenGLDisplay();
    }
}

void
android_gles_server_path(char* buff, size_t buffsize)
{
    strncpy_safe(buff, rendererAddress, buffsize);
}

#else // CONFIG_ANDROID_OPENGLES

int android_initOpenglesEmulation(void)
{
    return -1;
}

int android_startOpenglesRenderer(int width, int height)
{
    return -1;
}

void
android_setPostCallback(OnPostFunc onPost, void* onPostContext)
{
}

void android_getOpenglesHardwareStrings(char* vendor, size_t vendorBufSize,
                                       char* renderer, size_t rendererBufSize,
                                       char* version, size_t versionBufSize)
{
    assert(vendorBufSize > 0 && rendererBufSize > 0 && versionBufSize > 0);
    assert(vendor != NULL && renderer != NULL && version != NULL);
    vendor[0] = renderer[0] = version[0] = 0;
}

void android_stopOpenglesRenderer(void)
{}

int android_showOpenglesWindow(void* window, int x, int y, int width, int height, float rotation)
{
    return -1;
}

int android_hideOpenglesWindow(void)
{
    return -1;
}

void android_redrawOpenglesWindow(void)
{}

void android_gles_server_path(char* buff, size_t buffsize)
{
    buff[0] = '\0';
}

#endif // !CONFIG_ANDROID_OPENGLES
