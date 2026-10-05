/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Buffer-age compatibility for the validated p930 EGL driver on Adreno 220.
 */
#include <system/window.h>
static void finishPartialTiles(EGLDisplay,EGLSurface,bool);
#include <map>
#include <mutex>
namespace {
void* ageLookup(const char* name) {
    static void* vendor=dlopen("libEGL_adreno200.so",RTLD_NOW|RTLD_LOCAL);
    void* entry=vendor ? dlsym(vendor,name) : nullptr;
    LOG_ALWAYS_FATAL_IF(!entry,"Missing vendor EGL entry point %s",name);
    return entry;
}
struct WindowAgeState { ANativeWindow* window; bool active; int age; bool usable; bool invalidated; uint64_t swaps; };
std::mutex gWindowMutex;
std::map<std::pair<EGLDisplay,EGLSurface>,WindowAgeState> gWindows;
using SurfaceQuery = EGLBoolean (*)(EGLDisplay,EGLSurface,EGLint,EGLint*);
using SurfaceAttrib = EGLBoolean (*)(EGLDisplay,EGLSurface,EGLint,EGLint);
SurfaceQuery ageQuery() {
    static auto fn=reinterpret_cast<SurfaceQuery>(ageLookup("eglQuerySurface"));
    return fn;
}
SurfaceAttrib ageAttrib() {
    static auto fn=reinterpret_cast<SurfaceAttrib>(ageLookup("eglSurfaceAttrib"));
    return fn;
}
bool currentAgeSurface(EGLDisplay display,EGLSurface surface) {
    using Display=EGLDisplay (*)(); using Surface=EGLSurface (*)(EGLint);
    static auto currentDisplay=reinterpret_cast<Display>(ageLookup("eglGetCurrentDisplay"));
    static auto currentSurface=reinterpret_cast<Surface>(ageLookup("eglGetCurrentSurface"));
    return currentDisplay()==display && currentSurface(EGL_DRAW)==surface;
}
bool endBufferAgeFrame(EGLDisplay display,EGLSurface surface) {
    if (!currentAgeSurface(display,surface)) return true;
    std::lock_guard<std::mutex> lock(gWindowMutex);
    auto it=gWindows.find(std::make_pair(display,surface));
    if (it!=gWindows.end() && it->second.active) {
        if (!ageAttrib()(display,surface,EGL_SWAP_BEHAVIOR,EGL_BUFFER_DESTROYED)) return false;
        it->second.active=false;
    }
    return true;
}
void finishBufferAgeSwap(EGLDisplay display,EGLSurface surface,EGLBoolean success) {
    std::lock_guard<std::mutex> lock(gWindowMutex);
    auto it=gWindows.find(std::make_pair(display,surface));
    if (it!=gWindows.end()) {
        it->second.invalidated=!success;
        if (success && it->second.swaps!=UINT64_MAX) ++it->second.swaps;
    }
}
}
static bool bufferAgeEnabled() {
    static const bool enabled=[]() {
        // The tile-load/swap behavior was verified on this p930 EGL build.
        // Do not extend it silently to a replacement vendor driver.
        void* vendor=dlopen("libEGL_adreno200.so",RTLD_NOW|RTLD_LOCAL);
        const void* query=vendor ? dlsym(vendor,"qeglDrvAPI_eglGetRenderBufferANDROID") : nullptr;
        const void* attrib=vendor ? dlsym(vendor,"qeglDrvAPI_eglSurfaceAttrib") : nullptr;
        const uint32_t queryCode[]={0xe92d40f0,0xe24dd014,0xe1a06000,0xe1a07001,
            0xeb0019f7,0xe2504000,0x0a000024,0xe5945020};
        const uint32_t attribCode[]={0xe92d47f0,0xe24dd088,0xe1a07000,0xe1a09001,
            0xe1a05002,0xe1a0a003,0xeb003819,0xe2506000};
        return query && attrib && !memcmp(query,queryCode,sizeof(queryCode)) &&
                !memcmp(attrib,attribCode,sizeof(attribCode));
    }();
    return enabled;
}
static EGLBoolean querySurfaceWithBufferAge(EGLDisplay display,EGLSurface surface,EGLint attr,EGLint* value) {
    if (!bufferAgeEnabled()) return ageQuery()(display,surface,attr,value);
    if (attr!=EGL_BUFFER_AGE_EXT) {
        EGLBoolean result=ageQuery()(display,surface,attr,value);
        if (result && value && attr==EGL_SWAP_BEHAVIOR) {
            std::lock_guard<std::mutex> lock(gWindowMutex);
            auto it=gWindows.find(std::make_pair(display,surface));
            if (it!=gWindows.end() && it->second.active) *value=EGL_BUFFER_DESTROYED;
        }
        return result;
    }
    // Ask the vendor to validate the display/surface before using our tracking.
    EGLint behavior=0;
    if (!ageQuery()(display,surface,EGL_SWAP_BEHAVIOR,&behavior)) return EGL_FALSE;
    if (!value) return ageQuery()(display,surface,EGL_WIDTH,value);
    if (!currentAgeSurface(display,surface)) {
        // Generate EGL_BAD_SURFACE through the vendor's normal validation.
        EGLint ignored;
        return ageQuery()(display,EGL_NO_SURFACE,EGL_WIDTH,&ignored);
    }
    using Buffer = EGLClientBuffer (*)(EGLDisplay,EGLSurface);
    static auto getBuffer=reinterpret_cast<Buffer>(ageLookup("eglGetRenderBufferANDROID"));
    std::lock_guard<std::mutex> lock(gWindowMutex);
    auto it=gWindows.find(std::make_pair(display,surface));
    if (it==gWindows.end()) { *value=0; return EGL_TRUE; }
    if (it->second.active) { *value=it->second.age; return EGL_TRUE; }
    if (behavior==EGL_BUFFER_PRESERVED) { *value=1; return EGL_TRUE; }
    if (!it->second.usable) { *value=0; return EGL_TRUE; }
    using Current=EGLContext (*)();
    static auto current=reinterpret_cast<Current>(ageLookup("eglGetCurrentContext"));
    pthread_once(&gOnce,loadVendorEntryPoints);
    EGLint version=0;
    if (!gQueryContext(display,current(),EGL_CONTEXT_CLIENT_VERSION,&version)) return EGL_FALSE;
    if (version!=2) { *value=0; return EGL_TRUE; }
    // On this driver PRESERVED enables tile loads. Request DESTROYED at the
    // frame boundary: the next buffer is repaired using its native age rather
    // than requiring EGL_BUFFER_PRESERVED semantics for every new frame.
    if (!ageAttrib()(display,surface,EGL_SWAP_BEHAVIOR,EGL_BUFFER_PRESERVED)) return EGL_FALSE;
    it->second.active=true; it->second.age=0;
    // Keep the original vendor error on allocation failure. A later swap or
    // explicit swap-behavior request restores the logical surface state.
    if (!getBuffer(display,surface)) return EGL_FALSE;
    int age=0;
    if (it->second.window->query(it->second.window,NATIVE_WINDOW_BUFFER_AGE,&age)!=0 || age<0) age=0;
    // A native window can outlive an EGL surface. Do not reuse content from
    // before this surface's own swap history (or after a failed swap).
    if (it->second.invalidated || static_cast<uint64_t>(age)>it->second.swaps) age=0;
    it->second.age=age; *value=age;
    return EGL_TRUE;
}
static EGLBoolean setSurfaceAttributeWithBufferAge(EGLDisplay display,EGLSurface surface,EGLint attr,EGLint value) {
    if (attr==EGL_SWAP_BEHAVIOR) finishPartialTiles(display,surface,false);
    EGLBoolean result=ageAttrib()(display,surface,attr,value);
    if (result && attr==EGL_SWAP_BEHAVIOR) {
        std::lock_guard<std::mutex> lock(gWindowMutex);
        auto it=gWindows.find(std::make_pair(display,surface));
        if (it!=gWindows.end()) it->second.active=false;
    }
    return result;
}
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay d,EGLSurface s,EGLint attr,EGLint* value) {
    return querySurfaceWithBufferAge(d,s,attr,value);
}
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay d,EGLSurface s,EGLint attr,EGLint value) {
    return setSurfaceAttributeWithBufferAge(d,s,attr,value);
}

