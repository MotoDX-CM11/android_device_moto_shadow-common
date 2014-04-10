/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "ShadowCameraWrapper"

#include <cmath>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cutils/properties.h>
#include <camera/Camera.h>
#include "ShadowCameraWrapper.h"

namespace android {

wp<ShadowCameraWrapper> ShadowCameraWrapper::singleton;

static bool
deviceCardMatches(const char *device, const char *matchCard)
{
    struct v4l2_capability caps;
    int fd = ::open(device, O_RDWR);
    bool ret;

    if (fd < 0) {
        return false;
    }

    if (::ioctl(fd, VIDIOC_QUERYCAP, &caps) < 0) {
        ret = false;
    } else {
        const char *card = (const char *) caps.card;

        ALOGD("device %s card is %s\n", device, card);
        ret = strstr(card, matchCard) != NULL;
    }

    ::close(fd);

    return ret;
}

static sp<CameraHardwareInterface>
openMotoInterface(const char *libName, const char *funcName)
{
    sp<CameraHardwareInterface> interface;
    void *libHandle = ::dlopen(libName, RTLD_NOW);

    if (libHandle != NULL) {
        typedef sp<CameraHardwareInterface> (*OpenCamFunc)();
        OpenCamFunc func = (OpenCamFunc) ::dlsym(libHandle, funcName);
        if (func != NULL) {
            interface = func();
        } else {
            ALOGE("Could not find library entry point!");
        }
    } else {
        ALOGE("dlopen() error: %s\n", dlerror());
    }

    return interface;
}

sp<ShadowCameraWrapper> ShadowCameraWrapper::createInstance(int cameraId)
{
    ALOGV("%s :", __func__);
    if (singleton != NULL) {
        sp<ShadowCameraWrapper> hardware = singleton.promote();
        if (hardware != NULL) {
            return hardware;
        }
    }

    CameraType type = CAM_NULL;
    sp<CameraHardwareInterface> motoInterface;
    sp<ShadowCameraWrapper> hardware;

    if (deviceCardMatches("/dev/video0", "ov8810")) {
        ALOGI("Detected OmniVision OV8810 device\n");
        /* entry point of 8M Bayer driver is android::CameraHal::createInstance() */
        motoInterface = openMotoInterface("libcamera.so", "_ZN7android9CameraHal14createInstanceEv");
        type = CAM_OV8810;
    } else {
        ALOGE("Camera type detection failed");
    }

    if (motoInterface != NULL) {
        hardware = new ShadowCameraWrapper(motoInterface, type);
        singleton = hardware;
    } else {
        ALOGE("Could not open hardware interface");
    }

    return hardware;
}

ShadowCameraWrapper::ShadowCameraWrapper(sp<CameraHardwareInterface>& motoInterface, CameraType type) :
    mMotoInterface(motoInterface),
    mCameraType(type),
    mVideoMode(false),
    mNotifyCb(NULL),
    mDataCb(NULL),
    mDataCbTimestamp(NULL),
    mCbUserData(NULL)
{
}

sp<IMemoryHeap>
ShadowCameraWrapper::getPreviewHeap() const
{
    return mMotoInterface->getPreviewHeap();
}

sp<IMemoryHeap>
ShadowCameraWrapper::getRawHeap() const
{
    return mMotoInterface->getRawHeap();
}

void
ShadowCameraWrapper::setCallbacks(notify_callback notify_cb,
                                  data_callback data_cb,
                                  data_callback_timestamp data_cb_timestamp,
                                  void* user)
{
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCbUserData = user;

    if (mNotifyCb != NULL) {
        notify_cb = &ShadowCameraWrapper::notifyCb;
    }
    if (mDataCb != NULL) {
        data_cb = &ShadowCameraWrapper::dataCb;
    }
    if (mDataCbTimestamp != NULL) {
        data_cb_timestamp = &ShadowCameraWrapper::dataCbTimestamp;
    }

    mMotoInterface->setCallbacks(notify_cb, data_cb, data_cb_timestamp, this);
}

void
ShadowCameraWrapper::notifyCb(int32_t msgType, int32_t ext1, int32_t ext2, void* user)
{
    ShadowCameraWrapper *_this = (ShadowCameraWrapper *) user;
    user = _this->mCbUserData;
    _this->mNotifyCb(msgType, ext1, ext2, user);
}

void
ShadowCameraWrapper::dataCb(int32_t msgType, const sp<IMemory>& dataPtr, void* user)
{
    ShadowCameraWrapper *_this = (ShadowCameraWrapper *) user;
    user = _this->mCbUserData;

    if (msgType == CAMERA_MSG_COMPRESSED_IMAGE) {
        _this->fixUpBrokenGpsLatitudeRef(dataPtr);
    }
    _this->mDataCb(msgType, dataPtr, user);

    if ((msgType == CAMERA_MSG_RAW_IMAGE || msgType == CAMERA_MSG_COMPRESSED_IMAGE)) {
        if (_this->mTorchThread != NULL) {
            _this->mTorchThread->scheduleTorch();
        }
    }
}

void
ShadowCameraWrapper::dataCbTimestamp(nsecs_t timestamp, int32_t msgType,
                                     const sp<IMemory>& dataPtr, void* user)
{
    ShadowCameraWrapper *_this = (ShadowCameraWrapper *) user;
    user = _this->mCbUserData;

    _this->mDataCbTimestamp(timestamp, msgType, dataPtr, user);
}

/*
 * Motorola's libcamera fails in writing the GPS latitude reference
 * tag properly. Instead of writing 'N' or 'S', it writes 'W' or 'E'.
 * Below is a very hackish workaround for that: We search for the GPS
 * latitude reference tag by pattern matching into the first couple of
 * data bytes. As the output format of Motorola's libcamera is static,
 * this should be fine until Motorola fixes their lib.
 */
void
ShadowCameraWrapper::fixUpBrokenGpsLatitudeRef(const sp<IMemory>& dataPtr)
{
    ssize_t offset;
    size_t size;
    sp<IMemoryHeap> heap = dataPtr->getMemory(&offset, &size);
    uint8_t *data = (uint8_t*)heap->base();

    if (data != NULL) {
        data += offset;

        /* scan first 512 bytes for GPS latitude ref marker */
        static const unsigned char sLatitudeRefMarker[] = {
            0x01, 0x00, /* GPS Latitude ref tag */
            0x02, 0x00, /* format: string */
            0x02, 0x00, 0x00, 0x00 /* 2 bytes long */
        };

        for (size_t i = 0; i < 512 && i < (size - 10); i++) {
            if (memcmp(data + i, sLatitudeRefMarker, sizeof(sLatitudeRefMarker)) == 0) {
                char *ref = (char *) (data + i + sizeof(sLatitudeRefMarker));
                if ((*ref == 'W' || *ref == 'E') && *(ref + 1) == '\0') {
                    ALOGI("Found broken GPS latitude ref marker, offset %d, item %c",
                         i + sizeof(sLatitudeRefMarker), *ref);
                    *ref = (*ref == 'W') ? 'N' : 'S';
                }
                break;
            }
        }
    }
}

void
ShadowCameraWrapper::enableMsgType(int32_t msgType)
{
    mMotoInterface->enableMsgType(msgType);
}

void
ShadowCameraWrapper::disableMsgType(int32_t msgType)
{
    mMotoInterface->disableMsgType(msgType);
}

bool
ShadowCameraWrapper::msgTypeEnabled(int32_t msgType)
{
    return mMotoInterface->msgTypeEnabled(msgType);
}

status_t
ShadowCameraWrapper::startPreview()
{
    return mMotoInterface->startPreview();
}

bool
ShadowCameraWrapper::useOverlay()
{
    return mMotoInterface->useOverlay();
}

status_t
ShadowCameraWrapper::setOverlay(const sp<Overlay> &overlay)
{
    return mMotoInterface->setOverlay(overlay);
}

void
ShadowCameraWrapper::stopPreview()
{
    mMotoInterface->stopPreview();
}

bool
ShadowCameraWrapper::previewEnabled()
{
    return mMotoInterface->previewEnabled();
}

status_t
ShadowCameraWrapper::startRecording()
{
    return mMotoInterface->startRecording();
}

void
ShadowCameraWrapper::stopRecording()
{
    mMotoInterface->stopRecording();
}

bool
ShadowCameraWrapper::recordingEnabled()
{
    return mMotoInterface->recordingEnabled();
}

void
ShadowCameraWrapper::releaseRecordingFrame(const sp<IMemory>& mem)
{
    return mMotoInterface->releaseRecordingFrame(mem);
}

status_t
ShadowCameraWrapper::autoFocus()
{
    return mMotoInterface->autoFocus();
}

status_t
ShadowCameraWrapper::cancelAutoFocus()
{
    return mMotoInterface->cancelAutoFocus();
}

status_t
ShadowCameraWrapper::takePicture()
{
    return mMotoInterface->takePicture();
}

status_t
ShadowCameraWrapper::cancelPicture()
{
    return mMotoInterface->cancelPicture();
}

status_t
ShadowCameraWrapper::setParameters(const CameraParameters& params)
{
    CameraParameters pars(params.flatten());
    String8 sceneMode;
    status_t retval;
    int width, height;
    char buf[10];
    bool isWide;

    /*
     * getInt returns -1 if the value isn't present and 0 on parse failure,
     * so if it's larger than 0, we can be sure the value was parsed properly
     */

    mVideoMode = pars.getInt("cam-mode") > 0;
    pars.remove("cam-mode");

    pars.getPreviewSize(&width, &height);
    isWide = (float) (width/height) >= 1.5;

    if (isWide && !mVideoMode) {
        pars.setPreviewFrameRate(24);
    }
    if (mVideoMode) {
        pars.setPreviewFrameRate(24);
    }

    sceneMode = pars.get(CameraParameters::KEY_SCENE_MODE);
    if (sceneMode != CameraParameters::SCENE_MODE_AUTO) {
       /* The lib doesn't seem to update the flash mode correctly when a scene
       mode is set, so we need to do it here. Also do focus mode, just do
       be on the safe side. */
       pars.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);

       if (sceneMode == CameraParameters::SCENE_MODE_PORTRAIT ||
           sceneMode == CameraParameters::SCENE_MODE_NIGHT_PORTRAIT)
       {
           pars.set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_AUTO);
       } else {
           pars.set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);
       }
    }
    
    mFlashMode = pars.get(CameraParameters::KEY_FLASH_MODE);

    float exposure = pars.getFloat(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    /* exposure-compensation comes multiplied in the -9...9 range, while
       we need it in the -3...3 range -> adjust for that */
    exposure /= 3;

    /* format the setting in a way the lib understands */
    bool even = (exposure - round(exposure)) < 0.05;
    snprintf(buf, sizeof(buf), even ? "%.0f" : "%.2f", exposure);
    pars.set("mot-exposure-offset", buf);

    /* kill off the original setting */
    pars.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");

    retval = mMotoInterface->setParameters(pars);

    return retval;
}

