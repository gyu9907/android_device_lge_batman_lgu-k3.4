/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "BatmanEglImageCompat"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <cutils/log.h>

static bool bufferAgeEnabled();
static bool partialTilesEnabled();
static void bindFramebufferWithPartialTiles(unsigned int,unsigned int);
static void bindFramebufferOesWithPartialTiles(unsigned int,unsigned int);
static EGLBoolean makeCurrentWithPartialTiles(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
static EGLBoolean setPartialDamage(EGLDisplay,EGLSurface,EGLint*,EGLint);
static EGLBoolean querySurfaceWithBufferAge(EGLDisplay,EGLSurface,EGLint,EGLint*);
static EGLBoolean setSurfaceAttributeWithBufferAge(EGLDisplay,EGLSurface,EGLint,EGLint);
static EGLSurface createWindowWithBufferAge(EGLDisplay,EGLConfig,EGLNativeWindowType,const EGLint*);
static EGLBoolean destroySurfaceWithBufferAge(EGLDisplay,EGLSurface);
static EGLBoolean swapWithBufferAge(EGLDisplay,EGLSurface);
static EGLBoolean terminateWithBufferAge(EGLDisplay);

namespace {
using CreateImage = EGLImageKHR (*)(EGLDisplay, EGLContext, EGLenum,
                                    EGLClientBuffer, const EGLint*);
using GetProcAddress = __eglMustCastToProperFunctionPointerType (*)(const char*);
using GetCurrentContext = EGLContext (*)();
using QueryContext = EGLBoolean (*)(EGLDisplay, EGLContext, EGLint, EGLint*);
using Finish = void (*)();
using GetConfigAttrib = EGLBoolean (*)(EGLDisplay, EGLConfig, EGLint, EGLint*);
using ChooseConfig = EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
using QueryString = const char* (*)(EGLDisplay, EGLint);
constexpr EGLint kEs3Bit = EGL_OPENGL_ES3_BIT_KHR;

pthread_once_t gOnce = PTHREAD_ONCE_INIT;
CreateImage gCreateImage;
GetProcAddress gGetProcAddress;
GetCurrentContext gGetCurrentContext;
QueryContext gQueryContext;
GetConfigAttrib gGetConfigAttrib;
ChooseConfig gChooseConfig;
QueryString gQueryString;

void loadVendorEntryPoints() {
    // This unmodified blob is also a DT_NEEDED dependency, so dlsym on our
    // driver handle still finds every EGL entry point we do not override.
    void* vendor = dlopen("libEGL_adreno200.so", RTLD_NOW | RTLD_LOCAL);
    LOG_ALWAYS_FATAL_IF(!vendor, "Cannot load vendor EGL: %s", dlerror());
    gCreateImage = reinterpret_cast<CreateImage>(dlsym(vendor, "eglCreateImageKHR"));
    gGetProcAddress = reinterpret_cast<GetProcAddress>(dlsym(vendor, "eglGetProcAddress"));
    gGetCurrentContext = reinterpret_cast<GetCurrentContext>(
            dlsym(vendor, "eglGetCurrentContext"));
    gQueryContext = reinterpret_cast<QueryContext>(dlsym(vendor, "eglQueryContext"));
    gGetConfigAttrib = reinterpret_cast<GetConfigAttrib>(dlsym(vendor, "eglGetConfigAttrib"));
    gChooseConfig = reinterpret_cast<ChooseConfig>(dlsym(vendor, "eglChooseConfig"));
    gQueryString = reinterpret_cast<QueryString>(dlsym(vendor, "eglQueryString"));
    LOG_ALWAYS_FATAL_IF(!gCreateImage || !gGetProcAddress ||
                        !gGetCurrentContext || !gQueryContext ||
                        !gGetConfigAttrib || !gChooseConfig || !gQueryString,
                        "Vendor EGL image entry points are missing");
    // Keep the handle for the lifetime of the driver. Returned entry points
    // and vendor-owned EGL objects must remain valid.
}

Finish loadFinish(const char* path) {
    void* client = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    LOG_ALWAYS_FATAL_IF(!client, "Cannot load vendor GLES: %s", dlerror());
    Finish finish = reinterpret_cast<Finish>(dlsym(client, "glFinish"));
    LOG_ALWAYS_FATAL_IF(!finish, "Vendor GLES glFinish is missing in %s", path);
    return finish;
}

std::string filterExtensions(const char* extensions) {
    std::string result;
    while (*extensions) {
        extensions += strspn(extensions, " ");
        const size_t length = strcspn(extensions, " ");
        if (!length) break;
        if (length != sizeof("EGL_KHR_create_context") - 1 ||
                strncmp(extensions, "EGL_KHR_create_context", length)) {
            if (!result.empty()) result += ' ';
            result.append(extensions, length);
        }
        extensions += length;
    }
    if (bufferAgeEnabled()) result += " EGL_EXT_buffer_age";
    if (partialTilesEnabled()) result += " EGL_KHR_partial_update";
    return result;
}

const char* queryString(EGLDisplay display, EGLint name) {
    pthread_once(&gOnce, loadVendorEntryPoints);
    const char* result = gQueryString(display, name);
    if (!result || name != EGL_EXTENSIONS) return result;
    // Older ANGLE selects ES3 configs when this extension is advertised and
    // fails to fall back when eglChooseConfig succeeds with zero matches.
    // Use the EGL 1.4 ES2 creation path on this ES2-only device. Keep the
    // vendor query above so invalid displays retain their original errors.
    static const std::string extensions = filterExtensions(result);
    return extensions.c_str();
}

EGLBoolean getConfigAttrib(EGLDisplay display, EGLConfig config,
                          EGLint attribute, EGLint* value) {
    pthread_once(&gOnce, loadVendorEntryPoints);
    EGLBoolean result = gGetConfigAttrib(display, config, attribute, value);
    // The p930 blob exposes ES3 configurations on Adreno 220 even though
    // eglCreateContext rejects ES3. Chromium uses these bits to select ES2.
    if (result && value && (attribute == EGL_RENDERABLE_TYPE || attribute == EGL_CONFORMANT)) {
        *value &= ~kEs3Bit;
    }
    return result;
}

EGLBoolean chooseConfig(EGLDisplay display, const EGLint* attributes,
                        EGLConfig* configs, EGLint size, EGLint* count) {
    pthread_once(&gOnce, loadVendorEntryPoints);
    // Let the vendor validate arguments and preserve its error semantics.
    EGLBoolean result = gChooseConfig(display, attributes, configs, size, count);
    if (result && attributes && count) {
        for (const EGLint* a = attributes; a[0] != EGL_NONE; a += 2) {
            if ((a[0] == EGL_RENDERABLE_TYPE || a[0] == EGL_CONFORMANT) &&
                    a[1] != EGL_DONT_CARE && (a[1] & kEs3Bit)) {
                // A required ES3 bit has no matching configs on this device.
                // Do not silently turn an ES3 request into an ES2 request.
                *count = 0;
                break;
            }
        }
    }
    return result;
}

EGLImageKHR createImage(EGLDisplay display, EGLContext context, EGLenum target,
                       EGLClientBuffer buffer, const EGLint* attributes) {
    pthread_once(&gOnce, loadVendorEntryPoints);
    EGLImageKHR image = gCreateImage(display, context, target, buffer, attributes);
    if (image != EGL_NO_IMAGE_KHR && target == EGL_GL_TEXTURE_2D_KHR &&
            context == gGetCurrentContext()) {
        // Adreno 220 can lose a subsequent TexSubImage update unless image
        // creation has completed first. glFlush only submits the work: larger
        // tiles still intermittently read as transparent zeros. Finish here,
        // before the upload; finishing before creation or after the upload
        // does not fix the race, even with a satisfied consumer fence.
        // This EGL blob's eglGetProcAddress does not expose core GL functions.
        // Use the matching vendor GLES client; no Android EGL handles or
        // private framework dispatch-table layout are involved.
        EGLint version = 0;
        if (gQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION, &version)) {
            if (version == 1) {
                static Finish finish = loadFinish("/system/lib/egl/libGLESv1_CM_adreno200.so");
                finish();
            } else if (version == 2) {
                static Finish finish = loadFinish("/system/lib/egl/libGLESv2_adreno200.so");
                finish();
            }
        }
    }
    return image;
}
}  // namespace

