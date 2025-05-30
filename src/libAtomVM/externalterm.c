/*
 * This file is part of AtomVM.
 *
 * Copyright 2017 Davide Bettio <davide@uninstall.it>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
 */

#include "externalterm.h"

#include "context.h"
#include "list.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstring.h"
#include "defaultatoms.h"
#include "memory.h"
#include "term.h"
#include "unicode.h"
#include "utils.h"

#define NEW_FLOAT_EXT 70
#define NEW_PID_EXT 88
#define NEWER_REFERENCE_EXT 90
#define SMALL_INTEGER_EXT 97
#define INTEGER_EXT 98
#define ATOM_EXT 100
#define PID_EXT 103
#define SMALL_TUPLE_EXT 104
#define LARGE_TUPLE_EXT 105
#define NIL_EXT 106
#define STRING_EXT 107
#define LIST_EXT 108
#define BINARY_EXT 109
#define SMALL_BIG_EXT 110
#define EXPORT_EXT 113
#define MAP_EXT 116
#define SMALL_ATOM_UTF8_EXT 119
#define V4_PORT_EXT 120
#define INVALID_TERM_SIZE -1

#define NEW_FLOAT_EXT_SIZE 9
#define SMALL_INTEGER_EXT_SIZE 2
#define INTEGER_EXT_SIZE 5
#define SMALL_BIG_EXT_BASE_SIZE 3
#define ATOM_EXT_BASE_SIZE 3
#define STRING_EXT_BASE_SIZE 3
#define LIST_EXT_BASE_SIZE 5
#define BINARY_EXT_BASE_SIZE 5
#define MAP_EXT_BASE_SIZE 5
#define SMALL_ATOM_EXT_BASE_SIZE 2

// Assuming two's-complement implementation of signed integers
#define REMOVE_SIGN(val, unsigned_type)                                                            \
    ((val) < 0 ? ~((unsigned_type) (val)) + 1 : (unsigned_type) (val))

// MAINTENANCE NOTE.  Range checking on the external term buffer is only performed in
// the calculate_heap_usage function, which will fail with an invalid term if there is
// insufficient space in the external term buffer (preventing reading off the end of the
// buffer).  The parse_external_terms function does NOT perform range checking, and MUST
// therefore always be preceeded by a call to calculate_heap_usage.

static term parse_external_terms(const uint8_t *external_term_buf, size_t *eterm_size, bool copy, Heap *heap, GlobalContext *glb);
static int calculate_heap_usage(const uint8_t *external_term_buf, size_t remaining, size_t *eterm_size, bool copy);
static size_t compute_external_size(term t, GlobalContext *glb);
static int externalterm_from_term(uint8_t **buf, size_t *len, term t, GlobalContext *glb);
static int serialize_term(uint8_t *buf, term t, GlobalContext *glb);

term externalterm_from_binary_with_roots(Context *ctx, size_t binary_ix, size_t offset, size_t *bytes_read, size_t num_roots, term *roots)
{
    if (!term_is_binary(roots[binary_ix])) {
        return term_invalid_term();
    }
    size_t binary_len = term_binary_size(roots[binary_ix]);
    if (binary_len <= offset) {
        return term_invalid_term();
    }
    const uint8_t *external_term_buf = (const uint8_t *) term_binary_data(roots[binary_ix]) + offset;
    if (UNLIKELY(external_term_buf[0] != EXTERNAL_TERM_TAG)) {
        return term_invalid_term();
    }
    size_t size = binary_len - offset;

    size_t eterm_size;
    int heap_usage = calculate_heap_usage(external_term_buf + 1, size - 1, &eterm_size, true);
    if (heap_usage == INVALID_TERM_SIZE) {
        return term_invalid_term();
    }

    if (UNLIKELY(memory_ensure_free_with_roots(ctx, heap_usage, num_roots, roots, MEMORY_CAN_SHRINK) != MEMORY_GC_OK)) {
        fprintf(stderr, "Unable to ensure %zu free words in heap\n", eterm_size);
        return term_invalid_term();
    }
    // Recompute external_term_buf
    external_term_buf = (const uint8_t *) term_binary_data(roots[binary_ix]) + offset;
    term result = parse_external_terms(external_term_buf + 1, &eterm_size, true, &ctx->heap, ctx->global);
    *bytes_read = eterm_size + 1;
    return result;
}

term externalterm_from_const_literal(const void *external_term, size_t size, Context *ctx)
{
    const uint8_t *external_term_buf = external_term;
    if (UNLIKELY(external_term_buf[0] != EXTERNAL_TERM_TAG)) {
        return term_invalid_term();
    }
    size_t eterm_size;
    int heap_usage = calculate_heap_usage(external_term_buf + 1, size - 1, &eterm_size, false);
    if (heap_usage == INVALID_TERM_SIZE) {
        return term_invalid_term();
    }
    // We need to allocate fragments as reading external terms from modules
    // is not accounted for by the compiler when it emits test_heap opcodes
    Heap heap;
    if (UNLIKELY(memory_init_heap(&heap, heap_usage) != MEMORY_GC_OK)) {
        return term_invalid_term();
    }
    term result = parse_external_terms(external_term_buf + 1, &eterm_size, false, &heap, ctx->global);
    memory_heap_append_heap(&ctx->heap, &heap);
    return result;
}

