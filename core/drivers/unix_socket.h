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

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#include "../message.h"
#include "../port.h"

/*!
 * This driver binds a port to a UNIX socket to communicate with a local
 * process. Only one client can be connected at the same time.
 */
class UnixSocketPort final : public Port {
 public:
  UnixSocketPort()
      : Port(),
        min_rx_interval_ns_(),
        last_idle_ns_(),
        accept_thread_stop_req_(false),
        listen_fd_(kNotConnectedFd),
        addr_(),
        client_fd_(kNotConnectedFd) {}

  /*!
   * Initialize the port, ie, open the socket.
   *
   * PARAMETERS:
   * * string path : file name to bind the socket to.
   */
  CommandResponse Init(const bess::pb::UnixSocketPortArg &arg);

  /*!
   * Close the socket / shut down the port.
   */
  void DeInit() override;

  // Multi-queue is not supported. qid must be 0.
  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  // Value for a disconnected socket.
  static const int kNotConnectedFd = -1;

  static const uint64_t kDefaultMinRxInterval = 50000;  // 50 microsec

  /*!
   * Calling recv() system call is expensive so we may not want to invoke it
   * too frequently. min_rx_interval_ns_ is a configurable parameter to throttle
   * the rate of busy-wait polling.
   */
  uint64_t min_rx_interval_ns_;
  uint64_t last_idle_ns_;

  /*!
   * Function for the thread accepting and monitoring clients (accept thread).
   */
  void AcceptThread();

  /*!
   * Accept thread handle.
   */
  std::thread accept_thread_;

  /*!
   * Sent stop request to accept thread.
   */
  std::atomic<bool> accept_thread_stop_req_;

  /*!
   * The listener fd -- listen for new connections here.
   */
  int listen_fd_;

  /*!
   * My socket address on the listener fd.
   */
  struct sockaddr_un addr_;

  // NOTE: three threads (accept / recv / send) may race on this, so use
  // volatile.
  /* FD for client connection.*/
  volatile int client_fd_;
};

#endif  // BESS_DRIVERS_UNIXSOCKET_H_
