#undef NDEBUG

#include <cassert>
#include <iostream>
#include <random>

#include <DictionarySGL.hpp>
#include <DictionaryMLT.hpp>

using namespace ddd;

namespace {

static std::random_device rnd;
constexpr size_t NUM_KEYS      = 1U << 14;
constexpr size_t KEY_LENGTH    = 50;
constexpr size_t NUM_PREFIXES  = 2;
constexpr size_t PREFIX_LENGTH = 2;

char make_char() {
  return static_cast<char>('A' + (rnd() % 26));
}

void make_kvs(std::vector<KvPair>& kvs) {
  kvs.clear();
  kvs.reserve(NUM_KEYS);

  for (uint32_t i = 0; i < NUM_KEYS; ++i) {
    std::string key;
    size_t length = (rnd() % KEY_LENGTH) + 1;
    for (size_t j = 0; j < length; ++j) {
      key += make_char();
    }
    kvs.push_back(KvPair{key,i});
  }

  std::sort(kvs.begin(), kvs.end());
  kvs.erase(std::unique(kvs.begin(), kvs.end()), kvs.end());
  std::shuffle(kvs.begin(), kvs.end(), std::mt19937());
}

void make_prefixes(std::vector<std::string>& prefixes) {
  prefixes.clear();

  for (size_t i = 0; i < NUM_PREFIXES; ++i) {
    std::string prefix;
    size_t length = (rnd() % PREFIX_LENGTH) + 1;
    for (size_t j = 0; j < length; ++j) {
      prefix += make_char();
    }
    prefixes.push_back(prefix);
  }
}

template <typename T>
void test(const std::vector<KvPair>& kvs, std::unique_ptr<T> dic) {
  for (auto &kv : kvs) {
    assert(dic->insert_key(kv.key.c_str(), kv.value));
  }
  for (auto &kv : kvs) {
    assert(dic->search_key(kv.key.c_str()) == kv.value);
  }
  {
    Stat stat{};
    dic->stat(stat);
    assert(stat.num_keys == kvs.size());
  }
  {
    std::vector<KvPair> ret;
    dic->enumerate(ret);
    assert(kvs.size() == ret.size());
  }

  std::vector<const KvPair*> test_kvs[2];
  for (size_t i = 0; i < kvs.size(); ++i) {
    test_kvs[i % 2].push_back(&kvs[i]);
  }

  for (auto kv : test_kvs[0]) {
    assert(dic->delete_key(kv->key.c_str()) == kv->value);
  }
  for (auto kv : test_kvs[0]) {
    assert(dic->search_key(kv->key.c_str()) == NOT_FOUND);
  }
  for (auto kv : test_kvs[1]) {
    assert(dic->search_key(kv->key.c_str()) == kv->value);
  }

  const char* file_name = "test.index";
  {
    std::ofstream ofs{file_name};
    dic->write(ofs);
  }

  {
    std::ifstream ifs{file_name};
    auto size = static_cast<size_t>(ifs.seekg(0, std::ios::end).tellg());
    Stat stat{};
    dic->stat(stat);
    assert(stat.size_in_bytes == size);
  }

  {
    std::ifstream ifs{file_name};
    dic = make_unique<T>(ifs);
  }

  dic->pack();

  for (auto kv : test_kvs[0]) {
    assert(dic->search_key(kv->key.c_str()) == NOT_FOUND);
  }
  for (auto kv : test_kvs[1]) {
    assert(dic->search_key(kv->key.c_str()) == kv->value);
  }
  {
    Stat stat{};
    dic->stat(stat);
    assert(stat.num_keys == test_kvs[1].size());
  }

  {
    std::ifstream ifs{file_name};
    dic = make_unique<T>(ifs);
  }

  dic->rebuild();

  for (auto kv : test_kvs[0]) {
    assert(dic->search_key(kv->key.c_str()) == NOT_FOUND);
  }
  for (auto kv : test_kvs[1]) {
    assert(dic->search_key(kv->key.c_str()) == kv->value);
  }
  {
    Stat stat{};
    dic->stat(stat);
    assert(stat.num_keys == test_kvs[1].size());
  }
}

} // namespace

int main() {
  std::vector<KvPair> kvs;
  make_kvs(kvs);

  std::vector<std::string> buf_prefixes;
  make_prefixes(buf_prefixes);

  std::vector<const char*> prefixes;
  for (auto& prefix : buf_prefixes) {
    prefixes.push_back(prefix.c_str());
  }

  std::cerr << "-- test for SGL --" << std::endl;
  test(kvs, make_unique<DictionarySGL<false, false>>());
  std::cerr << "-- test for SGL_NL --" << std::endl;
  test(kvs, make_unique<DictionarySGL<false, true>>());
  std::cerr << "-- test for SGL_BL --" << std::endl;
  test(kvs, make_unique<DictionarySGL<true, false>>());
  std::cerr << "-- test for SGL_NL_BL --" << std::endl;
  test(kvs, make_unique<DictionarySGL<true, true>>());

  std::cerr << "-- test for MLT --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<false, false>>());
  std::cerr << "-- test for MLT_NL --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<false, true>>());
  std::cerr << "-- test for MLT_BL --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<true, false>>());
  std::cerr << "-- test for MLT_NL_BL --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<true, true>>());

  std::cerr << "-- test for MLT with pre-registered prefixes --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<false, false>>(prefixes));
  std::cerr << "-- test for MLT_NL with pre-registered prefixes --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<false, true>>(prefixes));
  std::cerr << "-- test for MLT_BL with pre-registered prefixes --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<true, false>>(prefixes));
  std::cerr << "-- test for MLT_NL_BL with pre-registered prefixes --" << std::endl;
  test(kvs, make_unique<DictionaryMLT<true, true>>(prefixes));

  return 0;
}
