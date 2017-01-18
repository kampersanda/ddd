#ifndef DDD_DA_TRIE_HPP
#define DDD_DA_TRIE_HPP

#include <algorithm>
#include <cassert>

#include "Basic.hpp"

namespace ddd {

template<bool WithBLM, bool WithNLM, bool Prefix>
class DaTrie {
public:
  using BlockType = typename std::conditional<WithBLM, BlockLink, Block>::type;

  DaTrie() {
    if (Prefix) {
      fix_(ROOT_POS, blocks_);
      bc_[ROOT_POS].set_base(INVALID_VALUE);
      bc_[ROOT_POS].set_check(INVALID_VALUE);
    }
  }

  DaTrie(const std::vector<const char*>& prefixes) {
    assert(Prefix);

    fix_(ROOT_POS, blocks_);
    bc_[ROOT_POS].set_base(INVALID_VALUE);
    bc_[ROOT_POS].set_check(INVALID_VALUE);

    for (auto prefix : prefixes) {
      Query query(prefix);
      search_prefix(query);
      if (*query.key() == '\0') {
        continue;
      }
      if (bc_[query.node_pos()].base() != INVALID_VALUE) {
        insert_edge_(query);
      }
      while (*query.key() != '\0') {
        append_edge_(query);
      }
      bc_[query.node_pos()].set_base(INVALID_VALUE);
    }
  }

  DaTrie(std::istream& is) {
    utils::read_vector(bc_, is);
    utils::read_vector(tail_, is);
    utils::read_vector(blocks_, is);
    utils::read_vector(node_links_, is);
    utils::read_value(head_pos_, is);
    utils::read_value(bc_emps_, is);
    utils::read_value(tail_emps_, is);
  }

  ~DaTrie() {}

  bool search_key(Query& query) const {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());
    assert(!Prefix);

    while (!bc_[query.node_pos()].is_leaf()) {
      auto child_pos = bc_[query.node_pos()].base() ^query.label();
      if (bc_[child_pos].check() != query.node_pos()) {
        return false;
      }
      query.next(child_pos);
    }

    auto value = bc_[query.node_pos()].value();
    if (query.is_finished()) {
      query.set_value(value);
      return true;
    }

    uint32_t len = 0;
    auto tail = tail_.data() + value;
    if (!utils::match(query.key(), tail, len)) {
      return false;
    }
    query.set_value(utils::extract_value(tail + len));
    return true;
  }

  bool insert_key(Query& query) {
    assert(!Prefix);

    if (bc_.empty()) { // first insert
      fix_(ROOT_POS, blocks_);
      bc_[ROOT_POS].set_check(INVALID_VALUE);
      insert_tail_(query);
      return true;
    }

    if (search_key(query)) {
      return false;
    }

    bc_[query.node_pos()].is_leaf() ? insert_branch_(query) : insert_edge_(query);
    insert_tail_(query);
    return true;
  }

  bool delete_key(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());
    assert(!Prefix);

    if (!search_key(query)) {
      return false;
    }

    if (query.node_pos() == ROOT_POS) {
      DaTrie().swap(*this);
      return true;
    }

    if (WithNLM) {
      delete_sib_(query.node_pos());
    }

    if (!is_terminal_(query.node_pos())) {
      auto tail_pos = bc_[query.node_pos()].value();
      tail_emps_ += utils::length(tail_.data() + tail_pos) + sizeof(uint32_t);
    }

    auto parent_pos = bc_[query.node_pos()].check();
    unfix_(query.node_pos(), blocks_);
    query.prev(parent_pos);

