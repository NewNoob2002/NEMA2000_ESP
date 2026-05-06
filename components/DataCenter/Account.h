/*
 * MIT License
 * Copyright (c) 2021 _VIFEXTech
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
#ifndef __ACCOUNT_H
#define __ACCOUNT_H

#include <stdint.h>
#include <string.h>
#include <vector>
#include "esp_timer.h"
#include "ringBuffer/ringBuffer.h"

#define ACCOUNT_USE_LOG 1

#if ACCOUNT_USE_LOG
#include <cstdio>
#define ACCOUNT_LOG_INFO(format, ...)  printf("[ACCOUNT][I] " format "\n", ##__VA_ARGS__)
#define ACCOUNT_LOG_WARN(format, ...)  printf("[ACCOUNT][W] " format "\n", ##__VA_ARGS__)
#define ACCOUNT_LOG_ERROR(format, ...) printf("[ACCOUNT][E] " format "\n", ##__VA_ARGS__)
#else
#define ACCOUNT_LOG_INFO(...)
#define ACCOUNT_LOG_WARN(...)
#define ACCOUNT_LOG_ERROR(...)
#endif

class DataCenter;

class Account {
  public:
    /* Event type enumeration */
    typedef enum {
        EVENT_NONE,
        EVENT_PUB_PUBLISH,      // Publisher posted information
        EVENT_PUB_PUBLISH_PULL, // Publisher posted information to call Subscriber pull
        EVENT_SUB_PULL,         // Subscriber data pull request
        EVENT_NOTIFY,           // Subscribers send notifications to publishers
        EVENT_TIMER,            // Timed event
        _EVENT_LAST
    } EventCode_t;

    /* Error type enumeration */
    typedef enum {
        RES_OK = 0,
        RES_UNKNOW = -1,
        RES_SIZE_MISMATCH = -2,
        RES_UNSUPPORTED_REQUEST = -3,
        RES_NO_CALLBACK = -4,
        RES_NO_CACHE = -5,
        RES_NO_COMMITED = -6,
        RES_NOT_FOUND = -7,
        RES_PARAM_ERROR = -8
    } ResCode_t;

    /* Event parameter structure */
    typedef struct {
        EventCode_t event; // Event type
        Account* tran;     // Pointer to sender
        Account* recv;     // Pointer to receiver
        void* data_p;      // Pointer to data
        uint32_t size;     // The length of the data
    } EventParam_t;

    const char*
    EVENT_CODE_TO_STRING(EventCode_t event) {
        switch (event) {
            case EVENT_NONE: return "EVENT_NONE";
            case EVENT_PUB_PUBLISH: return "EVENT_PUB_PUBLISH";
            case EVENT_PUB_PUBLISH_PULL: return "EVENT_PUB_PUBLISH_PULL";
            case EVENT_SUB_PULL: return "EVENT_SUB_PULL";
            case EVENT_NOTIFY: return "EVENT_NOTIFY";
            case EVENT_TIMER: return "EVENT_TIMER";
            default: return "EVENT_NONE";
        }
    }

    const char*
    RES_CODE_TO_STRING(ResCode_t res) {
        switch (res) {
            case RES_OK: return "RES_OK";
            case RES_UNKNOW: return "RES_UNKNOW";
            case RES_SIZE_MISMATCH: return "RES_SIZE_MISMATCH";
            case RES_UNSUPPORTED_REQUEST: return "RES_UNSUPPORTED_REQUEST";
            case RES_NO_CALLBACK: return "RES_NO_CALLBACK";
            case RES_NO_CACHE: return "RES_NO_CACHE";
            case RES_NO_COMMITED: return "RES_NO_COMMITED";
            case RES_NOT_FOUND: return "RES_NOT_FOUND";
            case RES_PARAM_ERROR: return "RES_PARAM_ERROR";
            default: return "RES_UNKNOW";
        }
    }

    /* Event callback function pointer */
    typedef int (*EventCallback_t)(Account* account, EventParam_t* param);

    typedef std::vector<Account*> AccountVector_t;

  public:
    Account(const char* id, DataCenter* center, uint32_t bufSize = 0, void* userData = nullptr);
    ~Account();

    Account* Subscribe(const char* pubID);
    bool Unsubscribe(const char* pubID);
    bool Commit(void* data_p, uint32_t size);
    int Publish();
    int Pull(const char* pubID, void* data_p, uint32_t* size);
    int Pull(Account* pub, void* data_p, uint32_t* size);
    int Notify(const char* pubID, const void* data_p, uint32_t size);
    int Notify(Account* pub, const void* data_p, uint32_t size);
    void SetEventCallback(EventCallback_t callback);
    void SetTimerPeriod(uint32_t period);
    void SetTimerEnable(bool en, bool once = false);
    size_t GetPublishersSize();
    size_t GetSubscribersSize();

    const char*
    GetId() {
        return this->ID;
    }

  public:
    const char* ID;     /* Unique account ID */
    DataCenter* Center; /* Pointer to the data center */
    void* UserData;

    AccountVector_t _publishers;  /* Followed publishers */
    AccountVector_t _subscribers; /* Followed subscribers */

    struct {
        uint64_t _timer_period;
        EventCallback_t _eventCallback;
        esp_timer_create_args_t _timer_config;
        esp_timer_handle_t _timer;
        ringBuffer* _ringBuffer;
        uint32_t _bufferSize;
    } priv;

  private:
    static void TimerCallbackHandler(void* timer_args);
};

#endif