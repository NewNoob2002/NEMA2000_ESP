/*
 * MIT License
 * Copyright (c) 2023
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H

#ifdef __cplusplus

#define RB_USE_LOG 1

#if RB_USE_LOG
#include <cstdio>
#define RB_LOG_INFO(format, ...)  printf("[RB][I] " format "\n", ##__VA_ARGS__)
#define RB_LOG_WARN(format, ...)  printf("[RB][W] " format "\n", ##__VA_ARGS__)
#define RB_LOG_ERROR(format, ...) printf("[RB][E] " format "\n", ##__VA_ARGS__)
#else
#define RB_LOG_INFO(...)
#define RB_LOG_WARN(...)
#define RB_LOG_ERROR(...)
#endif

#define WRAP_OFFSET(offset, increment, arraySize)                                                                      \
    {                                                                                                                  \
        offset += increment;                                                                                           \
        if (offset >= arraySize)                                                                                       \
            offset -= arraySize;                                                                                       \
    }

#include <stdbool.h>
#include <stdint.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Account;

typedef struct {
    Account* subscriber;
    uint16_t readPos;
    uint16_t readOffsetIndex;
    uint32_t lostFrames;
} SubscriberReadInfo_t;

class ringBuffer {
  public:
    ringBuffer(uint32_t data_size, const uint8_t averageLengthInBytes, const char* id);
    ~ringBuffer();

    bool Init();

    bool WriteToRingBuf(void* pWriteBuf, uint32_t Totalsize);

    bool ReadRingBuf(Account* subscriber, void* pReadBuf, uint32_t* size);

    void UpdateFreeSpace();

    bool AddSubscriber(Account* subscriber);

    bool RemoveSubscriber(Account* subscriber);

    SubscriberReadInfo_t GetSlowestSubscriber();

  private:
    const char* ID;

    uint16_t* _rbOffsetArray;
    uint16_t _rbOffsetSize;
    uint16_t _rbOffsetHead;
    uint16_t _rbOffsetTail;

    uint8_t* _ringBuffer;
    uint8_t _averageLengthInBytes;
    uint32_t _ringBufferSize;

    uint32_t _freeSpace;
    uint32_t _usedBytes;
    uint32_t _frameCount;
    uint32_t _droppedFrames;

    uint16_t _dataHead;
    uint16_t _dataTail;
    SemaphoreHandle_t _mutex;

    std::vector<SubscriberReadInfo_t> subscriberReads;

    SubscriberReadInfo_t* FindSubscriberInfo(Account* subscriber);

    bool TakeLock(uint32_t timeoutMs = 20);
    void GiveLock();
    uint16_t Distance(uint16_t from, uint16_t to) const;
    void CopyToRing(uint16_t offset, const uint8_t* data, uint16_t size);
    void CopyFromRing(uint16_t offset, uint8_t* data, uint16_t size);
    void DropOldestFrame();
    void UpdateFreeSpaceLocked();
};

#endif // __cplusplus

#endif // __RING_BUFFER_H
