#ifndef DDD_DICTIONARY_MLT_HPP
#define DDD_DICTIONARY_MLT_HPP

#include <thread>
#include "Dictionary.hpp"

namespace ddd {

template<bool WithBLM, bool WithNLM>
class DictionaryMLT : public Dictionary {
public:
  using PrefixTrieType = DaTrie<WithBLM, WithNLM, true>;
  using SuffixTrieType = DaTrie<WithBLM, WithNLM, false>;

  std::string name() const {
    return "DictionaryMLT";
  }

  DictionaryMLT() {
    prefix_subtrie_ = make_unique<PrefixTrieType>();
  }

  DictionaryMLT(const std::vector<const char*>& prefixes) {
    prefix_subtrie_ = make_unique<PrefixTrieType>(prefixes);
  }

  DictionaryMLT(std::istream& is) {
    prefix_subtrie_ = make_unique<PrefixTrieType>(is);
    size_t num_suffixes = 0;
    utils::read_value(num_suffixes, is);
    suffix_subtries_.resize(num_suffixes);
    for (size_t i = 0; i < num_suffixes; ++i) {
      bool has_trie{};
      utils::read_value(has_trie, is);
      if (has_trie) {
        suffix_subtries_[i] = make_unique<SuffixTrieType>(is);
      }
    }
    utils::read_value(suffix_head_, is);
    utils::read_value(num_keys_, is);
  }

  ~DictionaryMLT() {}

  uint32_t search_key(const char* key) const {
    Query query(key);

    if (!prefix_subtrie_->search_prefix(query)) {
      return NOT_FOUND;
    }

    if (query.is_finished()) {
      return query.value();
    }

    query.set_node_pos(ROOT_POS);
    if (!suffix_subtries_[query.value()]->search_key(query)) {
      return NOT_FOUND;
    }
    return query.value();
  }

  bool insert_key(const char* key, uint32_t value) {
    assert((value >> 31) == 0);

    Query query(key);

    if (!prefix_subtrie_->search_prefix(query)) {
      if (*query.key() != '\0') {
        query.set_value(new_suffix_id_());
      } else {
        query.set_value(value);
      }

      prefix_subtrie_->insert_prefix_leaf(query);

      if (query.is_finished()) {
        ++num_keys_;
        return true;
      }
    }

    auto suffix_id = query.value();
    query.set_node_pos(ROOT_POS);
    query.set_value(value);

    if (!suffix_subtries_[suffix_id]->insert_key(query)) {
      return false;
    }

    ++num_keys_;
    return true;
  }

  uint32_t delete_key(const char* key) {
    Query query(key);

    if (!prefix_subtrie_->search_prefix(query)) {
      return NOT_FOUND;
    }

    if (query.is_finished()) {
      prefix_subtrie_->delete_prefix_leaf(query);
      --num_keys_;
      return query.value();
    }

    auto leaf_pos = query.node_pos();
    auto suffix_id = query.value();

    query.set_node_pos(ROOT_POS);
    if (!suffix_subtries_[suffix_id]->delete_key(query)) {
      return NOT_FOUND;
    }

    if (suffix_subtries_[suffix_id]->is_empty()) { // update suffix link
      query.set_node_pos(leaf_pos);
      prefix_subtrie_->delete_prefix_leaf(query);
      suffix_subtries_[suffix_id].reset();

      if (suffix_id + 1 == suffix_subtries_.size()) {
        suffix_subtries_.pop_back();
      } else if (suffix_id < suffix_head_) {
        suffix_head_ = suffix_id;
      }
    }

    --num_keys_;
    return query.value();
  }

  void enumerate(std::vector<KvPair>& kvs) const {
    kvs.clear();
    if (prefix_subtrie_->is_empty()) {
      return;
    }
    kvs.reserve(num_keys_);

    std::vector<KvPair> prefix_kvs;
    prefix_subtrie_->enumerate_prefix(ROOT_POS, std::string{}, prefix_kvs);

    for (const auto& prefix_kv : prefix_kvs) {
      if ((prefix_kv.value >> 31) == 1) { // is terminal
        kvs.push_back(KvPair{prefix_kv.key, prefix_kv.value & (1U << 31)});
      } else {
        suffix_subtries_[prefix_kv.value]->enumerate(ROOT_POS, prefix_kv.key, kvs);
      }
    }
  }

  void pack() {
    auto func = [&](uint32_t id) {
      if (suffix_subtries_[id]) {
        suffix_subtries_[id]->pack_bc();
        suffix_subtries_[id]->pack_tail();
      }
    };

    std::vector<std::thread> threads;
    threads.resize(suffix_subtries_.size());
    for (uint32_t i = 0; i < suffix_subtries_.size(); ++i) {
      threads[i] = std::thread(func, i);
    }
    for (auto& th : threads) {
      th.join();
    }
  }

