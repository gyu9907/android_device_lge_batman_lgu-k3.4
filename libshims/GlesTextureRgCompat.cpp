/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "BatmanGlesTextureRgCompat"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cutils/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <string>

namespace {
using GetString = const GLubyte* (*)(GLenum);
using Storage = void (*)(GLenum, GLenum, GLsizei, GLsizei);
using GetContext = void* (*)();
pthread_once_t once = PTHREAD_ONCE_INIT;
GetString vendorGetString;
Storage vendorStorage;
GetContext vendorGetContext;
bool hasP930Validation;

void load() {
    void* h = dlopen("libGLESv2_adreno200.so", RTLD_NOW | RTLD_LOCAL);
    LOG_ALWAYS_FATAL_IF(!h, "Cannot load vendor GLES2: %s", dlerror());
    vendorGetString = reinterpret_cast<GetString>(dlsym(h, "glGetString"));
    vendorStorage = reinterpret_cast<Storage>(dlsym(h, "glRenderbufferStorage"));
    vendorGetContext = reinterpret_cast<GetContext>(dlsym(h, "gl2_GetContext"));
    LOG_ALWAYS_FATAL_IF(!vendorGetString || !vendorStorage, "Missing vendor GLES2 entry points");
    // p930 V@14 / CL3896081 already implements RED/RG textures and native
    // R8/RG8 surfaces. Its ES2 renderbuffer validator alone excludes R8/RG8.
    // Verify the exact Thumb instructions that read the context capability
    // word at 0x708 and gate the sized-format validator on bit 10. Fail closed
    // for a different driver instead of writing an unknown context layout.
    const uintptr_t fn = reinterpret_cast<uintptr_t>(dlsym(h, "is_internal_renderbuffer_fmt"));
    const unsigned char gate[] = {
        0xd3, 0xf8, 0x08, 0x37, 0x80, 0xf0, 0x01, 0x0c,
        0x1c, 0xea, 0x93, 0x23, 0x02, 0xd0
    };
    hasP930Validation = fn && vendorGetContext &&
            !memcmp(reinterpret_cast<const void*>((fn & ~uintptr_t(1)) + 0x54),
                    gate, sizeof(gate));
}

bool supported() {
    const char* version = reinterpret_cast<const char*>(vendorGetString(GL_VERSION));
    return hasP930Validation && version && strstr(version, "V@14.0") &&
            strstr(version, "CL@3896081");
}
}  // namespace

extern "C" GL_APICALL const GLubyte* GL_APIENTRY glGetString(GLenum name) {
    pthread_once(&once, load);
    const GLubyte* result = vendorGetString(name);
    if (name != GL_EXTENSIONS || !result || !supported()) return result;
    // Immutable after initialization, and valid for the lifetime of the DSO.
    static const std::string extensions = std::string(reinterpret_cast<const char*>(result)) +
            " GL_EXT_texture_rg";
    return reinterpret_cast<const GLubyte*>(extensions.c_str());
}

extern "C" GL_APICALL void GL_APIENTRY glRenderbufferStorage(
        GLenum target, GLenum format, GLsizei width, GLsizei height) {
    pthread_once(&once, load);
    if ((format == GL_R8_EXT || format == GL_RG8_EXT) && supported()) {
        void* context = vendorGetContext();
        if (context) {
            // An EGL context can only be current on one thread. Temporarily
            // allow the native sized-format validator for this call only;
            // preserve every capability bit, including on allocation failure.
            uint32_t* flags = reinterpret_cast<uint32_t*>(
                    static_cast<unsigned char*>(context) + 0x708);
            const uint32_t saved = *flags;
            *flags = saved | (1u << 10);
            vendorStorage(target, format, width, height);
            *flags = saved;
            return;
        }
    }
    vendorStorage(target, format, width, height);
}

#include "GlesPartialTiles.inc"
