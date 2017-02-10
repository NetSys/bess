<a name="top"/>

<a name="module_msg.proto"/>
<p align="right"><a href="#top">Top</a></p>

## module_msg.proto



<a name="bess.pb.ACL()"/>
### ACL()
The module ACL creates an access control module which by default blocks all traffic, unless it contains a rule which specifies otherwise.
Examples of ACL can be found in [acl.bess](https://github.com/NetSys/bess/blob/master/bessctl/conf/samples/acl.bess)

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rules | [ACL().Rule](#bess.pb.ACL().Rule) | repeated | A list of ACL rules. |


<a name="bess.pb.ACL().Rule"/>
### ACL().Rule
One ACL rule is represented by the following 6-tuple.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| src_ip | [string](#string) | optional | Source IP block in CIDR. Wildcard if "". |
| dst_ip | [string](#string) | optional | Destination IP block in CIDR. Wildcard if "". |
| src_port | [uint32](#uint32) | optional | TCP/UDP source port. Wildcard if 0. |
| dst_port | [uint32](#uint32) | optional | TCP/UDP Destination port. Wildcard if 0. |
| established | [bool](#bool) | optional | Not implemented |
| drop | [bool](#bool) | optional | Drop matched packets if true. |


<a name="bess.pb.BPF()"/>
### BPF()
The BPF module is an access control module that sends packets our on a particular gate based on whether they match a BPF filter.

__Input Gates__: 1
__Output Gates__: many (configurable)

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| filters | [BPF().Filter](#bess.pb.BPF().Filter) | repeated | The BPF initialized function takes a list of BPF filters. |


<a name="bess.pb.BPF().Filter"/>
### BPF().Filter
One BPF filter is represented by the following 3-tuple.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| priority | [int64](#int64) | optional | The priority level for this rule. If a packet matches multiple rules, it will be forwarded out the gate with the highest priority. If a packet matches multiple rules with the same priority, the behavior is undefined. |
| filter | [string](#string) | optional | The actual BPF string. |
| gate | [int64](#int64) | optional | What gate to forward packets that match this BPF to. |


<a name="bess.pb.BPF.Clear()"/>
### BPF.Clear()
The BPF module has a command `clear()` that takes no parameters.
This command removes all filters from the module.



<a name="bess.pb.Buffer()"/>
### Buffer()
The Buffer module takes no parameters to initialize (ie, `Buffer()` is sufficient to create one).
Buffer accepts packets and stores them; it may forard them to the next module only after it has
received enough packets to fill an entire PacketBatch.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.Bypass()"/>
### Bypass()
The Bypass module forwards packets without any processing. It requires no parameters to initialize. Bypass is useful primarily for testing and performance evaluation.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.Dump()"/>
### Dump()
The Dump module blindly forwards packets without modifying them. It periodically samples a packet and prints out out to the BESS log (by default stored in `/tmp/bessd.INFO`).

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| interval | [double](#double) | optional | How frequently to sample and print a packet, in seconds. |


<a name="bess.pb.EtherEncap()"/>
### EtherEncap()
The EtherEncap module wraps packets in an Ethernet header, but it takes no parameters. Instead, Ethernet source, destination, and type are pulled from a packet's metadata attributes.
For example: `SetMetadata('dst_mac', 11:22:33:44:55) -> EtherEncap()`
This is useful when upstream modules wish to assign a MAC address to a packet, e.g., due to an ARP request.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.ExactMatch()"/>
### ExactMatch()
The ExactMatch module splits packets along output gates according to exact match values in arbitrary packet fields.
To instantiate an ExactMatch module, you must specify which fields in the packet to match over. You can add rules using the function `ExactMatch.add(...)`
Fields may be stored either in the packet data or its metadata attributes.
An example script using the ExactMatch code is found
in [`bess/bessctl/conf/samples/exactmatch.bess`](https://github.com/NetSys/bess/blob/master/bessctl/conf/samples/exactmatch.bess).

__Input Gates__: 1
__Output Gates__: many (configurable)

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| fields | [ExactMatch().Field](#bess.pb.ExactMatch().Field) | repeated | A list of ExactMatch Fields |


<a name="bess.pb.ExactMatch().Field"/>
### ExactMatch().Field
An ExactMatch Field specifies a field over which to check for ExactMatch rules. Field may be in EITHER the packet's data OR it's metadata attributes.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| size | [uint64](#uint64) | optional | The length, in bytes, of the field to inspect. |
| mask | [uint64](#uint64) | optional | A bitmask over the field to specify which bits to inspect (default 0xff). |
| name | [string](#string) | optional | Metadata attribute name, if field resides in metadata. |
| offset | [int64](#int64) | optional | The offset, in bytes, from the start of the packet that the field resides in (if field resides in packet data).. |


<a name="bess.pb.ExactMatch.Add()"/>
### ExactMatch.Add()
The ExactMatch module has a command `add(...)` that takes two parameters.
The ExactMatch initializer specifies what fields in a packet to inspect; add() specifies
which values to check for over these fields.
Add() inserts a new rule into the ExactMatch module such that traffic matching i
that bytestring will be forwarded
out a specified gate.
Example use: `add(fields=[aton('12.3.4.5'), aton('5.4.3.2')], gate=2)`

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gate | [uint64](#uint64) | optional | The gate to forward out packets that mach this rule. |
| fields | [bytes](#bytes) | repeated | The exact match values to check for |


<a name="bess.pb.ExactMatch.Clear()"/>
### ExactMatch.Clear()
The ExactMatch module has a command `clear()` which takes no parameters.
This command removes all rules from the ExactMatch module.



<a name="bess.pb.ExactMatch.Delete()"/>
### ExactMatch.Delete()
The ExactMatch module has a command `delete(...)` which deletes an existing rule.
Example use: `delete(fields=[aton('12.3.4.5'), aton('5.4.3.2')])`

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| fields | [bytes](#bytes) | repeated | The field values for the rule to be deleted. |


<a name="bess.pb.ExactMatch.SetDefaultGate()"/>
### ExactMatch.SetDefaultGate()
The ExactMatch module has a command `setDefaultGate(...)` which takes one parameter.
This command routes all traffic which does _not_ match a rule to a specified gate.
Example use in bessctl: `setDefaultGate(gate=2)`

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gate | [uint64](#uint64) | optional | The gate number to send the default traffic out. |


<a name="bess.pb.FlowGen()"/>
### FlowGen()
The FlowGen module generates simulated TCP flows of packets with correct SYN/FIN flags and sequence numbers.
This module is useful for testing, e.g., a NAT module or other flow-aware code.
Packets are generated off a base, "template" packet by modifying the IP src/dst and TCP src/dst. By default, only the ports are changed and will be modified by incrementing the template ports by up to 2000 more than the template values.

__Input Gates__: 0
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| template | [bytes](#bytes) | optional | The packet "template". All data packets are derived from this template and contain the same payload. |
| pps | [double](#double) | optional | The total number of pps to generate. |
| flow_rate | [double](#double) | optional | The number of new flows to create every second. flow_rate must be <= pps. |
| flow_duration | [double](#double) | optional | The lifetime of a flow in seconds. |
| arrival | [string](#string) | optional | The packet arrival distribution -- must be either "uniform" or "exponential" |
| duration | [string](#string) | optional | The flow duration distribution -- must be either "uniform" or "pareto" |
| quick_rampup | [bool](#bool) | optional | Whether or not to populate the flowgenerator with initial flows (start generating full pps rate immediately) or to wait for new flows to be generated naturall (all flows have a SYN packet). |
| ip_src_range | [uint32](#uint32) | optional | When generating new flows, FlowGen modifies the template packet by changing the IP src, incrementing it by at most ip_src_range. |
| ip_dst_range | [uint32](#uint32) | optional | When generating new flows, FlowGen modifies the template packet by changing the IP dst, incrementing it by at most ip_dst_range. |
| port_src_range | [uint32](#uint32) | optional | When generating new flows, FlowGen modifies the template packet by changing the TCP port, incrementing it by at most port_src_range. |
| port_dst_range | [uint32](#uint32) | optional | When generating new flows, FlowGen modifies the template packet by changing the TCP dst port, incrementing it by at most port_dst_range. |


<a name="bess.pb.GenericDecap()"/>
### GenericDecap()
The GenericDecap module strips off the first few bytes of data from a packet.

__Input Gates__: 1
__Ouptut Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| bytes | [uint64](#uint64) | optional | The number of bytes to strip off. |


<a name="bess.pb.GenericEncap()"/>
### GenericEncap()
The GenericEncap module adds a header to packets passing through it.
Takes a list of fields. Each field is either:

 1. {'size': X, 'value': Y}		(for constant values)
 2. {'size': X, 'attr': Y}		(for metadata attributes)

e.g.: GenericEncap([{'size': 4, 'value':0xdeadbeef},
                    {'size': 2, 'attr':'foo'},
                    {'size': 2, 'value':0x1234}])
will prepend a 8-byte header:
   de ad be ef <xx> <xx> 12 34
where the 2-byte <xx> <xx> comes from the value of metadata arribute 'foo'
for each packet.
An example script using GenericEncap is in [`bess/bessctl/conf/samples/generic_encap.bess`](https://github.com/NetSys/bess/blob/master/bessctl/conf/samples/generic_encap.bess).

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| fields | [GenericEncap().Field](#bess.pb.GenericEncap().Field) | repeated |  |


<a name="bess.pb.GenericEncap().Field"/>
### GenericEncap().Field
A GenericEncap field represents one field in the new packet header.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| size | [uint64](#uint64) | optional | The length of the field. |
| attr_name | [string](#string) | optional | The metadata attribute name to pull the field value from |
| value | [uint64](#uint64) | optional | Or, the fixed value to insert into the packet. |


<a name="bess.pb.HashLB()"/>
### HashLB()
The HashLB module partitions packets between output gates according to either
a hash over their MAC src/dst (mode=l2), their IP src/dst (mode=l3), or the full IP/TCP 5-tuple (mode=l4).

__Input Gates__: 1
__Output Gates__: many (configurable)

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gates | [int64](#int64) | repeated | A list of gate numbers over which to partition packets |
| mode | [string](#string) | optional | The mode (l2, l3, or l4) for the hash function. |


<a name="bess.pb.HashLB.SetGates()"/>
### HashLB.SetGates()
The HashLB module has a command `setGates(...)` which takes one parameter.
This function takes in a list of gate numbers to send hashed traffic out over.
Example use in bessctl: `lb.setGates(gates=[0,1,2,3])`

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gates | [int64](#int64) | repeated | A list of gate numbers to load balance traffic over |


<a name="bess.pb.HashLB.SetMode()"/>
### HashLB.SetMode()
The HashLB module has a command `setMode(...)` which takes one parameter.
The mode specifies whether the load balancer will hash over the src/dest ethernet header (l2),
over the src/dest IP addresses (l3), or over the flow 5-tuple (l4).
Example use in bessctl: `lb.setMode('l2')`

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| mode | [string](#string) | optional | What fields to hash over, l1, l2, or l3 are only valid values. |


<a name="bess.pb.IPEncap()"/>
### IPEncap()
Encapsulates a packet with an IP header, where IP src, dst, and proto are filled in
by metadata values carried with the packet. Metadata attributes must include:
ip_src, ip_dst, ip_proto, ip_nexthop, and ether_type.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.IPLookup()"/>
### IPLookup()
An IPLookup module perfroms LPM lookups over a packet destination.
IPLookup takes no parameters to instantiate.
To add rules to the IPLookup table, use `IPLookup.add()`

__Input Gates__: 1
__Output Gates__: many (configurable, depending on rule values)



<a name="bess.pb.IPLookup.Add()"/>
### IPLookup.Add()
The IPLookup module has a command `add(...)` which takes three paramters.
This function accepts the routing rules -- CIDR prefix, CIDR prefix length, and what gate to forward
matching traffic out on.
Example use in bessctl: `table.add(prefix='10.0.0.0', prefix_len=8, gate=2)`

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| prefix | [string](#string) | optional | The CIDR IP part of the prefix to match |
| prefix_len | [uint64](#uint64) | optional | The prefix length |
| gate | [uint64](#uint64) | optional | The number of the gate to forward matching traffic on. |


<a name="bess.pb.IPLookup.Clear()"/>
### IPLookup.Clear()
The IPLookup module has a command `clear()` which takes no parameters.
This function removes all rules in the IPLookup table.
Example use in bessctl: `myiplookuptable.clear()`



<a name="bess.pb.L2Forward()"/>
### L2Forward()
An L2Forward module forwards packets to an output gate according to exact-match rules over
an Ethernet destination.
Note that this is _not_ a learning switch -- forwards according to fixed
routes specified by `add(..)`.

__Input Gates__: 1
__Ouput Gates__: many (configurable, depending on rules)

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| size | [int64](#int64) | optional | Configures the forwarding hash table -- total number of hash table entries. |
| bucket | [int64](#int64) | optional | Configures the forwarding hash table -- total number of slots per hash value. |


<a name="bess.pb.L2Forward.Add()"/>
### L2Forward.Add()
The L2Forward module forwards traffic via exact match over the Ethernet
destination address. The command `add(...)`  allows you to specifiy a
MAC address and which gate the L2Forward module should direct it out of.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| entries | [L2Forward.Add().Entry](#bess.pb.L2Forward.Add().Entry) | repeated | A list of L2Forward entries. |


<a name="bess.pb.L2Forward.Add().Entry"/>
### L2Forward.Add().Entry


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| addr | [string](#string) | optional | The MAC address to match |
| gate | [int64](#int64) | optional | Which gate to send out traffic matching this address. |


<a name="bess.pb.L2Forward.Delete()"/>
### L2Forward.Delete()
The L2Forward module has a function `delete(...)` to remove a rule
from the MAC forwarding table.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| addrs | [string](#string) | repeated | The address to remove from the forwarding table |


<a name="bess.pb.L2Forward.Lookup()"/>
### L2Forward.Lookup()
The L2Forward module has a function `lookup(...)` to query what output gate
a given MAC address will be forwared to; it returns the gate ID number.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| addrs | [string](#string) | repeated | The MAC address to query for |


<a name="bess.pb.L2Forward.LookupResponse"/>
### L2Forward.LookupResponse
This message type provides the reponse to the L2Forward function `lookup(..)`.
It returns the gate that a requested MAC address is currently assigned to.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gates | [uint64](#uint64) | repeated | The gate ID that the requested MAC address maps to |


<a name="bess.pb.L2Forward.Populate()"/>
### L2Forward.Populate()
The L2Forward module has a command `populate(...)` which allows for fast creation
of the forwarding table given a range of MAC addresses. The funciton takes in a
'base' MAC address, a count (number of MAC addresses), and a gate_id. The module
will route all MAC addresses starting from the base address, up to base+count address
round-robin over gate_count total gates.
For example, `populate(base='11:22:33:44:00', count = 10, gate_count = 2) would
route addresses 11:22:33:44::(00, 02, 04, 06, 08) out a gate 0 and the odd-suffixed
addresses out gate 1.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| base | [string](#string) | optional | The base MAC address |
| count | [int64](#int64) | optional | How many addresses beyond base to populate into the routing table |
| gate_count | [int64](#int64) | optional | How many gates to create in the L2Forward module. |


<a name="bess.pb.L2Forward.SetDefaultGate()"/>
### L2Forward.SetDefaultGate()
For traffic reaching the L2Forward module which does not match a MAC rule,
the function `setDefaultGate(...)` allows you to specify a default gate
to direct unmatched traffic to.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gate | [int64](#int64) | optional | The default gate to forward traffic which matches no entry to. |


<a name="bess.pb.MACSwap()"/>
### MACSwap()
The MACSwap module takes no arguments. It swaps the src/destination MAC addresses
within a packet.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.Measure()"/>
### Measure()
The measure module tracks latencies, packets per second, and other statistics.
It should be paired with a Timestamp module, which attaches a timestamp to packets.
The measure module will log how long (in nanoseconds) it has been for each packet it received since it was timsestamped.
An example of the Measure module in use is in [`bess/bessctl/conf/perftest/latency/bess`](https://github.com/NetSys/bess/blob/master/bessctl/conf/samples/latency.bess).

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| warmup | [int64](#int64) | optional | How long to wait, in seconds, between starting the module and taking measurements. |


<a name="bess.pb.Measure.GetSummary()"/>
### Measure.GetSummary()
This function requests a summary of the statistics stored in the Measure module.
It takes no parameters and returns a GetSummaryResponse.



<a name="bess.pb.Measure.GetSummaryResponse"/>
### Measure.GetSummaryResponse
The Measure module function `getSummary()` takes no parameters and returns the following
values.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| timestamp | [double](#double) | optional | What time it is now? |
| packets | [uint64](#uint64) | optional | The total number of packets seen by this module. |
| bits | [uint64](#uint64) | optional | The total number of bits seen by this module. |
| total_latency_ns | [uint64](#uint64) | optional | Sum of all round trip times across all packets |
| latency_min_ns | [uint64](#uint64) | optional | The minimum latency for any packet observed by the Measure module. |
| latency_avg_ns | [uint64](#uint64) | optional | The average latency for all packets. |
| latency_max_ns | [uint64](#uint64) | optional | The max latency for any packet |
| latency_50_ns | [uint64](#uint64) | optional | The 50th percentile latency over all packets |
| latency_99_ns | [uint64](#uint64) | optional | The 99th percentile latency over all packets. |


<a name="bess.pb.Merge()"/>
### Merge()
The merge module takes no parameters. It has multiple input gates,
and passes out all packets from a single output gate.

__Input Gates__: many (configurable)
__Output Gates__: 1



<a name="bess.pb.MetadataTest()"/>
### MetadataTest()
The MetadataTest module is used for internal testing purposes.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| read | [MetadataTest().ReadEntry](#bess.pb.MetadataTest().ReadEntry) | repeated |  |
| write | [MetadataTest().WriteEntry](#bess.pb.MetadataTest().WriteEntry) | repeated |  |
| update | [MetadataTest().UpdateEntry](#bess.pb.MetadataTest().UpdateEntry) | repeated |  |


<a name="bess.pb.MetadataTest().ReadEntry"/>
### MetadataTest().ReadEntry


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) | optional |  |
| value | [int64](#int64) | optional |  |


<a name="bess.pb.MetadataTest().UpdateEntry"/>
### MetadataTest().UpdateEntry


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) | optional |  |
| value | [int64](#int64) | optional |  |


<a name="bess.pb.MetadataTest().WriteEntry"/>
### MetadataTest().WriteEntry


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) | optional |  |
| value | [int64](#int64) | optional |  |


<a name="bess.pb.NAT()"/>
### NAT()
The NAT module implements address translation, rewriting packet source addresses
for a specified internal prefix with IPs according to a specified
external prefix. To see an example
of NAT in use, see [`bess/bessctl/conf/samples/nat.bess`](https://github.com/NetSys/bess/blob/master/bessctl/conf/samples/nat.bess)

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rules | [NAT().Rule](#bess.pb.NAT().Rule) | repeated | A list of rules for rewriting |


<a name="bess.pb.NAT().Rule"/>
### NAT().Rule


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| internal_addr_block | [string](#string) | optional | Internal IP block in CIDR. |
| external_addr_block | [string](#string) | optional | External IP block in CIDR. |


<a name="bess.pb.NoOp()"/>
### NoOp()
This module is used for testing purposes.



<a name="bess.pb.PortInc()"/>
### PortInc()
The PortInc module connects a physical or virtual port and releases
packets from it. PortInc does not support multiqueueing.
For details on how to configure PortInc using DPDK, virtual ports,
or libpcap, see the sidebar in the wiki.

__Input Gates__: 0
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| port | [string](#string) | optional | The portname to connect to. |
| burst | [int64](#int64) | optional | The number of packets per batch to release. |
| prefetch | [bool](#bool) | optional | Whether or not to prefetch packets from the port. |


<a name="bess.pb.PortInc.SetBurst()"/>
### PortInc.SetBurst()
The module PortInc has a function SetBurst that allows you to specify the maximum number of packets to be stored in a single PacketBatch released by the module.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| burst | [int64](#int64) | optional | The maximum "burst" of packets (ie, the maximum batch size) |


<a name="bess.pb.PortOut()"/>
### PortOut()
The PortOut module connects to a physical or virtual port and pushes
packets to it. For details on how to configure PortOut with DPDK,
virtual ports, libpcap, etc, see the sidebar in the wiki.

__Input Gates__: 1
__Output Gates__: 0

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| port | [string](#string) | optional | The portname to connect to. |


<a name="bess.pb.Queue()"/>
### Queue()
The Queue module implements a simple packet queue.

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| size | [uint64](#uint64) | optional | The maximum number of packets to store in the queue. |
| burst | [int64](#int64) | optional | The maximum number of packets to release from the queue in one batch. |
| prefetch | [bool](#bool) | optional | Whether or not to prefetch packet data for the next batch after releasing. SANGJIN WHAT IS THE PURPOSE OF THIS? |


<a name="bess.pb.Queue.SetBurst()"/>
### Queue.SetBurst()
The module QueueInc has a function `setBurst(...)` that allows you to specify the maximum number of packets to be stored in a single PacketBatch released by the module.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| burst | [int64](#int64) | optional | The maximum "burst" of packets (ie, the maximum batch size) |


<a name="bess.pb.Queue.SetSize()"/>
### Queue.SetSize()
The module QueueInc has a function `setSize(...)` that allows specifying the size of the queue in total number of packets.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| size | [uint64](#uint64) | optional | The maximum number of packets to store in the queue. |


<a name="bess.pb.QueueInc()"/>
### QueueInc()
The QueueInc produces input packets from a physical or virtual port.
Unlike PortInc, it supports multiqueue ports.
For details on how to configure QueueInc with DPDK, virtualports,
libpcap, etc, see the sidebar in the wiki.

__Input Gates__: 0
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| port | [string](#string) | optional | The portname to connect to (read from). |
| qid | [uint64](#uint64) | optional | The queue on that port to read from. |
| burst | [int64](#int64) | optional | The maximum number of packets to release in a batch. |
| prefetch | [bool](#bool) | optional | Ehrthrt ot not to prefetch packets from the port. |


<a name="bess.pb.QueueInc.SetBurst()"/>
### QueueInc.SetBurst()
The module QueueInc has a function `setBurst(...)` that allows you to specify the maximum number of packets to be stored in a single PacketBatch released by the module.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| burst | [int64](#int64) | optional | The maximum "burst" of packets (ie, the maximum batch size) |


<a name="bess.pb.QueueOut()"/>
### QueueOut()
The QueueOut module releases packets to a physical or virtual port.
Unlike PortOut, it supports multiqueue ports.
For details on how to configure QueueOut with DPDK, virtualports,
libpcap, etc, see the sidebar in the wiki.

__Input Gates__: 1
__Output Gates__: 0

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| port | [string](#string) | optional | The portname to connect to. |
| qid | [uint64](#uint64) | optional | The queue on that port to write out to. |


<a name="bess.pb.RandomUpdate()"/>
### RandomUpdate()
The RandomUpdate module rewrites a random field in a packet with a random value
between a specified min and max values.

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| fields | [RandomUpdate().Field](#bess.pb.RandomUpdate().Field) | repeated | A list of Random Update Fields. |


<a name="bess.pb.RandomUpdate().Field"/>
### RandomUpdate().Field
RandomUpdate's Field specifies where to rewrite, and what values to rewrite
in each packet processed.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| offset | [int64](#int64) | optional | Offset in bytes for where to rewrite. |
| size | [uint64](#uint64) | optional | The number of bytes to write. |
| min | [uint64](#uint64) | optional | The minimum value to insert into the packet. |
| max | [uint64](#uint64) | optional | The maximum value to insert into the packet. |


<a name="bess.pb.RandomUpdate.Clear()"/>
### RandomUpdate.Clear()
The function `clear()` for RandomUpdate takes no parameters and clears all state in the module.



<a name="bess.pb.Rewrite()"/>
### Rewrite()
The Rewrite module replaces an entire packet body with a packet "template"
converting all packets that pass through to copies of the of one of
the templates.

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| templates | [bytes](#bytes) | repeated | A list of bytestrings representing packet templates. |


<a name="bess.pb.Rewrite.Clear()"/>
### Rewrite.Clear()
The function `clear()` for Rewrite takes no parameters and clears all state in the module.



<a name="bess.pb.RoundRobin()"/>
### RoundRobin()
The RoundRobin module splits packets from one input gate across multiple output
gates.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gates | [int64](#int64) | repeated | A list of gate numbers to split packets across. |
| mode | [string](#string) | optional | Whether to split across gate with every 'packet' or every 'batch'. |


<a name="bess.pb.RoundRobin.SetGates()"/>
### RoundRobin.SetGates()
The RoundRobin module has a function `setGates(...)` which changes
the total number of gates in the module.

__Input Gates__: 1
__Output Gates__: many (configurable)

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gates | [int64](#int64) | repeated | A list of gate numbers to round-robin the traffic over. |


<a name="bess.pb.RoundRobin.SetMode()"/>
### RoundRobin.SetMode()
The RoundRobin module has a function `setMode(...)` which specifies whether
to balance traffic across gates per-packet or per-batch.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| mode | [string](#string) | optional | whether to perform 'packet' or 'batch' round robin partitioning. |


<a name="bess.pb.SetMetadata()"/>
### SetMetadata()
The SetMetadata module adds metadata attributes to packets, which are not stored
or sent out with packet data. For examples of SetMetadata use, see
[`bess/bessctl/conf/attr_match.bess`](https://github.com/NetSys/bess/blob/master/bessctl/conf/metadata/attr_match.bess)

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| attrs | [SetMetadata().Attribute](#bess.pb.SetMetadata().Attribute) | repeated | A list of attributes to attach to the packet. |


<a name="bess.pb.SetMetadata().Attribute"/>
### SetMetadata().Attribute
SetMetadata Attribute describes a metadata attribute and value to attach to every packet.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| name | [string](#string) | optional | The metadata attribute name. |
| size | [uint64](#uint64) | optional | The size of values stored in this attribute. |
| value_int | [uint64](#uint64) | optional | An integer value to store in the packet. |
| value_bin | [bytes](#bytes) | optional | A binary value to store in the packet. |
| offset | [int64](#int64) | optional | An index in the packet data to store copy into the metadata attribute. |


<a name="bess.pb.Sink()"/>
### Sink()
The sink module drops all packets that are sent to it.

__Input Gates__: 1
__Output Gates__: 0



<a name="bess.pb.Source()"/>
### Source()
The Source module generates packets with no payload contents.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| pkt_size | [uint64](#uint64) | optional | The size of packet data to produce. |
| burst | [uint64](#uint64) | optional | The number of packets to produce with every batch released. |


<a name="bess.pb.Source.SetBurst()"/>
### Source.SetBurst()
The Source module has a function `setBurst(...)` which
specifies the maximum number of packets to release in a single packetbatch
from the module.

__Input Gates__: 0
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| burst | [uint64](#uint64) | optional | The maximum number of packets to release in a packetbatch from the module. |


<a name="bess.pb.Source.SetPktSize()"/>
### Source.SetPktSize()
The Source module has a function `setPktSize(...)` which specifies the size
of packets to be produced by the Source module.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| pkt_size | [uint64](#uint64) | optional | The size of the packets for Source to create. |


<a name="bess.pb.Split()"/>
### Split()
The Split module is a basic classifier which directs packets out a gate
based on data in the packet (e.g., if the read in value is 3, the packet
is directed out output gate 3).

__Input Gates__: 1
__Output Gates__: many (up to 2^(size * 8))

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| size | [uint64](#uint64) | optional | The size of the value to read in bytes |
| name | [string](#string) | optional | The name of the metadata field to read. |
| offset | [int64](#int64) | optional | The offset of the data field to read. |


<a name="bess.pb.Timestamp()"/>
### Timestamp()
The timestamp module takes no parameters. It inserts the current
time in nanoseconds into the packet, to be used for latency measurements
alongside the Measure module.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.Update()"/>
### Update()
The Update module rewrites a field in a packet's data with a specific value.

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| fields | [Update().Field](#bess.pb.Update().Field) | repeated | A list of Update Fields. |


<a name="bess.pb.Update().Field"/>
### Update().Field
Update Field describes where in a packet's data to rewrite, and with what value.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| offset | [int64](#int64) | optional | The offset in the packet in bytes to rewrite at. |
| size | [uint64](#uint64) | optional | The number of bytes to rewrite. |
| value | [uint64](#uint64) | optional | The value to write into the packet. |


<a name="bess.pb.Update.Clear()"/>
### Update.Clear()
The function `clear()` for Update takes no parameters and clears all state in the module.



<a name="bess.pb.UrlFilter()"/>
### UrlFilter()
The URLFilter performs TCP reconstruction over a flow and blocks
connections which mention a banned URL.

__Input Gates__: 1
__Output Gates__: 2

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| blacklist | [UrlFilter().Url](#bess.pb.UrlFilter().Url) | repeated | A list of Urls to block. |


<a name="bess.pb.UrlFilter().Url"/>
### UrlFilter().Url
A URL consists of a host and a path.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| host | [string](#string) | optional | Host field, e.g. "www.google.com" |
| path | [string](#string) | optional | Path prefix, e.g. "/" |


<a name="bess.pb.VLANPop()"/>
### VLANPop()
VLANPop removes the VLAN tag.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.VLANPush()"/>
### VLANPush()
VLANPush appends a VLAN tag with a specified TCI value.

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| tci | [uint64](#uint64) | optional | The TCI value to insert in the VLAN tag. |


<a name="bess.pb.VLANSplit()"/>
### VLANSplit()
Splits packets across output gates according to VLAN id (e.g., id 3 goes out gate 3.

__Input Gates__: 1
__Output Gates__: many



<a name="bess.pb.VXLANDecap()"/>
### VXLANDecap()
VXLANDecap module decapsulates a VXLAN header on a packet.

__Input Gates__: 1
__Output Gates__: 1



<a name="bess.pb.VXLANEncap()"/>
### VXLANEncap()
VXLANEncap module wraps a packet in a VXLAN header with a specified destination port.

__Input Gates__: 1
__Output Gates__: 1

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| dstport | [uint64](#uint64) | optional | The destination UDP port |


<a name="bess.pb.WildcardMatch()"/>
### WildcardMatch()
The WildcardMatch module matches over multiple fields in a packet and
pushes packets that do match out specified gate, and those that don't out a default
gate. WildcardMatch is initialized wtih the fields it should inspect over,
rules are added via the `add(...)` function.
An example of WildcardMatch is in [`bess/bessctl/conf/samples/wildcardmatch.bess`](https://github.com/NetSys/bess/blob/master/bessctl/conf/samples/wildcardmatch.bess)

__Input Gates__: 1
__Output Gates__: many (configurable)

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| fields | [WildcardMatch().Field](#bess.pb.WildcardMatch().Field) | repeated | A list of WildcardMatch fields. |


<a name="bess.pb.WildcardMatch().Field"/>
### WildcardMatch().Field
A field over which the WildcardMatch module should inspect.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| size | [uint64](#uint64) | optional | The length in bytes of the field to inspect. |
| offset | [uint64](#uint64) | optional | The field offset into the packet data, if the field lies in the packet itself. |
| attribute | [string](#string) | optional | The metadata attribute to inspect, if the field is a metadata attribute. |


<a name="bess.pb.WildcardMatch.Add()"/>
### WildcardMatch.Add()
The module WildcardMatch has a command `add(...)` which inserts a new rule into the WildcardMatch module. For an example of code using WilcardMatch see `bess/bessctl/conf/samples/wildcardmatch.bess`.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gate | [uint64](#uint64) | optional | The gate to direct traffic matching this rule to. |
| priority | [int64](#int64) | optional | If a packet matches multiple rules, the rule with higher priority will be applied. If priorities are equal behavior is undefined. |
| values | [uint64](#uint64) | repeated | The values to check for in each fieild. |
| masks | [uint64](#uint64) | repeated | The bitmask for each field -- set 0x0 to ignore the field altogether. |


<a name="bess.pb.WildcardMatch.Clear()"/>
### WildcardMatch.Clear()
The function `clear()` for WildcardMatch takes no parameters, it clears
all state in the WildcardMatch module (is equivalent to calling delete for all rules)



<a name="bess.pb.WildcardMatch.Delete()"/>
### WildcardMatch.Delete()
The module WildcardMatch has a command `delete(...)` which removes a rule -- simply specify the values and masks from the previously inserted rule to remove them.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| values | [uint64](#uint64) | repeated | The values being checked for in the rule |
| masks | [uint64](#uint64) | repeated | The bitmask from the rule. |


<a name="bess.pb.WildcardMatch.SetDefaultGate()"/>
### WildcardMatch.SetDefaultGate()
For traffic which does not match any rule in the WildcardMatch module,
the `setDefaultGate(...)` function specifies which gate to send this extra traffic to.

| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| gate | [uint64](#uint64) | optional |  |







<a name="scalar-value-types"/>
## Scalar Value Types

| .proto Type | Notes | C++ Type | Java Type | Python Type |
| ----------- | ----- | -------- | --------- | ----------- |
| <a name="double"/> double |  | double | double | float |
| <a name="float"/> float |  | float | float | float |
| <a name="int32"/> int32 | Uses variable-length encoding. Inefficient for encoding negative numbers – if your field is likely to have negative values, use sint32 instead. | int32 | int | int |
| <a name="int64"/> int64 | Uses variable-length encoding. Inefficient for encoding negative numbers – if your field is likely to have negative values, use sint64 instead. | int64 | long | int/long |
| <a name="uint32"/> uint32 | Uses variable-length encoding. | uint32 | int | int/long |
| <a name="uint64"/> uint64 | Uses variable-length encoding. | uint64 | long | int/long |
| <a name="sint32"/> sint32 | Uses variable-length encoding. Signed int value. These more efficiently encode negative numbers than regular int32s. | int32 | int | int |
| <a name="sint64"/> sint64 | Uses variable-length encoding. Signed int value. These more efficiently encode negative numbers than regular int64s. | int64 | long | int/long |
| <a name="fixed32"/> fixed32 | Always four bytes. More efficient than uint32 if values are often greater than 2^28. | uint32 | int | int |
| <a name="fixed64"/> fixed64 | Always eight bytes. More efficient than uint64 if values are often greater than 2^56. | uint64 | long | int/long |
| <a name="sfixed32"/> sfixed32 | Always four bytes. | int32 | int | int |
| <a name="sfixed64"/> sfixed64 | Always eight bytes. | int64 | long | int/long |
| <a name="bool"/> bool |  | bool | boolean | boolean |
| <a name="string"/> string | A string must always contain UTF-8 encoded or 7-bit ASCII text. | string | String | str/unicode |
| <a name="bytes"/> bytes | May contain any arbitrary sequence of bytes. | string | ByteString | str |
