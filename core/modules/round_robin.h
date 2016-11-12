#ifndef BESS_MODULES_ROUNDROBIN_H_
#define BESS_MODULES_ROUNDROBIN_H_

#include "../module.h"
#include "../module_msg.pb.h"

/*!
 * TODO: RoundRobin currently does not support multiple workers.
 */

// Maxumum number of output gates to allow.
#define MAX_RR_GATES 16384

/*!
 * The RoundRobin module schedules packets from a single input gate across
 * multiple output gates according to a (you guessed it) RoundRobin scheduling
 * algorithm:
 * https://en.wikipedia.org/wiki/Round-robin_scheduling
 *
 * EXPECTS: Input packets in any format
 *
 * MODIFICATIONS: None
 *
 * INPUT GATES: 1
 *
 * OUTPUT GATES: 1..MAX_GATES
 *
 * PARAMETERS:
 *    * gates: the number of output gates for the module
 *    * mode: whether to schedule with per-packet or per-batch granularity
 * options
 *    are "packet" or "batch".
*/
class RoundRobin : public Module {
 public:
  RoundRobin()
      : Module(), gates_(), ngates_(), current_gate_(), per_packet_() {}

  virtual struct snobj *Init(struct snobj *arg);
  pb_error_t InitPb(const bess::pb::RoundRobinArg &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;

  /*!
   * Switches the RoundRobin module between "batch" vs "packet" scheduling.
   */
  struct snobj *CommandSetMode(struct snobj *arg);
  pb_cmd_response_t CommandSetModePb(
      const bess::pb::RoundRobinCommandSetModeArg &arg);
  /*!
   * Sets the number of output gates.
   */
  struct snobj *CommandSetGates(struct snobj *arg);
  pb_cmd_response_t CommandSetGatesPb(
      const bess::pb::RoundRobinCommandSetGatesArg &arg);

 private:
  // ID number for each egress gate.
  gate_idx_t gates_[MAX_RR_GATES];
  // The total number of output gates
  int ngates_;
  // The next gate to transmit on in the RoundRobin scheduler
  int current_gate_;
  // Whether or not to schedule per-packet or per-batch
  int per_packet_;
};

/*!
 * Sanity function: is this gate_idx_t possibly a real gate?
 * Note that true only indicates that the gate_idx_t is not > MAX_GATES and does
 * not represent the nullptr gate -- true does not indicate that the gate is
 * actually instantiated and connected to anything.
 */
static inline int is_valid_gate(gate_idx_t gate) {
  return (gate < MAX_GATES || gate == DROP_GATE);
}

#endif  // BESS_MODULES_ROUNDROBIN_H_
