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
config_ **/

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <jansson.h>

#include "errcodes.h"
#include "tag.h"

/**
 * @def GET_STATIC_VALUE_PTR(tag, out)
 * @brief Macro to retrieve a pointer to the static value stored in the tag.
 * @param tag The data tag.
 * @param out Output variable to store the pointer.
 */
#define GET_STATIC_VALUE_PTR(tag, out)                      \
    do {                                                    \
        memcpy(&(out), (tag)->meta, sizeof(neu_value_u *)); \
    } while (0)

/**
 * @def SET_STATIC_VALUE_PTR(tag, ptr)
 * @brief Macro to set the static value pointer in the tag.
 * @param tag The data tag.
 * @param ptr The pointer to be set.
 */
#define SET_STATIC_VALUE_PTR(tag, ptr)                      \
    do {                                                    \
        memcpy((tag)->meta, &(ptr), sizeof(neu_value_u *)); \
    } while (0)

/**
 * @brief Copies data from one data tag to another.
 *
 * @param _dst Destination data tag.
 * @param _src Source data tag.
 */
static void tag_array_copy(void *_dst, const void *_src)
{
    neu_datatag_t *dst = (neu_datatag_t *) _dst;
    neu_datatag_t *src = (neu_datatag_t *) _src;

    dst->type        = src->type;
    dst->attribute   = src->attribute;
    dst->precision   = src->precision;
    dst->decimal     = src->decimal;
    dst->option      = src->option;
    dst->address     = strdup(src->address);
    dst->name        = strdup(src->name);
    dst->description = strdup(src->description);

    if (NEU_ATTRIBUTE_STATIC & src->attribute) {
        neu_value_u *dst_val = NULL, *src_val = NULL;
        GET_STATIC_VALUE_PTR(src, src_val);
        if (src_val && (dst_val = calloc(1, sizeof(*dst_val)))) {
            memcpy(dst_val, src_val, sizeof(*dst_val));
            SET_STATIC_VALUE_PTR(dst, dst_val);
        } else {
            memset(dst->meta, 0, sizeof(dst->meta));
        }
    } else {
        memcpy(dst->meta, src->meta, sizeof(src->meta));
    }
}

/**
 * @brief Frees the memory occupied by a data tag element.
 *
 * @param _elt The data tag element to be freed.
 */
static void tag_array_free(void *_elt)
{
    neu_datatag_t *elt = (neu_datatag_t *) _elt;

    free(elt->name);
    free(elt->address);
    free(elt->description);

    if (NEU_ATTRIBUTE_STATIC & elt->attribute) {
        neu_value_u *cur = NULL;
        GET_STATIC_VALUE_PTR(elt, cur);
        free(cur);
        memset(elt->meta, 0, sizeof(elt->meta));
    }
}

/**
 * @brief Initializes the UT_icd structure for data tag handling.
 */
static UT_icd tag_icd = { sizeof(neu_datatag_t), NULL, tag_array_copy,
                          tag_array_free };

/**
 * @brief Retrieves the UT_icd structure for data tag handling.
 *
 * @return A pointer to the UT_icd structure.
 */
UT_icd *neu_tag_get_icd()
{
    return &tag_icd;
}

/**
 * @brief Duplicates a data tag.
 *
 * @param tag The data tag to duplicate.
 * @return A new data tag that is a duplicate of the input tag.
 */
neu_datatag_t *neu_tag_dup(const neu_datatag_t *tag)
{
    neu_datatag_t *new = calloc(1, sizeof(*new));
    tag_array_copy(new, tag);
    return new;
}

/**
 * @brief Copies the content of one data tag into another.
 *
 * @param tag The destination data tag.
 * @param other The source data tag.
 */
void neu_tag_copy(neu_datatag_t *tag, const neu_datatag_t *other)
{
    if (tag) {
        tag_array_free(tag);
        tag_array_copy(tag, other);
    }
}