term externalterm_from_binary(Context *ctx, term binary, size_t *bytes_read)
{
    return externalterm_from_binary_with_roots(ctx, 0, 0, bytes_read, 1, &binary);
}

static int externalterm_from_term(uint8_t **buf, size_t *len, term t, GlobalContext *glb)
{
    *len = compute_external_size(t, glb) + 1;
    *buf = malloc(*len);
    if (IS_NULL_PTR(*buf)) {
        fprintf(stderr, "Unable to allocate %zu bytes for externalized term.\n", *len);
        AVM_ABORT();
    }
    size_t k = serialize_term(*buf + 1, t, glb);
    *buf[0] = EXTERNAL_TERM_TAG;
    return k + 1;
}

term externalterm_to_binary(Context *ctx, term t)
{
    //
    // convert
    //
    uint8_t *buf;
    size_t len;
    externalterm_from_term(&buf, &len, t, ctx->global);
    //
    // Ensure enough free space in heap for binary
    //
    int size_in_terms = term_binary_heap_size(len);
    if (UNLIKELY(memory_ensure_free(ctx, size_in_terms) != MEMORY_GC_OK)) {
        fprintf(stderr, "Unable to ensure %i free words in heap\n", size_in_terms);
        return term_invalid_term();
    }
    //
    // create and return the binary
    //
    term binary = term_from_literal_binary((void *) buf, len, &ctx->heap, ctx->global);
    free(buf);
    return binary;
}

static size_t compute_external_size(term t, GlobalContext *glb)
{
    return serialize_term(NULL, t, glb);
}

static uint8_t get_num_bytes(avm_uint64_t val)
{
    uint8_t num_bytes = 0;
    while (val != 0) {
        val = val >> 8;
        ++num_bytes;
    }
    return num_bytes;
}

static void write_bytes(uint8_t *buf, avm_uint64_t val)
{
    uint8_t i = 0;
    while (val != 0) {
        uint8_t byte = val & 0xFF;
        buf[i] = byte;
        val = val >> 8;
        ++i;
    }
}

