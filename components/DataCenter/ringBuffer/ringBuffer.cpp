/*
 * MIT License
 * Copyright (c) 2023
 */
#include "ringBuffer.h"

#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include "Account.h"

ringBuffer::ringBuffer(uint32_t data_size, uint8_t averageLengthInBytes, const char* id)
    : ID(id), _rbOffsetArray(nullptr), _rbOffsetSize(0), _rbOffsetHead(0), _rbOffsetTail(0), _ringBuffer(nullptr),
      _averageLengthInBytes(averageLengthInBytes), _ringBufferSize(data_size), _freeSpace(data_size), _usedBytes(0),
      _frameCount(0), _droppedFrames(0), _releasedFrames(0), _dataHead(0), _dataTail(0), _mutex(nullptr) {}

ringBuffer::~ringBuffer() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
    free(_rbOffsetArray);
    _rbOffsetArray = nullptr;
    _ringBuffer = nullptr;
    subscriberReads.clear();
}

bool
ringBuffer::Init() {
    if ((_ringBufferSize == 0) || (_ringBufferSize > UINT16_MAX)) {
        RB_LOG_ERROR("Account[%s] invalid ring buffer size %" PRIu32, ID ? ID : "", _ringBufferSize);
        return false;
    }

    _rbOffsetSize = static_cast<uint16_t>(_ringBufferSize / _averageLengthInBytes);
    if (_rbOffsetSize < 4) {
        _rbOffsetSize = 4;
    }

    const size_t offsetBytes = static_cast<size_t>(_rbOffsetSize) * sizeof(uint16_t);
    const size_t totalBytes = offsetBytes + _ringBufferSize;
    _rbOffsetArray = static_cast<uint16_t*>(calloc(1, totalBytes));
    if (!_rbOffsetArray) {
        RB_LOG_ERROR("Account[%s] ringBuffer malloc failed", ID ? ID : "");
        return false;
    }

    _ringBuffer = reinterpret_cast<uint8_t*>(_rbOffsetArray) + offsetBytes;
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        free(_rbOffsetArray);
        _rbOffsetArray = nullptr;
        _ringBuffer = nullptr;
        RB_LOG_ERROR("Account[%s] ringBuffer mutex create failed", ID ? ID : "");
        return false;
    }

    _rbOffsetHead = 0;
    _rbOffsetTail = 0;
    _dataHead = 0;
    _dataTail = 0;
    _usedBytes = 0;
    _freeSpace = _ringBufferSize;
    _frameCount = 0;
    _droppedFrames = 0;
    _releasedFrames = 0;

    RB_LOG_INFO("Account[%s] created circular buffer Header-%p(%u entries) Data-%p(%" PRIu32 " bytes)", ID ? ID : "",
                _rbOffsetArray, _rbOffsetSize, _ringBuffer, _ringBufferSize);
    return true;
}

bool
ringBuffer::WriteToRingBuf(void* pWriteBuf, uint32_t totalSize) {
    if (!pWriteBuf || (totalSize == 0) || !_rbOffsetArray || !_ringBuffer) {
        return false;
    }
    if (totalSize > _ringBufferSize) {
        RB_LOG_WARN("Account[%s] frame too large: %" PRIu32 " > %" PRIu32, ID ? ID : "", totalSize, _ringBufferSize);
        return false;
    }
    if (!TakeLock()) {
        return false;
    }

    const uint16_t frameSize = static_cast<uint16_t>(totalSize);
    const uint16_t maxFrames = static_cast<uint16_t>(_rbOffsetSize - 1);
    while ((_frameCount >= maxFrames) || (_freeSpace < totalSize)) {
        DropOldestFrame(true);
    }

    const uint16_t start = _dataHead;
    CopyToRing(start, static_cast<uint8_t*>(pWriteBuf), frameSize);
    _dataHead = static_cast<uint16_t>((_dataHead + frameSize) % _ringBufferSize);
    _rbOffsetArray[_rbOffsetHead] = _dataHead;
    _rbOffsetHead = static_cast<uint16_t>((_rbOffsetHead + 1) % _rbOffsetSize);
    _frameCount++;
    _usedBytes += frameSize;
    _freeSpace = _ringBufferSize - _usedBytes;

    RB_LOG_INFO("%s write frame %u --> %u (size=%u frames=%" PRIu32 ")", ID ? ID : "", start, _dataHead, frameSize,
                _frameCount);
    GiveLock();
    return true;
}

