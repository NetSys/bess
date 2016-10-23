#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "../log.h"
#include "../message.h"
#include "../port.h"

#define NOT_CONNECTED -1

/* Only one client can be connected at the same time */

/* Polling sockets is quite exprensive, so we throttle the polling rate.
 * (by checking sockets once every RECV_TICKS schedules)
 * TODO: Revise this once the interrupt mode is implemented */
#define RECV_SKIP_TICKS 256
#define MAX_TX_FRAGS 8

class UnixSocketPort : public Port {
 public:
  virtual pb_error_t Init(const bess::UnixSocketPortArg &arg);
  virtual struct snobj *Init(struct snobj *arg);
  virtual void DeInit();

  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt);
  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt);

  void AcceptNewClient();

 private:
  void CloseConnection();

  uint32_t recv_skip_cnt_ = {0};
  int listen_fd_ = {0};
  struct sockaddr_un addr_ = {0};

  /* NOTE: three threads (accept / recv / send) may race on this,
   * so use volatile */
  volatile int client_fd_ = {0};
  int old_client_fd_ = {0};
};

void UnixSocketPort::AcceptNewClient() {
  int ret;

  for (;;) {
    ret = accept4(listen_fd_, NULL, NULL, SOCK_NONBLOCK);
    if (ret >= 0)
      break;

    if (errno != EINTR)
      log_perr("[UnixSocket]:accept4()");
  }

  recv_skip_cnt_ = 0;

  if (old_client_fd_ != NOT_CONNECTED) {
    /* Reuse the old file descriptor number by atomically
     * exchanging the new fd with the old one.
     * The zombie socket is closed silently (see dup2) */
    dup2(ret, client_fd_);
    close(ret);
  } else
    client_fd_ = ret;
}

/* This accept thread terminates once a new client is connected */
void *AcceptThreadMain(void *arg) {
  UnixSocketPort *p = reinterpret_cast<UnixSocketPort *>(arg);
  p->AcceptNewClient();
  return NULL;
}

/* The file descriptor for the connection will not be closed,
 * until we have a new client. This is to avoid race condition in TX process */
void UnixSocketPort::CloseConnection() {
  /* Keep client_fd, since it may be being used in unix_send_pkts() */
  old_client_fd_ = client_fd_;
  client_fd_ = NOT_CONNECTED;

  /* relaunch the accept thread */
  std::thread accept_thread(AcceptThreadMain, reinterpret_cast<void *>(this));
  accept_thread.detach();
}

struct snobj *UnixSocketPort::Init(struct snobj *conf) {
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  const char *path;
  size_t addrlen;

  int ret;

  client_fd_ = NOT_CONNECTED;
  old_client_fd_ = NOT_CONNECTED;

  if (num_txq > 1 || num_rxq > 1)
    return snobj_err(EINVAL, "Cannot have more than 1 queue per RX/TX");

  listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (listen_fd_ < 0)
    return snobj_err(errno, "socket(AF_UNIX) failed");

  addr_.sun_family = AF_UNIX;

  path = snobj_eval_str(conf, "path");
  if (path) {
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s", path);
  } else
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s/bess_unix_%s",
             P_tmpdir, name().c_str());

  /* This doesn't include the trailing null character */
  addrlen = sizeof(addr_.sun_family) + strlen(addr_.sun_path);

  /* non-abstract socket address? */
  if (addr_.sun_path[0] != '@') {
    /* remove existing socket file, if any */
    unlink(addr_.sun_path);
  } else
    addr_.sun_path[0] = '\0';

  ret = bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr_), addrlen);
  if (ret < 0)
    return snobj_err(errno, "bind(%s) failed", addr_.sun_path);

  ret = listen(listen_fd_, 1);
  if (ret < 0)
    return snobj_err(errno, "listen() failed");

  std::thread accept_thread(AcceptThreadMain, reinterpret_cast<void *>(this));
  accept_thread.detach();

  return NULL;
}

pb_error_t UnixSocketPort::Init(const bess::UnixSocketPortArg &arg) {
  const std::string path = arg.path();
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  size_t addrlen;

  int ret;

  client_fd_ = NOT_CONNECTED;
  old_client_fd_ = NOT_CONNECTED;

  if (num_txq > 1 || num_rxq > 1) {
    return pb_error(EINVAL, "Cannot have more than 1 queue per RX/TX");
  }

  listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (listen_fd_ < 0) {
    return pb_error(errno, "socket(AF_UNIX) failed");
  }

  addr_.sun_family = AF_UNIX;

  if (path.length() != 0) {
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s", path.c_str());
  } else
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s/bess_unix_%s",
             P_tmpdir, name().c_str());

  /* This doesn't include the trailing null character */
  addrlen = sizeof(addr_.sun_family) + strlen(addr_.sun_path);

  /* non-abstract socket address? */
  if (addr_.sun_path[0] != '@') {
    /* remove existing socket file, if any */
    unlink(addr_.sun_path);
  } else
    addr_.sun_path[0] = '\0';

  ret = bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr_), addrlen);
  if (ret < 0) {
    return pb_error(errno, "bind(%s) failed", addr_.sun_path);
  }

  ret = listen(listen_fd_, 1);
  if (ret < 0) {
    return pb_error(errno, "listen() failed");
  }

  std::thread accept_thread(AcceptThreadMain, reinterpret_cast<void *>(this));
  accept_thread.detach();

  return pb_errno(0);
}

void UnixSocketPort::DeInit() {
  close(listen_fd_);

  if (client_fd_ >= 0)
    close(client_fd_);
}

int UnixSocketPort::RecvPackets(queue_t qid, snb_array_t pkts, int cnt) {
  int client_fd = client_fd_;

  int received;

  if (client_fd == NOT_CONNECTED)
    return 0;

  if (recv_skip_cnt_) {
    recv_skip_cnt_--;
    return 0;
  }

  received = 0;
  while (received < cnt) {
    struct snbuf *pkt = static_cast<struct snbuf *>(snb_alloc());
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
    CloseConnection();
    break;
  }

  if (received == 0)
    recv_skip_cnt_ = RECV_SKIP_TICKS;

  return received;
}

int UnixSocketPort::SendPackets(queue_t qid, snb_array_t pkts, int cnt) {
  int client_fd = client_fd_;
  int sent = 0;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = pkts[i];
    struct rte_mbuf *mbuf = &pkt->mbuf;

    int nb_segs = mbuf->nb_segs;
    struct iovec iov[nb_segs];

    struct msghdr msg;
    msg.msg_iov = iov;
    msg.msg_iovlen = nb_segs;

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

ADD_DRIVER(UnixSocketPort, "unix_port",
           "packet exchange via a UNIX domain socket")
