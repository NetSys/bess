// Written by Kenneth Benzie
// https://infektor.net/posts/2017-03-31-range-based-enumerate.html

#ifndef BESS_UTILS_ENUMERATE_H_

#include <iterator>
#include <utility>
#include <vector>

namespace bess::utils {

template <class Iterator>
struct EnumerateIterator {
  using iterator = Iterator;
  using index_type = typename std::iterator_traits<iterator>::difference_type;
  using reference = typename std::iterator_traits<iterator>::reference;

  EnumerateIterator(index_type index, iterator iterator)
      : index(index), iter(iterator) {}

  EnumerateIterator &operator++() {
    ++index;
    ++iter;
    return *this;
  }

  bool operator!=(const EnumerateIterator &other) const {
    return iter != other.iter;
  }

  std::pair<index_type &, reference> operator*() { return {index, *iter}; }

 private:
  index_type index;
  iterator iter;
};

template <class Iterator>
struct EnumerateRange {
  using index_type = typename std::iterator_traits<Iterator>::difference_type;
  using iterator = EnumerateIterator<Iterator>;

  EnumerateRange(Iterator first, Iterator last, index_type initial)
      : first(first), last(last), initial(initial) {}

  iterator begin() const { return iterator(initial, first); }

  iterator end() const { return iterator(0, last); }

 private:
  Iterator first;
  Iterator last;
  index_type initial;
};

template <class Iterator>
decltype(auto) Enumerate(
    Iterator first, Iterator last,
    typename std::iterator_traits<Iterator>::difference_type initial) {
  return EnumerateRange(first, last, initial);
}

template <class Container>
decltype(auto) Enumerate(Container &content) {
  return EnumerateRange(std::begin(content), std::end(content), 0);
}

}  // namespace bess::utils

#endif  // BESS_UTILS_ENUMERATE_H_