static EGLBoolean terminateWithBufferAge(EGLDisplay display) {
    using Surface=EGLSurface (*)(EGLint);
    static auto currentSurface=reinterpret_cast<Surface>(ageLookup("eglGetCurrentSurface"));
    finishPartialTiles(display,currentSurface(EGL_DRAW),false);
    using F=EGLBoolean (*)(EGLDisplay);
    static auto fn=reinterpret_cast<F>(ageLookup("eglTerminate"));
    EGLBoolean result=fn(display);
    if (result) {
        std::lock_guard<std::mutex> lock(gWindowMutex);
        for (auto it=gWindows.begin();it!=gWindows.end();) {
            if (it->first.first==display) it=gWindows.erase(it); else ++it;
        }
    }
    return result;
}

static EGLSurface createWindowWithBufferAge(EGLDisplay display,EGLConfig config,EGLNativeWindowType window,const EGLint* attrs) {
    using F = EGLSurface (*)(EGLDisplay,EGLConfig,EGLNativeWindowType,const EGLint*);
    static F fn = reinterpret_cast<F>(ageLookup("eglCreateWindowSurface"));
    EGLSurface surface = fn(display,config,window,attrs);
    if (surface != EGL_NO_SURFACE) {
        pthread_once(&gOnce,loadVendorEntryPoints);
        EGLint type=0,samples=0;
        const bool usable=gGetConfigAttrib(display,config,EGL_SURFACE_TYPE,&type) &&
            gGetConfigAttrib(display,config,EGL_SAMPLES,&samples) &&
            (type&EGL_SWAP_BEHAVIOR_PRESERVED_BIT) && !samples;
        std::lock_guard<std::mutex> lock(gWindowMutex);
        gWindows[std::make_pair(display,surface)] = WindowAgeState{window,false,0,usable,true,0};
    }
    return surface;
}
static EGLBoolean destroySurfaceWithBufferAge(EGLDisplay display,EGLSurface surface) {
    finishPartialTiles(display,surface,false);
    using F = EGLBoolean (*)(EGLDisplay,EGLSurface);
    static F fn = reinterpret_cast<F>(ageLookup("eglDestroySurface"));
    EGLBoolean result = fn(display,surface);
    if (result) {
        std::lock_guard<std::mutex> lock(gWindowMutex);
        gWindows.erase(std::make_pair(display,surface));
    }
    return result;
}

static EGLBoolean swapWithBufferAge(EGLDisplay display,EGLSurface surface) {
    using F=EGLBoolean (*)(EGLDisplay,EGLSurface);
    static auto fn=reinterpret_cast<F>(ageLookup("eglSwapBuffers"));
    finishPartialTiles(display,surface,true);
    if (!endBufferAgeFrame(display,surface)) return EGL_FALSE;
    EGLBoolean result=fn(display,surface);
    finishBufferAgeSwap(display,surface,result);
    return result;
}
extern "C" EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay d,EGLConfig c,EGLNativeWindowType w,const EGLint* a) { return createWindowWithBufferAge(d,c,w,a); }
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay d,EGLSurface s) { return destroySurfaceWithBufferAge(d,s); }
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay d,EGLSurface s) { return swapWithBufferAge(d,s); }
extern "C" EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay d) { return terminateWithBufferAge(d); }