CameraParameters
ShadowCameraWrapper::getParameters() const
{
    CameraParameters ret = mMotoInterface->getParameters();

    /* cut down supported effects to values supported by framework */
    ret.set(CameraParameters::KEY_SUPPORTED_EFFECTS, "none,mono,sepia,negative,solarize,red-tint,green-tint,blue-tint");

    /* Motorola uses mot-exposure-offset instead of exposure-compensation
       for whatever reason -> adapt the values.
       The limits used here are taken from the lib, we surely also
       could parse it, but it's likely not worth the hassle */
    float exposure = ret.getFloat("mot-exposure-offset");
    int exposureParam = (int) round(exposure * 3);
    int width, height;
    ret.getVideoSize(&width, &height);

    ret.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, exposureParam);
    ret.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "9");
    ret.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-9");
    ret.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.3333333333333");
    ret.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, CameraParameters::PIXEL_FORMAT_YUV422I);
    //ret.set(CameraParameters::KEY_PREVIEW_FRAME_RATE, "24");
    ret.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
    ret.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "15000,30000");
    if (!(width == 1280 && height == 720)) {
        ret.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "640x480");
    }
    ret.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES, "");

    ret.set("cam-mode", mVideoMode ? "1" : "0");

    return ret;
}

status_t
ShadowCameraWrapper::sendCommand(int32_t cmd, int32_t arg1, int32_t arg2)
{
    return mMotoInterface->sendCommand(cmd, arg1, arg2);
}

void
ShadowCameraWrapper::release()
{
    mMotoInterface->release();
}

status_t
ShadowCameraWrapper::dump(int fd, const Vector<String16>& args) const
{
    return mMotoInterface->dump(fd, args);
}

}; //namespace android
