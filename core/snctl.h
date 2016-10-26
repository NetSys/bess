#ifndef BESS_CORE_SNCTL_H_
#define BESS_CORE_SNCTL_H_

struct client;
struct snobj;

struct snobj *handle_request(struct client *c, struct snobj *q);

#endif
