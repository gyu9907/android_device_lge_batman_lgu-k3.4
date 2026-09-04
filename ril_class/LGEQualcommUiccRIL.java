/*
 * Copyright (C) 2012-2015 The CyanogenMod Project
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

package com.android.internal.telephony;

import static com.android.internal.telephony.RILConstants.*;

import android.content.Context;
import android.os.AsyncResult;
import android.os.Message;
import android.os.Parcel;
import android.provider.Settings;
import android.telephony.Rlog;
import android.telephony.SignalStrength;
import android.text.TextUtils;

import com.android.internal.telephony.uicc.IccCardApplicationStatus;
import com.android.internal.telephony.uicc.IccCardStatus;

/**
 * CM 12.1 adapter for the legacy LGE MSM8660/MDM9600 QMI RIL.
 *
 * The stock LGE RIL appends four PIN/PUK retry counters to each UICC
 * application. AOSP's responseIccCardStatus() does not consume those fields,
 * which shifts the Parcel and prevents the SIM application from being parsed.
 *
 * {@hide}
 */
public class LGEQualcommUiccRIL extends RIL implements CommandsInterface {
    private String mAid = "";
    private boolean mUSIM;
    private boolean mLogicalAirplaneMode;

    public LGEQualcommUiccRIL(Context context, int networkMode,
            int cdmaSubscription) {
        super(context, networkMode, cdmaSubscription);
        initLgeRil();
    }

    public LGEQualcommUiccRIL(Context context, int networkMode,
            int cdmaSubscription, Integer instanceId) {
        super(context, networkMode, cdmaSubscription, instanceId);
        initLgeRil();
    }

    private void initLgeRil() {
        // The legacy Qualcomm RIL returns five strings per available network.
        mQANElements = 5;
        riljLog("LGEQualcommUiccRIL: using MSM8660/MDM9600 compatibility");
    }

    @Override
    public void setRadioPower(boolean on, Message result) {
        /*
         * Batman's SVLTE QMI proxy keeps the external MDM in OFFLINE while
         * the AP modem is ONLINE. On airplane-mode power-off the vendor RIL
         * waits for both modems to enter LPM, but the already-OFFLINE MDM
         * never reports that transition. Keep the physical modem state and
         * expose the requested logical state to telephony instead.
         */
        boolean airplaneModeOn = Settings.Global.getInt(
                mContext.getContentResolver(), Settings.Global.AIRPLANE_MODE_ON, 0) != 0;

        if (!on && airplaneModeOn) {
            riljLog("setRadioPower: logical airplane-mode power off");
            mLogicalAirplaneMode = true;
            setRadioState(RadioState.RADIO_OFF);
            completeRadioPowerRequest(result);
            return;
        }

        if (on && mLogicalAirplaneMode) {
            riljLog("setRadioPower: logical airplane-mode power on");
            mLogicalAirplaneMode = false;
            setRadioState(RadioState.RADIO_ON);
            completeRadioPowerRequest(result);
            return;
        }

        super.setRadioPower(on, result);
    }

    private static void completeRadioPowerRequest(Message result) {
        if (result != null) {
            AsyncResult.forMessage(result, null, null);
            result.sendToTarget();
        }
    }

    private void requestNotSupported(String request, Message result) {
        riljLog(request + " is not supported by the legacy LGE RIL");
        if (result != null) {
            CommandException ex = new CommandException(
                    CommandException.Error.REQUEST_NOT_SUPPORTED);
            AsyncResult.forMessage(result, null, ex);
            result.sendToTarget();
        }
    }

