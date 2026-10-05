/* Copyright (C) 2026 The CyanogenMod Project
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef BATMAN_CAMERA_CALLBACKS_H
#define BATMAN_CAMERA_CALLBACKS_H

#include <hardware/camera.h>
#include <pthread.h>
#include <string.h>
#include <new>

/* The JB HAL calls back while holding its preview-buffer mutex.  Deliver
 * preview callbacks on a separate thread so stopPreview/takePicture can
 * return while CameraClient::dataCallback waits for the client's lock.
 * Copy only callback frames; display and recording buffers remain zero-copy.
 */
class CameraCallbacks {
public:
    CameraCallbacks() : mStarted(false), mExit(false), mPreview(false),
            mPending(false), mMessages(0), mFaceCount(0), mNotify(NULL),
            mData(NULL), mTimestamp(NULL), mUser(NULL), mGetMemory(NULL),
            mMemoryUser(NULL), mFramePending(-1), mFrameBusy(-1) {
        memset(mFrameMemory, 0, sizeof(mFrameMemory));
        pthread_mutex_init(&mLock, NULL);
        pthread_cond_init(&mCondition, NULL);
    }

    ~CameraCallbacks() {
        shutdown();
        for (int i = 0; i < 2; ++i)
            if (mFrameMemory[i]) mFrameMemory[i]->release(mFrameMemory[i]);
        pthread_cond_destroy(&mCondition);
        pthread_mutex_destroy(&mLock);
    }

    int start() {
        int err = pthread_create(&mThread, NULL, threadMain, this);
        mStarted = (err == 0);
        return -err;
    }

    void set(camera_notify_callback notify, camera_data_callback data,
            camera_data_timestamp_callback timestamp,
            void *user, camera_request_memory getMemory, void *memoryUser) {
        pthread_mutex_lock(&mLock);
        mNotify = notify;
        mData = data;
        mTimestamp = timestamp;
        mUser = user;
        mGetMemory = getMemory;
        mMemoryUser = memoryUser;
        mPending = false;
        mFramePending = -1;
        pthread_mutex_unlock(&mLock);
    }

    void enable(int32_t messages) {
        pthread_mutex_lock(&mLock);
        mMessages |= messages;
        pthread_mutex_unlock(&mLock);
    }

    void disable(int32_t messages) {
        pthread_mutex_lock(&mLock);
        mMessages &= ~messages;
        if (messages & CAMERA_MSG_PREVIEW_METADATA) mPending = false;
        if (messages & CAMERA_MSG_PREVIEW_FRAME) mFramePending = -1;
        pthread_mutex_unlock(&mLock);
    }

    void preview(bool active) {
        pthread_mutex_lock(&mLock);
        mPreview = active;
        mPending = false;
        mFramePending = -1;
        pthread_mutex_unlock(&mLock);
        // Never join here: stopPreview is called with CameraClient's lock held.
    }

    // Called after vendor close has stopped its producers. CameraClient has
    // disabled its messages by this point, so an in-flight callback can exit.
    void shutdown() {
        pthread_mutex_lock(&mLock);
        mExit = true;
        mPreview = false;
        mPending = false;
        mFramePending = -1;
        pthread_cond_signal(&mCondition);
        pthread_mutex_unlock(&mLock);
        if (mStarted) {
            pthread_join(mThread, NULL);
            mStarted = false;
        }
    }

    static void notify(int32_t type, int32_t arg1, int32_t arg2, void *cookie) {
        CameraCallbacks *self = static_cast<CameraCallbacks *>(cookie);
        pthread_mutex_lock(&self->mLock);
        camera_notify_callback cb = self->mNotify;
        void *user = self->mUser;
        pthread_mutex_unlock(&self->mLock);
        if (cb) cb(type, arg1, arg2, user);
    }

    static void data(int32_t type, const camera_memory_t *memory,
            unsigned int index, camera_frame_metadata_t *metadata, void *cookie) {
        CameraCallbacks *self = static_cast<CameraCallbacks *>(cookie);
        pthread_mutex_lock(&self->mLock);
        camera_data_callback cb = self->mData;
        void *user = self->mUser;
        if ((type & CAMERA_MSG_PREVIEW_FRAME) && cb && self->mPreview &&
                !self->mExit && (self->mMessages & CAMERA_MSG_PREVIEW_FRAME))
            self->queueFrameLocked(memory, index);
        if ((type & CAMERA_MSG_PREVIEW_METADATA) && metadata && cb &&
                self->mPreview && !self->mExit &&
                (self->mMessages & CAMERA_MSG_PREVIEW_METADATA) &&
                metadata->number_of_faces >= 0 &&
                metadata->number_of_faces <= MAX_FACES &&
                (!metadata->number_of_faces || metadata->faces)) {
            self->mFaceCount = metadata->number_of_faces;
            if (self->mFaceCount) {
                memcpy(self->mFaces, metadata->faces,
                        self->mFaceCount * sizeof(camera_face_t));
            }
            // Coalesce to the latest result while the consumer is busy.
            self->mPending = true;
            pthread_cond_signal(&self->mCondition);
        }
        pthread_mutex_unlock(&self->mLock);

        // No preview callback may run synchronously under the vendor lock.
        bool split = (type & (CAMERA_MSG_PREVIEW_METADATA | CAMERA_MSG_PREVIEW_FRAME)) != 0;
        type &= ~(CAMERA_MSG_PREVIEW_METADATA | CAMERA_MSG_PREVIEW_FRAME);
        if (cb && type) cb(type, memory, index, split ? NULL : metadata, user);
    }

