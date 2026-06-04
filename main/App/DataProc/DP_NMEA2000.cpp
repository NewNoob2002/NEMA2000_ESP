#include "DataProc.h"

#include <stdint.h>

#include "HAL/HAL.h"
#include "N2kGroupFunction.h"
#include "N2kTimer.h"
#include "NMEA2000_esp32.h"
#include "Unicore_UM980.h"
#include "nmea0183_to_n2k.h"

namespace {

const char* kTag = "[DP_NMEA2000]";

constexpr uint32_t kNmea2000TimerPeriodMs = 10;
constexpr uint32_t kNmea2000DefaultGnssSendPeriodMs = 1000;
constexpr uint32_t kNmea2000MinGnssSendPeriodMs = 100;
constexpr uint32_t kNmea2000MaxGnssSendPeriodMs = 60000;
constexpr uint16_t kNmea2000MaxGnssSendOffset10Ms = 6000;

struct tN2kTxSchedule {
    unsigned long PGN;
    uint32_t DefaultIntervalMs;
    uint32_t DefaultOffsetMs;
    uint32_t IntervalMs;
    uint32_t OffsetMs;
    uint32_t NextSendMs;
    bool Enabled;
};

tNMEA2000_esp32 nmea2000;
tN2kTxSchedule txSchedules[] = {
    {129025L, kNmea2000DefaultGnssSendPeriodMs, 0, kNmea2000DefaultGnssSendPeriodMs, 0, 0, true},
    {129026L, kNmea2000DefaultGnssSendPeriodMs, 20, kNmea2000DefaultGnssSendPeriodMs, 20, 0, true},
    {129029L, kNmea2000DefaultGnssSendPeriodMs, 40, kNmea2000DefaultGnssSendPeriodMs, 40, 0, true},
};
bool nmea2000Ready = false;
uint32_t n2kSent = 0;
uint32_t n2kFailed = 0;

class tGatewayPgnGroupFunctionHandler : public tN2kGroupFunctionHandler {
  public:
    tGatewayPgnGroupFunctionHandler(tNMEA2000* nmea2000, tN2kTxSchedule* schedule)
        : tN2kGroupFunctionHandler(nmea2000, schedule ? schedule->PGN : 0), Schedule(schedule) {}

  protected:
    bool
    HandleRequest(const tN2kMsg& request, uint32_t transmissionInterval, uint16_t transmissionIntervalOffset,
                  uint8_t numberOfParameterPairs, int deviceIndex) override {
        if (Schedule == nullptr) {
            return false;
        }

        tN2kGroupFunctionTransmissionOrPriorityErrorCode intervalError =
            GetRequestGroupFunctionTransmissionOrPriorityErrorCode(
                transmissionInterval, transmissionIntervalOffset, true, kNmea2000MaxGnssSendPeriodMs,
                kNmea2000MinGnssSendPeriodMs, true, kNmea2000MaxGnssSendOffset10Ms);

        tN2kGroupFunctionParameterErrorCode parameterError = N2kgfpec_Acknowledge;
        if (numberOfParameterPairs != 0) {
            parameterError = N2kgfpec_RequestOrCommandNotSupported;
        }

        if (intervalError == N2kgfTPec_Acknowledge && parameterError == N2kgfpec_Acknowledge) {
            ApplyRequest(transmissionInterval, transmissionIntervalOffset);
        }

        if (!tNMEA2000::IsBroadcast(request.Destination)) {
            SendAcknowledge(pNMEA2000, request.Source, deviceIndex, Schedule->PGN, N2kgfPGNec_Acknowledge,
                            intervalError, numberOfParameterPairs, parameterError);
        }
        return true;
    }

  private:
    tN2kTxSchedule* Schedule;

    void
    ApplyRequest(uint32_t transmissionInterval, uint16_t transmissionIntervalOffset) {
        switch (transmissionInterval) {
            case N2k_KEEP_TRANSMISSION_INTERVAL: break;
            case N2k_RESTORE_TRANSMISSION_INTERVAL:
                Schedule->IntervalMs = Schedule->DefaultIntervalMs;
                Schedule->OffsetMs = Schedule->DefaultOffsetMs;
                Schedule->Enabled = true;
                break;
            case 0: Schedule->Enabled = false; break;
            default:
                Schedule->IntervalMs = transmissionInterval;
                Schedule->Enabled = true;
                break;
        }

        if (transmissionIntervalOffset != 0xffff) {
            Schedule->OffsetMs = static_cast<uint32_t>(transmissionIntervalOffset) * 10U;
        }
        Schedule->NextSendMs = N2kMillis() + Schedule->OffsetMs;
    }
};

tGatewayPgnGroupFunctionHandler txGroupHandlers[] = {
    {&nmea2000, &txSchedules[0]},
    {&nmea2000, &txSchedules[1]},
    {&nmea2000, &txSchedules[2]},
};

bool
sendN2kMessage(const tN2kMsg& message) {
    if (!nmea2000Ready) {
        return false;
    }

    if (nmea2000.SendMsg(message)) {
        n2kSent++;
        return true;
    }

    n2kFailed++;
    return false;
}

void
sendDueGnssMessages(uint32_t now) {
    if (HAL::gUm980 == nullptr) {
        ESP_LOGE(kTag, "GNSS not initialized");
        return;
    }

    tGatewayGnssData gnssData;
    tGatewayN2kMessages messages;
    if (!ReadGatewayGnssData(*HAL::gUm980, gnssData) || !BuildGatewayN2kMessages(gnssData, messages)) {
        return;
    }

    for (auto& schedule : txSchedules) {
        if (!schedule.Enabled) {
            continue;
        }
        if (schedule.NextSendMs == 0) {
            schedule.NextSendMs = now + schedule.OffsetMs;
        }
        if (N2kIsTimeBefore(now, schedule.NextSendMs)) {
            continue;
        }

        const tN2kMsg* message = nullptr;
        switch (schedule.PGN) {
            case 129025L: message = &messages.LatLonRapid; break;
            case 129026L: message = &messages.CogSogRapid; break;
            case 129029L: message = &messages.Gnss; break;
            default: break;
        }

        if (message != nullptr && sendN2kMessage(*message)) {
            schedule.NextSendMs = now + schedule.IntervalMs;
        }
    }
}

int
onEvent(Account* account, Account::EventParam_t* param) {
    if (!account || !param) {
        return Account::RES_PARAM_ERROR;
    }

    if (param->event == Account::EVENT_TIMER) {
        if (nmea2000Ready) {
            nmea2000.ParseMessages();
            const uint32_t now = N2kMillis();
            sendDueGnssMessages(now);
        }
        return Account::RES_OK;
    }

    return Account::RES_UNSUPPORTED_REQUEST;
}

} // namespace

DATA_PROC_INIT_DEF(NMEA2000) {
    ESP_LOGI(kTag, "NMEA2000 data processor initialized");
    account->SetEventCallback(onEvent);
    account->SetTimerPeriod(kNmea2000TimerPeriodMs);

    ConfigureGatewayNmea2000(nmea2000);
    for (auto& handler : txGroupHandlers) {
        nmea2000.AddGroupFunctionHandler(&handler);
    }
    ESP_LOGI(kTag, "NMEA data processor initialized");
    nmea2000.ClearCANStatus();
    nmea2000Ready = nmea2000.Open();
    ESP_LOGI(kTag, "NMEA2000 gateway opened");
}
