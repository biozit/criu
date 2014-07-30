#ifndef __CR_NET_H__
#define __CR_NET_H__

#include "list.h"

struct cr_fdset;
int dump_net_ns(int pid, int ns_id);
int prepare_net_ns(int pid);
int netns_pre_create(void);

struct veth_pair {
	struct list_head node;
	char *inside;
	char *outside;
};

extern int network_lock(void);
extern void network_unlock(void);

extern struct ns_desc net_ns_desc;

#include "protobuf/netdev.pb-c.h"
int write_netdev_img(NetDeviceEntry *nde, struct cr_fdset *fds);
int read_ns_sys_file(char *path, char *buf, int len);
int restore_link_parms(NetDeviceEntry *nde, int nlsk);

#endif /* __CR_NET_H__ */
