// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <sys/epoll.h>

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

  while (true) {
    for (;;) {
      ret = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
      if (ret >= 0) {
        // New client while we already have an active one; drop connection
        if (client_fd_ != kNotConnectedFd) {
          close(ret);
        }
        break;
      }

      if (errno != EINTR) {
        PLOG(ERROR) << "[UnixSocket]:accept4()";
      }
    }

    recv_skip_cnt_ = 0;
    client_fd_ = ret;

    // Detect when connection is dropped by client
    struct epoll_event event;
    event.events = EPOLLRDHUP;
    ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ret, &event);
    if (ret < 0) {
      PLOG(ERROR) << "[UnixSocket]:epoll_ctl()";
      break;
    }

    while (true) {
      ret = epoll_wait(epoll_fd_, &event, 1, -1);
      if (ret < 0) {
        if (errno == EINTR) {
          continue;
        } else {
          PLOG(ERROR) << "[UnixSocket]:epoll_wait()";
          break;
        }
      }

      if (event.events & EPOLLRDHUP) {
        break;
      }
    }

    // Close dropped connection (Send/Recv are resilient)
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd_, &event);
    close(client_fd_);
    client_fd_ = -1;
  }
}

// This accept thread terminates once a new client is connected.
void *AcceptThreadMain(void *arg) {
  // TODO: should make sure it's pinned to a non-worker CPU
  UnixSocketPort *p = reinterpret_cast<UnixSocketPort *>(arg);
  p->AcceptNewClient();
  return nullptr;
}

CommandResponse UnixSocketPort::Init(const bess::pb::UnixSocketPortArg &arg) {
  const std::string path = arg.path();
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  size_t addrlen;

  int ret;

  client_fd_ = kNotConnectedFd;

  if (num_txq > 1 || num_rxq > 1) {
    return CommandFailure(EINVAL, "Cannot have more than 1 queue per RX/TX");
  }

  // TODO: close in error paths to avoid leaking FDs
  epoll_fd_ = epoll_create(1);
  if (epoll_fd_ < 0) {
    return CommandFailure(errno, "epoll_create(1) failed");
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
  close(epoll_fd_);

  if (client_fd_ >= 0) {
    close(client_fd_);
  }
}

int UnixSocketPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int client_fd = client_fd_;

  DCHECK_EQ(qid, 0);

  if (client_fd == kNotConnectedFd) {
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
    ret = recv(client_fd, pkt->data(), SNBUF_DATA, 0);

    if (ret > 0) {
      pkt->append(ret);
      pkts[received++] = pkt;
      continue;
    }

    bess::Packet::Free(pkt);

    if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EBADF) {
        break;
      }

      if (errno == EINTR) {
        continue;
      }
    }

    // Connection closed.
    break;
  }

  if (received == 0) {
    recv_skip_cnt_ = RECV_SKIP_TICKS;
  }

  return received;
}

int UnixSocketPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int sent = 0;
  int client_fd = client_fd_;

  DCHECK_EQ(qid, 0);

  if (client_fd == kNotConnectedFd) {
    return 0;
  }

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

    ret = sendmsg(client_fd, &msg, 0);
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
