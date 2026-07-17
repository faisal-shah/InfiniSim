/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "host/ble_hs_mbuf.h"
#include "host/os_mbuf.h"
#include <cstdlib>
#include <cstring>

// Allocate a real single-buffer mbuf holding a copy of `buf` so the firmware's
// notification path (DfuService / FSService -> ble_gattc_notify_custom) carries
// actual bytes in the sim. ble_gattc_notify_custom frees it after capturing.
//
// FSService's LISTDIR builds a response by os_mbuf_append()-ing the entry name
// AFTER this initial flat copy, so we over-allocate the data buffer by the
// filesystem's max path length. Without this slack the append overruns the
// allocation and corrupts the heap (munmap_chunk: invalid pointer).
static constexpr uint16_t kAppendSlack = 256; // FSService maxpathlen

struct os_mbuf *ble_hs_mbuf_from_flat(const void *buf, uint16_t len)
{
    auto *om = static_cast<os_mbuf *>(std::malloc(sizeof(os_mbuf) + len + kAppendSlack));
    if (om == nullptr) {
        return nullptr;
    }
    std::memset(om, 0, sizeof(os_mbuf));
    om->om_data = om->om_databuf;
    om->om_len = len;
    if (len > 0 && buf != nullptr) {
        std::memcpy(om->om_databuf, buf, len);
    }
    return om;
}
