#ifndef DDD_DICTIONARY_HPP
#define DDD_DICTIONARY_HPP

#include "DaTrie.hpp"

namespace ddd {

class Dictionary {
public:
  virtual ~Dictionary() {}

  virtual std::string name() const = 0;

  virtual uint32_t search_key(const char* key) const = 0;
  virtual bool insert_key(const char* key, uint32_t value) = 0;
  virtual uint32_t delete_key(const char* key) = 0;
  virtual void enumerate(std::vector<KvPair>& kvs) const = 0;

  virtual void pack() = 0;
  virtual void rebuild() = 0;
  virtual void shrink() = 0;

  virtual void stat(Stat& ret) const = 0;
  virtual double ratio_singles() const = 0; // not in constant time

  virtual void write(std::ostream& os) const = 0;
};

} // namespace -- ddd

#endif // DDD_DICTIONARY_HPP
