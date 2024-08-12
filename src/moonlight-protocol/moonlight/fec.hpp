#pragma once

#include <memory>

extern "C" {
#include "rswrapper.h"
}

/**
 * FEC (Forward Error Correction)
 *
 * Moonlight uses Reed Solomon ( https://en.wikipedia.org/wiki/Reed%E2%80%93Solomon_error_correction )
 * to encode the payload so that it can be checked on the receiving end for transmission errors
 * (and possibly fix them).
 *
 * This is just a small wrapper on top of the excellent https://github.com/sleepybishop/nanors implementation
 */
namespace moonlight::fec {

/**
 * Maximum number of data shards that can be encoded in one go
 */
#define DATA_SHARDS_MAX 255

/**
 * One time initialization required by the library
 */
inline void init() {
  reed_solomon_init();
}

/**
 * A smart pointer to the reed_solomon data structure, it will release the memory when going out of scope
 */
using rs_ptr = std::shared_ptr<reed_solomon>;

/**
 * Creates and allocates the required Reed Solomon data structure.
 *
 * @param data_shards Number of data shards to be encoded
 * @param parity_shards Number of parity shards to be created
 *
 * @return A smart pointer, it will release the memory when going out of scope
 */
inline rs_ptr create(int data_shards, int parity_shards) {
  auto rs = reed_solomon_new_fn(data_shards, parity_shards);
  return std::shared_ptr<reed_solomon>(rs, reed_solomon_release_fn);
}

/**
 * Encodes the input data shards using Reed Solomon.
 * It will read \p nr_shards * \p block_size and then append all the newly created parity shards
 * to \p shards.
 *
 * @warning \p shards MUST be of size: shards[data_shards + parity_shards][block_size]
 * @warning The content of \p shards after \p nr_shards will be overwritten
 *
 * @param rs the reed solomon data structure created with `create()`
 * @param shards[in, out] the memory location where data and parity blocks will live
 * @param nr_shards the total number of shards ( data_shards + parity_shards )
 * @param block_size the size of each block that needs to be encoded
 *
 * @return zero on success or an error code if failing.
 */
inline int encode(reed_solomon *rs, uint8_t **shards, int nr_shards, int block_size) {
  return reed_solomon_encode_fn(rs, shards, nr_shards, block_size);
}

/**
 * Decodes back the input data shards using Reed Solomon.
 * It will recreate missing blocks based on the \p marks property
 *
 * @warning \p shards MUST be of size: shards[data_shards + parity_shards][block_size]
 * @warning The content of \p shards where blocks are missing will be overwritten
 *
 * @param rs the reed solomon data structure created with `create()`
 * @param shards[in, out] the memory location where data and parity blocks will live
 * @param marks an array of size \p nr_shards, if `marks[i] == 1` that blocks will be reconstructed
 * @param nr_shards the total number of shards ( data_shards + parity_shards )
 * @param block_size the size of each block that needs to be encoded
 *
 * @return zero on success or an error code if failing
 */
inline int decode(reed_solomon *rs, uint8_t **shards, uint8_t *marks, int nr_shards, int block_size) {
  return reed_solomon_decode_fn(rs, shards, marks, nr_shards, block_size);
}

} // namespace moonlight::fec