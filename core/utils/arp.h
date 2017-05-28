#ifndef BESS_UTILS_ARP_H_
#define BESS_UTILS_ARP_H_

#include "ether.h"
#include "ip.h"
#include "endian.h"

namespace bess {
namespace utils {

struct[[gnu::packed]] ARP {

	static const be16_t kEtherHardwareFormat = 1;

	enum Opcode : uint16_t {
	    REQUEST = 1,
	    REPLY = 2,
		REVREQUEST = 3,
		REVREPLY = 4,
	    INVREQUEST = 8,
		INVREPLY = 8,
	};

	be16_t  arp_hrd;    /* format of hardware address */
	be16_t  arp_pro;    /* format of protocol address */
	uint8_t arp_hln;    /* length of hardware address */
	uint8_t arp_pln;    /* length of protocol address */
	be16_t  arp_op;     /* ARP opcode (command) */

	/* ARP Data */
	Ethernet::Address arp_sha;  /* sender hardware address */
	Ipv4 arp_sip;               /* sender IP address */
	Ethernet::Address arp_tha;  /* target hardware address */
	Ipv4 arp_tip;               /* target IP address */

};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ARP_H_
