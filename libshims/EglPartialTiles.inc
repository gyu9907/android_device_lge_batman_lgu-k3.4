/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Translate buffer repair regions into tile loads on the validated p930 driver.
 */
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <algorithm>

namespace {
struct PartialTileState {
    EGLDisplay display;
    EGLSurface surface;
    bool frame;
    bool active;
    GLuint framebuffer;
    GLuint x, y, width, height;
    GLbitfield preserve;
};
thread_local PartialTileState gTile = {};
constexpr GLbitfield kKeepAll = GL_COLOR_BUFFER_BIT0_QCOM |
        GL_DEPTH_BUFFER_BIT0_QCOM | GL_STENCIL_BUFFER_BIT0_QCOM;
void* tileGl(const char* name) {
    static void* vendor = dlopen("libGLESv2_adreno200.so", RTLD_NOW | RTLD_LOCAL);
    void* entry = vendor ? dlsym(vendor, name) : nullptr;
    LOG_ALWAYS_FATAL_IF(!entry, "Missing tile entry point %s", name);
    return entry;
}
void closeTile(bool finalFrame) {
    if (gTile.active) {
        using End = void (*)(GLbitfield);
        static auto end = reinterpret_cast<End>(tileGl("glEndTilingQCOM"));
        end(finalFrame ? GL_COLOR_BUFFER_BIT0_QCOM : kKeepAll);
        gTile.active = false;
        gTile.preserve = kKeepAll;
    }
}
}
static bool partialTilesEnabled() {
    return bufferAgeEnabled();
}
static void finishPartialTiles(EGLDisplay display, EGLSurface surface, bool finalFrame) {
    if (gTile.frame && gTile.display == display && gTile.surface == surface) {
        closeTile(finalFrame);
        gTile.frame = false;
    }
}
static void startPartialTile() {
    if (!gTile.frame || gTile.active || gTile.framebuffer) return;
    using Start = void (*)(GLuint, GLuint, GLuint, GLuint, GLbitfield);
    static auto start = reinterpret_cast<Start>(tileGl("glStartTilingQCOM"));
    start(gTile.x, gTile.y, gTile.width, gTile.height, gTile.preserve);
    gTile.active = true;
}
extern "C" __attribute__((visibility("default"))) void batmanPartialBeforeBind(GLenum target, GLuint id) {
    if (!gTile.frame) return;
    // The p930 driver ends application tiling for either read or draw FBO
    // changes. Skia blits use the ANGLE split targets even on this ES2 device.
    if (target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER_ANGLE &&
            target != GL_DRAW_FRAMEBUFFER_ANGLE) return;
    bool changed = id != gTile.framebuffer;
    if (target == GL_READ_FRAMEBUFFER_ANGLE ||
            (target == GL_FRAMEBUFFER && !changed)) {
        using Get = void (*)(GLenum, GLint*);
        static auto get = reinterpret_cast<Get>(tileGl("glGetIntegerv"));
        GLint readFramebuffer = 0;
        get(GL_READ_FRAMEBUFFER_BINDING_ANGLE, &readFramebuffer);
        changed = id != static_cast<GLuint>(readFramebuffer);
    }
    if (changed) closeTile(false);
}
extern "C" __attribute__((visibility("default"))) void batmanPartialBeforeDelete(GLsizei count) {
    // Deleting even an unbound FBO detaches its attachments in the p930
    // driver, which resolves the current target and ends application tiling.
    if (gTile.frame && count > 0) closeTile(false);
}
extern "C" __attribute__((visibility("default"))) void batmanPartialAfterBind() {
    if (!gTile.frame) return;
    using Get = void (*)(GLenum, GLint*);
    static auto get = reinterpret_cast<Get>(tileGl("glGetIntegerv"));
    GLint framebuffer = 0;
    get(GL_FRAMEBUFFER_BINDING, &framebuffer);
    gTile.framebuffer = static_cast<GLuint>(framebuffer);
    startPartialTile();
}
static EGLBoolean setPartialDamage(EGLDisplay display, EGLSurface surface, EGLint* rects, EGLint count) {
    EGLint width = 0, height = 0;
    if (!ageQuery()(display, surface, EGL_WIDTH, &width) ||
            !ageQuery()(display, surface, EGL_HEIGHT, &height)) return EGL_FALSE;
    if (!currentAgeSurface(display, surface)) {
        EGLint ignored;
        return ageQuery()(display, EGL_NO_SURFACE, EGL_WIDTH, &ignored);
    }
    if (count < 0 || (count && !rects))
        return ageQuery()(display, surface, EGL_WIDTH, nullptr);
    if (!partialTilesEnabled()) return EGL_TRUE;
    finishPartialTiles(display, surface, false);
    {
        std::lock_guard<std::mutex> lock(gWindowMutex);
        auto it = gWindows.find(std::make_pair(display, surface));
        // Unsupported surfaces keep the conservative buffer-age path.
        if (it == gWindows.end() || !it->second.active || !it->second.usable) return EGL_TRUE;
    }
    int64_t left = width, bottom = height, right = 0, top = 0;
    for (int i = 0; i < count; ++i) {
        const EGLint* r = rects + i * 4;
        if (r[2] < 0 || r[3] < 0) return ageQuery()(display, surface, EGL_WIDTH, nullptr);
        if (!r[2] || !r[3]) continue;
        left = std::min(left, std::max<int64_t>(0, r[0]));
        bottom = std::min(bottom, std::max<int64_t>(0, r[1]));
        right = std::max(right, std::min<int64_t>(width, int64_t(r[0]) + r[2]));
        top = std::max(top, std::min<int64_t>(height, int64_t(r[1]) + r[3]));
    }
    // A bounding box of multiple repair rectangles may contain unchanged
    // pixels. Keep the conservative tile-load path instead of discarding them.
    if (count != 1) return EGL_TRUE;
    if (right <= left || top <= bottom) return EGL_TRUE;
    gTile = PartialTileState{display, surface, true, false, 0,
            GLuint(left), GLuint(bottom), GLuint(right-left), GLuint(top-bottom), 0};
    batmanPartialAfterBind();
    return EGL_TRUE;
}
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglSetDamageRegionKHR(EGLDisplay d, EGLSurface s, EGLint* r, EGLint n) {
    return setPartialDamage(d, s, r, n);
}
static EGLBoolean makeCurrentWithPartialTiles(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext context) {
    closeTile(false);
    gTile.frame = false;
    using Make = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    static auto make = reinterpret_cast<Make>(ageLookup("eglMakeCurrent"));
    return make(d, draw, read, context);
}

extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext context) {
    return makeCurrentWithPartialTiles(d, draw, read, context);
}

static void bindFramebufferWithPartialTiles(unsigned int target, unsigned int framebuffer) {
    using Bind = void (*)(GLenum, GLuint);
    static auto bind = reinterpret_cast<Bind>(tileGl("glBindFramebuffer"));
    batmanPartialBeforeBind(target,framebuffer);
    bind(target,framebuffer);
    batmanPartialAfterBind();
}
static void bindFramebufferOesWithPartialTiles(unsigned int target, unsigned int framebuffer) {
    using Bind = void (*)(GLenum, GLuint);
    static auto bind = reinterpret_cast<Bind>(gGetProcAddress("glBindFramebufferOES"));
    batmanPartialBeforeBind(target,framebuffer);
    bind(target,framebuffer);
    batmanPartialAfterBind();
}

static void deleteFramebuffersWithPartialTiles(int count, const unsigned int* framebuffers) {
    using Delete = void (*)(GLsizei, const GLuint*);
    static auto remove = reinterpret_cast<Delete>(tileGl("glDeleteFramebuffers"));
    batmanPartialBeforeDelete(count);
    remove(count, framebuffers);
    batmanPartialAfterBind();
}