    @Override
    protected void processUnsolicited(Parcel p, int type) {
        int originalPosition = p.dataPosition();
        int response = p.readInt();

        if (response == RIL_UNSOL_UICC_SUBSCRIPTION_STATUS_CHANGED) {
            // The legacy LGE RIL occasionally sends this indication with an
            // empty payload. CdmaSubscriptionSourceManager assumes one status
            // integer and crashes the phone process when given int[0].
            if (p.dataAvail() < 2 * Integer.SIZE / Byte.SIZE) {
                riljLog("Ignoring malformed UICC subscription status: "
                        + p.dataAvail() + " payload bytes");
                return;
            }

            int count = p.readInt();
            if (count < 1 || p.dataAvail() < Integer.SIZE / Byte.SIZE) {
                riljLog("Ignoring empty UICC subscription status indication");
                return;
            }

            int status = p.readInt();
            if (status != 0 && status != 1) {
                riljLog("Ignoring invalid UICC subscription status: "
                        + status);
                return;
            }

            if (mSubscriptionStatusRegistrants != null) {
                mSubscriptionStatusRegistrants.notifyRegistrants(
                        new AsyncResult(null, new int[] { status }, null));
            }
            return;
        }

        p.setDataPosition(originalPosition);
        super.processUnsolicited(p, type);
    }

    @Override
    public void getCellInfoList(Message result) {
        requestNotSupported("RIL_REQUEST_GET_CELL_INFO_LIST", result);
    }

    @Override
    public void setCellInfoListRate(int rateInMillis, Message result) {
        requestNotSupported("RIL_REQUEST_SET_UNSOL_CELL_INFO_LIST_RATE", result);
    }

    @Override
    public void setInitialAttachApn(String apn, String protocol, int authType,
            String username, String password, Message result) {
        requestNotSupported("RIL_REQUEST_SET_INITIAL_ATTACH_APN", result);
    }

    @Override
    public void nvReadItem(int itemId, Message result) {
        requestNotSupported("RIL_REQUEST_NV_READ_ITEM", result);
    }

    @Override
    public void nvWriteItem(int itemId, String itemValue, Message result) {
        requestNotSupported("RIL_REQUEST_NV_WRITE_ITEM", result);
    }

    @Override
    public void nvWriteCdmaPrl(byte[] preferredRoamingList, Message result) {
        requestNotSupported("RIL_REQUEST_NV_WRITE_CDMA_PRL", result);
    }

    @Override
    public void nvResetConfig(int resetType, Message result) {
        requestNotSupported("RIL_REQUEST_NV_RESET_CONFIG", result);
    }

    @Override
    public void getHardwareConfig(Message result) {
        requestNotSupported("RIL_REQUEST_GET_HARDWARE_CONFIG", result);
    }

    @Override
    public void requestIccSimAuthentication(int authContext, String data,
            String aid, Message result) {
        requestNotSupported("RIL_REQUEST_SIM_AUTHENTICATION", result);
    }

    @Override
    public void requestShutdown(Message result) {
        requestNotSupported("RIL_REQUEST_SHUTDOWN", result);
    }

    @Override
    public void supplyIccPin2(String pin, Message result) {
        supplyIccPin2ForApp(pin, mAid, result);
    }

    @Override
    public void changeIccPin2(String oldPin2, String newPin2, Message result) {
        changeIccPin2ForApp(oldPin2, newPin2, mAid, result);
    }

    @Override
    public void supplyIccPuk(String puk, String newPin, Message result) {
        supplyIccPukForApp(puk, newPin, mAid, result);
    }

    @Override
    public void supplyIccPuk2(String puk2, String newPin2, Message result) {
        supplyIccPuk2ForApp(puk2, newPin2, mAid, result);
    }

    @Override
    public void queryFacilityLock(String facility, String password,
            int serviceClass, Message result) {
        queryFacilityLockForApp(facility, password, serviceClass, mAid, result);
    }

    @Override
    public void setFacilityLock(String facility, boolean lockState,
            String password, int serviceClass, Message result) {
        setFacilityLockForApp(facility, lockState, password, serviceClass,
                mAid, result);
    }

    @Override
    public void getIMSI(Message result) {
        RILRequest rr = RILRequest.obtain(RIL_REQUEST_GET_IMSI, result);
        rr.mParcel.writeInt(1);
        rr.mParcel.writeString(mAid);
        riljLog(rr.serialString() + "> " + requestToString(rr.mRequest)
                + " aid=" + mAid);
        send(rr);
    }

