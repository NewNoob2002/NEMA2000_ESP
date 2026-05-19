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
#include "Account.h"
#include <inttypes.h>
#include "DataCenter.h"

/**
 * @brief  Account constructor
 * @param  id:       Unique name
 * @param  center:   Pointer to the data center
 * @param  bufSize:  The length of the data to be cached
 * @param  averageFrameSize: The average size of each frame in the ring buffer, used to calculate the number of frames that can be cached
 * @param  userData: Point to the address of user-defined data
 * @retval None
 */
Account::Account(const char* id, DataCenter* center, uint32_t bufSize, uint8_t averageFrameSize, void* userData) {
    memset(&priv, 0, sizeof(priv));

    ID = id;
    Center = center;
    UserData = userData;

    if (bufSize != 0) {
        priv._ringBuffer = new ringBuffer(bufSize, averageFrameSize, ID);
        priv._bufferSize = bufSize;
        if (!priv._ringBuffer || !priv._ringBuffer->Init()) {
            ACCOUNT_LOG_ERROR("Account[%s] Ring buffer initialization failed!", ID);
            if (priv._ringBuffer) {
                delete priv._ringBuffer;
                priv._ringBuffer = nullptr;
            }
            return;
        }
        ACCOUNT_LOG_INFO("Account[%s] Ring buffer cached %" PRIu32 " bytes", ID, bufSize);
    } else {
        priv._ringBuffer = nullptr;
    }

    if (Center) {
        Center->AddAccount(this);
    }

    ACCOUNT_LOG_INFO("Account[%s] created", ID);
}

/**
 * @brief  Account destructor
 * @param  None
 * @retval None
 */
Account::~Account() {
    ACCOUNT_LOG_INFO("Account[%s] deleting...", ID);

    /* Delete timer */
    if (priv._timer) {
        esp_timer_stop(priv._timer);
        esp_timer_delete(priv._timer);
        priv._timer = nullptr;
        ACCOUNT_LOG_INFO("Account[%s] timer deleted", ID);
    }

    /* Let subscribers unfollow */
    AccountVector_t subscribersSnapshot = _subscribers;
    for (auto iter : subscribersSnapshot) {
        if (iter) {
            iter->Unsubscribe(ID);
            ACCOUNT_LOG_INFO("sub[%s] unsubscribed pub[%s]", iter->ID, ID);
        }
    }

    /* Ask the publisher to delete this subscriber */
    AccountVector_t publishersSnapshot = _publishers;
    for (auto iter : publishersSnapshot) {
        if (iter) {
            Unsubscribe(iter->ID);
            ACCOUNT_LOG_INFO("pub[%s] removed sub[%s]", iter->ID, ID);
        }
    }

    /* Release cache */
    if (priv._ringBuffer) {
        delete priv._ringBuffer;
        priv._ringBuffer = nullptr;
    }

    /* Let the data center delete the account */
    if (Center) {
        Center->RemoveAccount(this);
    }
    ACCOUNT_LOG_INFO("Account[%s] deleted", ID);
}

/**
 * @brief  Subscribe to Publisher
 * @param  pubID: Publisher ID
 * @retval Pointer to publisher
 */
Account*
Account::Subscribe(const char* pubID) {
    if (!Center || !pubID || !ID) {
        return nullptr;
    }

    /* Not allowed to subscribe to yourself */
    if (strcmp(pubID, ID) == 0) {
        ACCOUNT_LOG_ERROR("Account[%s] try to subscribe to it itself", ID);
        return nullptr;
    }

    /* Whether to subscribe repeatedly */
    Account* pub = Center->Find(&_publishers, pubID);
    if (pub != nullptr) {
        ACCOUNT_LOG_ERROR("Multi subscribe pub[%s]", pubID);
        return nullptr;
    }

    /* Whether the account is created */
    pub = Center->SearchAccount(pubID);
    if (pub == nullptr) {
        ACCOUNT_LOG_ERROR("pub[%s] was not found", pubID);
        return nullptr;
    }

    /* Add the publisher to the subscription list */
    _publishers.push_back(pub);

    /* Let the publisher add this subscriber */
    pub->_subscribers.push_back(this);

    /* 如果发布者有环形缓冲区，将此订阅者添加到缓冲区 */
    if (pub->priv._ringBuffer) {
        if (!pub->priv._ringBuffer->AddSubscriber(this)) {
            Center->Remove(&_publishers, pub);
            Center->Remove(&pub->_subscribers, this);
            return nullptr;
        }
        ACCOUNT_LOG_INFO("sub[%s] subscribed pub[%s] ring buffer", ID, pubID);
    }

    ACCOUNT_LOG_INFO("sub[%s] subscribed pub[%s]", ID, pubID);

    return pub;
}

/**
 * @brief  Unsubscribe from publisher
 * @param  pubID: Publisher ID
 * @retval Return true if unsubscribe is successful
 */
