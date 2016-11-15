#include "master.h"

#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netinet/tcp.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glog/logging.h>
#include <rte_config.h>
#include <rte_lcore.h>

#include "opts.h"
#include "snctl.h"
#include "snobj.h"
#include "worker.h"

#define INIT_BUF_SIZE 4096
#define MAX_BUF_SIZE (8 * 1048576)

static struct {
  int listen_fd;
  int epoll_fd;

  struct client *lock_holder; /* nullptr if unlocked */

  struct cdlist_head clients_all;
  struct cdlist_head clients_lock_waiting;
  struct cdlist_head clients_pause_holding;
} master;

static void reset_core_affinity() {
  cpu_set_t set;
  unsigned int i;

  CPU_ZERO(&set);

  /* set all cores... */
  for (i = 0; i < rte_lcore_count(); i++)
    CPU_SET(i, &set);

  /* ...and then unset the ones where workers run */
  for (i = 0; i < MAX_WORKERS; i++)
    if (is_worker_active(i))
      CPU_CLR(workers[i]->core(), &set);

  rte_thread_set_affinity(&set);
}

static void wakeup_client(struct client *) {
  // NOTE: not implemented yet
}

static int init_listen_fd(uint16_t port) {
  struct sockaddr_in s_addr;

  int listen_fd;

  const int one = 1;

  struct linger l = {.l_onoff = 1, .l_linger = 0};

  if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    PLOG(FATAL) << "socket()";
  }

  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
    PLOG(ERROR) << "setsockopt(SO_REUSEADDR)";
    // error, but we can keep going
  }

  if (setsockopt(listen_fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) < 0) {
    PLOG(ERROR) << "setsockopt(SO_LINGER)";
    // error, but we can keep going
  }

  memset(&s_addr, 0, sizeof(s_addr));

  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  s_addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
    if (errno == EADDRINUSE) {
      LOG(ERROR) << "Error: TCP port " << port << " is already in use. "
                 << "You can specify another port number with -p option.";
      exit(EXIT_FAILURE);
    } else {
      PLOG(FATAL) << "bind()";
    }
  }

  if (listen(listen_fd, 10) < 0) {
    PLOG(FATAL) << "listen()";
  }

  LOG(INFO) << "Master: listening on " << inet_ntoa(s_addr.sin_addr) << ":"
            << port;

  return listen_fd;
}

static struct client *init_client(int fd, struct sockaddr_in c_addr) {
  struct client *c;

  const int one = 1;

  /* because this is just optimization, we can ignore errors */
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  c = (struct client *)mem_alloc(sizeof(struct client));
  if (!c)
    return nullptr;

  c->fd = fd;
  c->addr = c_addr;
  c->buf_size = INIT_BUF_SIZE;

  c->buf = (char *)mem_alloc(c->buf_size);
  if (!c->buf) {
    mem_free(c);
    return nullptr;
  }

  cdlist_add_tail(&master.clients_all, &c->master_all);
  cdlist_item_init(&c->master_lock_waiting);
  cdlist_item_init(&c->master_pause_holding);

  return c;
}

static void close_client(struct client *c) {
  LOG(INFO) << "Master: client " << inet_ntoa(c->addr.sin_addr) << ":"
            << c->addr.sin_port << " disconnected";

  close(c->fd);

  if (master.lock_holder == c) {
    assert(!is_waiting_lock(c));

    /* wake up the first one in the waiting queue */
    if (!cdlist_is_empty(&master.clients_lock_waiting)) {
      struct client *first;

      first = container_of(master.clients_lock_waiting.next, struct client,
                           master_lock_waiting);
      cdlist_del(&first->master_lock_waiting);
      wakeup_client(first);
    }
  }

  if (is_holding_pause(c))
    cdlist_del(&c->master_pause_holding);

  cdlist_del(&c->master_lock_waiting);
  cdlist_del(&c->master_all);

  mem_free(c->buf);
  mem_free(c);
}

static struct client *accept_client(int listen_fd) {
  int conn_fd;

  struct sockaddr_in c_addr;
  socklen_t addrlen = sizeof(c_addr);

  struct client *c;

  struct epoll_event ev;

  int ret;

  conn_fd = accept(listen_fd, (struct sockaddr *)&c_addr, &addrlen);
  if (conn_fd < 0) {
    PLOG(WARNING) << "accept()";
    return nullptr;
  }

  c = init_client(conn_fd, c_addr);
  if (!c) {
    close(conn_fd);
    return nullptr;
  }

  ev.events = EPOLLIN;
  ev.data.ptr = c;

  ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev);
  if (ret < 0) {
    PLOG(WARNING) << "epoll_ctl(EPOLL_CTL_ADD, conn_fd)";
    close_client(c);
  }

  return c;
}

