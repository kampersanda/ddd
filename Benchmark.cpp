#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include <DictionarySGL.hpp>
#include <DictionaryMLT.hpp>

using namespace ddd;

namespace {

enum class Times {
  sec, milli, micro
};

class StopWatch {
public:
  StopWatch() : tp_(std::chrono::high_resolution_clock::now()) {}
  ~StopWatch() {}

  double operator()(Times type) const {
    auto tp = std::chrono::high_resolution_clock::now() - tp_;
    double ret;

    switch (type) {
      case Times::sec:
        ret = std::chrono::duration<double>(tp).count();
        break;
      case Times::milli:
        ret = std::chrono::duration<double, std::milli>(tp).count();
        break;
      case Times::micro:
        ret = std::chrono::duration<double, std::micro>(tp).count();
        break;
    }

    return ret;
  }

  StopWatch(const StopWatch&) = delete;
  StopWatch& operator=(const StopWatch&) = delete;

private:
  std::chrono::high_resolution_clock::time_point tp_;
};

class KeyReader {
public:
  KeyReader(const char* file_name) {
    std::ios::sync_with_stdio(false);
    buf_.reserve(1 << 10);
    ifs_.open(file_name);
  }
  ~KeyReader() {}

  bool is_ready() const {
    return !ifs_.fail();
  }

  const char* next() {
    if (std::getline(ifs_, buf_)) {
      return buf_.c_str();
    }
    return nullptr;
  }