bool
ringBuffer::ReadRingBuf(Account* subscriber, void* pReadBuf, uint32_t* size) {
    if (!subscriber || !pReadBuf || !size || !_rbOffsetArray || !_ringBuffer) {
        return false;
    }
    if (!TakeLock()) {
        return false;
    }

    SubscriberReadInfo_t* info = FindSubscriberInfo(subscriber);
    if (!info) {
        RB_LOG_ERROR("Subscriber not found in ringBuffer");
        GiveLock();
        return false;
    }

    if (info->readOffsetIndex == _rbOffsetHead) {
        RB_LOG_WARN("No new data for subscriber");
        GiveLock();
        return false;
    }

    const uint16_t frameEnd = _rbOffsetArray[info->readOffsetIndex];
    const uint16_t frameSize = Distance(info->readPos, frameEnd);
    if ((frameSize == 0) || (frameSize > _ringBufferSize)) {
        RB_LOG_ERROR("Account[%s] invalid frame size %u", ID ? ID : "", frameSize);
        GiveLock();
        return false;
    }
    if (*size < frameSize) {
        *size = frameSize;
        RB_LOG_WARN("Subscriber buffer too small, need %u bytes", frameSize);
        GiveLock();
        return false;
    }
#if RB_USE_LOG
    const uint16_t oldPos = info->readPos;
#endif
    CopyFromRing(info->readPos, static_cast<uint8_t*>(pReadBuf), frameSize);
    info->readPos = frameEnd;
    info->readOffsetIndex = static_cast<uint16_t>((info->readOffsetIndex + 1) % _rbOffsetSize);
    *size = frameSize;
    UpdateFreeSpaceLocked();

    RB_LOG_INFO("ReadRingBuf %s, readBy %s: %u --> %u (read %u bytes)", ID ? ID : "", subscriber->GetId(), oldPos,
                info->readPos, frameSize);
    GiveLock();
    return true;
}

void
ringBuffer::UpdateFreeSpace() {
    if (!TakeLock()) {
        return;
    }
    UpdateFreeSpaceLocked();
    GiveLock();
}

bool
ringBuffer::AddSubscriber(Account* subscriber) {
    if (!subscriber || !TakeLock()) {
        return false;
    }
    if (FindSubscriberInfo(subscriber)) {
        RB_LOG_ERROR("Subscriber already exists in ringBuffer");
        GiveLock();
        return false;
    }

    SubscriberReadInfo_t info = {};
    info.subscriber = subscriber;
    info.readPos = _dataHead;
    info.readOffsetIndex = _rbOffsetHead;
    info.lostFrames = 0;
    subscriberReads.push_back(info);

    RB_LOG_INFO("Added subscriber(%s) to ringBuffer, total subscribers: %u", subscriber->GetId(),
                static_cast<unsigned>(subscriberReads.size()));
    GiveLock();
    return true;
}

bool
ringBuffer::RemoveSubscriber(Account* subscriber) {
    if (!subscriber || !TakeLock()) {
        return false;
    }
    for (auto it = subscriberReads.begin(); it != subscriberReads.end(); ++it) {
        if (it->subscriber == subscriber) {
            subscriberReads.erase(it);
            UpdateFreeSpaceLocked();
            RB_LOG_INFO("Removed subscriber(%s) from ringBuffer, remaining: %u", subscriber->GetId(),
                        static_cast<unsigned>(subscriberReads.size()));
            GiveLock();
            return true;
        }
    }

    RB_LOG_ERROR("Failed to remove subscriber, not found");
    GiveLock();
    return false;
}

