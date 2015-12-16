#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <assert.h>

#include <netinet/tcp.h>

#include <sys/socket.h>
#include <sys/epoll.h>

#include <rte_config.h>
#include <rte_lcore.h>
#include <rte_malloc.h>

#include "master.h"
#include "worker.h"
#include "snobj.h"
#include "snctl.h"

/* Port this SoftNIC instance listens on. 
 * Panda came up with this default number */
#define DEFAULT_PORT 	0x02912		/* 10514 in decimal */

#define INIT_BUF_SIZE	4096
#define MAX_BUF_SIZE	1048576

static struct {
	int listen_fd;
	int epoll_fd;

	struct client *lock_holder;	/* NULL if unlocked */

	struct cdlist_head clients_all;
	struct cdlist_head clients_lock_waiting;
	struct cdlist_head clients_pause_holding;
} master;

static void reset_core_affinity()
{
	cpu_set_t set;
	int i;

	CPU_ZERO(&set);

	/* set all cores... */
	for (i = 0; i < rte_lcore_count(); i++)
		CPU_SET(i, &set);

	/* ...and then unset the ones where workers run */
	for (i = 0; i < MAX_WORKERS; i++)
		if (is_worker_active(i))
			CPU_CLR(workers[i]->core, &set);

	rte_thread_set_affinity(&set);
}

static void wakeup_client(struct client *c)
{
	/* XXX */
}

static int init_listen_fd(uint16_t port)
{
	struct sockaddr_in s_addr;

	int listen_fd;

	if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Channel socket() failed");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, 
				&(int){1}, sizeof(int)) < 0) 
	{
		perror("Channel setsockopt(SO_REUSEADDR) failed");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(listen_fd, SOL_SOCKET, SO_LINGER,
				&(struct linger){.l_onoff = 1, .l_linger = 0},
				sizeof(struct linger)) < 0)
	{
		perror("Channel setsockopt(SO_LINGER) failed");
		exit(EXIT_FAILURE);
	}

	memset(&s_addr, 0, sizeof(s_addr));

	s_addr.sin_family = AF_INET;
	s_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	s_addr.sin_port = htons(port);

	if (bind(listen_fd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
		if (errno == EADDRINUSE)
			fprintf(stderr, "Error: port %u is already in use\n", 
					port);
		else
			perror("bind()");
		exit(EXIT_FAILURE);
	}

	if (listen(listen_fd, 0) < 0) {
		perror("listen()");
		exit(EXIT_FAILURE);
	}

	printf("Master: listening on %s:%hu\n", inet_ntoa(s_addr.sin_addr), port);

	return listen_fd;
}

static struct client *init_client(int fd, struct sockaddr_in c_addr)
{
	struct client *c;

	/* because this is just optimization, we can ignore errors */
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int));

	c = rte_zmalloc("client", sizeof(struct client), 0);
	if (!c) 
		return NULL;

	c->fd = fd;
	c->addr = c_addr;
	c->buf_size = INIT_BUF_SIZE;

	c->buf = rte_zmalloc("client_buf", c->buf_size, 0);
	if (!c->buf) {
		rte_free(c);
		return NULL;
	}

	cdlist_add_tail(&master.clients_all, &c->master_all);
	cdlist_item_init(&c->master_lock_waiting);
	cdlist_item_init(&c->master_pause_holding);

	return c;
}

static void close_client(struct client *c)
{
	printf("Master: client %s:%hu disconnected\n", 
			inet_ntoa(c->addr.sin_addr), c->addr.sin_port);

	close(c->fd);

	if (master.lock_holder == c) {
		assert(!is_waiting_lock(c));

		/* wake up the first one in the waiting queue */
		if (!cdlist_is_empty(&master.clients_lock_waiting)) {
			struct client *first;

			first = container_of(master.clients_lock_waiting.next, 
					struct client, master_lock_waiting);
			cdlist_del(&first->master_lock_waiting);
			wakeup_client(first);
		}
	}

	if (is_holding_pause(c)) {
		cdlist_del(&c->master_pause_holding);
	}

	cdlist_del(&c->master_lock_waiting);
	cdlist_del(&c->master_all);

	rte_free(c->buf);
	rte_free(c);
}

static struct client *accept_client(int listen_fd)
{
	int conn_fd;

	struct sockaddr_in c_addr;
	socklen_t addrlen = sizeof(c_addr);

	struct client *c;

	struct epoll_event ev;

	int ret;

	conn_fd = accept(listen_fd, (struct sockaddr *)&c_addr, 
			&addrlen);
	if (conn_fd < 0) {
		perror("accept()");
		return NULL;
	}

	c = init_client(conn_fd, c_addr);
	if (!c) {
		close(conn_fd);
		return NULL;
	}

	ev.events = EPOLLIN;
	ev.data.ptr = c;

	ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_ADD, conn_fd, 
			&(struct epoll_event){.events=EPOLLIN, .data.ptr = c}); 
	if (ret < 0) {
		perror("epoll_ctl(EPOLL_CTL_ADD, conn_fd)");
		close_client(c);
	}

	return c;
}

