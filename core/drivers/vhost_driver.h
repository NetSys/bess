#ifndef VHOST_DRIVER_H
#define	VHOST_DRIVER_H

#ifdef	__cplusplus
extern "C" {
#endif
#include <rte_ether.h>
#include "rte_virtio_net.h"

#define VHOST_DIR_PREFIX "/tmp/sn_vhost_"

/* State of virtio device. */
#define DEVICE_MAC_LEARNING     0
#define DEVICE_RX		1
#define DEVICE_SAFE_REMOVE	2

/* Config_core_flag status definitions. */
#define REQUEST_DEV_REMOVAL 1
#define ACK_DEV_REMOVAL     0    

/*
 * Device linked list structure for data path.
 */
struct vhost_dev {
        /**< Local copy of the port name. */
        char name[4096];
	/**< Pointer to device created by vhost lib. */
	struct virtio_net      *dev;
        /**< Device MAC address (Obtained on first TX packet). */
	struct ether_addr mac_address;
	/**< Data core that the device is added to. */
	uint16_t coreid;
	/**< A device is set as ready if the MAC address has been set. */
	volatile uint8_t ready;
	/**< Device is marked for removal from the data core. */
	volatile uint8_t remove;
} __rte_cache_aligned;

struct virtio_net_data_ll
{
	struct vhost_dev		*vdev;	/* Pointer to device created by configuration core. */
	struct virtio_net_data_ll	*next;  /* Pointer to next device in linked list. */
};

/*
 * Place an entry back on to the free linked list.
 */
static void
put_data_ll_free_entry(struct virtio_net_data_ll **ll_root_addr,
	struct virtio_net_data_ll *ll_dev)
{
	struct virtio_net_data_ll *ll_free = *ll_root_addr;

	if (ll_dev == NULL)
		return;

	ll_dev->next = ll_free;
	*ll_root_addr = ll_dev;
}

/*
 * Remove an entry from a used linked list. The entry must then be added to
 * the free linked list using put_data_ll_free_entry().
 */
static void
rm_data_ll_entry(struct virtio_net_data_ll **ll_root_addr,
	struct virtio_net_data_ll *ll_dev,
	struct virtio_net_data_ll *ll_dev_last)
{
	struct virtio_net_data_ll *ll = *ll_root_addr;

	if (unlikely((ll == NULL) || (ll_dev == NULL)))
		return;

	if (ll_dev == ll)
		*ll_root_addr = ll_dev->next;
	else
		if (likely(ll_dev_last != NULL))
			ll_dev_last->next = ll_dev->next;
		else
			printf("Remove entry from ll failed.\n");
}

/*
 * Add an entry to a used linked list. A free entry must first be found
 * in the free linked list using get_data_ll_free_entry();
 */
static void
add_data_ll_entry(struct virtio_net_data_ll **ll_root_addr,
	struct virtio_net_data_ll *ll_dev)
{
	struct virtio_net_data_ll *ll = *ll_root_addr;

	/* Set next as NULL and use a compiler barrier to avoid reordering. */
	ll_dev->next = NULL;
	rte_compiler_barrier();

	/* If ll == NULL then this is the first device. */
	if (ll) {
		/* Increment to the tail of the linked list. */
		while ((ll->next != NULL) )
			ll = ll->next;

		ll->next = ll_dev;
	} else {
		*ll_root_addr = ll_dev;
	}
}

/*
 * Find and return an entry from the free linked list.
 */
static struct virtio_net_data_ll *
get_data_ll_free_entry(struct virtio_net_data_ll **ll_root_addr)
{
	struct virtio_net_data_ll *ll_free = *ll_root_addr;
	struct virtio_net_data_ll *ll_dev;

	if (ll_free == NULL)
		return NULL;

	ll_dev = ll_free;
	*ll_root_addr = ll_free->next;

	return ll_dev;
}

#ifdef	__cplusplus
}
#endif

#endif	/* VHOST_DRIVER_H */

