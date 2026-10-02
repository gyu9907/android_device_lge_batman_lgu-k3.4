/* Copyright (C) 2026 The CyanogenMod Project
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef BATMAN_CAMERA_CALLBACKS_H
#define BATMAN_CAMERA_CALLBACKS_H

#include <hardware/camera.h>
#include <pthread.h>
#include <string.h>

/* The JB HAL calls back while holding its preview-buffer mutex.  Deliver
 * metadata on a separate thread so CameraClient::stopPreview can return
 * while CameraClient::dataCallback is waiting for the client's lock.
 */
class CameraCallbacks {
public:
    CameraCallbacks() : mStarted(false), mExit(false), mPreview(false),
            mPending(false), mMessages(0), mFaceCount(0), mNotify(NULL),
            mData(NULL), mTimestamp(NULL), mUser(NULL) {
        pthread_mutex_init(&mLock, NULL);
        pthread_cond_init(&mCondition, NULL);
    }

    ~CameraCallbacks() {
        shutdown();
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
            void *user) {
        pthread_mutex_lock(&mLock);
        mNotify = notify;
        mData = data;
        mTimestamp = timestamp;
        mUser = user;
        mPending = false;
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
        pthread_mutex_unlock(&mLock);
    }

    void preview(bool active) {
        pthread_mutex_lock(&mLock);
        mPreview = active;
        mPending = false;
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

        // A combined FRAME|METADATA callback must be split. Leaving the
        // metadata bit on the synchronous frame callback recreates the hang.
        bool split = (type & CAMERA_MSG_PREVIEW_METADATA) != 0;
        type &= ~CAMERA_MSG_PREVIEW_METADATA;
        if (cb && type) cb(type, memory, index, split ? NULL : metadata, user);
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

    static void *threadMain(void *cookie) {
        static_cast<CameraCallbacks *>(cookie)->run();
        return NULL;
    }

    void run() {
        for (;;) {
            camera_face_t faces[MAX_FACES];
            camera_frame_metadata_t metadata;
            pthread_mutex_lock(&mLock);
            while (!mExit && !mPending) pthread_cond_wait(&mCondition, &mLock);
            if (mExit) {
                pthread_mutex_unlock(&mLock);
                return;
            }
            metadata.number_of_faces = mFaceCount;
            memcpy(faces, mFaces, mFaceCount * sizeof(camera_face_t));
            metadata.faces = mFaceCount ? faces : NULL;
            camera_data_callback cb = mData;
            void *user = mUser;
            mPending = false;
            pthread_mutex_unlock(&mLock);
            if (cb) cb(CAMERA_MSG_PREVIEW_METADATA, NULL, 0, &metadata, user);
        }
    }

    CameraCallbacks(const CameraCallbacks &);
    CameraCallbacks &operator=(const CameraCallbacks &);
};
#endif