    change_branch_(query);
    return true;
  }

  void enumerate(uint32_t node_pos, const std::string& prefix, std::vector<KvPair>& kvs) const {
    assert(node_pos < bc_.size());
    assert(bc_[node_pos].is_fixed());
    assert(!Prefix);

    if (bc_[node_pos].is_leaf()) {
      KvPair kv;
      kv.key = prefix;
      if (is_terminal_(node_pos)) {
        kv.value = bc_[node_pos].value();
      } else {
        auto tail_pos = bc_[node_pos].value();
        while (tail_[tail_pos] != '\0') {
          kv.key += tail_[tail_pos++];
        }
        kv.key += tail_[tail_pos++];
        kv.value = utils::extract_value(tail_.data() + tail_pos);
      }
      kvs.push_back(kv);
      return;
    }

    auto base = bc_[node_pos].base();
    auto child_pos = base ^static_cast<uint8_t>('\0');
    if (bc_[child_pos].check() == node_pos) {
      enumerate(child_pos, prefix, kvs);
    }

    for (uint32_t label = 1; label < 256; ++label) {
      child_pos = base ^ label;
      if (bc_[child_pos].check() == node_pos) {
        enumerate(child_pos, prefix + static_cast<char>(label), kvs);
      }
    }
  }

  void pack_bc() {
    assert(!Prefix);

    Query query;
    Edge edge;

    while (BLOCK_SIZE <= bc_emps()) {
      auto max_pos = bc_size();
      for (uint32_t i = 0; i < BLOCK_SIZE; ++i) {
        if (bc_[--max_pos].is_fixed()) {
          break;
        }
      }
      assert(bc_[max_pos].is_fixed());

      query.set_node_pos(bc_[max_pos].check());
      edge_(query.node_pos(), edge);

      auto base = excheck_(edge, blocks_);
      if (base == NOT_FOUND) {
        break;
      }

      shelter_(base, edge, query);
      move_(query.node_pos(), base, edge, query);
    }
  }

  void pack_tail() {
    assert(!Prefix);

    std::vector<char> tail;
    tail.reserve(tail_.size() - tail_emps_);

    tail_.swap(tail);

    for (uint32_t node_pos = 0; node_pos < bc_size(); ++node_pos) {
      if (bc_[node_pos].is_leaf() && !is_terminal_(node_pos)) {
        auto _tail = tail.data() + bc_[node_pos].value();
        Query query(_tail);
        query.set_value(utils::extract_value(_tail + utils::length(_tail)));
        query.set_node_pos(node_pos);
        insert_tail_(query);
      }
    }
    tail_emps_ = 0;
  }

  void rebuild() {
    assert(!Prefix);

    DaTrie new_trie;

    const auto bc_capa = num_nodes() / 256 * 256 + 1024; // expecting avoidance of reallocation
    new_trie.bc_.reserve(bc_capa);
    new_trie.tail_.reserve(tail_.size() - tail_emps_);
    new_trie.blocks_.reserve(bc_capa / 256);
    if (WithNLM) {
      new_trie.node_links_.reserve(bc_capa);
    }

    rebuild_(new_trie);
    swap(new_trie);
  }

  void shrink() {
    bc_.shrink_to_fit();
    tail_.shrink_to_fit();
    blocks_.shrink_to_fit();
    if (WithNLM) {
      node_links_.shrink_to_fit();
    }
  }

  // for prefix trie
  bool search_prefix(Query& query) const {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());
    assert(Prefix);

    while (!bc_[query.node_pos()].is_leaf()) {
      auto base = bc_[query.node_pos()].base();
      if (base == INVALID_VALUE) {
        return false;
      }
      auto child_pos = base ^query.label();
      if (bc_[child_pos].check() != query.node_pos()) {
        return false;
      }
      query.next(child_pos);
    }

    query.set_value(bc_[query.node_pos()].value());
    return true;
  }

  // for prefix trie
  void insert_prefix_leaf(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(Prefix);

    if (bc_[query.node_pos()].base() != INVALID_VALUE) {
      insert_edge_(query);
    } else {
      append_edge_(query);
    }
    bc_[query.node_pos()].set_value(query.value());
  }

  // for prefix trie
  void delete_prefix_leaf(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_leaf());
    assert(Prefix);

    auto parent_pos = bc_[query.node_pos()].check();
    auto edge_size = edge_size_(parent_pos, 2);
    assert(edge_size != 0);

    if (WithNLM) {
      delete_sib_(query.node_pos());
    }

    unfix_(query.node_pos(), blocks_);
    if (edge_size == 1) {
      bc_[parent_pos].set_base(INVALID_VALUE);
    }
  }

  // for prefix trie
  void enumerate_prefix(uint32_t node_pos, const std::string& prefix,
                        std::vector<KvPair>& kvs) const {
    assert(node_pos < bc_.size());
    assert(bc_[node_pos].is_fixed());
    assert(Prefix);

    if (bc_[node_pos].is_leaf()) {
      auto value = bc_[node_pos].value();
      if (is_terminal_(node_pos)) {
        value |= (1U << 31);
      }
      kvs.push_back(KvPair{prefix, value});
      return;
    }

    auto base = bc_[node_pos].base();
    auto child_pos = base ^static_cast<uint8_t>('\0');
    if (bc_[child_pos].check() == node_pos) {
      enumerate_prefix(child_pos, prefix, kvs);
    }

    for (uint32_t label = 1; label < 256; ++label) {
      child_pos = base ^ label;
      if (bc_[child_pos].check() == node_pos) {
        enumerate_prefix(child_pos, prefix + static_cast<char>(label), kvs);
      }
    }
  }

  bool is_empty() const {
    return bc_.empty();
  }

  uint32_t num_nodes() const {
    return bc_size() - bc_emps();
  };

  uint32_t num_singles() const { // not in constant time
    uint32_t ret = 0;
    for (uint32_t i = 0; i < bc_size(); ++i) {
      if (!bc_[i].is_fixed()) {
        continue;
      }
      if (edge_size_(i, 2) == 1) {
        ++ret;
      }
    }
    return ret;
  }

  uint32_t num_blocks() const {
    return static_cast<uint32_t>(blocks_.size());
  }

  uint32_t bc_size() const {
    return static_cast<uint32_t>(bc_.size());
  }

  uint32_t bc_capa() const {
    return static_cast<uint32_t>(bc_.capacity());
  }

  uint32_t bc_emps() const {
    return bc_emps_;
  }

  uint32_t tail_size() const {
    return static_cast<uint32_t>(tail_.size());
  }

  uint32_t tail_capa() const {
    return static_cast<uint32_t>(tail_.capacity());
  }

  uint32_t tail_emps() const {
    return tail_emps_;
  }

  size_t size_in_bytes() const {
    size_t size = 0;
    size += utils::size_in_bytes(bc_);
    size += utils::size_in_bytes(tail_);
    size += utils::size_in_bytes(blocks_);
    size += utils::size_in_bytes(node_links_);
    size += sizeof(head_pos_);
    size += sizeof(bc_emps_);
    size += sizeof(tail_emps_);
    return size;
  }

  void write(std::ostream& os) const {
    utils::write_vector(bc_, os);
    utils::write_vector(tail_, os);
    utils::write_vector(blocks_, os);
    utils::write_vector(node_links_, os);
    utils::write_value(head_pos_, os);
    utils::write_value(bc_emps_, os);
    utils::write_value(tail_emps_, os);
  }

  void swap(DaTrie& rhs) {
    bc_.swap(rhs.bc_);
    tail_.swap(rhs.tail_);
    blocks_.swap(rhs.blocks_);
    node_links_.swap(rhs.node_links_);
    std::swap(head_pos_, rhs.head_pos_);
    std::swap(bc_emps_, rhs.bc_emps_);
    std::swap(tail_emps_, rhs.tail_emps_);
  }

  DaTrie(const DaTrie&) = delete;
  DaTrie& operator=(const DaTrie&) = delete;

