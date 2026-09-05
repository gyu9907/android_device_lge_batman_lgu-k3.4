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
#include <cutils/log.h>

namespace {
using CreateImage = EGLImageKHR (*)(EGLDisplay, EGLContext, EGLenum,
                                    EGLClientBuffer, const EGLint*);
using GetProcAddress = __eglMustCastToProperFunctionPointerType (*)(const char*);
using GetCurrentContext = EGLContext (*)();
using QueryContext = EGLBoolean (*)(EGLDisplay, EGLContext, EGLint, EGLint*);
using Finish = void (*)();

pthread_once_t gOnce = PTHREAD_ONCE_INIT;
CreateImage gCreateImage;
GetProcAddress gGetProcAddress;
GetCurrentContext gGetCurrentContext;
QueryContext gQueryContext;

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
    LOG_ALWAYS_FATAL_IF(!gCreateImage || !gGetProcAddress ||
                        !gGetCurrentContext || !gQueryContext,
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

extern "C" EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImageKHR(
        EGLDisplay display, EGLContext context, EGLenum target,
        EGLClientBuffer buffer, const EGLint* attributes) {
    return createImage(display, context, target, buffer, attributes);
}

extern "C" EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
        eglGetProcAddress(const char* name) {
    pthread_once(&gOnce, loadVendorEntryPoints);
    if (name && !strcmp(name, "eglCreateImageKHR")) {
        // Use the local function so ELF interposition cannot accidentally
        // return Android's public EGL wrapper to a caller with vendor handles.
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(createImage);
    }
    return gGetProcAddress(name);
}
