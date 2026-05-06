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
#ifndef __DATA_CENTER_H
#define __DATA_CENTER_H

#include "Account.h"

#define DC_USE_LOG 1

#if DC_USE_LOG
#include <cstdio>
#define DC_LOG_INFO(format, ...)  printf("[DATACENTER][I] " format "\n", ##__VA_ARGS__)
#define DC_LOG_WARN(format, ...)  printf("[DATACENTER][W] " format "\n", ##__VA_ARGS__)
#define DC_LOG_ERROR(format, ...) printf("[DATACENTER][E] " format "\n", ##__VA_ARGS__)
#else
#define DC_LOG_INFO(...)
#define DC_LOG_WARN(...)
#define DC_LOG_ERROR(...)
#endif

class DataCenter {
  public:
    /* The name of the data center will be used as the ID of the main account */
    const char* Name;

    /* Main account, will automatically follow all accounts */
    Account AccountMain;

  public:
    DataCenter(const char* name);
    ~DataCenter();
    bool AddAccount(Account* account);
    bool RemoveAccount(Account* account);
    bool Remove(Account::AccountVector_t* vec, Account* account);
    Account* SearchAccount(const char* id);
    Account* Find(Account::AccountVector_t* vec, const char* id);
    size_t GetAccountLen();

  private:
    /* Account pool */
    Account::AccountVector_t AccountPool;
};

#endif