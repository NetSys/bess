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
        client_fd_(),
        old_client_fd_() {}

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
   * Closes the client connection but does not shut down the listener fd.
   */
  void CloseConnection();

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

  // NOTE: three threads (accept / recv / send) may race on this, so use
  // volatile.
  /* FD for client connection.*/
  volatile int client_fd_;
  /* If client FD is not connected, what was the fd the last time we were
   * connected to a client? */
  int old_client_fd_;
};

#endif  // BESS_DRIVERS_UNIXSOCKET_H_
