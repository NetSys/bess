#ifndef BESS_UTILS_PCAP_H_
#define BESS_UTILS_PCAP_H_

#define PCAP_MAGIC_NUMBER 0xa1b2c3d4
#define PCAP_VERSION_MAJOR 2
#define PCAP_VERSION_MINOR 4
#define PCAP_THISZONE 0
#define PCAP_SIGFIGS 0
#define PCAP_SNAPLEN 65535
#define PCAP_NETWORK 1 /* Ethernet */

struct pcap_hdr {
  uint32_t magic_number;  /* magic number */
  uint16_t version_major; /* major version number */
  uint16_t version_minor; /* minor version number */
  int32_t thiszone;       /* GMT to local correction */
  uint32_t sigfigs;       /* accuracy of timestamps */
  uint32_t snaplen;       /* max length of captured packets, in octets */
  uint32_t network;       /* data link type */
};

struct pcap_rec_hdr {
  uint32_t ts_sec;   /* timestamp seconds */
  uint32_t ts_usec;  /* timestamp microseconds */
  uint32_t incl_len; /* number of octets of packet saved in file */
  uint32_t orig_len; /* actual length of packet */
};

#endif  // BESS_UTILS_PCAP_H_
