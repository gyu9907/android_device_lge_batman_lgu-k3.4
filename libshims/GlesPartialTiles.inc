/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Preserve an in-progress repair pass across framebuffer switches.
 */
extern "C" void batmanPartialBeforeBind(GLenum, GLuint);
extern "C" void batmanPartialAfterBind();
namespace {
void* partialGlEntry(const char* name) {
    static void* vendor = dlopen("libGLESv2_adreno200.so", RTLD_NOW | RTLD_LOCAL);
    void* entry = vendor ? dlsym(vendor, name) : nullptr;
    LOG_ALWAYS_FATAL_IF(!entry, "Missing partial-update GL entry %s", name);
    return entry;
}
}
extern "C" GL_APICALL void GL_APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer) {
    static auto fn = reinterpret_cast<decltype(&glBindFramebuffer)>(partialGlEntry("glBindFramebuffer"));
    batmanPartialBeforeBind(target, framebuffer);
    fn(target, framebuffer);
    batmanPartialAfterBind();
}