static int serialize_term(uint8_t *buf, term t, GlobalContext *glb)
{
    if (term_is_uint8(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = SMALL_INTEGER_EXT;
            buf[1] = term_to_uint8(t);
        }
        return 2;

    } else if (term_is_any_integer(t)) {

        avm_int64_t val = term_maybe_unbox_int64(t);
        if (val >= INT32_MIN && val <= INT32_MAX) {
            if (buf != NULL) {
                buf[0] = INTEGER_EXT;
                WRITE_32_UNALIGNED(buf + 1, (int32_t) val);
            }
            return INTEGER_EXT_SIZE;
        } else {
            avm_uint64_t unsigned_val = REMOVE_SIGN(val, avm_uint64_t);
            uint8_t num_bytes = get_num_bytes(unsigned_val);
            if (buf != NULL) {
                buf[0] = SMALL_BIG_EXT;
                buf[1] = num_bytes;
                buf[2] = val < 0 ? 0x01 : 0x00;
                write_bytes(buf + 3, unsigned_val);
            }
            return SMALL_BIG_EXT_BASE_SIZE + num_bytes;
        }

    } else if (term_is_float(t)) {
        if (!IS_NULL_PTR(buf)) {
            avm_float_t val = term_to_float(t);
            buf[0] = NEW_FLOAT_EXT;
            union {
                uint64_t intvalue;
                double doublevalue;
            } v;
            v.doublevalue = val;
            WRITE_64_UNALIGNED(buf + 1, v.intvalue);
        }
        return NEW_FLOAT_EXT_SIZE;

    } else if (term_is_atom(t)) {
        int atom_index = term_to_atom_index(t);
        size_t atom_len;
        atom_ref_t atom_ref = atom_table_get_atom_ptr_and_len(glb->atom_table, atom_index, &atom_len);
        if (!IS_NULL_PTR(buf)) {
            buf[0] = SMALL_ATOM_UTF8_EXT;
            buf[1] = atom_len;
            atom_table_write_bytes(glb->atom_table, atom_ref, atom_len, buf + 2);
        }
        return 2 + atom_len;

    } else if (term_is_tuple(t)) {
        size_t arity = term_get_tuple_arity(t);
        size_t k;
        if (!IS_NULL_PTR(buf)) {
            if (arity < 256) {
                buf[0] = SMALL_TUPLE_EXT;
                buf[1] = (int8_t) arity;
                k = 2;
            } else {
                buf[0] = LARGE_TUPLE_EXT;
                WRITE_32_UNALIGNED(buf + 1, (int32_t) arity);
                k = 5;
            }
        } else {
            k = arity < 256 ? 2 : 5;
        }
        for (size_t i = 0; i < arity; ++i) {
            term e = term_get_tuple_element(t, i);
            k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, e, glb);
        }
        return k;

    } else if (term_is_nil(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = NIL_EXT;
        }
        return 1;

    } else if (term_is_string(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = STRING_EXT;
        }
        size_t len = 0;
        size_t k = 3;
        term i = t;
        while (!term_is_nil(i)) {
            term e = term_get_list_head(i);
            if (!IS_NULL_PTR(buf)) {
                *(buf + k) = term_to_uint8(e);
            }
            ++k;
            i = term_get_list_tail(i);
            ++len;
        }
        if (!IS_NULL_PTR(buf)) {
            WRITE_16_UNALIGNED(buf + 1, len);
        }
        return k;

    } else if (term_is_list(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = LIST_EXT;
        }
        size_t len = 0;
        size_t k = 5;
        term i = t;
        while (term_is_nonempty_list(i)) {
            term e = term_get_list_head(i);
            k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, e, glb);
            i = term_get_list_tail(i);
            ++len;
        }
        k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, i, glb);
        if (!IS_NULL_PTR(buf)) {
            WRITE_32_UNALIGNED(buf + 1, len);
        }
        return k;

    } else if (term_is_binary(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = BINARY_EXT;
        }
        size_t len = term_binary_size(t);
        if (!IS_NULL_PTR(buf)) {
            const uint8_t *data = (const uint8_t *) term_binary_data(t);
            WRITE_32_UNALIGNED(buf + 1, len);
            memcpy(buf + 5, data, len);
        }
        return 5 + len;

    } else if (term_is_map(t)) {
        size_t size = term_get_map_size(t);
        if (!IS_NULL_PTR(buf)) {
            buf[0] = MAP_EXT;
            WRITE_32_UNALIGNED(buf + 1, size);
        }
        size_t k = 5;
        for (size_t i = 0; i < size; ++i) {
            term key = term_get_map_key(t, i);
            k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, key, glb);
            term value = term_get_map_value(t, i);
            k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, value, glb);
        }
        return k;
    } else if (term_is_function(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = EXPORT_EXT;
        }
        size_t k = 1;
        const term *boxed_value = term_to_const_term_ptr(t);
        for (size_t i = 1; i <= 3; ++i) {
            term mfa = boxed_value[i];
            k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, mfa, glb);
        }
        return k;
    } else if (term_is_local_pid(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = NEW_PID_EXT;
        }
        size_t k = 1;
        term node_name = glb->node_name;
        uint32_t creation = node_name == NONODE_AT_NOHOST_ATOM ? 0 : glb->creation;
        k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, node_name, glb);
        if (!IS_NULL_PTR(buf)) {
            WRITE_32_UNALIGNED(buf + k, term_to_local_process_id(t));
            WRITE_32_UNALIGNED(buf + k + 4, 0); // serial is 0 for local pids
            WRITE_32_UNALIGNED(buf + k + 8, creation);
        }
        return k + 12;
    } else if (term_is_external_pid(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = NEW_PID_EXT;
        }
        size_t k = 1;
        term node = term_get_external_node(t);
        k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, node, glb);
        if (!IS_NULL_PTR(buf)) {
            WRITE_32_UNALIGNED(buf + k, term_get_external_pid_process_id(t));
            WRITE_32_UNALIGNED(buf + k + 4, term_get_external_pid_serial(t));
            WRITE_32_UNALIGNED(buf + k + 8, term_get_external_node_creation(t));
        }
        return k + 12;
    } else if (term_is_local_port(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = V4_PORT_EXT;
        }
        size_t k = 1;
        term node_name = glb->node_name;
        uint32_t creation = node_name == NONODE_AT_NOHOST_ATOM ? 0 : glb->creation;
        k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, node_name, glb);
        if (!IS_NULL_PTR(buf)) {
            WRITE_64_UNALIGNED(buf + k, term_to_local_process_id(t));
            WRITE_32_UNALIGNED(buf + k + 8, creation); // creation
        }
        return k + 12;
    } else if (term_is_external_port(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = V4_PORT_EXT;
        }
        size_t k = 1;
        term node = term_get_external_node(t);
        k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, node, glb);
        if (!IS_NULL_PTR(buf)) {
            WRITE_64_UNALIGNED(buf + k, term_get_external_port_number(t));
            WRITE_32_UNALIGNED(buf + k + 8, term_get_external_node_creation(t));
        }
        return k + 12;
    } else if (term_is_local_reference(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = NEWER_REFERENCE_EXT;
        }
        size_t k = 1;
        uint32_t len = 2;
        if (!IS_NULL_PTR(buf)) {
            WRITE_16_UNALIGNED(buf + k, len);
        }
        k += 2;
        term node_name = glb->node_name;
        uint32_t creation = node_name == NONODE_AT_NOHOST_ATOM ? 0 : glb->creation;
        k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, node_name, glb);
        if (!IS_NULL_PTR(buf)) {
            uint64_t ticks = term_to_ref_ticks(t);
            WRITE_32_UNALIGNED(buf + k, creation);
            WRITE_64_UNALIGNED(buf + k + 4, ticks);
        }
        return k + 12;
    } else if (term_is_external_reference(t)) {
        if (!IS_NULL_PTR(buf)) {
            buf[0] = NEWER_REFERENCE_EXT;
        }
        size_t k = 1;
        uint32_t len = term_get_external_reference_len(t);
        if (!IS_NULL_PTR(buf)) {
            WRITE_16_UNALIGNED(buf + k, len);
        }
        k += 2;
        term node = term_get_external_node(t);
        k += serialize_term(IS_NULL_PTR(buf) ? NULL : buf + k, node, glb);
        if (!IS_NULL_PTR(buf)) {
            WRITE_32_UNALIGNED(buf + k, term_get_external_node_creation(t));
        }
        k += 4;
        if (!IS_NULL_PTR(buf)) {
            const uint32_t *data = term_get_external_reference_words(t);
            for (uint32_t i = 0; i < len; i++) {
                WRITE_32_UNALIGNED(buf + k + (i * 4), data[i]);
            }
        }
        return k + (4 * len);
    } else {
        fprintf(stderr, "Unknown external term type: %" TERM_U_FMT "\n", t);
        AVM_ABORT();
    }
}

