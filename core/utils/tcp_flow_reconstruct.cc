#include "tcp_flow_reconstruct.h"

namespace bess {
namespace utils {

TcpFlowReconstruct::TcpFlowReconstruct(uint32_t initial_buflen)
    : initialized_(false),
      init_seq_(0),
      buflen_(initial_buflen),
      received_map_(initial_buflen),
      contiguous_len_(0) {
  if (buflen_ <= 0) {
    buflen_ = 1;
    received_map_.resize(buflen_);
  }

  buf_ = (char *)mem_alloc(buflen_);
  assert(buf_ != nullptr);
}

TcpFlowReconstruct::~TcpFlowReconstruct() {
  free(buf_);
}

}  // namespace utils
}  // namespace bess
