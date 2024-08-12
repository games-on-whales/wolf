/**
 * @file src/rswrapper.h
 * @brief Wrappers for nanors vectorization
 * @details This is a drop-in replacement for nanors rs.h
 */
#pragma once

#include "rs.h"
#include <stdint.h>

typedef struct _reed_solomon reed_solomon;

typedef reed_solomon *(*reed_solomon_new_t)(int data_shards, int parity_shards);
typedef void (*reed_solomon_release_t)(reed_solomon *rs);
typedef int (*reed_solomon_encode_t)(reed_solomon *rs, uint8_t **shards, int nr_shards, int bs);
typedef int (*reed_solomon_decode_t)(reed_solomon *rs, uint8_t **shards, uint8_t *marks, int nr_shards, int bs);

extern reed_solomon_new_t reed_solomon_new_fn;
extern reed_solomon_release_t reed_solomon_release_fn;
extern reed_solomon_encode_t reed_solomon_encode_fn;
extern reed_solomon_decode_t reed_solomon_decode_fn;