    // Preserve the public camera_memory_t ABI and the framework's handle,
    // while remembering the frame boundaries of a vendor allocation.
    static camera_memory_t *wrapMemory(camera_memory_t *memory,
            size_t frameSize, unsigned int count) {
        if (!memory) return NULL;
        Memory *wrapped = new (std::nothrow) Memory;
        if (!wrapped) {
            memory->release(memory);
            return NULL;
        }
        wrapped->base = *memory;
        wrapped->base.release = releaseMemory;
        wrapped->original = memory;
        wrapped->frameSize = frameSize;
        wrapped->count = count;
        return &wrapped->base;
    }

    static void timestamp(int64_t time, int32_t type,
            const camera_memory_t *memory, unsigned int index, void *cookie) {
        CameraCallbacks *self = static_cast<CameraCallbacks *>(cookie);
        pthread_mutex_lock(&self->mLock);
        camera_data_timestamp_callback cb = self->mTimestamp;
        void *user = self->mUser;
        pthread_mutex_unlock(&self->mLock);
        if (cb) cb(time, type, memory, index, user);
    }


private:
    enum { MAX_FACES = 32 }; // The Batman HAL advertises five faces.
    pthread_mutex_t mLock;
    pthread_cond_t mCondition;
    pthread_t mThread;
    bool mStarted, mExit, mPreview, mPending;
    int32_t mMessages;
    int mFaceCount;
    camera_face_t mFaces[MAX_FACES];
    camera_notify_callback mNotify;
    camera_data_callback mData;
    camera_data_timestamp_callback mTimestamp;
    void *mUser;
    camera_request_memory mGetMemory;
    void *mMemoryUser;
    // One frame can be in flight and one pending. Coalesce newer frames
    // into the pending slot; never wait for the framework from a HAL callback.
    camera_memory_t *mFrameMemory[2];
    int mFramePending, mFrameBusy;

    struct Memory {
        camera_memory_t base;
        camera_memory_t *original;
        size_t frameSize;
        unsigned int count;
    };

    static void releaseMemory(camera_memory_t *memory) {
        Memory *wrapped = reinterpret_cast<Memory *>(memory);
        wrapped->original->release(wrapped->original);
        delete wrapped;
    }

    void queueFrameLocked(const camera_memory_t *memory, unsigned int index) {
        if (!memory || memory->release != releaseMemory || !memory->data || !mGetMemory)
            return;
        const Memory *source = reinterpret_cast<const Memory *>(memory);
        if (!source->frameSize || index >= source->count ||
                source->frameSize > memory->size ||
                index >= memory->size / source->frameSize)
            return;
        int slot = mFramePending >= 0 ? mFramePending : (mFrameBusy == 0 ? 1 : 0);
        mFramePending = -1;
        camera_memory_t *&copy = mFrameMemory[slot];
        if (copy && copy->size != source->frameSize) {
            copy->release(copy);
            copy = NULL;
        }
        if (!copy) copy = mGetMemory(-1, source->frameSize, 1, mMemoryUser);
        if (!copy) return;
        if (!copy->data || copy->size < source->frameSize) {
            copy->release(copy);
            copy = NULL;
            return;
        }
        memcpy(copy->data, static_cast<const char *>(memory->data) +
                index * source->frameSize, source->frameSize);
        mFramePending = slot;
        pthread_cond_signal(&mCondition);
    }

    static void *threadMain(void *cookie) {
        static_cast<CameraCallbacks *>(cookie)->run();
        return NULL;
    }

    void run() {
        for (;;) {
            camera_face_t faces[MAX_FACES];
            camera_frame_metadata_t metadata;
            pthread_mutex_lock(&mLock);
            while (!mExit && !mPending && mFramePending < 0)
                pthread_cond_wait(&mCondition, &mLock);
            if (mExit) {
                pthread_mutex_unlock(&mLock);
                return;
            }
            metadata.number_of_faces = mFaceCount;
            memcpy(faces, mFaces, mFaceCount * sizeof(camera_face_t));
            metadata.faces = mFaceCount ? faces : NULL;
            camera_data_callback cb = mData;
            void *user = mUser;
            bool haveMetadata = mPending;
            int frame = mFramePending;
            mFramePending = -1;
            mFrameBusy = frame;
            mPending = false;
            pthread_mutex_unlock(&mLock);
            if (cb && haveMetadata)
                cb(CAMERA_MSG_PREVIEW_METADATA, NULL, 0, &metadata, user);
            if (cb && frame >= 0)
                cb(CAMERA_MSG_PREVIEW_FRAME, mFrameMemory[frame], 0, NULL, user);
            pthread_mutex_lock(&mLock);
            mFrameBusy = -1;
            pthread_mutex_unlock(&mLock);
        }
    }

    CameraCallbacks(const CameraCallbacks &);
    CameraCallbacks &operator=(const CameraCallbacks &);
};
#endif
