#ifndef CORE_SNCTL_H_
#define CORE_SNCTL_H_

struct client;
struct snobj;

struct snobj *handle_request(struct client *c, struct snobj *q);

#endif