/**
 * @brief Finalizes a data tag, freeing any allocated memory.
 *
 * @param tag The data tag to be finalized.
 */
void neu_tag_fini(neu_datatag_t *tag)
{
    if (tag) {
        tag_array_free(tag);
    }
}

/**
 * @brief Frees the memory occupied by a data tag.
 *
 * @param tag The data tag to be freed.
 */
void neu_tag_free(neu_datatag_t *tag)
{
    if (tag) {
        tag_array_free(tag);
        free(tag);
    }
}

/**
 * @brief Find the last occurrence of a character in a string.
 *
 * @param str The string to search.
 * @param character The character to find.
 * @return A pointer to the last occurrence of the character.
 */
static char *find_last_character(char *str, char character)
{
    char *find = strchr(str, character);
    char *ret  = find;

    while (find != NULL) {
        ret  = find;
        find = strchr(find + 1, character);
    }

    return ret;
}

/**
 * @brief Parses the address option of a data tag and populates the corresponding structure.
 *
 * @param datatag The data tag to parse.
 * @param option Pointer to the structure to store the parsed option.
 * @return 0 on success, -1 on failure.
 */
int neu_datatag_parse_addr_option(const neu_datatag_t *      datatag,
                                  neu_datatag_addr_option_u *option)
{
    int ret = 0;

    switch (datatag->type) {
    case NEU_TYPE_BYTES: {
        char *op = find_last_character(datatag->address, '.');

        if (op == NULL) {
            ret = -1;
        } else {
            int n = sscanf(op, ".%hhd", &option->bytes.length);
            if (n != 1 || option->string.length <= 0) {
                ret = -1;
            }
        }
        break;
    }
    case NEU_TYPE_STRING: {
        char *op = find_last_character(datatag->address, '.');

        if (op == NULL) {
            ret = -1;
        } else {
            char t = 0;
            int  n = sscanf(op, ".%hd%c", &option->string.length, &t);

            switch (t) {
            case 'H':
                option->string.type = NEU_DATATAG_STRING_TYPE_H;
                break;
            case 'L':
                option->string.type = NEU_DATATAG_STRING_TYPE_L;
                break;
            case 'D':
                option->string.type = NEU_DATATAG_STRING_TYPE_D;
                break;
            case 'E':
                option->string.type = NEU_DATATAG_STRING_TYPE_D;
                break;
            default:
                option->string.type = NEU_DATATAG_STRING_TYPE_H;
                break;
            }

            if (n < 1 || option->string.length <= 0) {
                ret = -1;
            }
        }

        break;
    }
    case NEU_TYPE_INT16:
    case NEU_TYPE_UINT16: {
        char *op = find_last_character(datatag->address, '#');

        option->value16.endian = NEU_DATATAG_ENDIAN_L16;
        if (op != NULL) {
            char e = 0;
            sscanf(op, "#%c", &e);

            switch (e) {
            case 'B':
                option->value16.endian = NEU_DATATAG_ENDIAN_B16;
                break;
            case 'L':
                option->value16.endian = NEU_DATATAG_ENDIAN_L16;
                break;
            default:
                option->value16.endian = NEU_DATATAG_ENDIAN_L16;
                break;
            }
        }

        break;
    }
    case NEU_TYPE_FLOAT:
    case NEU_TYPE_INT32:
    case NEU_TYPE_UINT32: {
        char *op = find_last_character(datatag->address, '#');

        option->value32.endian = NEU_DATATAG_ENDIAN_LL32;
        if (op != NULL) {
            char e1 = 0;
            char e2 = 0;
            int  n  = sscanf(op, "#%c%c", &e1, &e2);

            if (n == 2) {
                if (e1 == 'B' && e2 == 'B') {
                    option->value32.endian = NEU_DATATAG_ENDIAN_BB32;
                }
                if (e1 == 'B' && e2 == 'L') {
                    option->value32.endian = NEU_DATATAG_ENDIAN_BL32;
                }
                if (e1 == 'L' && e2 == 'L') {
                    option->value32.endian = NEU_DATATAG_ENDIAN_LL32;
                }
                if (e1 == 'L' && e2 == 'B') {
                    option->value32.endian = NEU_DATATAG_ENDIAN_LB32;
                }
            }
        }

        break;
    }
    case NEU_TYPE_DOUBLE:
    case NEU_TYPE_INT64:
    case NEU_TYPE_UINT64: {
        char *op = find_last_character(datatag->address, '#');

        option->value64.endian = NEU_DATATAG_ENDIAN_L64;
        if (op != NULL) {
            char e = 0;
            sscanf(op, "#%c", &e);

            switch (e) {
            case 'B':
                option->value64.endian = NEU_DATATAG_ENDIAN_B64;
                break;
            case 'L':
                option->value64.endian = NEU_DATATAG_ENDIAN_L64;
                break;
            default:
                option->value64.endian = NEU_DATATAG_ENDIAN_L64;
                break;
            }
        }

        break;
    }

    case NEU_TYPE_BIT: {
        char *op = find_last_character(datatag->address, '.');

        if (op != NULL) {
            sscanf(op, ".%hhd", &option->bit.bit);
            option->bit.op = true;
        } else {
            option->bit.op = false;
        }

        break;
    }
    default:
        break;
    }

    return ret;
}

