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
  void Insert(const std::string& key, const T val);

  // Inserts a string into the trie, associating the key with
  // the value. If prefix is true, then any key that begins with
  // this key will also be a match, unless the trie contains a match
  // of greater specificity.
  void Insert(const std::string& key, const T val, bool prefix);

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
inline void Trie<T>::Insert(const std::string& key, const T val) {
  return Insert(key, val, false);
}

template <typename T>
inline void Trie<T>::Insert(const std::string& key, const T val, bool prefix) {
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