    @Override
    public void iccIO(int command, int fileId, String path, int p1, int p2,
            int p3, String data, String pin2, Message result) {
        if (mUSIM && path != null) {
            path = path.replaceAll("7F20$", "7FFF");
        }
        iccIOForApp(command, fileId, path, p1, p2, p3, data, pin2, mAid,
                result);
    }

    @Override
    protected Object responseIccCardStatus(Parcel p) {
        int cardState = p.readInt();

        // LGE also reports REMOVED/INSERTED states. Map them to AOSP states.
        if (cardState > 2) {
            cardState -= 3;
        }

        IccCardStatus status = new IccCardStatus();
        status.setCardState(cardState);
        status.setUniversalPinState(p.readInt());
        status.mGsmUmtsSubscriptionAppIndex = p.readInt();
        status.mCdmaSubscriptionAppIndex = p.readInt();
        status.mImsSubscriptionAppIndex = p.readInt();

        int reportedApplications = p.readInt();
        int numApplications = Math.max(0, Math.min(reportedApplications,
                IccCardStatus.CARD_MAX_APPS));
        status.mApplications =
                new IccCardApplicationStatus[numApplications];

        for (int i = 0; i < reportedApplications; i++) {
            IccCardApplicationStatus app = new IccCardApplicationStatus();
            app.app_type = app.AppTypeFromRILInt(p.readInt());
            app.app_state = app.AppStateFromRILInt(p.readInt());
            app.perso_substate = app.PersoSubstateFromRILInt(p.readInt());
            app.aid = p.readString();
            app.app_label = p.readString();
            app.pin1_replaced = p.readInt();
            app.pin1 = app.PinStateFromRILInt(p.readInt());
            app.pin2 = app.PinStateFromRILInt(p.readInt());

            // Present in the LGE UICC response but absent from AOSP's struct.
            p.readInt(); // remaining_count_pin1
            p.readInt(); // remaining_count_puk1
            p.readInt(); // remaining_count_pin2
            p.readInt(); // remaining_count_puk2

            if (i < numApplications) {
                status.mApplications[i] = app;
            }
        }

        int appIndex = mPhoneType == RILConstants.CDMA_PHONE
                ? status.mCdmaSubscriptionAppIndex
                : status.mGsmUmtsSubscriptionAppIndex;

        mAid = "";
        mUSIM = false;
        if (cardState != 0 && appIndex >= 0 && appIndex < numApplications) {
            IccCardApplicationStatus app = status.mApplications[appIndex];
            mAid = TextUtils.isEmpty(app.aid) ? "" : app.aid;
            mUSIM = app.app_type
                    == IccCardApplicationStatus.AppType.APPTYPE_USIM;
        }

        Rlog.d(RILJ_LOG_TAG, "LGE UICC: phoneType=" + mPhoneType
                + " appIndex=" + appIndex + " aid=" + mAid
                + " usim=" + mUSIM);
        return status;
    }

    @Override
    protected Object responseSetupDataCall(Parcel p) {
        if (needsOldRilFeature("datacall")) {
            // The LGE response has one leading string before the legacy body.
            p.readString();
        }
        return super.responseSetupDataCall(p);
    }

    @Override
    protected Object responseSignalStrength(Parcel p) {
        int[] response = new int[12];
        boolean noLte = false;
        boolean oldRil = needsOldRilFeature("signalstrength");

        for (int i = 0; i < response.length; i++) {
            if ((oldRil || noLte) && i > 6) {
                response[i] = -1;
            } else {
                response[i] = p.readInt();
            }
            if (i == 7 && response[i] == 99) {
                response[i] = -1;
                noLte = true;
            }
        }

        return new SignalStrength(response[0], response[1], response[2],
                response[3], response[4], response[5], response[6],
                response[7], response[8], response[9], response[10],
                response[11], true);
    }
}
