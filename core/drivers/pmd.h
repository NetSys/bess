#ifndef BESS_DRIVERS_PMD_H_
#define BESS_DRIVERS_PMD_H_

#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>

#include "../port.h"

typedef uint8_t dpdk_port_t;

#define DPDK_PORT_UNKNOWN RTE_MAX_ETHPORTS
/*!
 * This driver binds a port to a device using DPDK.
 * This is the recommended driver for performance.
 */
class PMDPort : public Port {
 public:
  PMDPort() : Port(), dpdk_port_id_(DPDK_PORT_UNKNOWN), hot_plugged_(false) {}

  /*!
  * Binds to device; call this after Init()
  */
  virtual void InitDriver();

  /*!
   * Initialize the port. Doesn't actually bind to the device, just grabs all
   * the parameters. InitDriver() does the binding.
   *
   * PARAMETERS:
   * * bool loopback : Is this a loopback device?
   * * uint32 port_id : The DPDK port ID for the device to bind to.
   * * string pci : The PCI address of the port to bind to.
   * * string vdev : If a virtual device, the virtual device address (e.g.
   * tun/tap)
   *
   * EXPECTS:
   * * Must specify exactly one of port_id or PCI or vdev.
   */
  pb_error_t InitPb(const bess::pb::PMDPortArg &arg);

  /*!
   * Deprecated! Don't use me :-)
   */
  virtual struct snobj *Init(struct snobj *arg);

  /*!
   * Release the device.
   */
  virtual void DeInit();

  /*!
   * Copies rte port statistics into queue_stats datastructure (see port.h).
   *
   * PARAMETERS:
   * * bool reset : if true, reset DPDK local statistics and return (do not
   * collect stats).
   */
  virtual void CollectStats(bool reset);

  /*!
   * Receives packets from the device.
   *
   * PARAMETERS:
   * * queue_t quid : NIC queue to receive from.
   * * snb_array_t pkts   : buffer to store received packets in to.
   * * int cnt  : max number of packets to pull.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets received (<=cnt)
   */
  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt);

  /*!
   * Sends packets out on the device.
   *
   * PARAMETERS:
   * * queue_t quid : NIC queue to transmit on.
   * * snb_array_t pkts   : packets to transmit.
   * * int cnt  : number of packets in pkts to transmit.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets sent (<=cnt).
   */
  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt);

 private:
  /*!
   * The DPDK port ID number (set after binding).
   */
  dpdk_port_t dpdk_port_id_;

  /*!
   * True if device did not exist when bessd started and was later patched in.
   */
  bool hot_plugged_;
};

#endif  // BESS_DRIVERS_PMD_H_
