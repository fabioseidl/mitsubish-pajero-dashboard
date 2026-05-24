#include "data_aggregator.h"
#include "pid_map.h"
#include <string.h>

#ifdef UNIT_TEST
#include <mutex>
static inline void mtx_lock(void* m)   { static_cast<std::mutex*>(m)->lock(); }
static inline void mtx_unlock(void* m) { static_cast<std::mutex*>(m)->unlock(); }
#else
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static inline void mtx_lock(void* m)   { xSemaphoreTake(static_cast<SemaphoreHandle_t>(m), portMAX_DELAY); }
static inline void mtx_unlock(void* m) { xSemaphoreGive(static_cast<SemaphoreHandle_t>(m)); }
#endif

DataAggregator::DataAggregator() : mil_on_(false), dtc_count_(0) {
    memset(values_, 0, sizeof(values_));
    memset(valid_,  0, sizeof(valid_));
#ifdef UNIT_TEST
    mutex_ = new std::mutex();
#else
    mutex_ = xSemaphoreCreateMutex();
#endif
}

DataAggregator::~DataAggregator() {
#ifdef UNIT_TEST
    delete static_cast<std::mutex*>(mutex_);
#else
    vSemaphoreDelete(static_cast<SemaphoreHandle_t>(mutex_));
#endif
}

void DataAggregator::update(uint16_t pid, float value) {
    mtx_lock(mutex_);
    values_[pid] = value;
    valid_[pid]  = true;
    mtx_unlock(mutex_);
}

void DataAggregator::updateMilStatus(bool mil_on) {
    mtx_lock(mutex_);
    mil_on_ = mil_on;
    mtx_unlock(mutex_);
}

void DataAggregator::updateDtcCount(uint8_t count) {
    mtx_lock(mutex_);
    dtc_count_ = count;
    mtx_unlock(mutex_);
}

float DataAggregator::get(uint16_t pid) const {
    mtx_lock(mutex_);
    float v = values_[pid];
    mtx_unlock(mutex_);
    return v;
}

bool DataAggregator::isValid(uint16_t pid) const {
    mtx_lock(mutex_);
    bool v = valid_[pid];
    mtx_unlock(mutex_);
    return v;
}

bool DataAggregator::getMilStatus() const {
    mtx_lock(mutex_);
    bool m = mil_on_;
    mtx_unlock(mutex_);
    return m;
}

uint8_t DataAggregator::getDtcCount() const {
    mtx_lock(mutex_);
    uint8_t c = dtc_count_;
    mtx_unlock(mutex_);
    return c;
}

bool DataAggregator::allRequiredPidsReceived() const {
    mtx_lock(mutex_);
    bool r = valid_[PID_RPM] && valid_[PID_SPEED] && valid_[PID_FUEL_RATE];
    mtx_unlock(mutex_);
    return r;
}

void DataAggregator::reset() {
    mtx_lock(mutex_);
    memset(values_, 0, sizeof(values_));
    memset(valid_,  0, sizeof(valid_));
    mil_on_    = false;
    dtc_count_ = 0;
    mtx_unlock(mutex_);
}