static int pre_num(unsigned char byte)
{
    unsigned char mask = 0x80;
    int           num  = 0;
    for (int i = 0; i < 8; i++) {
        if ((byte & mask) == mask) {
            mask = mask >> 1;
            num++;
        } else {
            break;
        }
    }
    return num;
}

bool neu_datatag_string_is_utf8(char *data, int len)
{
    int num = 0;
    int i   = 0;

    while (i < len) {
        if ((data[i] & 0x80) == 0x00) {
            // 0XXX_XXXX
            i++;
            continue;
        } else if ((num = pre_num(data[i])) > 2) {
            // 110X_XXXX 10XX_XXXX
            // 1110_XXXX 10XX_XXXX 10XX_XXXX
            // 1111_0XXX 10XX_XXXX 10XX_XXXX 10XX_XXXX
            // 1111_10XX 10XX_XXXX 10XX_XXXX 10XX_XXXX 10XX_XXXX
            // 1111_110X 10XX_XXXX 10XX_XXXX 10XX_XXXX 10XX_XXXX 10XX_XXXX
            i++;
            for (int j = 0; j < num - 1; j++) {
                if ((data[i] & 0xc0) != 0x80) {
                    return false;
                }
                i++;
            }
        } else {
            return false;
        }
    }
    return true;
}

/**
 * @brief Converts a string byte-order from H to L.
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_htol(char *str, int len)
{

    for (int i = 0; i < len; i += 2) {
        char t = str[i];

        str[i]     = str[i + 1];
        str[i + 1] = t;
    }

    return len;
}

/**
 * @brief Converts a string byte-order from L to H
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_ltoh(char *str, int len)
{
    return neu_datatag_string_htol(str, len);
}

/**
 * @brief Converts a string byte-order from E to D.
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_etod(char *str, int len)
{
    for (int i = 0; i < len; i += 2) {
        str[i + 1] = str[i];
        str[i]     = 0;
    }

    return len;
}

/**
 * @brief Converts a string byte-order from D to E.
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_dtoe(char *str, int len)
{
    for (int i = 0; i < len; i += 2) {
        str[i]     = str[i + 1];
        str[i + 1] = 0;
    }

    return len;
}

/**
 * @brief Converts a string byte-order from E to H.
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_etoh(char *str, int len)
{
    char *t = calloc(len, sizeof(char));

    for (int i = 0; i < len; i++) {
        t[i] = str[i * 2];
    }
    memset(str, 0, len);
    strncpy(str, t, strlen(str));

    free(t);
    return len / 2;
}

/**
 * @brief Converts a string byte-order from D to H.
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_dtoh(char *str, int len)
{
    char *t = calloc(len, sizeof(char));

    for (int i = 0; i < len; i++) {
        t[i] = str[i * 2 + 1];
    }
    memset(str, 0, len);
    strncpy(str, t, strlen(str));

    free(t);
    return len / 2;
}

/**
 * @brief Converts a string byte-order from T to D.
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_tod(char *str, int len, int buf_len)
{
    assert(len * 2 < len);
    char *t = strdup(str);

    memset(str, 0, buf_len);
    for (int i = 0; i < len; i++) {
        str[i * 2 + 1] = t[i];
    }

    free(t);
    return len * 2;
}

/**
 * @brief Converts a string byte-order from T to E.
 *
 * @param str The input string.
 * @param len The length of the string.
 * @return The length of the modified string.
 */