  void rebuild() {
    auto func = [&](uint32_t id) {
      if (suffix_subtries_[id]) {
        suffix_subtries_[id]->rebuild();
      }
    };

    std::vector<std::thread> threads;
    threads.resize(suffix_subtries_.size());
    for (uint32_t i = 0; i < suffix_subtries_.size(); ++i) {
      threads[i] = std::thread(func, i);
    }
    for (auto& th : threads) {
      th.join();
    }
  }

  void shrink() {
    for (size_t i = 0; i < suffix_subtries_.size(); ++i) {
      if (suffix_subtries_[i]) {
        suffix_subtries_[i]->shrink();
      }
    }
  }

  void stat(Stat& ret) const {
    ret.num_keys = num_keys_;
    ret.num_tries = 1;
    ret.num_nodes = prefix_subtrie_->num_nodes();
    ret.bc_size = prefix_subtrie_->bc_size();
    ret.bc_capa = prefix_subtrie_->bc_capa();
    ret.bc_emps = prefix_subtrie_->bc_emps();
    ret.tail_size = prefix_subtrie_->tail_size();
    ret.tail_capa = prefix_subtrie_->tail_capa();
    ret.tail_emps = prefix_subtrie_->tail_emps();
    ret.size_in_bytes = prefix_subtrie_->size_in_bytes();

    for (size_t i = 0; i < suffix_subtries_.size(); ++i) {
      auto& subtrie = suffix_subtries_[i];
      if (subtrie) {
        ret.num_nodes += subtrie->num_nodes();
        ret.bc_size += subtrie->bc_size();
        ret.bc_capa += subtrie->bc_capa();
        ret.bc_emps += subtrie->bc_emps();
        ret.tail_size += subtrie->tail_size();
        ret.tail_capa += subtrie->tail_capa();
        ret.tail_emps += subtrie->tail_emps();
        ret.size_in_bytes += subtrie->size_in_bytes();
        ++ret.num_tries;
      }
      ret.size_in_bytes += sizeof(bool);
    }

    ret.size_in_bytes += sizeof(suffix_subtries_.size());
    ret.size_in_bytes += sizeof(suffix_head_);
    ret.size_in_bytes += sizeof(num_keys_);
  }

  double ratio_singles() const { // not in constant time
    size_t num_singles = prefix_subtrie_->num_singles();
    size_t num_nodes = prefix_subtrie_->num_nodes();
    for (auto &subtrie : suffix_subtries_) {
      if (!subtrie) {
        continue;
      }
      num_singles += subtrie->num_singles();
      num_nodes += subtrie->num_nodes();
    }
    return static_cast<double>(num_singles) / num_nodes;
  }

  void write(std::ostream& os) const {
    prefix_subtrie_->write(os);
    auto num_suffixes = suffix_subtries_.size();
    utils::write_value(num_suffixes, os);
    for (size_t i = 0; i < num_suffixes; ++i) {
      if (suffix_subtries_[i]) {
        utils::write_value(true, os);
        suffix_subtries_[i]->write(os);
      } else {
        utils::write_value(false, os);
      }
    }
    utils::write_value(suffix_head_, os);
    utils::write_value(num_keys_, os);
  }

  DictionaryMLT(const DictionaryMLT&) = delete;
  DictionaryMLT& operator=(const DictionaryMLT&) = delete;

private:
  std::unique_ptr<PrefixTrieType> prefix_subtrie_{};
  std::vector<std::unique_ptr<SuffixTrieType>> suffix_subtries_{};
  uint32_t suffix_head_ = NOT_FOUND; // empty head in suffix_subtries_
  size_t num_keys_ = 0;

  uint32_t new_suffix_id_() {
    if (suffix_head_ == NOT_FOUND) {
      auto suffix_id = static_cast<uint32_t>(suffix_subtries_.size());
      suffix_subtries_.push_back(make_unique<SuffixTrieType>());
      return suffix_id;
    }

    auto suffix_id = suffix_head_;
    suffix_subtries_[suffix_id] = make_unique<SuffixTrieType>();

    for (auto i = suffix_head_ + 1; i < suffix_subtries_.size(); ++i) {
      if (!suffix_subtries_[i]) {
        suffix_head_ = i;
        break;
      }
    }
    if (suffix_head_ == suffix_id) {
      suffix_head_ = NOT_FOUND;
    }
    return suffix_id;
  }
};

} // namespace -- ddd

#endif // DDD_DICTIONARY_MLT_HPP
