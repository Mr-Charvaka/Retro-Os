#ifndef NETFS_H
#define NETFS_H

#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

void netfs_init(void);
struct vfs_node *netfs_mount(const char *host_ip, const char *path);

#ifdef __cplusplus
}
#endif

#endif