static avm_uint64_t read_bytes(const uint8_t *buf, uint8_t num_bytes)
{
    avm_uint64_t value = 0;
    for (uint8_t i = 0; i < num_bytes; ++i) {
        value |= (((avm_uint64_t) buf[i]) << (i * 8));
    }
    return value;
}

static term parse_external_terms(const uint8_t *external_term_buf, size_t *eterm_size, bool copy, Heap *heap, GlobalContext *glb)
{
    switch (external_term_buf[0]) {
        case NEW_FLOAT_EXT: {
            union {
                uint64_t intvalue;
                double doublevalue;
            } v;
            v.intvalue = READ_64_UNALIGNED(external_term_buf + 1);

            *eterm_size = NEW_FLOAT_EXT_SIZE;
            return term_from_float(v.doublevalue, heap);
        }

        case SMALL_INTEGER_EXT: {
            *eterm_size = 2;
            return term_from_int11(external_term_buf[1]);
        }

        case INTEGER_EXT: {
            int32_t value = READ_32_UNALIGNED(external_term_buf + 1);

            *eterm_size = 5;

            return term_make_maybe_boxed_int64(value, heap);
        }

        case SMALL_BIG_EXT: {
            uint8_t num_bytes = external_term_buf[1];
            uint8_t sign = external_term_buf[2];
            avm_uint64_t unsigned_value = read_bytes(external_term_buf + 3, num_bytes);
            // NB due to call to calculate_heap_usage, there is no loss of precision:
            // 1. 0 <= unsigned_value <= INT64_MAX if sign is 0
            // 2. 0 <= unsigned_value <= INT64_MAX + 1 if sign is not 0
            avm_int64_t value = 0;
            if (sign != 0x00) {
                value = -((avm_int64_t) unsigned_value);
            } else {
                value = (avm_int64_t) unsigned_value;
            }
            *eterm_size = SMALL_BIG_EXT_BASE_SIZE + num_bytes;
            return term_make_maybe_boxed_int64(value, heap);
        }

        case ATOM_EXT: {
            uint16_t atom_len = READ_16_UNALIGNED(external_term_buf + 1);

            if (UNLIKELY(atom_len > 255)) {
                return term_invalid_term();
            }

            const uint8_t *atom_chars = (const uint8_t *) (external_term_buf + 3);
            int global_atom_id;
            if (LIKELY(unicode_buf_is_ascii(atom_chars, atom_len))) {
                // there is a trick here: we are reusing LSB of len field as atom length
                global_atom_id = globalcontext_insert_atom_maybe_copy(
                    glb, (AtomString) (external_term_buf + 2), copy);
            } else {
                // need to re-encode latin1 to UTF-8
                size_t required_buf_size = unicode_latin1_buf_size_as_utf8(atom_chars, atom_len);
                if (UNLIKELY(required_buf_size > 255)) {
                    return term_invalid_term();
                }
                uint8_t *atom_buf = malloc(1 + required_buf_size);
                atom_buf[0] = required_buf_size;
                uint8_t *curr_codepoint = &atom_buf[1];
                for (int i = 0; i < atom_len; i++) {
                    size_t codepoint_size;
                    // latin1 encoding is always successful
                    bitstring_utf8_encode(atom_chars[i], curr_codepoint, &codepoint_size);
                    curr_codepoint += codepoint_size;
                }
                global_atom_id
                    = globalcontext_insert_atom_maybe_copy(glb, (AtomString) atom_buf, true);
                free(atom_buf);
            }

            if (UNLIKELY(global_atom_id) < 0) {
                return term_invalid_term();
            }

            *eterm_size = 3 + atom_len;
            return term_from_atom_index(global_atom_id);
        }

        case SMALL_TUPLE_EXT:
        case LARGE_TUPLE_EXT: {
            size_t arity;
            int buf_pos;
            if (external_term_buf[0] == SMALL_TUPLE_EXT) {
                arity = external_term_buf[1];
                buf_pos = 2;
            } else {
                arity = READ_32_UNALIGNED(external_term_buf + 1);
                buf_pos = 5;
            }
            term tuple = term_alloc_tuple(arity, heap);

            for (size_t i = 0; i < arity; i++) {
                size_t element_size;
                term put_value = parse_external_terms(external_term_buf + buf_pos, &element_size, copy, heap, glb);
                if (UNLIKELY(term_is_invalid_term(put_value))) {
                    return put_value;
                }
                term_put_tuple_element(tuple, i, put_value);

                buf_pos += element_size;
            }

            *eterm_size = buf_pos;
            return tuple;
        }

        case NIL_EXT: {
            *eterm_size = 1;
            return term_nil();
        }

        case STRING_EXT: {
            uint16_t string_size = READ_16_UNALIGNED(external_term_buf + 1);
            *eterm_size = 3 + string_size;
            return term_from_string((uint8_t *) external_term_buf + 3, string_size, heap);
        }

        case LIST_EXT: {
            uint32_t list_len = READ_32_UNALIGNED(external_term_buf + 1);

            term list_begin = term_nil();
            term *prev_term = NULL;

            int buf_pos = 5;

            for (unsigned int i = 0; i < list_len; i++) {
                size_t item_size;
                term head = parse_external_terms(external_term_buf + buf_pos, &item_size, copy, heap, glb);
                if (UNLIKELY(term_is_invalid_term(head))) {
                    return head;
                }
                term *new_list_item = term_list_alloc(heap);

                if (prev_term) {
                    prev_term[0] = term_list_from_list_ptr(new_list_item);
                } else {
                    list_begin = term_list_from_list_ptr(new_list_item);
                }

                prev_term = new_list_item;
                new_list_item[1] = head;

                buf_pos += item_size;
            }

            if (prev_term) {
                size_t tail_size;
                term tail = parse_external_terms(external_term_buf + buf_pos, &tail_size, copy, heap, glb);
                if (UNLIKELY(term_is_invalid_term(tail))) {
                    return tail;
                }
                prev_term[0] = tail;
                buf_pos += tail_size;
            }

            *eterm_size = buf_pos;
            return list_begin;
        }

        case BINARY_EXT: {
            uint32_t binary_size = READ_32_UNALIGNED(external_term_buf + 1);
            *eterm_size = 5 + binary_size;
            if (copy) {
                return term_from_literal_binary((uint8_t *) external_term_buf + 5, binary_size, heap, glb);
            } else {
                return term_from_const_binary((uint8_t *) external_term_buf + 5, binary_size, heap, glb);
            }
        }

        case EXPORT_EXT: {
            size_t buf_pos = 1;
            size_t element_size;

            term m = parse_external_terms(external_term_buf + buf_pos, &element_size, copy, heap, glb);
            if (UNLIKELY(term_is_invalid_term(m))) {
                return m;
            }
            buf_pos += element_size;

            term f = parse_external_terms(external_term_buf + buf_pos, &element_size, copy, heap, glb);
            if (UNLIKELY(term_is_invalid_term(f))) {
                return f;
            }
            buf_pos += element_size;

            term a = parse_external_terms(external_term_buf + buf_pos, &element_size, copy, heap, glb);
            if (UNLIKELY(term_is_invalid_term(a))) {
                return a;
            }
            buf_pos += element_size;

            *eterm_size = buf_pos;
            return term_make_function_reference(m, f, a, heap);
        }

        case MAP_EXT: {
            uint32_t size = READ_32_UNALIGNED(external_term_buf + 1);
            term map = term_alloc_map(size, heap);
            size_t buf_pos = 5;
            for (uint32_t i = 0; i < size; ++i) {
                size_t key_size;
                term key = parse_external_terms(external_term_buf + buf_pos, &key_size, copy, heap, glb);
                if (UNLIKELY(term_is_invalid_term(key))) {
                    return key;
                }
                buf_pos += key_size;

                size_t value_size;
                term value = parse_external_terms(external_term_buf + buf_pos, &value_size, copy, heap, glb);
                if (UNLIKELY(term_is_invalid_term(value))) {
                    return value;
                }
                buf_pos += value_size;

                term_set_map_assoc(map, i, key, value);
            }
            *eterm_size = buf_pos;
            return map;
        }

        case SMALL_ATOM_UTF8_EXT: {
            uint8_t atom_len = *(external_term_buf + 1);
            const uint8_t *atom_chars = external_term_buf + 2;

            if (UNLIKELY(!unicode_is_valid_utf8_buf((const uint8_t *) atom_chars, atom_len))) {
                return term_invalid_term();
            }

            // AtomString first byte is the atom length
            int global_atom_id = globalcontext_insert_atom_maybe_copy(glb, (AtomString) (external_term_buf + 1), copy);
            if (UNLIKELY(global_atom_id < 0)) {
                return term_invalid_term();
            }

            *eterm_size = 2 + atom_len;
            return term_from_atom_index(global_atom_id);
        }

        case NEW_PID_EXT: {
            size_t node_size;
            term node = parse_external_terms(external_term_buf + 1, &node_size, copy, heap, glb);
            if (UNLIKELY(!term_is_atom(node))) {
                return term_invalid_term();
            }
            uint32_t number = READ_32_UNALIGNED(external_term_buf + node_size + 1);
            uint32_t serial = READ_32_UNALIGNED(external_term_buf + node_size + 5);
            uint32_t creation = READ_32_UNALIGNED(external_term_buf + node_size + 9);
            *eterm_size = node_size + 13;
            if (node != NONODE_AT_NOHOST_ATOM) {
                term this_node = glb->node_name;
                uint32_t this_creation = this_node == NONODE_AT_NOHOST_ATOM ? 0 : glb->creation;
                if (node == this_node && creation == this_creation) {
                    return term_from_local_process_id(number);
                } else {
                    return term_make_external_process_id(node, number, serial, creation, heap);
                }
            } else {
                if (UNLIKELY(serial != 0 || creation != 0)) {
                    return term_invalid_term();
                }
                return term_from_local_process_id(number);
            }
        }

        case V4_PORT_EXT: {
            size_t node_size;
            term node = parse_external_terms(external_term_buf + 1, &node_size, copy, heap, glb);
            if (UNLIKELY(!term_is_atom(node))) {
                return term_invalid_term();
            }
            uint64_t number = READ_64_UNALIGNED(external_term_buf + node_size + 1);
            uint32_t creation = READ_32_UNALIGNED(external_term_buf + node_size + 9);
            *eterm_size = node_size + 13;
            if (node != NONODE_AT_NOHOST_ATOM) {
                term this_node = glb->node_name;
                uint32_t this_creation = this_node == NONODE_AT_NOHOST_ATOM ? 0 : glb->creation;
                if (node == this_node && creation == this_creation) {
                    if (UNLIKELY(number > TERM_MAX_LOCAL_PROCESS_ID)) {
                        return term_invalid_term();
                    }
                    return term_port_from_local_process_id(number);
                } else {
                    return term_make_external_port_number(node, number, creation, heap);
                }
            } else {
                if (UNLIKELY(number > TERM_MAX_LOCAL_PROCESS_ID || creation != 0)) {
                    return term_invalid_term();
                }
                return term_port_from_local_process_id(number);
            }
        }

        case NEWER_REFERENCE_EXT: {
            uint16_t len = READ_16_UNALIGNED(external_term_buf + 1);
            if (UNLIKELY(len > 5)) {
                return term_invalid_term();
            }
            size_t node_size;
            term node = parse_external_terms(external_term_buf + 3, &node_size, copy, heap, glb);
            if (UNLIKELY(!term_is_atom(node))) {
                return term_invalid_term();
            }
            uint32_t creation = READ_32_UNALIGNED(external_term_buf + node_size + 3);
            uint32_t data[5];
            for (uint16_t i = 0; i < len; i++) {
                data[i] = READ_32_UNALIGNED(external_term_buf + node_size + 7 + (i * 4));
            }
            *eterm_size = node_size + 7 + (len * 4);
            if (node != NONODE_AT_NOHOST_ATOM || len != 2 || creation != 0) {
                term this_node = glb->node_name;
                uint32_t this_creation = this_node == NONODE_AT_NOHOST_ATOM ? 0 : glb->creation;
                if (len == 2 && node == this_node && creation == this_creation) {
                    uint64_t ticks = ((uint64_t) data[0]) << 32 | data[1];
                    return term_from_ref_ticks(ticks, heap);
                } else {
                    return term_make_external_reference(node, len, data, creation, heap);
                }
            } else {
                uint64_t ticks = ((uint64_t) data[0]) << 32 | data[1];
                return term_from_ref_ticks(ticks, heap);
            }
        }

        default:
            return term_invalid_term();
    }
}

