#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_common.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_version.h>
#include <unistd.h>

#include "../driver.h"
#include "../port.h"
#include "../snobj.h"
#include "vhost_driver.h"

#define CHECK_BEFORE_ACCEPT
#define ENABLE_VHOST_RETRIES

#ifdef ENABLE_VHOST_RETRIES
#define BURST_RX_WAIT_US 15	/* Defines how long we wait between retries on RX */
#define BURST_RX_RETRIES 4	/* Number of retries on RX. */

/* Enable retries on RX. */
static uint32_t enable_retry = 1;
/* Specify timeout (in useconds) between retries on RX. */
static uint32_t burst_rx_delay_time = BURST_RX_WAIT_US;
/* Specify the number of retries on RX. */
static uint32_t burst_rx_retry_num = BURST_RX_RETRIES;
#endif

/* heads for the main used and free linked lists for the data path. */
static __rte_unused struct virtio_net_data_ll *ll_devlist_listening = NULL;
static __rte_unused struct virtio_net_data_ll *ll_devlist_in_use = NULL;

static void destroy_device (volatile struct virtio_net *dev)
{
	if (dev != NULL){
		dev->flags &= ~VIRTIO_DEV_RUNNING;
		log_info("(%lu) Device has been removed from socket %s\n",
				dev->device_fh, dev->ifname);
	}

#ifdef CHECK_BEFORE_ACCEPT 
	struct virtio_net_data_ll *ll_main_dev_cur;
	struct virtio_net_data_ll *ll_main_dev_last = NULL;
	struct vhost_dev *vdev;        
	vdev = (struct vhost_dev *)dev->priv;        

	rte_compiler_barrier();        

	/* Search for entry to be removed from main ll */
	ll_main_dev_cur = ll_devlist_in_use;
	ll_main_dev_last = NULL;
	while (ll_main_dev_cur != NULL) {
		if (ll_main_dev_cur->vdev == vdev)
			break;

		ll_main_dev_last = ll_main_dev_cur;
		ll_main_dev_cur = ll_main_dev_cur->next;
	}

	if (ll_main_dev_cur == NULL) {
		log_err("(%lu) Failed to find the dev to be destroy\n",
				dev->device_fh);
		return;
	}

	rm_data_ll_entry(&ll_devlist_in_use, ll_main_dev_cur, ll_main_dev_last);
	put_data_ll_free_entry(&ll_devlist_listening, ll_main_dev_cur);

	rte_mb();
	vdev->dev = NULL;
#endif
}

/*
 * A new virtio-net device is added to a vhost port.
 */
static int new_device(struct virtio_net *dev)
{
	log_info("(%lu) Searching device '%s'\n", dev->device_fh, dev->ifname);    

#ifdef CHECK_BEFORE_ACCEPT    
	struct vhost_dev *vdev;
	struct virtio_net_data_ll *ll_main_dev_cur;
	struct virtio_net_data_ll *ll_main_dev_last = NULL;
	ll_main_dev_cur = ll_devlist_listening;
	ll_main_dev_last = NULL;

	while (ll_main_dev_cur != NULL) {
		vdev = ll_main_dev_cur->vdev;
		log_info("Still searching - Current device '%s'\n",
				vdev->name);
		if (strncmp(dev->ifname, vdev->name, IF_NAME_SZ) == 0)
			break;

		ll_main_dev_last = ll_main_dev_cur;
		ll_main_dev_cur = ll_main_dev_cur->next;
	}

	if(ll_main_dev_cur == NULL){
		log_err("(%lu) Device '%s' can't be added - name not found\n",
				dev->device_fh, dev->ifname);
		return -1;
	}

	vdev->dev = dev;
	dev->priv = vdev;/*Only for easy access later*/

	rm_data_ll_entry(&ll_devlist_listening, ll_main_dev_cur, ll_main_dev_last);
	put_data_ll_free_entry(&ll_devlist_in_use, ll_main_dev_cur);
#endif

	/* Disable notifications. */
	rte_vhost_enable_guest_notification(dev, VIRTIO_RXQ, 0);
	rte_vhost_enable_guest_notification(dev, VIRTIO_TXQ, 0);
	dev->flags |= VIRTIO_DEV_RUNNING;

	log_info("(%lu) Device has been added at socket %s\n",
			dev->device_fh, dev->ifname);

	return 0;
}

/*
 * These callbacks allow virtio-net devices to be added to vhost ports when
 * configuration has been fully complete.
 */
static const struct virtio_net_device_ops virtio_net_device_ops =
{
	.new_device =  new_device,
	.destroy_device = destroy_device,
};

static void vhost_loop(void *arg __rte_unused)
{
	pthread_detach(pthread_self());
	rte_vhost_driver_session_start();
}

static int vhost_init_driver(struct driver *driver)
{
	static pthread_t vhost_user_t;

	rte_vhost_feature_disable(1ULL << VIRTIO_NET_F_MRG_RXBUF);
	rte_vhost_driver_callback_register(&virtio_net_device_ops);

	if (pthread_create(&vhost_user_t, NULL, (void*)vhost_loop, NULL)) {
		log_err("[vhost_drv]: Error creating thread\n");
		return -1;
	}

	return 0;
}

