#define LOG_TAG "AudioALSAFMController"

#include "AudioALSAFMController.h"

#include <sys/ioctl.h>
#include <cutils/properties.h>

#include "WCNChipController.h"

#include <AudioLock.h>
#include "AudioALSADriverUtility.h"
#include "AudioALSAHardwareResourceManager.h"
//#include "AudioALSAVolumeController.h"
//#include "AudioVolumeInterface.h"

#include "AudioVolumeFactory.h"
#include "AudioALSAStreamManager.h"

#include "AudioALSASampleRateController.h"

#include "AudioALSADeviceParser.h"

#if defined(MTK_HYBRID_NLE_SUPPORT)
#include "AudioALSANLEController.h"
#endif


namespace android {

/*==============================================================================
 *                     Property keys
 *============================================================================*/

const char PROPERTY_KEY_FM_FORCE_DIRECT_MODE_TYPE[PROPERTY_KEY_MAX]  = "af.fm.force_direct_mode_type";

/*==============================================================================
 *                     Const Value
 *============================================================================*/

/*==============================================================================
 *                     Enumerator
 *============================================================================*/

enum fm_force_direct_mode_t {
    FM_FORCE_NONE           = 0x0,
    FM_FORCE_DIRECT_MODE    = 0x1,
    FM_FORCE_INDIRECT_MODE  = 0x2,
};

/*==============================================================================
 *                     Singleton Pattern
 *============================================================================*/

AudioALSAFMController *AudioALSAFMController::mAudioALSAFMController = NULL;

AudioALSAFMController *AudioALSAFMController::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSAFMController == NULL) {
        mAudioALSAFMController = new AudioALSAFMController();
    }
    ASSERT(mAudioALSAFMController != NULL);
    return mAudioALSAFMController;
}

/*==============================================================================
 *                     Constructor / Destructor / Init / Deinit
 *============================================================================*/

AudioALSAFMController::AudioALSAFMController() :
    mFmDeviceCallback(NULL),
    mHardwareResourceManager(AudioALSAHardwareResourceManager::getInstance()),
    mAudioALSAVolumeController(AudioVolumeFactory::CreateAudioVolumeController()),
    mFmEnable(false),
    mIsFmDirectConnectionMode(true),
    mUseFmDirectConnectionMode(true),
    mFmVolume(0.0), // valid volume value: 0.0 ~ 1.0
    mPcm(NULL),
    mOuput_device(AUDIO_DEVICE_NONE),
    mHyBridNLERegister(false) {
    ALOGD("%s()", __FUNCTION__);

    memset(&mConfig, 0, sizeof(mConfig));
}

AudioALSAFMController::~AudioALSAFMController() {
    ALOGD("%s()", __FUNCTION__);
}

/*==============================================================================
 *                     FM Control
 *============================================================================*/

bool AudioALSAFMController::checkFmNeedUseDirectConnectionMode() {
    return mUseFmDirectConnectionMode;
}


bool AudioALSAFMController::getFmEnable() {
    AL_AUTOLOCK(mLock);
    ALOGV("%s(), mFmEnable = %d", __FUNCTION__, mFmEnable);
    return mFmEnable;
}