  KeyReader(const KeyReader&) = delete;
  KeyReader& operator=(const KeyReader&) = delete;

private:
  std::ifstream ifs_;
  std::string buf_;
};

std::string get_ext(std::string file_name) {
  return file_name.substr(file_name.find_last_of(".") + 1);
}

std::unique_ptr<Dictionary> create_dic(const std::string dic_type) {
  if (dic_type == "SGL") {
    return make_unique<DictionarySGL<false, false>>();
  } else if (dic_type == "SGL_NL") {
    return make_unique<DictionarySGL<false, true>>();
  } else if (dic_type == "SGL_BL") {
    return make_unique<DictionarySGL<true, false>>();
  } else if (dic_type == "SGL_NL_BL") {
    return make_unique<DictionarySGL<true, true>>();
  } else if (dic_type == "MLT") {
    return make_unique<DictionaryMLT<false, false>>();
  } else if (dic_type == "MLT_NL") {
    return make_unique<DictionaryMLT<false, true>>();
  } else if (dic_type == "MLT_BL") {
    return make_unique<DictionaryMLT<true, false>>();
  } else if (dic_type == "MLT_NL_BL") {
    return make_unique<DictionaryMLT<true, true>>();
  }
  return nullptr;
}

std::unique_ptr<Dictionary> create_dic(const std::string dic_type,
                                       std::vector<const char*>& prefixes) {
  if (dic_type == "MLT") {
    return make_unique<DictionaryMLT<false, false>>(prefixes);
  } else if (dic_type == "MLT_NL") {
    return make_unique<DictionaryMLT<false, true>>(prefixes);
  } else if (dic_type == "MLT_BL") {
    return make_unique<DictionaryMLT<true, false>>(prefixes);
  } else if (dic_type == "MLT_NL_BL") {
    return make_unique<DictionaryMLT<true, true>>(prefixes);
  }
  return create_dic(dic_type);
}

std::unique_ptr<Dictionary> read_dic(const std::string dic_name) {
  std::string dic_type{dic_name.substr(dic_name.find_last_of(".") + 1)};

  std::cout << "read dic from " << dic_name << std::endl;
  std::ifstream ifs{dic_name};
  if (!ifs) {
    std::cerr << "failed to open " << dic_name << std::endl;
    return nullptr;
  }

  if (dic_type == "SGL") {
    return make_unique<DictionarySGL<false, false>>(ifs);
  } else if (dic_type == "SGL_NL") {
    return make_unique<DictionarySGL<false, true>>(ifs);
  } else if (dic_type == "SGL_BL") {
    return make_unique<DictionarySGL<true, false>>(ifs);
  } else if (dic_type == "SGL_NL_BL") {
    return make_unique<DictionarySGL<true, true>>(ifs);
  } else if (dic_type == "MLT") {
    return make_unique<DictionaryMLT<false, false>>(ifs);
  } else if (dic_type == "MLT_NL") {
    return make_unique<DictionaryMLT<false, true>>(ifs);
  } else if (dic_type == "MLT_BL") {
    return make_unique<DictionaryMLT<true, false>>(ifs);
  } else if (dic_type == "MLT_NL_BL") {
    return make_unique<DictionaryMLT<true, true>>(ifs);
  }

  std::cerr << "invalid extension " << dic_type << std::endl;
  return nullptr;
}

void show_stat(std::ostream& os, const std::unique_ptr<Dictionary>& dic, bool need_singles) {
  Stat stat{};
  dic->stat(stat);

  os << "stat of " << dic->name() << std::endl;
  os << "- num keys        : " << stat.num_keys << std::endl;
  os << "- num tries       : " << stat.num_tries << std::endl;
  os << "- num nodes       : " << stat.num_nodes << std::endl;
  os << "- bc size         : " << stat.bc_size << std::endl;
  os << "- bc capa         : " << stat.bc_capa << std::endl;
  os << "- bc emps         : " << stat.bc_emps << std::endl;
  os << "- bc load factor  : " << double(stat.bc_size - stat.bc_emps) / stat.bc_size
     << std::endl;
  os << "- tail size       : " << stat.tail_size << std::endl;
  os << "- tail capa       : " << stat.tail_capa << std::endl;
  os << "- tail emps       : " << stat.tail_emps << std::endl;
  os << "- tail load factor: " << double(stat.tail_size - stat.tail_emps) / stat.tail_size
     << std::endl;
  os << "- size in bytes   : " << stat.size_in_bytes << std::endl;
  if (!need_singles) {
    return;
  }
  os << "- ratio singles   : " << dic->ratio_singles() << std::endl;
}

void show_usage(std::ostream& os) {
  os << "Benchmark 1 <type> <dic> <key> <pfxs...>" << std::endl;
  os << "- insert <key> and write the dictionary to <dic>" << std::endl;
  os << "- MLTs can give pre-registered prefixes on <pfxs...> (optional)" << std::endl;
  os << "- <type>: DaTrie type" << std::endl;
  os << "    SGL      : Normal" << std::endl;
  os << "    SGL_NL   : With node-link" << std::endl;
  os << "    SGL_BL   : With block-link" << std::endl;
  os << "    SGL_NL_BL: With node- and block-links" << std::endl;
  os << "    MLT      : Using the trie division" << std::endl;
  os << "    MLT_NL   : With node-link" << std::endl;
  os << "    MLT_BL   : With block-link" << std::endl;
  os << "    MLT_NL_BL: With node- and block-links" << std::endl;
  os << "Benchmark 2 <dic1> <dic2> <key>" << std::endl;
  os << "- delete <key> from <dic1> and write the dictionary to <dic2>" << std::endl;
  os << "Benchmark 3 <dic> <key>" << std::endl;
  os << "- search <key> for <dic>" << std::endl;
  os << "Benchmark 4 <rear> <dic1> <dic2>" << std::endl;
  os << "- rearrange <dic1> using <rear> and write the dictionary to <dic2>" << std::endl;
  os << "- <rear>: Rearrangement mode" << std::endl;
  os << "    1: pack()" << std::endl;
  os << "    2: rebuild()" << std::endl;
  os << "Benchmark 5 <dic> <key> <pat>" << std::endl;
  os << "- generate a random key set registered in <dic> to <key>" << std::endl;
  os << "- given <pat>, generate the patterns of random sub key sets (optional)" << std::endl;
}

int run_insertion(int argc, const char* argv[]) {
  std::cout << "run insertion" << std::endl;

  if (argc < 5) {
    show_usage(std::cerr);
    return 1;
  }

  std::unique_ptr<Dictionary> dic;
  if (argc == 5) {
    dic = create_dic(argv[2]);
  } else {
    std::vector<const char*> prefixes;
    for (int i = 5; i < argc; ++i) {
      prefixes.push_back(argv[i]);
    }
    dic = create_dic(argv[2], prefixes);
  }
  if (!dic) {
    show_usage(std::cerr);
    return 1;
  }

  {
    KeyReader reader{argv[4]};
    if (!reader.is_ready()) {
      std::cerr << "failed to open " << argv[4] << std::endl;
      return 1;
    }

    StopWatch sw;
    uint32_t N = 0;

    while (auto key = reader.next()) {
      if (!dic->insert_key(key, N++)) {
        std::cerr << "failed to insert " << key << std::endl;
        return 1;
      }
    }
    std::cout << "- insertion time: " << sw(Times::micro) / N << " us / key" << std::endl;
  }

  show_stat(std::cout, dic, true);

  std::string dic_name{argv[3]};
  dic_name += ".";
  dic_name += argv[2];

  {
    std::ofstream ofs{dic_name};
    if (!ofs) {
      std::cerr << "failed to open " << dic_name << std::endl;
      return 1;
    }
    dic->write(ofs);
  }

  std::cout << "write dic to " << dic_name << std::endl;
  return 0;
}

int run_deletion(int argc, const char* argv[]) {
  std::cout << "run deletion" << std::endl;

  if (argc < 5) {
    show_usage(std::cerr);
    return 1;
  }

  auto dic = read_dic(argv[2]);
  if (!dic) {
    return 1;
  }

  {
    KeyReader reader{argv[4]};
    if (!reader.is_ready()) {
      std::cerr << "failed to open " << argv[4] << std::endl;
      return 1;
    }

    uint32_t N = 0;
    StopWatch sw;

    while (auto key = reader.next()) {
      if (dic->delete_key(key) == NOT_FOUND) {
        std::cerr << "failed to delete " << key << std::endl;
        return 1;
      }
      ++N;
    }

    std::cout << "- deletion time: " << sw(Times::micro) / N << " us / key" << std::endl;
  }

  show_stat(std::cout, dic, true);

  std::string dic_name{argv[3]};
  dic_name += ".";
  dic_name += get_ext(argv[2]);

  {
    std::ofstream ofs{dic_name};
    if (!ofs) {
      std::cerr << "failed to open " << dic_name << std::endl;
      return 1;
    }
    dic->write(ofs);
  }
  std::cout << "write dic to " << dic_name << std::endl;

  return 0;
}

int run_search(int argc, const char* argv[]) {
  std::cout << "run search" << std::endl;

  if (argc < 4) {
    show_usage(std::cerr);
    return 1;
  }

  auto dic = read_dic(argv[2]);
  if (!dic) {
    return 1;
  }

  std::vector<std::string> keys;
  {
    std::ifstream ifs{argv[3]};
    if (!ifs) {
      std::cerr << "failed to open " << argv[3] << std::endl;
      return 1;
    }

    std::string line;
    std::ios::sync_with_stdio(false);

    while (std::getline(ifs, line)) {
      if (!line.empty()) {
        keys.push_back(line);
      }
    }
  }

  const auto N = 10;
  StopWatch sw;

  for (int r = 0; r < N; ++r) {
    for (size_t i = 0; i < keys.size(); ++i) {
      if (dic->search_key(keys[i].c_str()) == NOT_FOUND) {
        std::cerr << "failed to search " << keys[i] << std::endl;
        return 1;
      }
    }
  }

  std::cout << "- search time: " << sw(Times::micro) / keys.size() / N
            << " us / key (on " << N << " runs)" << std::endl;

  return 0;
}

int run_rearrangement(int argc, const char* argv[]) {
  std::cout << "run rearrangement" << std::endl;

  if (argc < 5) {
    show_usage(std::cerr);
    return 1;
  }

  auto dic = read_dic(argv[3]);
  if (!dic) {
    return 1;
  }

  show_stat(std::cout, dic, false);

  auto rear_mode = *argv[2];
  if (rear_mode == '1') {
    std::cout << "using pack()" << std::endl;
  } else if (rear_mode == '2') {
    std::cout << "using rebuild()" << std::endl;
  } else {
    show_usage(std::cerr);
    return 1;
  }

  {
    StopWatch sw;
    if (rear_mode == '1') {
      dic->pack();
    } else {
      dic->rebuild();
    }
    std::cout << "- rearrangement time: " << sw(Times::sec) << " sec" << std::endl;
  }

  show_stat(std::cout, dic, false);

  std::string dic_name{argv[4]};
  dic_name += ".";
  dic_name += get_ext(argv[3]);

  {
    std::ofstream ofs{dic_name};
    if (!ofs) {
      std::cerr << "failed to open " << dic_name << std::endl;
      return 1;
    }
    dic->write(ofs);
  }
  std::cout << "write dic to " << dic_name << std::endl;

  return 0;
}

int generate_keys(int argc, const char* argv[]) {
  std::cout << "generate random keys" << std::endl;

  if (argc < 4) {
    show_usage(std::cerr);
    return 1;
  }

  auto dic = read_dic(argv[2]);
  if (!dic) {
    return 1;
  }

  std::vector<KvPair> kvs;
  dic->enumerate(kvs);
  std::shuffle(kvs.begin(), kvs.end(), std::mt19937());

  if (argc < 5) {
    std::string file_name{argv[3]};
    file_name += ".keys";

    std::ofstream ofs{file_name};
    if (!ofs) {
      std::cerr << "failed to open " << file_name << std::endl;
      return 1;
    }

    for (size_t i = 0; i < kvs.size(); ++i) {
      ofs << kvs[i].key.c_str() << std::endl;
    }
    std::cout << "write " << kvs.size() << " keys to " << file_name <<  std::endl;
  } else {
    int N = std::stoi(argv[4]);
    std::cout << "- " << N << " patterns" << std::endl;


    for (int d = 1; d <= N; ++d) {
      int percent = 100 / N * d;
      std::stringstream file_name;
      file_name << argv[3] << "." << std::setw(3) << std::setfill('0') << percent << ".keys";

      std::ofstream ofs{file_name.str()};
      if (!ofs) {
        std::cerr << "failed to open " << file_name.str() << std::endl;
        return 1;
      }

      size_t num_keys = kvs.size() / N * d;
      for (size_t i = 0; i < num_keys; ++i) {
        ofs << kvs[i].key.c_str() << std::endl;
      }
      std::cout << "write " << num_keys << " keys to " << file_name.str() <<  std::endl;
    }
  }

  return 0;
}

} // namespace

int main(int argc, const char* argv[]) {
  if (argc < 2) {
    show_usage(std::cerr);
    return 1;
  }

  switch (*argv[1]) {
    case '1':
      return run_insertion(argc, argv);
    case '2':
      return run_deletion(argc, argv);
    case '3':
      return run_search(argc, argv);
    case '4':
      return run_rearrangement(argc, argv);
    case '5':
      return generate_keys(argc, argv);
    default:
      show_usage(std::cerr);
      break;
  }

  return 1;
}
