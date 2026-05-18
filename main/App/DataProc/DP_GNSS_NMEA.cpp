#include <string.h>
#include "DataProc.h"
#include "States.h"

namespace {

Account* gnssNmeaAccount = nullptr;

// bool
// startsWithIgnoreCase(const char* text, const char* prefix) {
//     if (!text || !prefix) {
//         return false;
//     }

//     while (*prefix) {
//         char lhs = *text++;
//         char rhs = *prefix++;
//         if ((lhs >= 'A') && (lhs <= 'Z')) {
//             lhs = static_cast<char>(lhs - 'A' + 'a');
//         }
//         if ((rhs >= 'A') && (rhs <= 'Z')) {
//             rhs = static_cast<char>(rhs - 'A' + 'a');
//         }
//         if (lhs != rhs) {
//             return false;
//         }
//     }
//     return true;
// }

// bool
// sentenceHasType(const char* sentence, const char* suffix) {
//     if (!sentence || !suffix || sentence[0] != '$') {
//         return false;
//     }

//     const char* nameStart = sentence + 1;
//     const char* nameEnd = strchr(nameStart, ',');
//     if (!nameEnd) {
//         return false;
//     }

//     const size_t nameLen = static_cast<size_t>(nameEnd - nameStart);
//     const size_t suffixLen = strlen(suffix);
//     if (nameLen < suffixLen) {
//         return false;
//     }

//     return strncmp(nameEnd - suffixLen, suffix, suffixLen) == 0;
// }

// bool
// isNavigationNmeaSentence(const char* sentence, uint16_t length) {
//     if (!sentence || length == 0 || sentence[0] != '$') {
//         return false;
//     }

//     if (startsWithIgnoreCase(sentence, "$CONFIG,") || startsWithIgnoreCase(sentence, "$command,")) {
//         return false;
//     }

//     static const char* const supportedTypes[] = {
//         "DTM", "GBS", "GGA", "GLL", "GNS", "GRS", "GSA", "GST", "GSV", "RMC", "ROT", "THS", "VTG", "ZDA",
//     };

//     for (const char* type : supportedTypes) {
//         if (sentenceHasType(sentence, type)) {
//             return true;
//         }
//     }
//     return false;
// }

} // namespace

void
DP_GNSS_DataHandler(const char* sentence, uint16_t length, void* userdata) {
    if (!sentence) {
        return;
    }
    if (!inRoverMode() && !inBaseMode() && !inWebConfigMode()) {
        return;
    }
    if (gnssNmeaAccount) {
        if (gnssNmeaAccount->Commit((void*)sentence, length)) {
            gnssNmeaAccount->Publish();
        }
    }
}

DATA_PROC_INIT_DEF(GNSS_NMEA) { gnssNmeaAccount = account; }
