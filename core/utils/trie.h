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

#ifndef BESS_UTILS_TRIE_H_
#define BESS_UTILS_TRIE_H_

#include <memory>
#include <string>

namespace bess {
namespace utils {

// A utility Trie class that supports prefix lookups
class Trie {
 public:
  // Node definition
  struct Node {
    Node() : leaf(), children() {}

    Node(const Node& other) {
      leaf = other.leaf;
      for (int i = 0; i < 256; i++) {
        if (other.children[i] != nullptr) {
          children[i].reset(new Node(*(other.children[i])));
        }
      }
    }

    Node& operator=(const Node& other) {
      leaf = other.leaf;
      for (int i = 0; i < 256; i++) {
        if (other.children[i] != nullptr) {
          children[i].reset(new Node(*(other.children[i])));
        }
      }
      return *this;
    }

    bool leaf;
    std::unique_ptr<Node> children[256];
  };

  Trie() : root_() {}
  Trie(const Trie& t) : root_(t.root_) { }

  // Inserts a string into the trie
  void Insert(const std::string& key);

  // Returns true if prefix matches the stored keys
  bool Lookup(const std::string& prefix);

  // View stored keys as prefixes. Returns true if any prefix match the key.
  bool LookupKey(const std::string& key);

 private:
  Node root_;
};

inline void Trie::Insert(const std::string& key) {
  Node* cur = &root_;
  for (const char& c : key) {
    size_t idx = c;
    if (cur->children[idx] == nullptr) {
      cur->children[idx].reset(new Node());
    }
    cur = cur->children[idx].get();
  }
  cur->leaf = true;
}

inline bool Trie::Lookup(const std::string& prefix) {
  Node* cur = &root_;
  for (const char& c : prefix) {
    size_t idx = c;
    if (cur->children[idx] == nullptr) {
      return false;
    }
    cur = cur->children[idx].get();
  }
  return true;
}

inline bool Trie::LookupKey(const std::string& key) {
  Node* cur = &root_;
  for (const char& c : key) {
    size_t idx = c;
    if (cur->children[idx] == nullptr) {
      break;
    }
    cur = cur->children[idx].get();
  }
  return cur->leaf;
}

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_TRIE_H_