static struct snobj *vhost_init_port(struct port *p, struct snobj *conf)
{
	struct vhost_dev *vdev = get_port_priv(p);
	struct virtio_net_data_ll *ll_dev;
	int err;

	ll_dev = (struct virtio_net_data_ll *)rte_zmalloc("vhost device",
			sizeof(struct virtio_net_data_ll *), RTE_CACHE_LINE_SIZE);
	if(ll_dev == NULL)
		return snobj_err(ENOMEM, "[vhost_drv]: Couldn't init port %s\n"
				"Driver register failed",p->name);

	snprintf(vdev->name, strlen(VHOST_DIR_PREFIX)+strlen(p->name)+1, "%s%s",
			VHOST_DIR_PREFIX, p->name);

	/* Create a socket for this port */
	err = rte_vhost_driver_register(vdev->name);
	if (err) 
		return snobj_err(EMFILE, "[vhost_drv]: Couldn't init port %s\n"
				"Driver register failed",p->name);

	/* Add vdev to main ll */
	ll_dev->vdev = vdev;
	add_data_ll_entry(&ll_devlist_listening, ll_dev);
	log_info("[vhost_drv]: Listening on socket %s for port %s\n",
			vdev->name, p->name);

	return NULL;
}

static void vhost_deinit_port(struct port *p)
{
	struct virtio_net_data_ll *ll_main_dev_cur;
	struct virtio_net_data_ll *ll_main_dev_last = NULL;

	struct vhost_dev *vdev = get_port_priv(p);

	/* Check if the corresponding device is in use */
	if (vdev->dev != NULL) {
		log_err("[vhost_drv]: Couldn't deinit port %s."
				"Device still attached to guest\n",p->name);
		return;
	}

	ll_main_dev_cur = ll_devlist_listening;
	ll_main_dev_last = NULL;

	while (ll_main_dev_cur != NULL) {
		vdev = ll_main_dev_cur->vdev;
		if (ll_main_dev_cur->vdev == vdev)
			break;

		ll_main_dev_last = ll_main_dev_cur;
		ll_main_dev_cur = ll_main_dev_cur->next;
	}

	if(ll_main_dev_cur == NULL) {
		log_err("[vhost_drv]:Couldn't deinit port %s. "
				"Device not found\n", p->name);
		return;
	}

	rm_data_ll_entry(&ll_devlist_listening, ll_main_dev_cur, 
			ll_main_dev_last);

#if RTE_VERSION >= RTE_VERSION_NUM(2,1,0,0)        
	/* Unregister vhost driver. */
	int ret = rte_vhost_driver_unregister(vdev->name);
	if (ret != 0)
		log_err("vhost driver unregister failure.\n");
#endif        
	rte_free(ll_main_dev_cur);
}

static int vhost_recv_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct vhost_dev *vdev = get_port_priv(p);
	uint16_t count = 0;

	/*TODO: Use qid when multi-queue is available in DPDK 2.2 */
	if (vdev->dev != NULL && (vdev->dev->flags & VIRTIO_DEV_RUNNING))
		count = rte_vhost_dequeue_burst(vdev->dev, VIRTIO_TXQ, 
				ctx.pframe_pool, (struct rte_mbuf **)pkts, cnt);

	return count;
}

static int 
vhost_send_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct vhost_dev *vdev = get_port_priv(p);
	struct virtio_net *dev = vdev->dev;
	uint16_t count = 0;        

	/*TODO: Use qid when multi-queue is available in DPDK 2.2 */        
	if (cnt && dev && (dev->flags & VIRTIO_DEV_RUNNING)) {
#ifdef ENABLE_VHOST_RETRIES
		int available;
		
		available = rte_vring_available_entries(dev, VIRTIO_RXQ);
		if (enable_retry && unlikely(cnt > available)) {
			int retry;
			for (retry = 0; retry < burst_rx_retry_num; retry++) {
				rte_delay_us(burst_rx_delay_time);
				if (cnt <= rte_vring_available_entries(dev, 
							VIRTIO_RXQ))
					break;
			}
		}
#endif
		count = rte_vhost_enqueue_burst(dev, VIRTIO_RXQ,
				(struct rte_mbuf **)pkts, cnt);

		/* Free only the packets that were successfully sent */
		snb_free_bulk(pkts, count);
	}

	return count;
}

static const struct driver vhost_user = {
	.name 		= "vhost_user",
	.priv_size	= sizeof(struct vhost_dev),
	.init_driver	= vhost_init_driver,
	.init_port 	= vhost_init_port,
	.deinit_port	= vhost_deinit_port,
	.recv_pkts 	= vhost_recv_pkts,
	.send_pkts 	= vhost_send_pkts,
};

ADD_DRIVER(vhost_user)