static int calculate_heap_usage(const uint8_t *external_term_buf, size_t remaining, size_t *eterm_size, bool copy)
{
    if (UNLIKELY(remaining < 1)) {
        return INVALID_TERM_SIZE;
    }
    switch (external_term_buf[0]) {
        case NEW_FLOAT_EXT: {
            if (UNLIKELY(remaining < NEW_FLOAT_EXT_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = NEW_FLOAT_EXT_SIZE;
            return FLOAT_SIZE;
        }

        case SMALL_INTEGER_EXT: {
            if (UNLIKELY(remaining < SMALL_INTEGER_EXT_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = SMALL_INTEGER_EXT_SIZE;
            return 0;
        }

        case INTEGER_EXT: {
            if (UNLIKELY(remaining < INTEGER_EXT_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            int32_t value = READ_32_UNALIGNED(external_term_buf + 1);
            *eterm_size = INTEGER_EXT_SIZE;
            return term_boxed_integer_size(value);
        }

        case SMALL_BIG_EXT: {
            size_t num_bytes = external_term_buf[1];
            if (UNLIKELY(num_bytes > 8 || remaining < (SMALL_BIG_EXT_BASE_SIZE + num_bytes))) {
                return INVALID_TERM_SIZE;
            }
            uint8_t sign = external_term_buf[2];
            *eterm_size = SMALL_BIG_EXT_BASE_SIZE + num_bytes;
            avm_uint64_t unsigned_value = read_bytes(external_term_buf + 3, num_bytes);
            // NB.  We currently support max 64-bit signed integers (assuming two's complement signed values in 63 bits)
            if (UNLIKELY((sign == 0 && unsigned_value > INT64_MAX) || (sign != 0 && unsigned_value > (((avm_uint64_t) INT64_MAX) + 1)))) {
                return INVALID_TERM_SIZE;
            }
            // Compute the size with the sign as -2^27 or -2^59 can be encoded
            // on 1 term while 2^27 and 2^59 respectively (32/64 bits) cannot.
            avm_int64_t value = 0;
            if (sign != 0x00) {
                value = -((avm_int64_t) unsigned_value);
            } else {
                value = (avm_int64_t) unsigned_value;
            }
            return term_boxed_integer_size(value);
        }

        case ATOM_EXT: {
            if (UNLIKELY(remaining < ATOM_EXT_BASE_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            uint16_t atom_len = READ_16_UNALIGNED(external_term_buf + 1);
            remaining -= ATOM_EXT_BASE_SIZE;
            if (UNLIKELY(remaining < atom_len)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = ATOM_EXT_BASE_SIZE + atom_len;
            return 0;
        }

        case SMALL_TUPLE_EXT:
        case LARGE_TUPLE_EXT: {
            size_t arity;
            size_t buf_pos;
            if (external_term_buf[0] == SMALL_TUPLE_EXT) {
                if (UNLIKELY(remaining < 1)) {
                    return INVALID_TERM_SIZE;
                }
                remaining--;
                arity = external_term_buf[1];
                buf_pos = 2;
            } else {
                if (UNLIKELY(remaining < 5)) {
                    return INVALID_TERM_SIZE;
                }
                remaining -= 5;
                arity = READ_32_UNALIGNED(external_term_buf + 1);
                buf_pos = 5;
            }

            if (UNLIKELY(remaining < arity)) {
                return INVALID_TERM_SIZE;
            }

            int heap_usage = 1;

            for (size_t i = 0; i < arity; i++) {
                size_t element_size = 0;
                int u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &element_size, copy);
                if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                    return INVALID_TERM_SIZE;
                }
                u += 1;
                if (UNLIKELY(remaining < element_size)) {
                    return INVALID_TERM_SIZE;
                }
                remaining -= element_size;
                heap_usage += u;

                buf_pos += element_size;
            }

            *eterm_size = buf_pos;
            return heap_usage;
        }

        case NIL_EXT: {
            if (UNLIKELY(remaining < 1)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = 1;
            return 0;
        }

        case STRING_EXT: {
            if (UNLIKELY(remaining < STRING_EXT_BASE_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            uint16_t string_size = READ_16_UNALIGNED(external_term_buf + 1);
            remaining -= STRING_EXT_BASE_SIZE;
            if (UNLIKELY(remaining < string_size)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = STRING_EXT_BASE_SIZE + string_size;
            return string_size * 2;
        }

        case LIST_EXT: {
            if (UNLIKELY(remaining < LIST_EXT_BASE_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            uint32_t list_len = READ_32_UNALIGNED(external_term_buf + 1);
            remaining -= LIST_EXT_BASE_SIZE;
            if (UNLIKELY(remaining < list_len)) {
                return INVALID_TERM_SIZE;
            }

            int buf_pos = 5;
            int heap_usage = 0;

            for (unsigned int i = 0; i < list_len; i++) {
                size_t item_size = 0;
                int u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &item_size, copy);
                if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                    return INVALID_TERM_SIZE;
                }
                u += 2;
                heap_usage += u;
                buf_pos += item_size;
                if (UNLIKELY(remaining < item_size)) {
                    return INVALID_TERM_SIZE;
                }
                remaining -= item_size;
            }

            size_t tail_size = 0;
            int u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &tail_size, copy);
            if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            heap_usage += u;
            buf_pos += tail_size;
            if (UNLIKELY(remaining < tail_size)) {
                return INVALID_TERM_SIZE;
            }
            // remaining -= tail_size; Not needed, since remaining is local, but here for completeness

            *eterm_size = buf_pos;
            return heap_usage;
        }

        case BINARY_EXT: {
            if (UNLIKELY(remaining < BINARY_EXT_BASE_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            uint32_t binary_size = READ_32_UNALIGNED(external_term_buf + 1);
            remaining -= LIST_EXT_BASE_SIZE;
            if (UNLIKELY(remaining < binary_size)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = BINARY_EXT_BASE_SIZE + binary_size;

            #if TERM_BYTES == 4
                int size_in_terms = ((binary_size + 4 - 1) >> 2);
            #elif TERM_BYTES == 8
                int size_in_terms = ((binary_size + 8 - 1) >> 3);
            #else
                #error
            #endif

            if (copy && term_binary_size_is_heap_binary(binary_size)) {
                return 2 + size_in_terms;
            } else {
                return TERM_BOXED_REFC_BINARY_SIZE;
            }
        }

        case EXPORT_EXT: {
            if (UNLIKELY(remaining < 1)) {
                return INVALID_TERM_SIZE;
            }
            int heap_usage = 1;
            int buf_pos = 1;
            remaining -= 1;
            for (int i = 0; i < 3; i++) {
                size_t element_size = 0;
                int u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &element_size, copy);
                if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                    return INVALID_TERM_SIZE;
                }
                u += 1;
                heap_usage += u;
                buf_pos += element_size;
                if (UNLIKELY(remaining < element_size)) {
                    return INVALID_TERM_SIZE;
                }
                remaining -= element_size;
            }

            *eterm_size = buf_pos;
            return heap_usage;
        }

        case MAP_EXT: {
            if (UNLIKELY(remaining < MAP_EXT_BASE_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            uint32_t size = READ_32_UNALIGNED(external_term_buf + 1);
            remaining -= MAP_EXT_BASE_SIZE;
            if (UNLIKELY(remaining < size)) {
                return INVALID_TERM_SIZE;
            }
            int heap_usage = 1;
            size_t buf_pos = MAP_EXT_BASE_SIZE;
            for (uint32_t i = 0; i < size; ++i) {
                size_t key_size = 0;
                int u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &key_size, copy);
                if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                    return INVALID_TERM_SIZE;
                }
                u += 1;
                heap_usage += u;
                buf_pos += key_size;
                if (UNLIKELY(remaining < key_size)) {
                    return INVALID_TERM_SIZE;
                }
                remaining -= key_size;
                size_t value_size = 0;
                u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &value_size, copy);
                if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                    return INVALID_TERM_SIZE;
                }
                u += 1;
                heap_usage += u;
                buf_pos += value_size;
                if (UNLIKELY(remaining < value_size)) {
                    return INVALID_TERM_SIZE;
                }
                remaining -= value_size;
            }
            *eterm_size = buf_pos;
            return heap_usage + 2 + 1; // keys tuple header and size (2 words) + tuple_ptr (1 word)
        }

        case SMALL_ATOM_UTF8_EXT: {
            if (UNLIKELY(remaining < SMALL_ATOM_EXT_BASE_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            uint8_t atom_len = *(external_term_buf + 1);
            remaining -= SMALL_ATOM_EXT_BASE_SIZE;
            if (UNLIKELY(remaining < atom_len)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = SMALL_ATOM_EXT_BASE_SIZE + atom_len;
            return 0;
        }

        case NEW_PID_EXT:
        case V4_PORT_EXT: {
            if (UNLIKELY(remaining < 1)) {
                return INVALID_TERM_SIZE;
            }
            remaining -= 1;
            int buf_pos = 1;
            size_t heap_size = EXTERNAL_PID_SIZE;
            size_t node_size = 0;
            int u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &node_size, copy);
            if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            if (external_term_buf[1] == SMALL_ATOM_UTF8_EXT) {
                // Check if it's non-distributed node, in which case it's always a local pid
                if (external_term_buf[2] == strlen("nonode@nohost") && memcmp(external_term_buf + 3, "nonode@nohost", strlen("nonode@nohost")) == 0) {
                    heap_size = 0;
                }
                // If this is our node, but we're distributed, we'll allocate more memory and may not use it.
                // This way we're sure to not go out of bounds if distribution changes between now and when we deserialize
            } else if (UNLIKELY(external_term_buf[1] != ATOM_EXT)) {
                return INVALID_TERM_SIZE;
            }
            buf_pos += node_size;
            remaining -= node_size;
            if (UNLIKELY(remaining < 3 * 4)) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = buf_pos + 12;
            return heap_size + u;
        }

        case NEWER_REFERENCE_EXT: {
            if (UNLIKELY(remaining < 3)) {
                return INVALID_TERM_SIZE;
            }
            remaining -= 3;
            int buf_pos = 3;
            uint16_t len = READ_16_UNALIGNED(external_term_buf + 1);
            size_t heap_size = EXTERNAL_REF_SIZE(len);
            size_t node_size = 0;
            int u = calculate_heap_usage(external_term_buf + buf_pos, remaining, &node_size, copy);
            if (UNLIKELY(u == INVALID_TERM_SIZE)) {
                return INVALID_TERM_SIZE;
            }
            if (external_term_buf[3] == SMALL_ATOM_UTF8_EXT) {
                // Check if it's non-distributed node, in which case it's always a local ref
                if (len == 2 && external_term_buf[4] == strlen("nonode@nohost") && memcmp(external_term_buf + 5, "nonode@nohost", strlen("nonode@nohost")) == 0) {
                    heap_size = REF_SIZE;
                }
                // See above for pids
            } else if (UNLIKELY(external_term_buf[3] != ATOM_EXT)) {
                return INVALID_TERM_SIZE;
            }
            buf_pos += node_size;
            remaining -= node_size;
            if (UNLIKELY(remaining < (size_t) ((len + 1) * 4))) {
                return INVALID_TERM_SIZE;
            }
            *eterm_size = buf_pos + 4 + (len * 4);
            return heap_size + u;
        }

        default:
            return INVALID_TERM_SIZE;
    }
}

enum ExternalTermResult externalterm_compute_external_size_raw(
    term t, size_t *size, GlobalContext *glb)
{
    *size = compute_external_size(t, glb);

    return EXTERNAL_TERM_OK;
}

enum ExternalTermResult externalterm_serialize_term_raw(void *buf, term t, GlobalContext *glb)
{
    serialize_term(buf, t, glb);

    return EXTERNAL_TERM_OK;
}