bool
Account::Unsubscribe(const char* pubID) {
    if (!Center || !pubID) {
        return false;
    }

    /* Whether to subscribe to the publisher */
    Account* pub = Center->Find(&_publishers, pubID);
    if (pub == nullptr) {
        ACCOUNT_LOG_WARN("sub[%s] was not subscribe pub[%s]", ID, pubID);
        return false;
    }

    /* Remove the publisher from the subscription list */
    Center->Remove(&_publishers, pub);

    /* Let the publisher remove this subscriber */
    Center->Remove(&pub->_subscribers, this);

    /* 如果发布者有环形缓冲区，从缓冲区中移除此订阅者 */
    if (pub->priv._ringBuffer) {
        pub->priv._ringBuffer->RemoveSubscriber(this);
    }

    return true;
}

/**
 * @brief  Submit data to cache
 * @param  data_p: Pointer to data
 * @param  size:   The size of the data
 * @retval Return true if the submission is successful
 */
bool
Account::Commit(void* data_p, uint32_t size) {
    if (!data_p || size == 0 || !priv._ringBuffer) {
        ACCOUNT_LOG_ERROR("pub[%s] has not ring buffer", ID);
        return false;
    }

    if (!priv._ringBuffer->WriteToRingBuf(data_p, size)) {
        ACCOUNT_LOG_WARN("pub[%s] commit data to ring buffer failed", ID);
        return false;
    }
    ACCOUNT_LOG_INFO("pub[%s] commit data(0x%p)[%" PRIu32 "] >> ring buffer done", ID, data_p, size);

    return true;
}

/**
 * @brief  Publish data to subscribers
 * @param  None
 * @retval error code
 */
int
Account::Publish() {
    int retval = RES_UNKNOW;

    if (!priv._ringBuffer) {
        ACCOUNT_LOG_ERROR("pub[%s] has not cache", ID);
        return RES_NO_CACHE;
    }

    EventParam_t param;

    param.event = EVENT_PUB_PUBLISH_PULL;
    param.data_p = nullptr;
    param.size = 0;
    param.tran = this;
    param.recv = nullptr;

    AccountVector_t subscribersSnapshot = _subscribers;

    /* Publish notification to subscribers */
    for (auto iter : subscribersSnapshot) {
        Account* sub = iter;
        if (!sub) {
            continue;
        }
        EventCallback_t callback = sub->priv._eventCallback;

        ACCOUNT_LOG_INFO("pub[%s] publish notification >> sub[%s]...", ID, sub->ID);

        if (callback != nullptr) {
            param.recv = sub;
            int ret = callback(sub, &param);

            ACCOUNT_LOG_INFO("publish done in callback: %s", RES_CODE_TO_STRING((ResCode_t)ret));
            retval = ret;
        } else {
            ACCOUNT_LOG_INFO("sub[%s] not register callback", sub->ID);
        }
    }

#if ACCOUNT_DISCARD_READ_DATA
    if (!RingBufferEnable) {
        PingPongBuffer_SetReadDone(&priv.pingPongBuffer);
    }
#endif
    return retval;
}

/**
 * @brief  Pull data from the publisher
 * @param  pubID:  Publisher ID
 * @param  data_p: Pointer to data
 * @param  size:   The size of the data
 * @retval error code
 */
int
Account::Pull(const char* pubID, void* data_p, uint32_t* size) {
    if (!Center || !pubID || !data_p || !size) {
        return RES_PARAM_ERROR;
    }

    Account* pub = Center->Find(&_publishers, pubID);
    if (pub == nullptr) {
        ACCOUNT_LOG_ERROR("sub[%s] was not subscribe pub[%s]", ID, pubID);
        return RES_NOT_FOUND;
    }
    return Pull(pub, data_p, size);
}

int
Account::Pull(Account* pub, void* data_p, uint32_t* size) {
    int retval = RES_UNKNOW;

    if (!pub) {
        return RES_NOT_FOUND;
    }
    if (!data_p || !size || (*size == 0)) {
        return RES_PARAM_ERROR;
    }

    EventCallback_t callback = pub->priv._eventCallback;
    if (callback != nullptr) {
        EventParam_t param;
        param.event = EVENT_SUB_PULL;
        param.tran = this;
        param.recv = pub;
        param.data_p = data_p;
        param.size = *size;

        int ret = callback(pub, &param);
        ACCOUNT_LOG_INFO("sub[%s] pull << data(%p)[%" PRIu32 "] << pub[%s] ...", ID, data_p, *size, pub->ID);
        ACCOUNT_LOG_INFO("pull done: %s", RES_CODE_TO_STRING((ResCode_t)ret));
        retval = ret;
    }

    if (retval != RES_OK) {
        if (callback == nullptr) {
            ACCOUNT_LOG_INFO("pub[%s] not registered pull callback, read from ring buffer...", pub->ID);
        } else {
            ACCOUNT_LOG_INFO("pub[%s] event callback pull failed, read from ring buffer...", pub->ID);
        }

        if (pub->priv._ringBuffer) {
            if (pub->priv._ringBuffer->ReadRingBuf(this, data_p, size)) {
                ACCOUNT_LOG_INFO("sub[%s] read pub[%s] ring buffer [%" PRIu32 "] done", ID, pub->ID, *size);
                retval = RES_OK;
            } else {
                ACCOUNT_LOG_WARN("pub[%s] no new data available in ring buffer!", pub->ID);
                retval = RES_NO_COMMITED;
            }
        } else {
            ACCOUNT_LOG_ERROR("pub[%s] has no ring buffer", pub->ID);
            retval = RES_NO_CACHE;
        }
    }

    return retval;
}

