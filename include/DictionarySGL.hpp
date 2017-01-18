#ifndef DDD_DICTIONARY_SGL_HPP
#define DDD_DICTIONARY_SGL_HPP

#include "Dictionary.hpp"

namespace ddd {

template<bool WithBLM, bool WithNLM>
class DictionarySGL : public Dictionary {
public:
  using TrieType = DaTrie<WithBLM, WithNLM, false>;

  std::string name() const {
    return "DictionarySGL";
  }

  DictionarySGL() {
    trie_ = make_unique<TrieType>();
  }

  DictionarySGL(std::istream& is) {
    trie_ = make_unique<TrieType>(is);
    utils::read_value(num_keys_, is);
  }

  ~DictionarySGL() {}

  uint32_t search_key(const char* key) const {
    Query agent(key);
    if (!trie_->search_key(agent)) {
      return NOT_FOUND;
    }
    return agent.value();
  }

  bool insert_key(const char* key, uint32_t value) {
    assert((value >> 31) == 0);

    Query query(key);
    query.set_value(value);
    if (!trie_->insert_key(query)) {
      return false;
    }
    ++num_keys_;
    return true;
  }

  uint32_t delete_key(const char* key) {
    Query query(key);
    if (!trie_->delete_key(query)) {
      return NOT_FOUND;
    }
    --num_keys_;
    return query.value();
  }

  void enumerate(std::vector<KvPair>& kvs) const {
    kvs.clear();
    if (trie_->is_empty()) {
      return;
    }
    kvs.reserve(num_keys_);
    trie_->enumerate(ROOT_POS, std::string(), kvs);
  }

  void pack() {
    trie_->pack_bc();
    trie_->pack_tail();
  }

  void rebuild() {
    trie_->rebuild();
  }

  void shrink() {
    trie_->shrink();
  }

  void stat(Stat& ret) const {
    ret.num_keys = num_keys_;
    ret.num_tries = 1;
    ret.num_nodes = trie_->num_nodes();
    ret.bc_size = trie_->bc_size();
    ret.bc_capa = trie_->bc_capa();
    ret.bc_emps = trie_->bc_emps();
    ret.tail_size = trie_->tail_size();
    ret.tail_capa = trie_->tail_capa();
    ret.tail_emps = trie_->tail_emps();
    ret.size_in_bytes = trie_->size_in_bytes() + sizeof(num_keys_);
  }

  double ratio_singles() const { // not in constant time
    return static_cast<double>(trie_->num_singles()) / trie_->num_nodes();
  }

  void write(std::ostream& os) const {
    trie_->write(os);
    utils::write_value(num_keys_, os);
  }

  DictionarySGL(const DictionarySGL&) = delete;
  DictionarySGL& operator=(const DictionarySGL&) = delete;

private:
  std::unique_ptr<TrieType> trie_;
  size_t num_keys_ = 0;
};

} // namespace -- ddd

#endif // DDD_DICTIONARY_SGL_HPP