SubscriberReadInfo_t
ringBuffer::GetSlowestSubscriber() {
    SubscriberReadInfo_t slowest = {};
    if (!TakeLock()) {
        return slowest;
    }
    if (subscriberReads.empty()) {
        RB_LOG_ERROR("No subscriber in This RingBuffer %s", ID ? ID : "");
        GiveLock();
        return slowest;
    }

    slowest = subscriberReads[0];
    uint16_t slowestDistance = Distance(slowest.readPos, _dataHead);
    for (const auto& info : subscriberReads) {
        const uint16_t distance = Distance(info.readPos, _dataHead);
        if (distance > slowestDistance) {
            slowest = info;
            slowestDistance = distance;
        }
    }
    GiveLock();
    return slowest;
}

SubscriberReadInfo_t*
ringBuffer::FindSubscriberInfo(Account* subscriber) {
    for (auto& info : subscriberReads) {
        if (info.subscriber == subscriber) {
            return &info;
        }
    }
    return nullptr;
}

bool
ringBuffer::TakeLock(uint32_t timeoutMs) {
    return !_mutex || (xSemaphoreTake(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE);
}

void
ringBuffer::GiveLock() {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

uint16_t
ringBuffer::Distance(uint16_t from, uint16_t to) const {
    return (to >= from) ? static_cast<uint16_t>(to - from) : static_cast<uint16_t>(_ringBufferSize - from + to);
}

void
ringBuffer::CopyToRing(uint16_t offset, const uint8_t* data, uint16_t size) {
    const uint16_t firstBytes =
        ((_ringBufferSize - offset) < size) ? static_cast<uint16_t>(_ringBufferSize - offset) : size;
    memcpy(&_ringBuffer[offset], data, firstBytes);
    if (firstBytes < size) {
        memcpy(_ringBuffer, data + firstBytes, static_cast<size_t>(size - firstBytes));
    }
}

void
ringBuffer::CopyFromRing(uint16_t offset, uint8_t* data, uint16_t size) {
    const uint16_t firstBytes =
        ((_ringBufferSize - offset) < size) ? static_cast<uint16_t>(_ringBufferSize - offset) : size;
    memcpy(data, &_ringBuffer[offset], firstBytes);
    if (firstBytes < size) {
        memcpy(data + firstBytes, _ringBuffer, static_cast<size_t>(size - firstBytes));
    }
}

void
ringBuffer::DropOldestFrame(bool forcedDrop) {
    if (_frameCount == 0) {
        _dataTail = _dataHead;
        _usedBytes = 0;
        _freeSpace = _ringBufferSize;
        return;
    }
#if RB_USE_LOG
    const uint16_t oldTail = _dataTail;
#endif
    const uint16_t oldIndex = _rbOffsetTail;
    const uint16_t frameEnd = _rbOffsetArray[_rbOffsetTail];
    const uint16_t frameSize = Distance(_dataTail, frameEnd);
    _dataTail = frameEnd;
    _rbOffsetTail = static_cast<uint16_t>((_rbOffsetTail + 1) % _rbOffsetSize);
    _frameCount--;
    _usedBytes = (_usedBytes >= frameSize) ? (_usedBytes - frameSize) : 0;
    _freeSpace = _ringBufferSize - _usedBytes;

    if (forcedDrop) {
        _droppedFrames++;
        for (auto& info : subscriberReads) {
            if (info.readOffsetIndex == oldIndex) {
                info.readOffsetIndex = _rbOffsetTail;
                info.readPos = _dataTail;
                info.lostFrames++;
            }
        }
        RB_LOG_WARN("%s drop frame %u -> %u size=%u dropped=%" PRIu32, ID ? ID : "", oldTail, _dataTail, frameSize,
                    _droppedFrames);
    } else {
        _releasedFrames++;
        RB_LOG_INFO("%s release frame %u -> %u size=%u released=%" PRIu32, ID ? ID : "", oldTail, _dataTail, frameSize,
                    _releasedFrames);
    }
}

void
ringBuffer::UpdateFreeSpaceLocked() {
    while (_frameCount > 0) {
        bool oldestNeeded = false;
        for (const auto& info : subscriberReads) {
            if (info.readOffsetIndex == _rbOffsetTail) {
                oldestNeeded = true;
                break;
            }
        }
        if (oldestNeeded) {
            break;
        }
        DropOldestFrame(false);
    }
}
