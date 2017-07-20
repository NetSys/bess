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

#ifndef BESS_BESSCTL_H_
#define BESS_BESSCTL_H_

#include <string>

namespace grpc {
class ServerBuilder;
}  // namespace grpc

// gRPC server encapsulation. Usage:
//   ApiServer server;
//   server.Listen('0.0.0.0', 777);
//   server.Listen('127.0.0.1', 888);
//   server.run();
class ApiServer {
 public:
  ApiServer() : builder_(nullptr) {}

  // This class is neither copyable nor movable.
  ApiServer(const ApiServer &) = delete;
  ApiServer &operator=(const ApiServer &) = delete;

  // Adds a host:port pair. host can be an ipv6 address.
  void Listen(const std::string &host, int port);

  // Runs the API server until it is shutdown by KillBess RPC.
  void Run();

 private:
  grpc::ServerBuilder *builder_;
  static bool grpc_cb_set_;
};

#endif  // BESS_BESSCTL_H_