static void request_done(struct client *c) {
  struct epoll_event ev;

  struct snobj *q = nullptr;
  struct snobj *r = nullptr;

  char *buf;

  int ret;

  c->buf_off = 0;
  c->msg_len_off = 0;

  q = snobj_decode(c->buf, c->msg_len);
  if (!q) {
    LOG(ERROR) << "Incorrect message";
    goto err;
  }

  r = handle_request(q);

  ev.events = EPOLLOUT;
  ev.data.ptr = c;

  ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_MOD, c->fd, &ev);
  if (ret < 0) {
    PLOG(WARNING) << "epoll_ctl(EPOLL_CTL_MOD, listen_fd, OUT)";
    goto err;
  }

  c->msg_len = snobj_encode(r, &buf, 0);
  if (c->msg_len == 0) {
    LOG(ERROR) << "Encoding error";
    goto err;
  }

  /* XXX: DRY */
  if (c->msg_len > c->buf_size) {
    char *new_buf;

    if (c->msg_len > MAX_BUF_SIZE) {
      LOG(ERROR) << "too large response was attempted";
      goto err;
    }

    new_buf = (char *)mem_realloc(c->buf, c->msg_len);
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

static void response_done(struct client *c) {
  struct epoll_event ev;
  int ret;

  ev.events = EPOLLIN;
  ev.data.ptr = c;

  ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_MOD, c->fd, &ev);
  if (ret < 0) {
    PLOG(WARNING) << "epoll_ctl(EPOLL_CTL_MOD, listen_fd, IN)";
    close_client(c);
  }

  c->buf_off = 0;
  c->msg_len = 0;
  c->msg_len_off = 0;
}

static void client_recv(struct client *c) {
  int received;

  assert(c->msg_len_off <= sizeof(uint32_t));
  assert(c->msg_len == 0 || (c->buf_off < c->msg_len));

  if (c->msg_len_off < sizeof(c->msg_len)) {
    received = recv(c->fd, ((char *)&c->msg_len) + c->msg_len_off,
                    sizeof(c->msg_len) - c->msg_len_off, MSG_NOSIGNAL);
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

    if (c->msg_len > MAX_BUF_SIZE) {
      LOG(ERROR) << "too large request was attempted";
      close_client(c);
      return;
    }

    new_buf = (char *)mem_realloc(c->buf, c->msg_len);
    if (!new_buf) {
      LOG(ERROR) << "Out of memory";
      close_client(c);
      return;
    }

    c->buf = new_buf;
    c->buf_size = c->msg_len;
  }

  received =
      recv(c->fd, c->buf + c->buf_off, c->msg_len - c->buf_off, MSG_NOSIGNAL);
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

static void client_send(struct client *c) {
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

  sent =
      send(c->fd, c->buf + c->buf_off, c->msg_len - c->buf_off, MSG_NOSIGNAL);
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

static void init_server() {
  master.epoll_fd = epoll_create(16);
  if (master.epoll_fd < 0) {
    PLOG(FATAL) << "epoll_create()";
  }

  if (FLAGS_p) {
    struct epoll_event ev = {
        .events = EPOLLIN, .data = {.fd = master.listen_fd},
    };

    master.listen_fd = init_listen_fd(FLAGS_p);

    ev.events = EPOLLIN;
    ev.data.fd = master.listen_fd;

    int ret = epoll_ctl(master.epoll_fd, EPOLL_CTL_ADD, master.listen_fd, &ev);
    if (ret < 0) {
      PLOG(FATAL) << "epoll_ctl(EPOLL_CTL_ADD, listen_fd)";
    }
  } else {
    LOG(WARNING) << "Running without the control channel.";
    master.listen_fd = -1; /* controller-less mode */
  }
}

void SetupMaster() {
  reset_core_affinity();

  ctx.SetNonWorker();

  cdlist_head_init(&master.clients_all);
  cdlist_head_init(&master.clients_lock_waiting);
  cdlist_head_init(&master.clients_pause_holding);

  init_server();
}

void RunMaster() {
  struct client *c;
  struct epoll_event ev;
  int ret;

again:
  ret = epoll_wait(master.epoll_fd, &ev, 1, -1);
  if (ret <= 0) {
    if (errno != EINTR) {
      PLOG(WARNING) << "epoll_wait()";
    }
    goto again;
  }

  if (ev.data.fd == master.listen_fd) {
    if ((c = accept_client(master.listen_fd)) == nullptr)
      goto again;

    LOG(INFO) << "Master: a new client from " << inet_ntoa(c->addr.sin_addr)
              << ":" << c->addr.sin_port;
  } else {
    c = (struct client *)ev.data.ptr;

    if (ev.events & (EPOLLERR | EPOLLHUP)) {
      close_client(c);
      goto again;
    }

    if (ev.events & EPOLLIN) {
      client_recv(c);
    } else if (ev.events & EPOLLOUT) {
      client_send(c);
    } else {
      LOG(ERROR) << "Unknown epoll event " << ev.events;
      close_client(c);
    }
  }

  /* loop forever */
  goto again;
}