/**
 * @brief  Send a notification to the publisher
 * @param  pubID: Publisher ID
 * @param  data_p: Pointer to data
 * @param  size:   The size of the data
 * @retval error code
 */
int
Account::Notify(const char* pubID, const void* data_p, uint32_t size) {
    if (!Center || !pubID) {
        return RES_PARAM_ERROR;
    }

    Account* pub = Center->Find(&_publishers, pubID);
    if (pub == nullptr) {
        ACCOUNT_LOG_ERROR("sub[%s] was not subscribe pub[%s]", ID, pubID);
        return RES_NOT_FOUND;
    }
    ACCOUNT_LOG_INFO("find pub[%s]", pub->ID);
    return Notify(pub, data_p, size);
}

/**
 * @brief  Send a notification to the publisher
 * @param  pub:    Pointer to publisher
 * @param  data_p: Pointer to data
 * @param  size:   The size of the data
 * @retval error code
 */
int
Account::Notify(Account* pub, const void* data_p, uint32_t size) {
    int retval = RES_UNKNOW;

    if (pub == nullptr) {
        return RES_NOT_FOUND;
    }

    ACCOUNT_LOG_INFO("sub[%s] notify >> data(0x%p)[%" PRIu32 "] >> pub[%s] ...", ID, data_p, size, pub->ID);

    EventCallback_t callback = pub->priv._eventCallback;
    if (callback != nullptr) {
        EventParam_t param;
        param.event = EVENT_NOTIFY;
        param.tran = this;
        param.recv = pub;
        param.data_p = (void*)data_p;
        param.size = size;

        int ret = callback(pub, &param);

        ACCOUNT_LOG_INFO("send done: %d", ret);
        retval = ret;
    } else {
        ACCOUNT_LOG_WARN("pub[%s] not register callback", pub->ID);
        retval = RES_NO_CALLBACK;
    }

    return retval;
}

/**
 * @brief  Set event callback
 * @param  callback: Callback function pointer
 * @retval None
 */
void
Account::SetEventCallback(EventCallback_t callback) {
    priv._eventCallback = callback;
}

/**
 * @brief  Timer callback entry function
 * @param  timer: Pointer to timer
 * @retval None
 */
void
Account::TimerCallbackHandler(void* timer_args) {
    Account* instance = (Account*)(timer_args);
    EventCallback_t callback = instance->priv._eventCallback;
    if (callback) {
        EventParam_t param;
        param.event = EVENT_TIMER;
        param.tran = instance;
        param.recv = instance;
        param.data_p = nullptr;
        param.size = 0;

        callback(instance, &param);
    }
}

/**
 * @brief  Set timing period
 * @param  period: Timing period(ms)
 * @retval None
 */
void
Account::SetTimerPeriod(uint32_t period) {
    if (priv._timer) {
        esp_timer_delete(priv._timer);
        priv._timer = nullptr;
    }

    if (period == 0) {
        return;
    }
    priv._timer_period = period;
    priv._timer_config = {
        .callback = TimerCallbackHandler,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = ID,
        .skip_unhandled_events = true,
    };

    ESP_ERROR_CHECK(esp_timer_create(&priv._timer_config, &priv._timer));
    esp_timer_start_periodic(priv._timer, 1000 * priv._timer_period);
}

/**
 * @brief  Set timer enable
 * @param  en: Whether to enable
 * @retval None
 */
void
Account::SetTimerEnable(bool en, bool once) {
    esp_timer_handle_t timer = priv._timer;

    if (timer == nullptr) {
        return;
    }

    if (en) {
        esp_timer_stop(timer);
        if (once) {
            esp_timer_start_once(timer, 1000 * priv._timer_period);
        } else {
            if (priv._timer_period > 0) {
                esp_timer_start_periodic(timer, 1000 * priv._timer_period);
            } else {
                ACCOUNT_LOG_ERROR("timer period is 0 & want to start periodic timer");
            }
        }
    } else {
        esp_timer_stop(timer);
    }
}

/**
 * @brief  Get the number of publishers
 * @retval number of publishers
 */
size_t
Account::GetPublishersSize() {
    return _publishers.size();
}

/**
 * @brief  Get the number of subscribes
 * @retval number of subscribes
 */
size_t
Account::GetSubscribersSize() {
    return _subscribers.size();
}
