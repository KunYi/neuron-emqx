/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2023 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#include "msg.h"
#include "utils/log.h"

#include "msg_internal.h"

/**
 * @brief Generate a message with a given header and data.
 *
 * This function generates a message by calculating the size of the data based
 * on the type, ensuring that the length is sufficient for the header and data,
 * and copying the data into the message after the header.
 *
 * @param header Pointer to the header of the message.
 * @param data   Pointer to the data to be copied into the message.
 */
void neu_msg_gen(neu_reqresp_head_t *header, void *data)
{
    // Calculate the size of the data based on the type
    size_t data_size = neu_reqresp_size(header->type);
    // Ensure that the length is sufficient for the header and data
    assert(header->len >= sizeof(neu_reqresp_head_t) + data_size);
    // Copy the data into the message after the header
    memcpy((uint8_t *) &header[1], data, data_size);
}
