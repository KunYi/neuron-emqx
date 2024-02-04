/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
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
#ifndef _NEU_PLUGIN_MODBUS_POINT_H_
#define _NEU_PLUGIN_MODBUS_POINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <neuron.h>

#include "modbus.h"

/**
 * @brief Structure representing a Modbus point.
 */
typedef struct modbus_point {
    uint8_t                slave_id;       ///< Modbus slave ID.
    modbus_area_e          area;           ///< Modbus area (e.g., COIL, INPUT, INPUT_REGISTER, HOLD_REGISTER).
    uint16_t               start_address;  ///< Starting address of the Modbus point.
    uint16_t               n_register;     ///< Number of registers associated with the Modbus point.

    neu_type_e             type;           ///< Type of data associated with the Modbus point.
    neu_datatag_addr_option_u option;       ///< Addressing options for the Modbus point.
    char                   name[NEU_TAG_NAME_LEN];  ///< Name of the Modbus point.
} modbus_point_t;

/**
 * @brief Structure representing a Modbus point for writing operations.
 */
typedef struct modbus_point_write {
    modbus_point_t point;    ///< Modbus point information.
    neu_value_u    value;    ///< Value to be written to the Modbus point.
} modbus_point_write_t;

/**
 * @brief Converts a Neuron data tag to a Modbus point.
 *
 * This function converts a Neuron data tag to a Modbus point, filling in the necessary information.
 *
 * @param tag    Neuron data tag to be converted.
 * @param point  Pointer to the Modbus point structure to store the converted information.
 * @return       Returns an error code indicating the success or failure of the conversion.
 */
int modbus_tag_to_point(const neu_datatag_t *tag, modbus_point_t *point);

/**
 * @brief Converts a Neuron plugin tag value to a Modbus write point.
 *
 * This function converts a Neuron plugin tag value to a Modbus point for writing operations, filling in the
 * necessary information.
 *
 * @param tag    Neuron plugin tag value to be converted.
 * @param point  Pointer to the Modbus write point structure to store the converted information.
 * @return       Returns an error code indicating the success or failure of the conversion.
 */
int modbus_write_tag_to_point(const neu_plugin_tag_value_t *tag,
                              modbus_point_write_t *        point);

/**
 * @brief Structure representing a Modbus read command.
 */
typedef struct modbus_read_cmd {
    uint8_t       slave_id;       ///< Modbus slave ID.
    modbus_area_e area;           ///< Modbus area (e.g., COIL, INPUT, INPUT_REGISTER, HOLD_REGISTER).
    uint16_t      start_address;  ///< Starting address of the Modbus command.
    uint16_t      n_register;     ///< Number of registers associated with the Modbus command.

    UT_array *    tags;           ///< Tags associated with the Modbus command (modbus_point_t pointers).
} modbus_read_cmd_t;

/**
 * @brief Structure representing a sorted array of Modbus read commands.
 */
typedef struct modbus_read_cmd_sort {
    uint16_t           n_cmd;  ///< Number of Modbus read commands.
    modbus_read_cmd_t *cmd;    ///< Array of Modbus read commands.
} modbus_read_cmd_sort_t;

/**
 * @brief Structure representing a Modbus write command.
 */
typedef struct modbus_write_cmd {
    uint8_t         slave_id;       ///< Modbus slave ID.
    modbus_area_e   area;           ///< Modbus area (e.g., COIL, INPUT, INPUT_REGISTER, HOLD_REGISTER).
    uint16_t        start_address;  ///< Starting address of the Modbus command.
    uint16_t        n_register;     ///< Number of registers associated with the Modbus command.
    uint8_t         n_byte;         ///< Number of bytes for writing operation.
    uint8_t *       bytes;          ///< Byte array containing values to be written.

    UT_array *      tags;           ///< Tags associated with the Modbus command (modbus_point_write_t pointers).
} modbus_write_cmd_t;

/**
 * @brief Structure representing a sorted array of Modbus write commands.
 */
typedef struct modbus_write_cmd_sort {
    uint16_t            n_cmd;  ///< Number of Modbus write commands.
    modbus_write_cmd_t *cmd;    ///< Array of Modbus write commands.
} modbus_write_cmd_sort_t;

/**
 * @brief Sorts and organizes Modbus read commands based on specified criteria.
 *
 * This function sorts and organizes Modbus read commands based on specified criteria, considering factors such as
 * slave ID, Modbus area, start address, and number of registers.
 *
 * @param tags      Array of Modbus read commands (modbus_point_t pointers).
 * @param max_byte  Maximum number of bytes allowed for a Modbus read command.
 * @return          Returns a pointer to the sorted array of Modbus read commands (modbus_read_cmd_sort_t structure).
 */
modbus_read_cmd_sort_t *modbus_tag_sort(UT_array *tags, uint16_t max_byte);

/**
 * @brief Sorts and organizes Modbus write commands based on specified criteria.
 *
 * This function sorts and organizes Modbus write commands based on specified criteria, considering factors such as
 * slave ID, Modbus area, start address, and number of registers.
 *
 * @param tags  Array of Modbus write commands (modbus_point_write_t pointers).
 * @return      Returns a pointer to the sorted array of Modbus write commands (modbus_write_cmd_sort_t structure).
 */
modbus_write_cmd_sort_t *modbus_write_tags_sort(UT_array *tags);

/**
 * @brief Frees the memory allocated for a sorted array of Modbus read commands.
 *
 * This function frees the memory allocated for a sorted array of Modbus read commands, including associated tags and structures.
 *
 * @param cs  Pointer to the sorted array of Modbus read commands (modbus_read_cmd_sort_t structure) to be freed.
 */
void modbus_tag_sort_free(modbus_read_cmd_sort_t *cs);

#ifdef __cplusplus
}
#endif

#endif
