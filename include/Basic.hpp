#ifndef DDD_BASIC_HPP
#define DDD_BASIC_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace ddd {

using std::size_t;
using std::uint8_t;
using std::uint32_t;

constexpr uint32_t ROOT_POS = 0;
constexpr uint32_t BLOCK_SIZE = 1U << 8;
constexpr uint32_t INVALID_VALUE = UINT32_MAX >> 1;
constexpr uint32_t NOT_FOUND = UINT32_MAX;

template<typename T, typename... Ts>
inline std::unique_ptr<T> make_unique(Ts&& ... params) {
  return std::unique_ptr<T>(new T(std::forward<Ts>(params)...));
};

struct KvPair {
  std::string key;
  uint32_t value;
};

inline bool operator==(const KvPair& lhs, const KvPair& rhs) {
  return lhs.key == rhs.key;
}

inline bool operator<(const KvPair& lhs, const KvPair& rhs) {
  return lhs.key < rhs.key;
}

struct Stat {
  size_t num_keys = 0;
  size_t num_tries = 0;
  size_t num_nodes = 0;
  size_t bc_size = 0;
  size_t bc_capa = 0;
  size_t bc_emps = 0;
  size_t tail_size = 0;
  size_t tail_capa = 0;
  size_t tail_emps = 0;
  size_t size_in_bytes = 0;
};

class Bc {
public:
  Bc() : base_{0}, is_leaf_{0}, check_{0}, is_fixed_{0} {}
  ~Bc() {}

  uint32_t base() const { return base_; }
  uint32_t value() const { return base_; }
  uint32_t check() const { return check_; }
  bool is_leaf() const { return is_leaf_ == 1; }
  bool is_fixed() const { return is_fixed_ == 1; }

  void set_base(uint32_t base) {
    base_ = base;
    is_leaf_ = 0;
  }
  void set_value(uint32_t value) {
    base_ = value;
    is_leaf_ = 1;
  }
  void set_check(uint32_t check) { check_ = check; }
  void fix() { is_fixed_ = 1; }
  void unfix() { is_fixed_ = 0; }

private:
  uint32_t base_     : 31;
  uint32_t is_leaf_  : 1;
  uint32_t check_    : 31;
  uint32_t is_fixed_ : 1;
};

struct Block {
  uint32_t num_emps = BLOCK_SIZE;
};

struct BlockLink {
  uint32_t next = 0;
  uint32_t prev = 0;
  uint32_t head = 0;
  uint32_t num_emps = BLOCK_SIZE;
};

struct NodeLink {
  uint8_t child = '\0';
  uint8_t sib = '\0';
};

class Query {
public:
  Query() {}
  Query(const char* key) : key_{key} {}
  ~Query() {}

  const char* key() const { return key_ + pos_; }
  uint8_t label() const { return static_cast<uint8_t>(key_[pos_]); }
  uint32_t value() const { return value_; }
  uint32_t node_pos() const { return node_pos_; }
  bool is_finished() const { return is_finished_; }

  void next() { is_finished_ = key_[pos_++] == '\0'; }
  void next(uint32_t node_pos) {
    set_node_pos(node_pos);
    next();
  }
  void prev() {
    is_finished_ = false;
    --pos_;
  }
  void prev(uint32_t node_pos) {
    set_node_pos(node_pos);
    prev();
  }

  void set_value(uint32_t value) { value_ = value; }
  void set_node_pos(uint32_t node_pos) { node_pos_ = node_pos; }

  Query(const Query&) = delete;
  Query& operator=(const Query&) = delete;

private:
  const char* key_ = nullptr;
  uint32_t pos_ = 0;
  uint32_t value_ = INVALID_VALUE;
  uint32_t node_pos_ = ROOT_POS;
  bool is_finished_ = false;
};

class Edge {
public:
  Edge() {}
  ~Edge() {}

  uint8_t operator[](size_t pos) const { return labels_[pos]; }

  void push(uint8_t label) { labels_[size_++] = label; }
  void pop() { --size_; }
  const uint8_t* begin() const { return labels_; }
  const uint8_t* end() const { return labels_ + size_; }
  size_t size() const { return size_; }
  void clear() { size_ = 0; }

  Edge(const Edge&) = delete;
  Edge& operator=(const Edge&) = delete;

private:
  uint8_t labels_[256] = {};
  size_t size_ = 0;
};

namespace utils {

inline bool match(const char* lhs, const char* rhs, uint32_t& len) {
  len = 0;
  while (lhs[len] != '\0') {
    if (lhs[len] != rhs[len]) {
      return false;
    }
    ++len;
  }
  if (rhs[len] != '\0') {
    return false;
  }
  ++len;
  return true;
}

inline uint32_t length(const char* str) {
  return static_cast<uint32_t>(std::strlen(str)) + 1;
}

inline uint32_t extract_value(const char* str) {
  uint32_t value = 0;
  std::memcpy(&value, str, sizeof(uint32_t));
  return value;
}

template<class T>
inline size_t size_in_bytes(const std::vector<T>& vec) {
  return vec.size() * sizeof(T) + sizeof(vec.size());
}

template<class T>
inline void write_value(const T val, std::ostream& os) {
  os.write(reinterpret_cast<const char*>(&val), sizeof(val));
}

template<class T>
inline void write_vector(const std::vector<T>& vec, std::ostream& os) {
  auto size = vec.size();
  write_value(size, os);
  os.write(reinterpret_cast<const char*>(&vec[0]), sizeof(T) * size);
}

template<class T>
inline void read_value(T& val, std::istream& is) {
  is.read(reinterpret_cast<char*>(&val), sizeof(val));
}

template<class T>
inline void read_vector(std::vector<T>& vec, std::istream& is) {
  vec.clear();
  size_t size = 0;
  read_value(size, is);
  vec.resize(size);
  is.read(reinterpret_cast<char*>(&vec[0]), sizeof(T) * size);
}

};

} // namespace -- ddd

#endif // DDD_BASIC_HPP
