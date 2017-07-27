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
#include <utility>

namespace bess {
namespace utils {

// A utility Trie class that supports prefix lookups
template <typename T>
class Trie {
 public:
  // Node definition
  struct Node {
    Node() : leaf(), prefix(), val(), children() {}

    Node(const Node& other) {
      leaf = other.leaf;
      prefix = other.prefix;
      val = other.val;
      for (int i = 0; i < 256; i++) {
        if (other.children[i] != nullptr) {
          children[i].reset(new Node(*(other.children[i])));
        }
      }
    }

    Node& operator=(const Node& other) {
      leaf = other.leaf;
      prefix = other.prefix;
      val = other.val;
      for (int i = 0; i < 256; i++) {
        if (other.children[i] != nullptr) {
          children[i].reset(new Node(*(other.children[i])));
        }
      }
      return *this;
    }

    bool leaf;
    bool prefix;
    T val;
    std::unique_ptr<Node> children[256];
  };

  Trie() : root_() {}
  Trie(const Trie& t) : root_(t.root_) {}

  // Inserts a string into the trie, associating the key
  // with the value.
  void Insert(const std::string& key, const T& val);

  // Inserts a string into the trie, associating the key with
  // the value. If prefix is true, then any key that begins with
  // this key will also be a match, unless the trie contains a match
  // of greater specificity.
  void Insert(const std::string& key, const T& val, bool prefix);

  // Returns true if the key is in the trie.
  bool Match(const std::string& key);

  // Returns true if there is a key in the trie that begins with the given
  // prefix.
  bool MatchPrefix(const std::string& prefix);

  // Look up the value associated with the given key.
  // Returns the pair {true, <value>} if the key is in the trie.
  // Returns a pair whose first element is false if the key is not found.
  std::pair<bool, T> Lookup(const std::string& key);

 private:
  Node root_;
};

template <typename T>
inline void Trie<T>::Insert(const std::string& key, const T& val) {
  return Insert(key, val, false);
}

template <typename T>
inline void Trie<T>::Insert(const std::string& key, const T& val, bool prefix) {
  Node* cur = &root_;
  for (const char& c : key) {
    size_t idx = c;
    if (cur->children[idx] == nullptr) {
      cur->children[idx].reset(new Node());
    }
    cur = cur->children[idx].get();
  }
  cur->leaf = true;
  cur->prefix = prefix;
  cur->val = val;
}

template <typename T>
inline bool Trie<T>::Match(const std::string& key) {
  Node* cur = &root_;
  if (cur->prefix) {
    return true;
  }

  for (const char& c : key) {
    size_t idx = c;
    if (cur->children[idx] == nullptr) {
      return false;
    }
    cur = cur->children[idx].get();
    if (cur->prefix) {
      return true;
    }
  }
  return cur->leaf;
}

template <typename T>
inline bool Trie<T>::MatchPrefix(const std::string& prefix) {
  Node* cur = &root_;
  if (cur->prefix) {
    return true;
  }

  for (const char& c : prefix) {
    size_t idx = c;
    if (cur->children[idx] == nullptr) {
      return false;
    }
    cur = cur->children[idx].get();
    if (cur->prefix) {
      return true;
    }
  }
  return true;
}

template <typename T>
inline std::pair<bool, T> Trie<T>::Lookup(const std::string& key) {
  Node* cur = &root_;
  Node* prefix_match = nullptr;
  if (cur->prefix) {
    prefix_match = cur;
  }

  for (const char& c : key) {
    size_t idx = c;
    if (cur->children[idx] == nullptr) {
      if (prefix_match != nullptr) {
        return {true, prefix_match->val};
      }
      return {false, T()};
    }
    cur = cur->children[idx].get();
    if (cur->prefix) {
      prefix_match = cur;
    }
  }
  if (cur->leaf) {
    return {true, cur->val};
  } else if (prefix_match != nullptr) {
    return {true, prefix_match->val};
  } else {
    return {false, T()};
  }
}

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_TRIE_H_