status_t AudioALSAFMController::setFmEnable(const bool enable, const audio_devices_t output_device, bool bForceControl, bool bForce2DirectConn, bool bNeedSyncVolume __unused) { // TODO(Harvey)
    // Lock to Protect HW Registers & AudioMode
    // TODO(Harvey): get stream manager lock here?

    AL_AUTOLOCK(mLock);

    ALOGD("+%s(), mFmEnable = %d => enable = %d, output_device = 0x%x, bForceControl = %d,  bForce2DirectConn = %d",
          __FUNCTION__, mFmEnable, enable, output_device, bForceControl, bForce2DirectConn);


    if (WCNChipController::GetInstance()->IsSupportFM() == false) {
        ALOGW("-%s(), Don't support FM in the platform", __FUNCTION__);
        return INVALID_OPERATION;
    }

    // Check Current Status
    if (enable == mFmEnable) {
        ALOGW("-%s(), enable == mFmEnable, return.", __FUNCTION__);
        return NO_ERROR;
    }

    // Update Enable Status
    mFmEnable = enable;
    // update output  device
    mOuput_device = output_device;

#if defined(MTK_AUDIO_SW_DRE) && defined(MTK_NEW_VOL_CONTROL)
    AudioMTKGainController::getInstance()->setFmEnable(enable);
#endif

    AudioALSASampleRateController *pAudioALSASampleRateController = AudioALSASampleRateController::getInstance();

    if (mFmEnable == true) { // Open
#if 0 // local print only
        ALOGD("IsFMMergeInterfaceSupported = %d", WCNChipController::GetInstance()->IsFMMergeInterfaceSupported());
        ALOGD("IsFmChipPadSelConnSys = %d", WCNChipController::GetInstance()->IsFmChipPadSelConnSys());
        ALOGD("IsFmChipUseSlaveMode = %d", WCNChipController::GetInstance()->IsFmChipUseSlaveMode());
        ALOGD("GetFmChipSamplingRate = %d", WCNChipController::GetInstance()->GetFmChipSamplingRate());

        ALOGD("IsBTMergeInterfaceSupported = %d", WCNChipController::GetInstance()->IsBTMergeInterfaceSupported());
        ALOGD("BTChipHWInterface = %d", WCNChipController::GetInstance()->BTChipHWInterface());
        ALOGD("BTUseCVSDRemoval = %d", WCNChipController::GetInstance()->BTUseCVSDRemoval());
        ALOGD("BTChipSamplingRate = %d", WCNChipController::GetInstance()->BTChipSamplingRate());
        ALOGD("BTChipSamplingRateNumber = %d", WCNChipController::GetInstance()->BTChipSamplingRateNumber());
        ALOGD("BTChipSyncFormat = %d", WCNChipController::GetInstance()->BTChipSyncFormat());
        ALOGD("BTChipSyncLength = %d", WCNChipController::GetInstance()->BTChipSyncLength());
        ALOGD("BTChipSecurityHiLo = %d", WCNChipController::GetInstance()->BTChipSecurityHiLo());

        ALOGD("GetBTCurrentSamplingRateNumber = %d", WCNChipController::GetInstance()->GetBTCurrentSamplingRateNumber());
#endif

#if defined(MTK_BASIC_PACKAGE)||defined(MTK_BSP_PACKAGE)
        if (!isPreferredSampleRate(getFmDownlinkSamplingRate()))
#endif
        {
            if(checkFmNeedUseDirectConnectionMode() == true)
                pAudioALSASampleRateController->setPrimaryStreamOutSampleRate(48000);
        }

        if (true != bForceControl || false != bForce2DirectConn) { //If indirect mode, normal handler will set PLAYBACK_SCENARIO_STREAM_OUT
            pAudioALSASampleRateController->setScenarioStatus(PLAYBACK_SCENARIO_FM);
        }

        if (WCNChipController::GetInstance()->IsFMMergeInterfaceSupported() == true) {
            //I2S fixed at 32K Hz
            WCNChipController::GetInstance()->SetFmChipSampleRate(getFmDownlinkSamplingRate());
        }


        // Set Audio Digital/Analog HW Register
        // if (checkFmNeedUseDirectConnectionMode(output_device) == true)
        // {
        if (true == bForceControl && false == bForce2DirectConn) {
            mIsFmDirectConnectionMode = false;
        } else {
            // acquire pmic clk
            mHardwareResourceManager->EnableAudBufClk(true);

            setFmDirectConnection(true, true);
            mHardwareResourceManager->startOutputDevice(output_device, getFmDownlinkSamplingRate());

            mAudioALSAVolumeController->setMasterVolume(mAudioALSAVolumeController->getMasterVolume(),
                                                        AudioALSAStreamManager::getInstance()->getModeForGain(),
                                                        output_device);
        }
        // }
        // else
        // {
        // mIsFmDirectConnectionMode = false;
        // }

        // Set Direct/Indirect Mode to FMAudioPlayer
        // doDeviceChangeCallback();
    } else { // Close
        // Disable Audio Digital/Analog HW Register
        if (mIsFmDirectConnectionMode == true) {
            mHardwareResourceManager->stopOutputDevice();

            //release pmic clk
            mHardwareResourceManager->EnableAudBufClk(false);
        }
        setFmDirectConnection(false, true);

        // reset FM playback status
        pAudioALSASampleRateController->resetScenarioStatus(PLAYBACK_SCENARIO_FM);
    }

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSAFMController::routing(const audio_devices_t pre_device, const audio_devices_t new_device) {
    AL_AUTOLOCK(mLock);

    ASSERT(mFmEnable == true);

    ALOGD("+%s(), pre_device = 0x%x, new_device = 0x%x", __FUNCTION__, pre_device, new_device);

    if (WCNChipController::GetInstance()->IsSupportFM() == false) {
        ALOGW("-%s(), Don't support FM in the platform", __FUNCTION__);
        return INVALID_OPERATION;
    }

    // Close
    if (mIsFmDirectConnectionMode == true) { // Direct mode, close it directly
        // wait until HW Gain stable
        setFmVolume(0.0);// make sure
        usleep(430000); // -74.5/0.25 * 64 / 44100 = 430 ms
        mHardwareResourceManager->stopOutputDevice();
    }

    mOuput_device = new_device;

    // Open
    setFmDirectConnection(checkFmNeedUseDirectConnectionMode(), false);

    // Set Direct/Indirect Mode for FM Chip
    // doDeviceChangeCallback();

    // Enable PMIC Analog Part
    if (mIsFmDirectConnectionMode == true) { // Direct mode, open it directly
        mHardwareResourceManager->startOutputDevice(new_device, getFmDownlinkSamplingRate());
        setFmVolume(mFmVolume);
    }

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}

void AudioALSAFMController::setFmDeviceCallback(const AUDIO_DEVICE_CHANGE_CALLBACK_STRUCT *callback_data) {
    if (callback_data == NULL) {
        mFmDeviceCallback = NULL;
    } else {
        mFmDeviceCallback = callback_data->callback;
        ASSERT(mFmDeviceCallback != NULL);
    }
}

status_t AudioALSAFMController::doDeviceChangeCallback() {
    ALOGD("+%s(), mIsFmDirectConnectionMode = %d, callback = %p", __FUNCTION__, mIsFmDirectConnectionMode, mFmDeviceCallback);

    ASSERT(mFmEnable == true);

    if (mFmDeviceCallback == NULL) { // factory mode might not set mFmDeviceCallback
        ALOGE("-%s(), mFmDeviceCallback == NULL", __FUNCTION__);
        return NO_INIT;
    }


    if (mIsFmDirectConnectionMode == true) {
        mFmDeviceCallback((void *)false); // Direct Mode, No need to create in/out stream
        ALOGD("-%s(), mFmDeviceCallback(false)", __FUNCTION__);
    } else {
        mFmDeviceCallback((void *)true);  // Indirect Mode, Need to create in/out stream
        ALOGD("-%s(), mFmDeviceCallback(true)", __FUNCTION__);
    }

    return NO_ERROR;
}

bool AudioALSAFMController::isPreferredSampleRate(uint32_t rate) const {
    switch (rate) {
    case 44100:
    case 48000:
        return true;
    default:
        return false;
    }
}

/*==============================================================================
 *                     Audio HW Control
 *============================================================================*/

uint32_t AudioALSAFMController::getFmUplinkSamplingRate() const {
    uint32_t Rate = AudioALSASampleRateController::getInstance()->getPrimaryStreamOutSampleRate();

    if (Rate != 48000 && Rate != 44100) {
        return 44100;
    } else {
        return Rate;
    }
}

uint32_t AudioALSAFMController::getFmDownlinkSamplingRate() const {
    return AudioALSASampleRateController::getInstance()->getPrimaryStreamOutSampleRate();
}

status_t AudioALSAFMController::setFmDirectConnection(const bool enable, const bool bforce) {
    ALOGD("+%s(), enable = %d, bforce = %d", __FUNCTION__, enable, bforce);

    // Check Current Status
    if (mIsFmDirectConnectionMode == enable && bforce == false) {
        ALOGW("-%s(), enable = %d, bforce = %d", __FUNCTION__, enable, bforce);
        return INVALID_OPERATION;
    }


    // Apply
    if (enable == true) {
        memset(&mConfig, 0, sizeof(mConfig));
        mConfig.channels = 2;
        mConfig.rate = getFmDownlinkSamplingRate();
        mConfig.period_size = 3072;
        mConfig.period_count = 2;
        mConfig.format = PCM_FORMAT_S16_LE;
        mConfig.start_threshold = 0;
        mConfig.stop_threshold = 0;
        mConfig.silence_threshold = 0;

        // Get pcm open Info
        int card_index = -1;
        int pcm_index = -1;

        if (mPcm == NULL) {
            AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());
            if (WCNChipController::GetInstance()->IsFMMergeInterfaceSupported() == true) {
                card_index = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmMRGrxPlayback);
                pcm_index = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmMRGrxPlayback);
            } else {
                card_index = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmFMI2SPlayback);
                pcm_index = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmFMI2SPlayback);
            }

            ALOGD("%s(), card_index = %d, pcm_index = %d", __FUNCTION__, card_index, pcm_index);
            mPcm = pcm_open(card_index, pcm_index, PCM_OUT, &mConfig);
            ALOGD("%s(), pcm_open mPcm = %p", __FUNCTION__, mPcm);