extern "C" EGLAPI const char* EGLAPIENTRY eglQueryString(EGLDisplay display, EGLint name) {
    return queryString(display, name);
}

extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(
        EGLDisplay display, EGLConfig config, EGLint attribute, EGLint* value) {
    return getConfigAttrib(display, config, attribute, value);
}

extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(
        EGLDisplay display, const EGLint* attributes, EGLConfig* configs,
        EGLint size, EGLint* count) {
    return chooseConfig(display, attributes, configs, size, count);
}

extern "C" EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImageKHR(
        EGLDisplay display, EGLContext context, EGLenum target,
        EGLClientBuffer buffer, const EGLint* attributes) {
    return createImage(display, context, target, buffer, attributes);
}

extern "C" EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
        eglGetProcAddress(const char* name) {
    pthread_once(&gOnce, loadVendorEntryPoints);
    if (name && !strcmp(name, "glBindFramebuffer") && partialTilesEnabled()) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(bindFramebufferWithPartialTiles);
    }
    if (name && !strcmp(name, "glBindFramebufferOES") && partialTilesEnabled() && gGetProcAddress(name)) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(bindFramebufferOesWithPartialTiles);
    }
    if (name && !strcmp(name, "eglMakeCurrent")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(makeCurrentWithPartialTiles);
    }
    if (name && !strcmp(name, "eglSetDamageRegionKHR")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(setPartialDamage);
    }
    if (name && !strcmp(name, "eglQuerySurface")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(querySurfaceWithBufferAge);
    }
    if (name && !strcmp(name, "eglSurfaceAttrib")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(setSurfaceAttributeWithBufferAge);
    }
    if (name && !strcmp(name, "eglCreateWindowSurface")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(createWindowWithBufferAge);
    }
    if (name && !strcmp(name, "eglDestroySurface")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(destroySurfaceWithBufferAge);
    }
    if (name && !strcmp(name, "eglSwapBuffers")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(swapWithBufferAge);
    }
    if (name && !strcmp(name, "eglTerminate")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(terminateWithBufferAge);
    }
    if (name && !strcmp(name, "eglQueryString")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(queryString);
    }
    if (name && (!strcmp(name, "glGetString") || !strcmp(name, "glRenderbufferStorage"))) {
        static void* client = dlopen("/system/lib/egl/libGLESv2_adreno200_compat.so",
                                    RTLD_NOW | RTLD_LOCAL);
        if (client) {
            return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(dlsym(client, name));
        }
    }
    if (name && !strcmp(name, "eglGetConfigAttrib")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(getConfigAttrib);
    }
    if (name && !strcmp(name, "eglChooseConfig")) {
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(chooseConfig);
    }
    if (name && !strcmp(name, "eglCreateImageKHR")) {
        // Use the local function so ELF interposition cannot accidentally
        // return Android's public EGL wrapper to a caller with vendor handles.
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(createImage);
    }
    return gGetProcAddress(name);
}

#include "EglBufferAgeCompat.inc"
#include "EglPartialTiles.inc"
