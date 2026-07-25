#include "ble_beacon.h"
#ifdef ARDUINO
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include "config.h"

static BLEAdvertising *s_adv = nullptr;

void beacon_init(void) {
    BLEDevice::init("CF-Stick");
    s_adv = BLEDevice::getAdvertising();
    s_adv->setMinInterval(BLE_ADV_INTERVAL);
    s_adv->setMaxInterval(BLE_ADV_INTERVAL);
    /* non-connectable advertising: pure beacon */
    s_adv->setAdvertisementType(ADV_TYPE_NONCONN_IND);
}

void beacon_burst_start(uint8_t seq, uint8_t batt) {
    uint8_t payload[BEACON_PAYLOAD_LEN];
    beacon_build_payload(payload, 0x01, seq, batt);
    BLEAdvertisementData data;
    data.setManufacturerData(std::string((char *)payload, BEACON_PAYLOAD_LEN));
    s_adv->stop();
    s_adv->setAdvertisementData(data);
    s_adv->start();
}

void beacon_stop(void) {
    if (s_adv) s_adv->stop();
}
#endif
