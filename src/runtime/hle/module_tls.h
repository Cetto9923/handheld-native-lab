#ifndef NSO_MODULE_TLS_H
#define NSO_MODULE_TLS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A caller-verified module template, already relocated if relocation is needed.
 * Keep initialized_data readable and its module alive until create returns.
 * No source or module pointer is retained after the synchronous copy. */
typedef struct nso_module_tls_template {
    const void *initialized_data;
    size_t initialized_size;
    size_t total_size;
    size_t alignment; /* Nonzero power of two; applies to payload, not anchor. */
    size_t reserved_header_size; /* Opaque zero-filled prefix; no TCB fields. */
} nso_module_tls_template;

typedef struct nso_module_tls_image nso_module_tls_image;
typedef struct nso_module_tls_view {
    unsigned char *anchor;
    unsigned char *payload;
    size_t initialized_size,total_size,alignment,reserved_header_size;
    size_t storage_size; /* Rounded anchor-to-end span, including prefix. */
} nso_module_tls_view;

/* Return 0 after a real allocation/copy, or a POSIX error value. On error *out
 * is NULL. Empty templates (initialized_size==0) need no source pointer.
 * For the reviewed main, use sizes 0x220/0x1019, alignment 8 and prefix 0x10.
 * The anchor then models only the address relation payload==anchor+0x10;
 * it is NOT a complete SDK ThreadPointer and is never installed in a TP register.
 * No global current-image pointer, TLS key, import binding or destructor exists.
 */
int nso_module_tls_create(const nso_module_tls_template *source,nso_module_tls_image **out);

/* Metadata belongs to the image. Payload is mutable storage owned by its caller.
 * Different images may be used concurrently; sharing one image needs caller
 * synchronization. The view and all its pointers expire when the image is freed.
 */
const nso_module_tls_view *nso_module_tls_get_view(const nso_module_tls_image *image);

/* Free only this storage. Join/quiesce its users first. No guest callback runs. */
void nso_module_tls_free(nso_module_tls_image *image);

#ifdef __cplusplus
}
#endif
#endif
