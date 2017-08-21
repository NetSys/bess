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

#ifndef BESS_DRIVERS_UNIXSOCKET_H_
#define BESS_DRIVERS_UNIXSOCKET_H_

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include <glog/logging.h>

#include "../message.h"
#include "../port.h"

/*!
 * This driver binds a port to a UNIX socket to communicate with a local
 * process.
 */
class UnixSocketPort final : public Port {
 public:
  UnixSocketPort()
      : Port(),
        recv_skip_cnt_(),
        listen_fd_(),
        addr_(),
        client_fd_() {}

  /*!
   * Initialize the port, ie, open the socket.
   *
   * PARAMETERS:
   * * string path : file name to bind the socket ti.
   */
  CommandResponse Init(const bess::pb::UnixSocketPortArg &arg);

  /*!
   * Close the socket / shut down the port.
   */
  void DeInit() override;

  /*!
   * Receives packets from the device.
   *
   * PARAMETERS:
   * * queue_t quid : socket has no notion of queues so this is ignored.
   * * bess::Packet ** pkts   : buffer to store received packets in to.
   * * int cnt  : max number of packets to pull.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets received (<=cnt)
   */
  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

  /*!
   * Sends packets out on the device.
   *
   * PARAMETERS:
   * * queue_t quid : PCAP has no notion of queues so this is ignored.
   * * bess::Packet ** pkts   : packets to transmit.
   * * int cnt  : number of packets in pkts to transmit.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets sent (<=cnt).
   */
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

  /*!
   * Waits for a client to connect to the socket.
   */
  void AcceptNewClient();

 private:
  // Value for a disconnected socket.
  static const int kNotConnectedFd = -1;

  /*!
  * Calling recv() system call is expensive so we only do it every
  * RECV_SKIP_TICKS times -- this counter keeps track of how many ticks its been
  * since we last called recv().
  * */
  uint32_t recv_skip_cnt_;

  /*!
   * The listener fd -- listen for new connections here.
   */
  int listen_fd_;

  /*!
   * My socket address on the listener fd.
   */
  struct sockaddr_un addr_;

  /*!
   * The epoll fd -- detect when client closes.
   */
  int epoll_fd_;

  // NOTE: three threads (accept / recv / send) may race on this, so use
  // volatile.
  /* FD for client connection.*/
  volatile int client_fd_;
};

#endif  // BESS_DRIVERS_UNIXSOCKET_H_