#if defined(MTK_HYBRID_NLE_SUPPORT)
            if (mPcm != NULL) {
                if (mHyBridNLERegister == false && AudioALSAHyBridNLEManager::getInstance()->addHwPathStream(AUDIO_STREAM_HW_FM) == NO_ERROR) {
                    mHyBridNLERegister = true;
                } else {
                    ALOGE("%s, Err mHyBridNLERegister %d", __FUNCTION__, mHyBridNLERegister);
                }
            }
#endif
        }
        if (mPcm == NULL || pcm_is_ready(mPcm) == false) {
            ALOGE("%s(), Unable to open mPcm device %u (%s)", __FUNCTION__, pcm_index, pcm_get_error(mPcm));
        }
        pcm_start(mPcm);
    } else {
        if (mPcm != NULL) {
            AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());

            pcm_stop(mPcm);
            pcm_close(mPcm);
            mPcm = NULL;
#if defined(MTK_HYBRID_NLE_SUPPORT)
            if (mHyBridNLERegister) {
                if (AudioALSAHyBridNLEManager::getInstance()->removeHwPathStream(AUDIO_STREAM_HW_FM) == NO_ERROR) {
                    mHyBridNLERegister = false;
                } else {
                    ALOGE("%s, Err removeHwPathStream Fail", __FUNCTION__);
                }
            }
#endif
        }
    }


    // Update Direct Mode Status
    mIsFmDirectConnectionMode = enable;

    // Update (HW_GAIN2) Volume for Direct Mode Only
    if (mIsFmDirectConnectionMode == true) {
        setFmVolume(mFmVolume);
    }


    ALOGD("-%s(), enable = %d, bforce = %d", __FUNCTION__, enable, bforce);
    return NO_ERROR;
}

