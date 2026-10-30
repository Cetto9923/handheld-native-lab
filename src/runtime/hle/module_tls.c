#define _POSIX_C_SOURCE 200112L
#include "module_tls.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct nso_module_tls_image {
    void *allocation;
    nso_module_tls_view view;
};
static int rounded(size_t value,size_t alignment,size_t *out) {
    if(value>SIZE_MAX-(alignment-1))return EOVERFLOW;
    *out=(value+alignment-1)&~(alignment-1);
    return 0;
}
int nso_module_tls_create(const nso_module_tls_template *source,nso_module_tls_image **out) {
    if(!out)return EINVAL;
    *out=NULL;
    if(!source || !source->alignment || (source->alignment&(source->alignment-1)) ||
       source->initialized_size>source->total_size ||
       (source->initialized_size && !source->initialized_data))return EINVAL;
    if(source->total_size>SIZE_MAX-source->reserved_header_size)return EOVERFLOW;
    size_t storage_size,prefix_padded;
    int result=rounded(source->reserved_header_size+source->total_size,source->alignment,&storage_size);
    if(result)return result;
    size_t host_alignment=source->alignment<sizeof(void*)?sizeof(void*):source->alignment;
    result=rounded(source->reserved_header_size,host_alignment,&prefix_padded);
    if(result)return result;
    size_t leading=prefix_padded-source->reserved_header_size;
    if(storage_size>SIZE_MAX-leading)return EOVERFLOW;
    size_t allocation_size=leading+storage_size;
    /* Keep a valid owner even for a zero-span, zero-header template. */
    if(!allocation_size)allocation_size=1;
    nso_module_tls_image *image=calloc(1,sizeof(*image));
    if(!image)return ENOMEM;
    result=posix_memalign(&image->allocation,host_alignment,allocation_size);
    if(result) {free(image);return result;}
    memset(image->allocation,0,allocation_size);
    image->view=(nso_module_tls_view){
        .anchor=(unsigned char*)image->allocation+leading,
        .payload=(unsigned char*)image->allocation+prefix_padded,
        .initialized_size=source->initialized_size,.total_size=source->total_size,
        .alignment=source->alignment,.reserved_header_size=source->reserved_header_size,
        .storage_size=storage_size
    };
    if(source->initialized_size)
        memcpy(image->view.payload,source->initialized_data,source->initialized_size);
    *out=image;
    return 0;
}
const nso_module_tls_view *nso_module_tls_get_view(const nso_module_tls_image *image) {
    return image?&image->view:NULL;
}
void nso_module_tls_free(nso_module_tls_image *image) {
    if(image) {free(image->allocation);free(image);}
}