int neu_datatag_string_toe(char *str, int len, int buf_len)
{
    assert(len * 2 < len);
    char *t = strdup(str);

    memset(str, 0, buf_len);
    for (int i = 0; i < len; i++) {
        str[i * 2] = t[i];
    }

    free(t);
    return len * 2;
}

/**
 * @brief Gets the static value from a datatag.
 *
 * This function retrieves the static value from the specified datatag.
 *
 * @param tag   The datatag from which to get the static value.
 * @param value Pointer to store the retrieved static value.
 * @return 0 if successful, -1 if the datatag is not static or if the static value is not set.
 */
int neu_tag_get_static_value(const neu_datatag_t *tag, neu_value_u *value)
{
    neu_value_u *cur = NULL;

    if (!neu_tag_attribute_test(tag, NEU_ATTRIBUTE_STATIC)) {
        return -1;
    }

    GET_STATIC_VALUE_PTR(tag, cur);
    if (NULL == cur) {
        return -1;
    }

    memcpy(value, cur, sizeof(*cur));
    return 0;
}

/**
 * @brief Sets the static value for a datatag.
 *
 * This function sets the static value for the specified datatag. If the datatag is not
 * already static, it will be marked as such.
 *
 * @param tag   The datatag for which to set the static value.
 * @param value Pointer to the static value to be set.
 * @return 0 if successful, -1 if memory allocation fails or if the datatag is not static.
 */
int neu_tag_set_static_value(neu_datatag_t *tag, const neu_value_u *value)
{
    neu_value_u *cur = NULL;

    if (!neu_tag_attribute_test(tag, NEU_ATTRIBUTE_STATIC)) {
        return -1;
    }

    GET_STATIC_VALUE_PTR(tag, cur);
    if (NULL == cur) {
        cur = calloc(1, sizeof(*cur));
        if (NULL == cur) {
            return -1;
        }
        SET_STATIC_VALUE_PTR(tag, cur);
    }

    memcpy(cur, value, sizeof(*cur));

    return 0;
}

/**
 * @brief Gets the JSON representation of the static value from a datatag.
 *
 * This function retrieves the JSON representation of the static value from the specified datatag.
 *
 * @param tag The datatag from which to get the static value JSON representation.
 * @param t   Pointer to store the JSON type of the static value.
 * @param v   Pointer to store the JSON value union of the static value.
 * @return 0 if successful, -1 if the datatag is not static or if the static value is not set.
 */
