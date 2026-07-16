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

#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/os_mbuf.h"
#include "notify_queue.h"
#include <cstdlib>

int ble_gattc_notify_custom(uint16_t conn_handle, uint16_t att_handle,
                            struct os_mbuf *om)
{
    // Route the firmware's notification payload to the GATT bridge instead of
    // the (stubbed) NimBLE notify path, then free the mbuf (on hardware NimBLE
    // consumes it). See sim/notify_queue.h.
    if (om != nullptr) {
        if (om->om_data != nullptr) {
            SimNotify::Push(om->om_data, om->om_len);
        }
        std::free(om);
    }
    return 0;
}

namespace {
    // Deterministic attribute handle for a characteristic. DfuService and
    // FSService route by handle (not UUID); on hardware NimBLE assigns these,
    // but the sim stubs registration. Both services' custom UUIDs encode a
    // distinguishable 16-bit id in bytes [12..13] (DFU 0x1531/1532/1534, FS
    // 0x0100/0x0200), so we use that as the handle — stable across find_chr
    // (Dfu) and add_svcs val_handle (FS).
    uint16_t HandleForChr(const ble_uuid_t* chr_uuid) {
        if (chr_uuid != nullptr && chr_uuid->type == BLE_UUID_TYPE_128) {
            const auto* u = reinterpret_cast<const ble_uuid128_t*>(chr_uuid);
            return static_cast<uint16_t>(u->value[12] | (u->value[13] << 8));
        }
        if (chr_uuid != nullptr && chr_uuid->type == BLE_UUID_TYPE_16) {
            return reinterpret_cast<const ble_uuid16_t*>(chr_uuid)->value;
        }
        return 0;
    }
}

int ble_gatts_find_chr(const ble_uuid_t* /*svc_uuid*/, const ble_uuid_t* chr_uuid,
                       uint16_t* out_def_handle, uint16_t* out_val_handle) {
    const uint16_t handle = HandleForChr(chr_uuid);
    if (out_def_handle != nullptr) {
        *out_def_handle = handle;
    }
    if (out_val_handle != nullptr) {
        *out_val_handle = handle;
    }
    return 0;
}

int
ble_gatts_count_cfg(const struct ble_gatt_svc_def *defs)
{
    //struct ble_gatt_resources res = { 0 };
    //int rc;

    //rc = ble_gatts_count_resources(defs, &res);
    //if (rc != 0) {
    //    return rc;
    //}

    //ble_hs_max_services += res.svcs;
    //ble_hs_max_attrs += res.attrs;

    ///* Reserve an extra CCCD for the cache. */
    //ble_hs_max_client_configs +=
    //    res.cccds * (MYNEWT_VAL(BLE_MAX_CONNECTIONS) + 1);

    return 0;
}

int
ble_gatts_add_svcs(const struct ble_gatt_svc_def *svcs)
{
//    void *p;
//    int rc;
//
//    ble_hs_lock();
//    if (!ble_gatts_mutable()) {
//        rc = BLE_HS_EBUSY;
//        goto done;
//    }
//
//    p = realloc(ble_gatts_svc_defs,
//                (ble_gatts_num_svc_defs + 1) * sizeof *ble_gatts_svc_defs);
//    if (p == NULL) {
//        rc = BLE_HS_ENOMEM;
//        goto done;
//    }
//
//    ble_gatts_svc_defs = p;
//    ble_gatts_svc_defs[ble_gatts_num_svc_defs] = svcs;
//    ble_gatts_num_svc_defs++;
//
//    rc = 0;
//
//done:
//    ble_hs_unlock();
//    return rc;
    // Fill each characteristic's val_handle the same way find_chr derives it, so
    // handle-routed services (FSService uses .val_handle) work in the sim.
    for (const struct ble_gatt_svc_def* svc = svcs; svc != nullptr && svc->type != 0; svc++) {
        if (svc->characteristics == nullptr) {
            continue;
        }
        for (const struct ble_gatt_chr_def* chr = svc->characteristics; chr != nullptr && chr->uuid != nullptr; chr++) {
            if (chr->val_handle != nullptr) {
                *chr->val_handle = HandleForChr(chr->uuid);
            }
        }
    }
    return 0;
}