protected:
  std::vector<Bc> bc_;
  std::vector<char> tail_;
  std::vector<BlockType> blocks_;
  std::vector<NodeLink> node_links_;

  uint32_t head_pos_ = NOT_FOUND;
  uint32_t bc_emps_ = 0; // in bc_
  uint32_t tail_emps_ = 0; // in tail_

  bool is_terminal_(uint32_t node_pos) const {
    if (!bc_[node_pos].is_leaf()) {
      return false;
    }
    if (node_pos == ROOT_POS) {
      return false;
    }
    return (bc_[bc_[node_pos].check()].base() ^ node_pos) == 0;
  }

  uint32_t next_(uint32_t pos) const {
    return bc_[pos].base();
  }

  uint32_t prev_(uint32_t pos) const {
    return bc_[pos].check();
  }

  void set_next_(uint32_t pos, uint32_t next) {
    bc_[pos].set_base(next);
  }

  void set_prev_(uint32_t pos, uint32_t prev) {
    bc_[pos].set_check(prev);
  }

  void insert_branch_(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_leaf());

    auto tail_pos = bc_[query.node_pos()].value();

    while (*query.key() == tail_[tail_pos]) {
      append_edge_(query);
      ++tail_pos;
      ++tail_emps_;
    }

    auto branch = static_cast<uint8_t>(tail_[tail_pos++]);
    ++tail_emps_;

    Edge edge;
    edge.push(branch);
    edge.push(query.label());

    auto base = xcheck_(edge, blocks_);
    bc_[query.node_pos()].set_base(base);

    auto child_pos = base ^branch;
    fix_(child_pos, blocks_);

    bc_[child_pos].set_check(query.node_pos());
    if (branch != '\0') {
      bc_[child_pos].set_value(tail_pos);
    } else {
      uint32_t value = 0;
      std::memcpy(&value, tail_.data() + tail_pos, sizeof(uint32_t));
      bc_[child_pos].set_value(value);
      tail_emps_ += sizeof(uint32_t);
    }

    if (WithNLM) {
      node_links_[query.node_pos()].child = branch;
      node_links_[child_pos].sib = branch;
    }
    insert_edge_(query);
  }

  void insert_edge_(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());

    auto child_pos = bc_[query.node_pos()].base() ^query.label();

    if (bc_[child_pos].is_fixed()) {
      solve_(query);
      child_pos = bc_[query.node_pos()].base() ^ query.label();
    }

    fix_(child_pos, blocks_);
    bc_[child_pos].set_check(query.node_pos());

    if (WithNLM) {
      auto _child_pos = bc_[query.node_pos()].base() ^node_links_[query.node_pos()].child;
      node_links_[child_pos].sib = node_links_[_child_pos].sib;
      node_links_[_child_pos].sib = query.label();
    }
    query.next(child_pos);
  }

  void append_edge_(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());

    auto base = xcheck_(query.label(), blocks_);
    auto child_pos = base ^query.label();

    fix_(child_pos, blocks_);
    bc_[query.node_pos()].set_base(base);
    bc_[child_pos].set_check(query.node_pos());

    if (WithNLM) {
      node_links_[query.node_pos()].child = query.label();
      node_links_[child_pos].sib = query.label();
    }
    query.next(child_pos);
  }

  void insert_tail_(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());

    if (query.is_finished()) {
      bc_[query.node_pos()].set_value(query.value());
      return;
    }

    auto tail_pos = tail_size();
    bc_[query.node_pos()].set_value(tail_pos);

    while (!query.is_finished()) {
      tail_.push_back(query.label());
      query.next();
    }

    auto value = query.value();
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
      tail_.push_back(static_cast<uint8_t>(value & 0xFF));
      value >>= 8;
    }
  }

  void delete_sib_(uint32_t node_pos) {
    assert(node_pos < bc_.size());
    assert(bc_[node_pos].is_fixed());

    auto parent_pos = bc_[node_pos].check();
    auto base = bc_[parent_pos].base();
    auto label = static_cast<uint8_t>(base ^ node_pos);

    auto _node_pos = base ^node_links_[parent_pos].child;
    while (node_links_[_node_pos].sib != label) {
      _node_pos = base ^ node_links_[_node_pos].sib;
    }

    if (node_links_[parent_pos].child == node_links_[_node_pos].sib) {
      node_links_[parent_pos].child = node_links_[node_pos].sib;
    }
    node_links_[_node_pos].sib = node_links_[node_pos].sib;
  }

  void change_branch_(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());

    Edge edge;
    edge_(query.node_pos(), edge, 2);

    if (edge.size() != 1) {
      return;
    }

    auto child_pos = bc_[query.node_pos()].base() ^*edge.begin();
    if (!bc_[child_pos].is_leaf()) {
      return;
    }

    auto value = bc_[child_pos].value();
    unfix_(child_pos, blocks_);

    size_t num_regress = 0;
    while (query.node_pos() != ROOT_POS) {
      auto parent_pos = bc_[query.node_pos()].check();
      if (edge_size_(parent_pos, 2) != 1) {
        break;
      }
      if (WithNLM) {
        delete_sib_(query.node_pos());
      }
      unfix_(query.node_pos(), blocks_);
      query.prev(parent_pos);
      ++num_regress;
    }

    bc_[query.node_pos()].set_value(tail_size());
    while (0 < num_regress--) {
      tail_.push_back(*query.key());
      query.next();
    }
    tail_.push_back(*edge.begin());

    if (*edge.begin() != '\0') {
      while (tail_[value] != '\0') {
        tail_.push_back(tail_[value++]);
        ++tail_emps_;
      }
      tail_.push_back(tail_[value++]);
      ++tail_emps_;
      for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        tail_.push_back(tail_[value++]);
        ++tail_emps_;
      }
    } else {
      for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        tail_.push_back(static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
      }
    }
  }

  void rebuild_(DaTrie& rhs_trie) const {
    assert(rhs_trie.is_empty());

    if (is_empty()) {
      return;
    }

    using NodePair = std::pair<uint32_t, uint32_t>;

    std::vector<NodePair> np_stack;
    np_stack.reserve(num_nodes());
    np_stack.push_back({ROOT_POS, ROOT_POS});

    rhs_trie.fix_(ROOT_POS, rhs_trie.blocks_);
    rhs_trie.bc_[ROOT_POS].set_check(INVALID_VALUE);

    while (!np_stack.empty()) {
      const NodePair node_pair = np_stack.back();
      np_stack.pop_back();

      if (WithNLM) {
        rhs_trie.node_links_[node_pair.second] = node_links_[node_pair.first];
      }

      if (bc_[node_pair.first].is_leaf()) {
        if (is_terminal_(node_pair.first)) {
          rhs_trie.bc_[node_pair.second].set_value(bc_[node_pair.first].value());
        } else {
          auto tail = tail_.data() + bc_[node_pair.first].value();
          Query query(tail);
          query.set_value(utils::extract_value(tail + utils::length(tail)));
          query.set_node_pos(node_pair.second);
          rhs_trie.insert_tail_(query);
        }
        continue;
      }

      Edge edge;
      edge_(node_pair.first, edge);

      auto rhs_base = rhs_trie.xcheck_(edge, rhs_trie.blocks_);
      rhs_trie.bc_[node_pair.second].set_base(rhs_base);

      for (auto label : edge) {
        auto rhs_child_pos = rhs_base ^label;
        rhs_trie.fix_(rhs_child_pos, rhs_trie.blocks_);
        rhs_trie.bc_[rhs_child_pos].set_check(node_pair.second);
        np_stack.push_back({bc_[node_pair.first].base() ^ label, rhs_child_pos});
      }
    }
  }

  void solve_(Query& query) {
    assert(query.node_pos() < bc_.size());
    assert(bc_[query.node_pos()].is_fixed());

    Edge edges[2] = {};
    edge_(query.node_pos(), edges[0]);

    uint32_t _node_pos = 0;
    auto child_pos = bc_[query.node_pos()].base() ^query.label();

    if (child_pos != 0) {
      _node_pos = bc_[child_pos].check();
      edge_(_node_pos, edges[1]);
    }

    if (child_pos == 0 || edges[0].size() < edges[1].size()) {
      edges[0].push(query.label());
      auto base = xcheck_(edges[0], blocks_);
      edges[0].pop();
      move_(query.node_pos(), base, edges[0], query);
    } else {
      auto base = xcheck_(edges[1], blocks_);
      move_(_node_pos, base, edges[1], query);
    }
  }

  void shelter_(uint32_t base, const Edge& edge, Query& query) {
    Edge _edge;
    auto ng_block = base / BLOCK_SIZE;

    for (auto label : edge) {
      auto child_pos = base ^label;
      if (bc_[child_pos].is_fixed()) {
        auto node_pos = bc_[child_pos].check();
        edge_(node_pos, _edge);

        auto _base = xcheck_(_edge, ng_block, blocks_);
        move_(node_pos, _base, _edge, query);
      }
    }
  }

  void move_(uint32_t node_pos, uint32_t base, const Edge& edge, Query& query) {
    assert(node_pos < bc_.size());
    assert(bc_[node_pos].is_fixed());
    assert(0 < edge.size());

    auto orig_base = bc_[node_pos].base();

    for (auto label : edge) {
      auto src_node_pos = orig_base ^label;
      auto dst_node_pos = base ^label;

      fix_(dst_node_pos, blocks_);
      bc_[dst_node_pos] = bc_[src_node_pos];
      if (WithNLM) {
        node_links_[dst_node_pos] = node_links_[src_node_pos];
      }

      Edge src_edge;
      edge_(src_node_pos, src_edge);

      auto src_base = bc_[src_node_pos].base();
      for (auto src_label : src_edge) {
        auto src_child_pos = src_base ^src_label;
        bc_[src_child_pos].set_check(dst_node_pos);
      }

      unfix_(src_node_pos, blocks_);

      if (src_node_pos == query.node_pos()) {
        query.set_node_pos(dst_node_pos);
      }
    }

    bc_[node_pos].set_base(base);
  }

  uint32_t xcheck_(uint8_t label, const std::vector<Block>& blocks) const {
    return head_pos_ == NOT_FOUND ? bc_size() ^ label : head_pos_ ^ label;
  }

  uint32_t xcheck_(const Edge& edge, const std::vector<Block>& blocks) const {
    if (edge.size() == 1) {
      return xcheck_(*edge.begin(), blocks);
    }

    if (head_pos_ == NOT_FOUND) {
      return bc_size() ^ *edge.begin();
    }

    auto node_pos = head_pos_;
    do {
      if (blocks[node_pos / BLOCK_SIZE].num_emps < edge.size()) {
        continue;
      }

      auto base = node_pos ^*edge.begin();
      if (is_target_(base, edge)) {
        return base;
      }
    } while ((node_pos = next_(node_pos)) != head_pos_);

    return bc_size() ^ *edge.begin();
  }

  uint32_t xcheck_(const Edge& edge, const uint32_t ng_block,
                   const std::vector<Block>& blocks) const {
    if (head_pos_ == NOT_FOUND) {
      return bc_size() ^ *edge.begin();
    }

    auto node_pos = head_pos_;
    do {
      if (node_pos / BLOCK_SIZE == ng_block) {
        continue;
      }
      if (blocks[node_pos / BLOCK_SIZE].num_emps < edge.size()) {
        continue;
      }

      auto base = node_pos ^*edge.begin();
      if (is_target_(base, edge)) {
        return base;
      }
    } while ((node_pos = next_(node_pos)) != head_pos_);

    return bc_size() ^ *edge.begin();
  }

  uint32_t excheck_(const Edge& edge, const std::vector<Block>& blocks) {
    if (head_pos_ == NOT_FOUND) {
      return NOT_FOUND;
    }

    auto upper_limit = bc_size() - BLOCK_SIZE;
    auto node_pos = head_pos_;

    do {
      if (upper_limit <= node_pos) {
        continue;
      }
      auto base = node_pos ^*edge.begin();
      if (is_target_ex_(base, edge)) {
        head_pos_ = node_pos; // update for remaining
        return base;
      }
    } while ((node_pos = next_(node_pos)) != head_pos_);

    return NOT_FOUND;
  }

  uint32_t xcheck_(uint8_t label, const std::vector<BlockLink>& blocks) const {
    return head_pos_ == NOT_FOUND ? bc_size() ^ label : blocks[head_pos_].head ^ label;
  }

  uint32_t xcheck_(const Edge& edge, const std::vector<BlockLink>& blocks) const {
    assert(0 < edge.size());

    if (edge.size() == 1) {
      return xcheck_(*edge.begin(), blocks);
    }

    if (head_pos_ == NOT_FOUND) {
      return bc_size() ^ *edge.begin();
    }

    auto block_pos = head_pos_;
    do {
      auto base = xcheck_in_block_(edge, block_pos, blocks);
      if (base != NOT_FOUND) {
        return base;
      }
    } while ((block_pos = blocks[block_pos].next) != head_pos_);

    return bc_size() ^ *edge.begin();
  }

  uint32_t xcheck_(const Edge& edge, const uint32_t ng_block,
                   const std::vector<BlockLink>& blocks) const {
    assert(0 < edge.size());

    if (head_pos_ == NOT_FOUND) {
      return bc_size() ^ *edge.begin();
    }

    auto block_pos = head_pos_;
    do {
      if (block_pos == ng_block) {
        continue;
      }
      auto base = xcheck_in_block_(edge, block_pos, blocks);
      if (base != NOT_FOUND) {
        return base;
      }
    } while ((block_pos = blocks[block_pos].next) != head_pos_);

    return bc_size() ^ *edge.begin();
  }

  uint32_t xcheck_in_block_(const Edge& edge, uint32_t block_pos,
                            const std::vector<BlockLink>& blocks) const {
    assert(0 < edge.size());

    if (blocks[block_pos].num_emps < edge.size()) {
      return NOT_FOUND;
    }

    auto head = blocks[block_pos].head;
    auto node_pos = head;

    do {
      auto base = node_pos ^*edge.begin();
      if (is_target_(base, edge)) {
        return base;
      }
    } while ((node_pos = next_(node_pos)) != head);

    return NOT_FOUND;
  }

  uint32_t excheck_(const Edge& edge, const std::vector<BlockLink>& blocks) {
    assert(0 < edge.size());

    if (head_pos_ == NOT_FOUND) {
      return NOT_FOUND;
    }

    auto block_pos = head_pos_;
    auto last_block = blocks.size() - 1;

    do {
      if (last_block == block_pos) {
        continue;
      }
      auto base = excheck_in_block_(edge, block_pos, blocks);
      if (base != NOT_FOUND) {
        head_pos_ = block_pos; // update for remaining
        return base;
      }
    } while ((block_pos = blocks[block_pos].next) != head_pos_);

    return NOT_FOUND;
  }

  uint32_t excheck_in_block_(const Edge& edge, uint32_t block_pos,
                             const std::vector<BlockLink>& blocks) const {
    assert(0 < edge.size());

    auto head = blocks[block_pos].head;
    auto node_pos = head;

    do {
      auto base = node_pos ^*edge.begin();
      if (is_target_ex_(base, edge)) {
        return base;
      }
    } while ((node_pos = next_(node_pos)) != head);

    return NOT_FOUND;
  }

  bool is_target_(uint32_t base, const Edge& edge) const {
    assert(0 < edge.size());

    for (auto label : edge) {
      auto child_pos = base ^label;
      if (bc_[child_pos].is_fixed()) {
        return false;
      }
    }
    return true;
  }

  bool is_target_ex_(uint32_t base, const Edge& edge) const {
    assert(0 < edge.size());

    for (auto label : edge) {
      auto child_pos = base ^label;
      if (child_pos == ROOT_POS) {
        return false;
      }
      if (bc_[child_pos].is_fixed()) {
        auto node_pos = bc_[child_pos].check();
        if (edge.size() <= edge_size_(node_pos, edge.size())) {
          return false;
        }
      }
    }
    return true;
  }

  void edge_(uint32_t node_pos, Edge& edge, size_t upper = 256) const {
    assert(bc_[node_pos].is_fixed());

    edge.clear();
    if (bc_[node_pos].is_leaf()) {
      return;
    }

    auto base = bc_[node_pos].base();
    if (base == INVALID_VALUE) { // for prefix subtrie
      return;
    }

    if (WithNLM) {
      edge.push(node_links_[node_pos].child);
      auto child_pos = base ^node_links_[node_pos].child;
      assert(bc_[child_pos].check() == node_pos);
      while (edge.size() < upper && node_links_[child_pos].sib != node_links_[node_pos].child) {
        edge.push(node_links_[child_pos].sib);
        child_pos = base ^ node_links_[child_pos].sib;
        assert(bc_[child_pos].check() == node_pos);
      }
    } else {
      for (uint32_t label = 0; label < 256; ++label) {
        auto child_pos = base ^label;
        if (bc_[child_pos].check() == node_pos) {
          edge.push(static_cast<uint8_t>(label));
          if (edge.size() == upper) {
            break;
          }
        }
      }
    }
  }

  size_t edge_size_(uint32_t node_pos, size_t upper = 256) const {
    assert(bc_[node_pos].is_fixed());

    if (bc_[node_pos].is_leaf()) {
      return 0;
    }

    auto base = bc_[node_pos].base();
    if (base == INVALID_VALUE) { // for prefix subtrie
      return 0;
    }

    size_t size = 0;
    if (WithNLM) {
      auto child_pos = base ^node_links_[node_pos].child;
      while (++size < upper && node_links_[child_pos].sib != node_links_[node_pos].child) {
        child_pos = base ^ node_links_[child_pos].sib;
      }
    } else {
      for (uint32_t label = 0; label < 256; ++label) {
        auto child_pos = base ^label;
        if (bc_[child_pos].check() == node_pos && ++size >= upper) {
          break;
        }
      }
    }
    return size;
  }

  void fix_(uint32_t node_pos, std::vector<Block>& blocks) {
    auto block_pos = node_pos / BLOCK_SIZE;
    while (num_blocks() <= block_pos) {
      push_block_();
    }

    assert(node_pos < bc_.size());
    assert(!bc_[node_pos].is_fixed());

    --bc_emps_;
    --blocks[block_pos].num_emps;

    if (bc_emps_ == 0) {
      head_pos_ = NOT_FOUND;
    } else {
      if (node_pos == head_pos_) {
        head_pos_ = next_(head_pos_);
      }
      auto next = next_(node_pos);
      auto prev = prev_(node_pos);
      set_next_(prev, next);
      set_prev_(next, prev);
    }
    bc_[node_pos].fix();
  }

  void fix_(uint32_t node_pos, std::vector<BlockLink>& blocks) {
    auto block_pos = node_pos / BLOCK_SIZE;
    while (num_blocks() <= block_pos) {
      push_block_();
    }

    assert(node_pos < bc_.size());
    assert(!bc_[node_pos].is_fixed());

    --bc_emps_;
    --blocks[block_pos].num_emps;

    if (blocks[block_pos].num_emps == 0) {
      delete_block_link_(block_pos, blocks_);
    } else {
      auto next = next_(node_pos);
      auto prev = prev_(node_pos);
      set_next_(prev, next);
      set_prev_(next, prev);
      if (node_pos == blocks[block_pos].head) {
        blocks[block_pos].head = next;
      }
    }
    bc_[node_pos].fix();
  }

  void unfix_(uint32_t node_pos, std::vector<Block>& blocks) {
    assert(node_pos < bc_.size());
    assert(bc_[node_pos].is_fixed());

    auto block_pos = node_pos / BLOCK_SIZE;

    if (bc_emps_ == 0) {
      set_next_(node_pos, node_pos);
      set_prev_(node_pos, node_pos);
      head_pos_ = node_pos;
    } else {
      set_prev_(node_pos, prev_(head_pos_));
      set_next_(node_pos, head_pos_);
      set_next_(prev_(head_pos_), node_pos);
      set_prev_(head_pos_, node_pos);
    }

    bc_[node_pos].unfix();

    ++bc_emps_;
    ++blocks[block_pos].num_emps;

    if (block_pos == num_blocks() - 1) {
      while (blocks[block_pos].num_emps == BLOCK_SIZE) {
        pop_block_();
        if (num_blocks() == 0) {
          break;
        }
        --block_pos;
      }
    }
  }

  void unfix_(uint32_t node_pos, std::vector<BlockLink>& blocks) {
    assert(node_pos < bc_.size());
    assert(bc_[node_pos].is_fixed());

    auto block_pos = node_pos / BLOCK_SIZE;

    if (blocks[block_pos].num_emps == 0) {
      set_next_(node_pos, node_pos);
      set_prev_(node_pos, node_pos);
      blocks[block_pos].head = node_pos;
      insert_block_link_(block_pos, blocks);
    } else {
      auto head = blocks[block_pos].head;
      set_prev_(node_pos, prev_(head));
      set_next_(node_pos, head);
      set_next_(prev_(head), node_pos);
      set_prev_(head, node_pos);
    }

    bc_[node_pos].unfix();

    ++bc_emps_;
    ++blocks[block_pos].num_emps;

    if (block_pos == num_blocks() - 1) {
      while (blocks[block_pos].num_emps == BLOCK_SIZE) {
        pop_block_();
        if (num_blocks() == 0) {
          break;
        }
        --block_pos;
      }
    }
  }

  void push_block_() {
    auto block_pos = num_blocks();

    for (uint32_t i = 0; i < BLOCK_SIZE; ++i) {
      bc_.push_back(Bc{});
    }
    if (WithNLM) {
      for (uint32_t i = 0; i < BLOCK_SIZE; ++i) {
        node_links_.push_back(NodeLink{});
      }
    }
    blocks_.push_back(BlockType{});

    auto begin = block_pos * BLOCK_SIZE;
    auto end = begin + BLOCK_SIZE;

    for (auto i = begin; i < end; ++i) {
      set_next_(i, i + 1);
      set_prev_(i, i - 1);
    }

    push_block_(block_pos, blocks_);
    bc_emps_ += BLOCK_SIZE;
  }

  void push_block_(uint32_t block_pos, std::vector<Block>& blocks) {
    auto begin = block_pos * BLOCK_SIZE;
    auto end = begin + BLOCK_SIZE;

    if (bc_emps_ != 0) {
      set_prev_(begin, prev_(head_pos_));
      set_next_(end - 1, head_pos_);
      set_next_(prev_(head_pos_), begin);
      set_prev_(head_pos_, end - 1);
    } else {
      set_next_(end - 1, begin);
      set_prev_(begin, end - 1);
      head_pos_ = begin;
    }
  }

  void push_block_(uint32_t block_pos, std::vector<BlockLink>& blocks) {
    auto begin = block_pos * BLOCK_SIZE;
    auto end = begin + BLOCK_SIZE;
    set_next_(end - 1, begin);
    set_prev_(begin, end - 1);

    blocks[block_pos].head = begin;
    insert_block_link_(block_pos, blocks);
  }

  void pop_block_() {
    assert(!bc_.empty());

    auto block_pos = num_blocks() - 1;
    pop_block_(block_pos, blocks_);

    for (uint32_t i = 0; i < BLOCK_SIZE; ++i) {
      bc_.pop_back();
    }
    if (WithNLM) {
      for (uint32_t i = 0; i < BLOCK_SIZE; ++i) {
        node_links_.pop_back();
      }
    }

    blocks_.pop_back();
    bc_emps_ -= BLOCK_SIZE;
  }

  void pop_block_(uint32_t block_pos, std::vector<Block>& blocks) {
    auto begin = block_pos * BLOCK_SIZE;
    auto end = begin + BLOCK_SIZE;

    for (auto pos = begin; pos < end; ++pos) {
      if (pos == head_pos_) {
        head_pos_ = next_(head_pos_);
      }
      auto next = next_(pos);
      auto prev = prev_(pos);
      set_next_(prev, next);
      set_prev_(next, prev);
    }
  }

  void pop_block_(uint32_t block_pos, std::vector<BlockLink>& blocks) {
    delete_block_link_(block_pos, blocks);
  }

  void insert_block_link_(uint32_t block_pos, std::vector<BlockLink>& blocks) {
    assert(block_pos < blocks.size());

    if (head_pos_ != NOT_FOUND) {
      auto tail_pos = blocks[head_pos_].prev;
      blocks[block_pos].prev = tail_pos;
      blocks[block_pos].next = head_pos_;
      blocks[tail_pos].next = block_pos;
      blocks[head_pos_].prev = block_pos;
    } else {
      blocks[block_pos].next = block_pos;
      blocks[block_pos].prev = block_pos;
      head_pos_ = block_pos;
    }
  }

  void delete_block_link_(uint32_t block_pos, std::vector<BlockLink>& blocks) {
    assert(block_pos < blocks.size());

    if (blocks[block_pos].next == block_pos) {
      head_pos_ = NOT_FOUND;
      return;
    }

    if (block_pos == head_pos_) {
      head_pos_ = blocks[block_pos].next;
    }

    auto prev = blocks[block_pos].prev;
    auto next = blocks[block_pos].next;
    blocks[prev].next = next;
    blocks[next].prev = prev;
  }
};

} // namespace -- ddd

#endif // DDD_DA_TRIE_HPP
