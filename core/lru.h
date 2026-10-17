#pragma once
#include <cstddef>
#include <list>
#include <map>

namespace pb {

// Least recently used first out, within a byte budget; always keeps the newest entry. Not
// thread-safe: owners lock around it.
template <class K, class V>
class Lru {
 public:
  explicit Lru(size_t budget) : budget_(budget) {}
  V get(const K& key) {  // V{} when absent; marks the entry used
    const auto it = index_.find(key);
    if (it == index_.end()) return {};
    items_.splice(items_.begin(), items_, it->second);
    return it->second->value;
  }
  const V* find(const K& key) const {  // without marking it used
    const auto it = index_.find(key);
    return it == index_.end() ? nullptr : &it->second->value;
  }
  void erase(const K& key) {
    if (const auto it = index_.find(key); it != index_.end()) {
      used_ -= it->second->bytes;
      items_.erase(it->second);
      index_.erase(it);
    }
  }
  void put(const K& key, V value, size_t bytes) {  // replaces the key's entry
    erase(key);
    items_.push_front({key, std::move(value), bytes});
    index_[key] = items_.begin();
    used_ += bytes;
    while (used_ > budget_ && items_.size() > 1) {
      used_ -= items_.back().bytes;
      index_.erase(items_.back().key);
      items_.pop_back();
    }
  }
  // Drops the entries `alike` picks past the `keep` most recently used of them.
  template <class F>
  void keep_recent(const F& alike, size_t keep) {
    for (auto it = items_.begin(); it != items_.end();) {
      if (!alike(it->key)) {
        ++it;
      } else if (keep > 0) {
        keep--;
        ++it;
      } else {
        used_ -= it->bytes;
        index_.erase(it->key);
        it = items_.erase(it);
      }
    }
  }

 private:
  struct Item {
    K key;
    V value;
    size_t bytes;
  };
  std::list<Item> items_;
  std::map<K, typename std::list<Item>::iterator> index_;
  size_t budget_, used_ = 0;
};

}  // namespace pb