int neu_tag_get_static_value_json(neu_datatag_t *tag, neu_json_type_e *t,
                                  neu_json_value_u *v)
{
    neu_value_u *value = NULL;
    GET_STATIC_VALUE_PTR(tag, value);

    if (NULL == value) {
        return -1;
    }

    switch (tag->type) {
    case NEU_TYPE_BIT:
        *t         = NEU_JSON_INT;
        v->val_bit = value->u8;
        break;
    case NEU_TYPE_BOOL:
        *t          = NEU_JSON_BOOL;
        v->val_bool = value->boolean;
        break;
    case NEU_TYPE_INT8:
        *t         = NEU_JSON_INT;
        v->val_int = value->i8;
        break;
    case NEU_TYPE_UINT8:
        *t         = NEU_JSON_INT;
        v->val_int = value->u8;
        break;
    case NEU_TYPE_INT16:
        *t         = NEU_JSON_INT;
        v->val_int = value->i16;
        break;
    case NEU_TYPE_WORD:
    case NEU_TYPE_UINT16:
        *t         = NEU_JSON_INT;
        v->val_int = value->u16;
        break;
    case NEU_TYPE_INT32:
        *t         = NEU_JSON_INT;
        v->val_int = value->i32;
        break;
    case NEU_TYPE_DWORD:
    case NEU_TYPE_UINT32:
        *t         = NEU_JSON_INT;
        v->val_int = value->u32;
        break;
    case NEU_TYPE_INT64:
        *t         = NEU_JSON_INT;
        v->val_int = value->i64;
        break;
    case NEU_TYPE_LWORD:
    case NEU_TYPE_UINT64:
        *t         = NEU_JSON_INT;
        v->val_int = value->u64;
        break;
    case NEU_TYPE_FLOAT:
        *t           = NEU_JSON_FLOAT;
        v->val_float = value->f32;
        break;
    case NEU_TYPE_DOUBLE:
        *t            = NEU_JSON_DOUBLE;
        v->val_double = value->d64;
        break;
    case NEU_TYPE_STRING:
        *t         = NEU_JSON_STR;
        v->val_str = value->str;
        break;
    default:
        return -1;
    }

    return 0;
}

int neu_tag_set_static_value_json(neu_datatag_t *tag, neu_json_type_e t,
                                  const neu_json_value_u *v)
{
    int         rv    = 0;
    neu_value_u value = { 0 };

    if (!neu_tag_attribute_test(tag, NEU_ATTRIBUTE_STATIC)) {
        return -1;
    }

    switch (t) {
    case NEU_JSON_BIT: {
        if (NEU_TYPE_BIT == tag->type) {
            value.u8 = v->val_bit;
        } else {
            rv = -1;
        }
        break;
    }
    case NEU_JSON_BOOL: {
        if (NEU_TYPE_BOOL == tag->type) {
            value.boolean = v->val_bool;
        } else {
            rv = -1;
        }
        break;
    }
    case NEU_JSON_INT: {
        switch (tag->type) {
        case NEU_TYPE_BIT:
            value.u8 = v->val_int;
            break;
        case NEU_TYPE_INT8:
            value.i8 = v->val_int;
            break;
        case NEU_TYPE_UINT8:
            value.u8 = v->val_int;
            break;
        case NEU_TYPE_INT16:
            value.i16 = v->val_int;
            break;
        case NEU_TYPE_WORD:
        case NEU_TYPE_UINT16:
            value.u16 = v->val_int;
            break;
        case NEU_TYPE_INT32:
            value.i32 = v->val_int;
            break;
        case NEU_TYPE_DWORD:
        case NEU_TYPE_UINT32:
            value.u32 = v->val_int;
            break;
        case NEU_TYPE_INT64:
            value.i64 = v->val_int;
            break;
        case NEU_TYPE_LWORD:
        case NEU_TYPE_UINT64:
            value.u64 = v->val_int;
            break;
        case NEU_TYPE_FLOAT:
            value.f32 = v->val_int;
            break;
        case NEU_TYPE_DOUBLE:
            value.d64 = v->val_int;
            break;
        default:
            rv = -1;
            break;
        }
        break;
    }
    case NEU_JSON_FLOAT: {
        if (NEU_TYPE_FLOAT == tag->type) {
            value.f32 = v->val_float;
        } else if (NEU_TYPE_DOUBLE == tag->type) {
            value.d64 = v->val_float;
        } else {
            rv = -1;
        }
        break;
    }
    case NEU_JSON_DOUBLE: {
        if (NEU_TYPE_FLOAT == tag->type) {
            value.f32 = v->val_double;
        } else if (NEU_TYPE_DOUBLE == tag->type) {
            value.d64 = v->val_double;
        } else {
            rv = -1;
        }
        break;
    }
    case NEU_JSON_STR: {
        if (NEU_TYPE_STRING == tag->type) {
            snprintf(value.str, sizeof(value.str), "%s", v->val_str);
        } else {
            rv = -1;
        }
        break;
    }
    default:
        rv = -1;
    }

    if (0 == rv) {
        rv = neu_tag_set_static_value(tag, &value);
    }

    return rv;
}

