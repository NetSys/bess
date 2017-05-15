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
