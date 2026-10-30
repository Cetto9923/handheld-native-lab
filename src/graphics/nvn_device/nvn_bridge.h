/* The objects the replay layer reads, made out of the state this runtime
 * keeps.
 *
 * The replay layer of this game reads NVN objects the way the reference host
 * writes them: a texture is a description, a memory pool and a number; a
 * program is the shaders of its stages and a number. This runtime keeps the
 * state of those objects on the host, keyed by the guest's address, so the
 * objects the replay layer sees are copies that this bridge makes.
 *
 * The copies live as long as the objects they describe. That matters twice
 * over: a command that was recorded holds the pointer to the texture it
 * named, and the replay layer reads that texture when the command is
 * submitted, which can be much later; and the replay layer keys its own
 * tables by the number of the object, so the number has to stay the same for
 * as long as the object is the same one.
 *
 * The number changes when the guest initializes the same address again: the
 * guest may reuse an address for a different object, and the replay layer
 * has to see two objects there and not one.
 */
#ifndef FFRS_NVN_BRIDGE_H
#define FFRS_NVN_BRIDGE_H

#include <stddef.h>

#include "nvn_obj_ffrs.h"

/* The texture of the guest's address, or NULL when this runtime has no
 * texture there. The pointer stays the same for as long as the texture is
 * the same object. */
const ffrs_tex *nvn_bridge_texture(const void *guest_texture);
/* End one generation; recorded immutable copies remain allocated. */
void nvn_bridge_texture_end(const void *guest_texture, uint64_t serial);

/* The program of the guest's address, or NULL. */
const ffrs_prog *nvn_bridge_program(const void *guest_program);

/* The memory pool of the guest's address, or NULL. */
const ffrs_pool *nvn_bridge_pool(const void *guest_pool);

/* The texture descriptor pool of the guest's address, or NULL. */
const ffrs_dpool *nvn_bridge_texture_pool(const void *guest_pool);

/* The sampler descriptor pool of the guest's address, or NULL. */
const ffrs_dpool *nvn_bridge_sampler_pool(const void *guest_pool);

/* The sampler of the guest's address, or NULL. */
const ffrs_samp_par *nvn_bridge_sampler(const void *guest_sampler);

/* The view of the guest's address, or NULL. */
const ffrs_view *nvn_bridge_view(const void *guest_view);

/* How many objects the bridge holds, and how many times it had to make a new
 * one because the guest initialized the same address again. */
void nvn_bridge_get_stats(uint64_t *objects, uint64_t *regenerated);

#endif /* FFRS_NVN_BRIDGE_H */