static void request_done(struct client *c)
{
	struct epoll_event ev;

	struct snobj *q = NULL;
	struct snobj *r = NULL;

	char *buf;

	int ret;

	c->buf_off = 0;
	c->msg_len_off = 0;

	q = snobj_decode(c->buf, c->msg_len);
	if (!q) {
		fprintf(stderr, "Incorrect message\n");
		goto err;
	}

	r = handle_request(c, q);

	ev.events = EPOLLOUT;
	ev.data.ptr = c;

	ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_MOD, c->fd, &ev);
	if (ret < 0) {
		perror("epoll_ctl(EPOLL_CTL_MOD, listen_fd, OUT)");
		goto err;
	}

	c->msg_len = snobj_encode(r, &buf, 0);
	if (c->msg_len == 0) {
		fprintf(stderr, "Encoding error\n");
		goto err;
	}

	/* XXX: DRY */
	if (c->msg_len > c->buf_size) {
		char *new_buf;

		if (c->msg_len > MAX_BUF_SIZE)  {
			fprintf(stderr, "too large response was attempted\n");
			goto err;
		}

		new_buf = rte_realloc(c->buf, c->msg_len, 0);
		if (!new_buf)
			goto err;

		c->buf = new_buf;
		c->buf_size = c->msg_len;
	}

	memcpy(c->buf, buf, c->msg_len);

	if (buf)
		_FREE(buf);

	snobj_free(q);
	snobj_free(r);
	return;

err:
	snobj_free(q);
	snobj_free(r);
	close_client(c);
	return;
}

static void response_done(struct client *c)
{
	struct epoll_event ev;
	int ret;

	ev.events = EPOLLIN;
	ev.data.ptr = c;

	ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_MOD, c->fd, &ev);
	if (ret < 0) {
		perror("epoll_ctl(EPOLL_CTL_MOD, listen_fd, IN)");
		close_client(c);
	}

	c->buf_off = 0;
	c->msg_len = 0;
	c->msg_len_off = 0;
}

static void client_recv(struct client *c)
{
	int received;

	assert(c->msg_len_off <= sizeof(uint32_t));
	assert(c->msg_len == 0 || (c->buf_off < c->msg_len));

	if (c->msg_len_off < sizeof(c->msg_len)) {
		received = recv(c->fd, ((char *)&c->msg_len) + c->msg_len_off, 
				sizeof(c->msg_len) - c->msg_len_off, 
				MSG_NOSIGNAL);
		if (received <= 0) {
			close_client(c);
			return;
		}

		c->msg_len_off += received;
		assert(c->msg_len_off <= sizeof(c->msg_len));
		return;
	}

	if (c->msg_len > c->buf_size) {
		char *new_buf;

		if (c->msg_len > MAX_BUF_SIZE)  {
			fprintf(stderr, "too large request was attempted\n");
			close_client(c);
			return;
		}

		new_buf = rte_realloc(c->buf, c->msg_len, 0);
		if (!new_buf) {
			fprintf(stderr, "Out of memory\n");
			close_client(c);
			return;
		}

		c->buf = new_buf;
		c->buf_size = c->msg_len;
	}

	received = recv(c->fd, c->buf + c->buf_off, c->msg_len - c->buf_off, 
			MSG_NOSIGNAL);
	if (received < 0) {
		close_client(c);
		return;
	}

	c->buf_off += received;
	assert(c->buf_off <= c->msg_len);

	/* request done? */
	if (c->buf_off == c->msg_len)
		request_done(c);
}

static void client_send(struct client *c)
{
	int sent;

	assert(c->msg_len_off <= sizeof(c->msg_len));
	assert(c->buf_off < c->msg_len);

	if (c->msg_len_off < sizeof(c->msg_len)) {
		sent = send(c->fd, ((char *)&c->msg_len) + c->msg_len_off, 
				sizeof(c->msg_len) - c->msg_len_off, MSG_NOSIGNAL);
		if (sent < 0) {
			close_client(c);
			return;
		}

		c->msg_len_off += sent;
		assert(c->msg_len_off <= sizeof(c->msg_len));
		return;
	}

	sent = send(c->fd, c->buf + c->buf_off, c->msg_len - c->buf_off, 
			MSG_NOSIGNAL);
	if (sent < 0) {
		close_client(c);
		return;
	}

	c->buf_off += sent;

	assert(c->buf_off <= c->msg_len);

	/* reponse done? */
	if (c->buf_off == c->msg_len) 
		response_done(c);
}

void init_server(uint16_t port)
{
	struct epoll_event ev;

	int ret;

	master.epoll_fd = epoll_create(16);
	if (master.epoll_fd < 0) {
		perror("epoll_create()");
		exit(EXIT_FAILURE);
	}

	master.listen_fd = init_listen_fd(port ? : DEFAULT_PORT);

	ev.events = EPOLLIN;
	ev.data.fd = master.listen_fd;

	ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_ADD, master.listen_fd, &ev);
	if (ret < 0) {
		perror("epoll_ctl(EPOLL_CTL_ADD, listen_fd)");
		exit(EXIT_FAILURE);
	}
}

void setup_master(uint16_t port) 
{
	reset_core_affinity();
	
	set_non_worker();
	
	cdlist_head_init(&master.clients_all);
	cdlist_head_init(&master.clients_lock_waiting);
	cdlist_head_init(&master.clients_pause_holding);

	init_server(port);
}

void run_master() 
{
	struct client *c;

	struct epoll_event ev;

	int ret;

again:
	ret = epoll_wait(master.epoll_fd, &ev, 1, -1);
	if (ret <= 0) {
		if (errno != EINTR)
			perror("epoll_wait()");
		goto again;
	}

	if (ev.data.fd == master.listen_fd) {
		if ((c = accept_client(master.listen_fd)) == NULL)
			goto again;

		printf("Master: a new client from %s:%hu\n", 
				inet_ntoa(c->addr.sin_addr), 
				c->addr.sin_port);
	} else {
		c = ev.data.ptr;

		if (ev.events & (EPOLLERR | EPOLLHUP)) {
			close_client(c);
			goto again;
		}

		if (ev.events & EPOLLIN) {
			client_recv(c);
		} else if (ev.events & EPOLLOUT) {
			client_send(c);
		} else {
			fprintf(stderr, "Unknown epoll event %u\n", ev.events);
			close_client(c);
		}
	}

	/* loop forever */
	goto again;
}
