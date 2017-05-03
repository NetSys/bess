#include "unix_socket.h"

// TODO(barath): Clarify these comments.
// Only one client can be connected at the same time.  Polling sockets is quite
// exprensive, so we throttle the polling rate.  (by checking sockets once every
// RECV_TICKS schedules)

// TODO: Revise this once the interrupt mode is  implemented.

#define RECV_SKIP_TICKS 256
#define MAX_TX_FRAGS 8

void UnixSocketPort::AcceptNewClient() {
  int ret;

  for (;;) {
    ret = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
    if (ret >= 0) {
      break;
    }

    if (errno != EINTR) {
      PLOG(ERROR) << "[UnixSocket]:accept4()";
    }
  }

  recv_skip_cnt_ = 0;

  if (old_client_fd_ != kNotConnectedFd) {
    // Reuse the old file descriptor number by atomically exchanging the new fd
    // with the
    // old one.  The zombie socket is closed silently (see dup2).
    dup2(ret, client_fd_);
    close(ret);
  } else {
    client_fd_ = ret;
  }
}

// This accept thread terminates once a new client is connected.
void *AcceptThreadMain(void *arg) {
  UnixSocketPort *p = reinterpret_cast<UnixSocketPort *>(arg);
  p->AcceptNewClient();
  return nullptr;
}

// The file descriptor for the connection will not be closed, until we have a
// new client.
// This is to avoid race condition in TX process.
void UnixSocketPort::CloseConnection() {
  // Keep client_fd, since it may be being used in unix_send_pkts().
  old_client_fd_ = client_fd_;
  client_fd_ = kNotConnectedFd;

  // Relaunch the accept thread.
  std::thread accept_thread(AcceptThreadMain, reinterpret_cast<void *>(this));
  accept_thread.detach();
}

CommandResponse UnixSocketPort::Init(const bess::pb::UnixSocketPortArg &arg) {
  const std::string path = arg.path();
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  size_t addrlen;

  int ret;

  client_fd_ = kNotConnectedFd;
  old_client_fd_ = kNotConnectedFd;

  if (num_txq > 1 || num_rxq > 1) {
    return CommandFailure(EINVAL, "Cannot have more than 1 queue per RX/TX");
  }

  listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (listen_fd_ < 0) {
    return CommandFailure(errno, "socket(AF_UNIX) failed");
  }

  addr_.sun_family = AF_UNIX;

  if (path.length() != 0) {
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s", path.c_str());
  } else {
    snprintf(addr_.sun_path, sizeof(addr_.sun_path), "%s/bess_unix_%s",
             P_tmpdir, name().c_str());
  }

  // This doesn't include the trailing null character.
  addrlen = sizeof(addr_.sun_family) + strlen(addr_.sun_path);

  // Non-abstract socket address?
  if (addr_.sun_path[0] != '@') {
    // Remove existing socket file, if any.
    unlink(addr_.sun_path);
  } else {
    addr_.sun_path[0] = '\0';
  }

  ret = bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr_), addrlen);
  if (ret < 0) {
    return CommandFailure(errno, "bind(%s) failed", addr_.sun_path);
  }

  ret = listen(listen_fd_, 1);
  if (ret < 0) {
    return CommandFailure(errno, "listen() failed");
  }

  std::thread accept_thread(AcceptThreadMain, reinterpret_cast<void *>(this));
  accept_thread.detach();

  return CommandSuccess();
}

void UnixSocketPort::DeInit() {
  close(listen_fd_);

  if (client_fd_ >= 0) {
    close(client_fd_);
  }
}

int UnixSocketPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  DCHECK_EQ(qid, 0);

  if (client_fd_ == kNotConnectedFd) {
    return 0;
  }

  if (recv_skip_cnt_) {
    recv_skip_cnt_--;
    return 0;
  }

  int received = 0;
  while (received < cnt) {
    bess::Packet *pkt = static_cast<bess::Packet *>(bess::Packet::Alloc());
    int ret;

    if (!pkt) {
      break;
    }

    // Datagrams larger than 2KB will be truncated.
    ret = recv(client_fd_, pkt->data(), SNBUF_DATA, 0);

    if (ret > 0) {
      pkt->append(ret);
      pkts[received++] = pkt;
      continue;
    }

    bess::Packet::Free(pkt);

    if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }

      if (errno == EINTR) {
        continue;
      }
    }

    // Connection closed.
    CloseConnection();
    break;
  }

  if (received == 0) {
    recv_skip_cnt_ = RECV_SKIP_TICKS;
  }

  return received;
}

int UnixSocketPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int sent = 0;

  DCHECK_EQ(qid, 0);

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = pkts[i];

    int nb_segs = pkt->nb_segs();
    struct iovec iov[nb_segs];

    struct msghdr msg = msghdr();
    msg.msg_iov = iov;
    msg.msg_iovlen = nb_segs;

    ssize_t ret;

    for (int j = 0; j < nb_segs; j++) {
      iov[j].iov_base = pkt->head_data();
      iov[j].iov_len = pkt->head_len();
      pkt = pkt->next();
    }

    ret = sendmsg(client_fd_, &msg, 0);
    if (ret < 0) {
      break;
    }

    sent++;
  }

  if (sent) {
    bess::Packet::Free(pkts, sent);
  }

  return sent;
}

ADD_DRIVER(UnixSocketPort, "unix_port",
           "packet exchange via a UNIX domain socket")