/**
 * @brief Dumps the static value of a datatag to a JSON-formatted string.
 *
 * This function converts the static value of the specified datatag into a JSON-formatted string.
 *
 * @param tag The datatag from which to dump the static value.
 * @return A JSON-formatted string representing the static value, or NULL if unsuccessful.
 *         The caller is responsible for freeing the returned string.
 */
char *neu_tag_dump_static_value(const neu_datatag_t *tag)
{
    json_t *jval = NULL;

    if (!neu_tag_attribute_test(tag, NEU_ATTRIBUTE_STATIC)) {
        return NULL;
    }

    neu_value_u value = { 0 };
    if (0 != neu_tag_get_static_value(tag, &value)) {
        return NULL;
    }

    switch (tag->type) {
    case NEU_TYPE_BOOL:
        jval = json_boolean(value.boolean);
        break;
    case NEU_TYPE_INT8:
        jval = json_integer(value.i8);
        break;
    case NEU_TYPE_BIT:
    case NEU_TYPE_UINT8:
        jval = json_integer(value.u8);
        break;
    case NEU_TYPE_INT16:
        jval = json_integer(value.i16);
        break;
    case NEU_TYPE_WORD:
    case NEU_TYPE_UINT16:
        jval = json_integer(value.u16);
        break;
    case NEU_TYPE_INT32:
        jval = json_integer(value.i32);
        break;
    case NEU_TYPE_DWORD:
    case NEU_TYPE_UINT32:
        jval = json_integer(value.u32);
        break;
    case NEU_TYPE_INT64:
        jval = json_integer(value.i64);
        break;
    case NEU_TYPE_LWORD:
    case NEU_TYPE_UINT64:
        jval = json_integer(value.u64);
        break;
    case NEU_TYPE_FLOAT:
        jval = json_real(value.f32);
        break;
    case NEU_TYPE_DOUBLE:
        jval = json_real(value.d64);
        break;
    case NEU_TYPE_STRING:
        jval = json_string(value.str);
        break;
    default:
        break;
    }

    char *s = json_dumps(jval, JSON_ENCODE_ANY);
    json_decref(jval);
    return s;
}

/**
 * @brief Loads the static value of a datatag from a JSON-formatted string.
 *
 * This function sets the static value of the specified datatag using the provided JSON-formatted string.
 *
 * @param tag The datatag to which the static value should be set.
 * @param s   The JSON-formatted string representing the static value.
 * @return 0 if successful, -1 if the datatag is not static, the JSON string is invalid, or an error occurs during processing.
 */
int neu_tag_load_static_value(neu_datatag_t *tag, const char *s)
{
    if (!neu_tag_attribute_test(tag, NEU_ATTRIBUTE_STATIC) || !s) {
        return -1;
    }

    void *jval = neu_json_decode_new(s);
    if (NULL == jval) {
        return -1;
    }

    neu_json_elem_t elem = {
        .name = NULL,
        .t    = NEU_JSON_VALUE,
    };

    int rv = neu_json_decode_value(jval, &elem);
    if (0 == rv) {
        rv = neu_tag_set_static_value_json(tag, elem.t, &elem.v);
    }

    if (NEU_JSON_STR == elem.t) {
        free(elem.v.val_str);
    }
    neu_json_decode_free(jval);

    return rv;
}