status_t AudioALSAFMController::setFmVolume(const float fm_volume) {
    ALOGD("%s(), mFmVolume = %f => fm_volume = %f", __FUNCTION__, mFmVolume, fm_volume);

    const float kMaxFmVolume = 1.0;
    ASSERT(0 <= fm_volume && fm_volume <= kMaxFmVolume); // valid volume value: 0.0 ~ 1.0

    mFmVolume = fm_volume;

    if (WCNChipController::GetInstance()->IsSupportFM() == false) {
        ALOGW("-%s(), Don't support FM in the platform", __FUNCTION__);
        return INVALID_OPERATION;
    }

    // Set HW Gain for Direct Mode // TODO(Harvey): FM Volume
    if (mFmEnable == true && mIsFmDirectConnectionMode == true) {
        mAudioALSAVolumeController->setFmVolume(mFmVolume);
    } else {
        ALOGD("%s(), Do nothing. mFMEnable = %d, mIsFmDirectConnectionMode = %d", __FUNCTION__, mFmEnable, mIsFmDirectConnectionMode);
    }

    return NO_ERROR;
}

/*==============================================================================
 *                     WCN FM Chip Control
 *============================================================================*/

bool AudioALSAFMController::getFmChipPowerInfo() {
    return WCNChipController::GetInstance()->GetFmChipPowerInfo();
}

} // end of namespace android
