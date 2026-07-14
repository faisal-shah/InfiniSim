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

#include "host/os_mbuf.h"

#include <string.h>

// sim: real single-buffer implementations (the original stubs were no-ops,
// which silently broke any service using the proper NimBLE parsing idioms).
// A fake mbuf is one flat buffer: om_data points at the data, om_len is its
// length. There is no chaining.

int
os_mbuf_copydata(const struct os_mbuf *m, int off, int len, void *dst)
{
    if (m == NULL || m->om_data == NULL || off < 0 || len < 0 ||
        off + len > m->om_len) {
        return -1;
    }
    memcpy(dst, m->om_data + off, (size_t) len);
    return 0;
}

// Appends into the buffer om_data points at. The creator of the fake mbuf is
// responsible for pointing om_data at storage large enough for the appended
// data (om_flags carries the capacity for bounds checking when nonzero — see
// gatt_bridge). Firmware code only checks the return value.
int
os_mbuf_append(struct os_mbuf *om, const void *data,  uint16_t len)
{
    if (om == NULL || om->om_data == NULL) {
        return -1;
    }
    memcpy(om->om_data + om->om_len, data, len);
    om->om_len += len;
    return 0;
}