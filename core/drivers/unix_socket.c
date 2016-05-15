#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "../port.h"

#define NOT_CONNECTED	-1

/* Only one client can be connected at the same time */

/* Polling sockets is quite exprensive, so we throttle the polling rate.
 * (by checking sockets once every RECV_TICKS schedules)
 * TODO: Revise this once the interrupt mode is implemented */ 
#define RECV_SKIP_TICKS	256

#define MAX_TX_FRAGS	8

struct unix_priv {
	uint32_t recv_skip_cnt;
	int listen_fd;
	struct sockaddr_un addr;

	/* NOTE: three threads (accept / recv / send) may race on this, 
	 * so use volatile */
	volatile int client_fd;
	int old_client_fd;

	pthread_t accept_thread;
};

static void accept_new_client(struct unix_priv *priv)
{
	int ret;

	for (;;) {
		ret = accept4(priv->listen_fd, NULL, NULL, SOCK_NONBLOCK);
		if (ret >= 0)
			break;

		if (errno != EINTR)
			log_perr("[UnixSocket]:accept4()");
	}

	priv->recv_skip_cnt = 0;

	if (priv->old_client_fd != NOT_CONNECTED) {
		/* Reuse the old file descriptor number by atomically 
		 * exchanging the new fd with the old one.
		 * The zombie socket is closed silently (see dup2) */
		dup2(ret, priv->client_fd);
		close(ret);
	} else 
		priv->client_fd = ret;
}

/* This accept thread terminates once a new client is connected */
static void *accept_thread_main(void *arg)
{
	struct unix_priv *priv = arg;

	pthread_detach(pthread_self());
	accept_new_client(priv);
	priv->accept_thread = 0;

	return NULL;
}

/* The file descriptor for the connection will not be closed, 
 * until we have a new client. This is to avoid race condition in TX process */
static void close_connection(struct unix_priv *priv)
{
	int ret;

	/* Keep client_fd, since it may be being used in unix_send_pkts() */
	priv->old_client_fd = priv->client_fd;
	priv->client_fd = NOT_CONNECTED;

	/* relaunch the accept thread */
	ret = pthread_create(&priv->accept_thread, NULL, 
			accept_thread_main, priv);
	if (ret)
		log_err("[UnixSocket]:pthread_create() returned errno %d", ret);
}

static struct snobj *unix_init_port(struct port *p, struct snobj *conf)
{
	struct unix_priv *priv = get_port_priv(p);

	int num_txq = p->num_queues[PACKET_DIR_OUT];
	int num_rxq = p->num_queues[PACKET_DIR_INC];

	const char *path;
	size_t addrlen;

	int ret;

	priv->client_fd = NOT_CONNECTED;
	priv->old_client_fd = NOT_CONNECTED;

	if (num_txq > 1 || num_rxq > 1)
		return snobj_err(EINVAL, 
				"Cannot have more than 1 queue per RX/TX");

	priv->listen_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (priv->listen_fd < 0)
		return snobj_err(errno, "socket(AF_UNIX) failed");

	priv->addr.sun_family = AF_UNIX;

	path = snobj_eval_str(conf, "path");
	if (path) {
		snprintf(priv->addr.sun_path, sizeof(priv->addr.sun_path), 
				"%s", path);
	} else
		snprintf(priv->addr.sun_path, sizeof(priv->addr.sun_path),
				"%s/bess_unix_%s", P_tmpdir, p->name);

	/* This doesn't include the trailing null character */
	addrlen = sizeof(priv->addr.sun_family) + strlen(priv->addr.sun_path);

	/* non-abstract socket address? */
	if (priv->addr.sun_path[0] != '@') {
		/* remove existing socket file, if any */
		unlink(priv->addr.sun_path);
	} else
		priv->addr.sun_path[0] = '\0';

	ret = bind(priv->listen_fd, &priv->addr, addrlen);
	if (ret < 0)
		return snobj_err(errno, "bind(%s) failed", priv->addr.sun_path);

	ret = listen(priv->listen_fd, 1);
	if (ret < 0)
		return snobj_err(errno, "listen() failed");

	ret = pthread_create(&priv->accept_thread, NULL, 
			accept_thread_main, priv);
	if (ret)
		return snobj_err(ret, "pthread_create() failed");

	return NULL;
}

static void unix_deinit_port(struct port *p)
{
	struct unix_priv *priv = get_port_priv(p);

	if (priv->accept_thread)
		pthread_cancel(priv->accept_thread);

	close(priv->listen_fd);

	if (priv->client_fd >= 0)
		close(priv->client_fd);
}

static int 
unix_recv_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct unix_priv *priv = get_port_priv(p);
	
	int client_fd = priv->client_fd;

	int received;

	if (client_fd == NOT_CONNECTED)
		return 0;

	if (priv->recv_skip_cnt) {
		priv->recv_skip_cnt--;
		return 0;
	}

	received = 0;
	while (received < cnt) {
		struct snbuf *pkt = snb_alloc();
		int ret;

		if (!pkt) 
			break;

		/* datagrams larger than 2KB will be truncated */
		ret = recv(client_fd, pkt->_data, SNBUF_DATA, 0);

		if (ret > 0) {
			snb_append(pkt, ret);
			pkts[received++] = pkt;
			continue;
		}

		snb_free(pkt);

		if (ret < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;

			if (errno == EINTR)
				continue;
		}

		/* connection closed */
		close_connection(priv);
		break;
	}

	if (received == 0)
		priv->recv_skip_cnt = RECV_SKIP_TICKS;

	return received;
}

static int 
unix_send_pkts(struct port *p, queue_t qid, snb_array_t pkts, int cnt)
{
	struct unix_priv *priv = get_port_priv(p);

	int client_fd = priv->client_fd;
	int sent = 0;

	for (int i = 0; i < cnt; i++) {
		struct snbuf *pkt = pkts[i];
		struct rte_mbuf *mbuf = &pkt->mbuf;

		int nb_segs = mbuf->nb_segs;
		struct iovec iov[nb_segs];

		struct msghdr msg = {
			.msg_iov = iov,
			.msg_iovlen  = nb_segs,
		};

		ssize_t ret;

		for (int j = 0; j < nb_segs; j++) {
			iov[j].iov_base = rte_pktmbuf_mtod(mbuf, void *);
			iov[j].iov_len = rte_pktmbuf_data_len(mbuf);
			mbuf = mbuf->next;
		}

		ret = sendmsg(client_fd, &msg, 0);
		if (ret < 0)
			break;

		sent++;
	}

	if (sent)
		snb_free_bulk(pkts, sent);

	return sent;
}

static const struct driver unix_socket = {
	.name 		= "UnixSocket",
	.priv_size 	= sizeof(struct unix_priv),
	.init_port 	= unix_init_port,
	.deinit_port	= unix_deinit_port, 
	.recv_pkts 	= unix_recv_pkts,
	.send_pkts 	= unix_send_pkts,
};

ADD_DRIVER(unix_socket)
