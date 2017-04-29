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
    Node() : leaf(), children() {
      for (int i = 0; i < 256; i++) {
        children[i] = nullptr;
      }
    }

    ~Node() {
      for (int i = 0; i < 256; i++) {
        delete children[i];
      }
    }

    Node(const Node& n) {
      leaf = n.leaf;
      for (int i = 0; i < 256; i++) {
        if (n.children[i] != nullptr) {
          children[i] = new Node(*(n.children[i]));
        } else {
          children[i] = nullptr;
        }
      }
    }

    bool leaf;
    Node* children[256];
  };

  Trie() : root_() {}
  Trie(const Trie& t) { root_ = t.root_; }

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
      cur->children[idx] = new Node();
    }
    cur = cur->children[idx];
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
    cur = cur->children[idx];
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
    cur = cur->children[idx];
  }
  return cur->leaf;
}

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_TRIE_H_
