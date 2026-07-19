#include "config_model.h"

#include <cstdint>

#include "Config.h"

ConfigModel ConfigModel::defaults() {
    ConfigModel m;
    m.beeperEnabled = true;
    m.diffThresholdC100 =
        static_cast<int16_t>(cfg::temp::kDiffThresholdC * cfg::sample::kStoreScale);
    m.diffHysteresisC100 = static_cast<int16_t>(
        (cfg::temp::kDiffThresholdC - cfg::temp::kDiffClearC) * cfg::sample::kStoreScale);
    m.fireThrC100 = static_cast<int16_t>(cfg::temp::kFireThresholdC * cfg::sample::kStoreScale);
    m.fireHysteresisC100 = static_cast<int16_t>(
        (cfg::temp::kFireThresholdC - cfg::temp::kFireClearC) * cfg::sample::kStoreScale);
    m.lcdContrastPwm = cfg::ledc::kBacklightDefault;
    m.emailEnabled   = true;
    m.windowGoal     = static_cast<uint8_t>(cfg::window_advisor::kDefaultGoal);
    m.quietFromMin   = static_cast<int16_t>(cfg::beep::kQuietFromMin);
    m.quietToMin     = static_cast<int16_t>(cfg::beep::kQuietToMin);
    return m;
}

bool ConfigModel::validate() const {
    if (diffThresholdC100 <= 0) {
        return false;
    }
    if (diffHysteresisC100 < 0 || diffHysteresisC100 >= diffThresholdC100) {
        return false;
    }
    if (fireThrC100 <= 0) {
        return false;
    }
    if (fireHysteresisC100 < 0 || fireHysteresisC100 >= fireThrC100) {
        return false;
    }
    if (windowGoal > 1U) {
        return false;
    }
    if (quietFromMin < 0 || quietFromMin > 1439 || quietToMin < 0 || quietToMin > 1439) {
        return false;
    }
    return true;
}
