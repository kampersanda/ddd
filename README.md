# ddd: dynamic double-array dictionary

The __ddd__ is an experimental library for dynamic double-array dictionaries using some techniques.
Especially, this library is used for evaluating rearrangement methods.

You can download and compile __ddd__ as the following commands:

```
$ git clone https://github.com/kamp78/ddd.git
$ cd ddd
$ mkdir build
$ cd build
$ cmake ..
$ make
```

The usage is as follows:


```
$ ./Benchmark
Benchmark 1 <type> <dic> <key> <pfxs...>
- insert <key> and write the dictionary to <dic>
- MLTs can give pre-registered prefixes on <pfxs...> (optional)
- <type>: DaTrie type
    SGL      : Normal
    SGL_NL   : With node-link
    SGL_BL   : With block-link
    SGL_NL_BL: With node- and block-links
    MLT      : Using the trie division
    MLT_NL   : With node-link
    MLT_BL   : With block-link
    MLT_NL_BL: With node- and block-links
Benchmark 2 <dic1> <dic2> <key>
- delete <key> from <dic1> and write the dictionary to <dic2>
Benchmark 3 <dic> <key>
- search <key> for <dic>
Benchmark 4 <rear> <dic1> <dic2>
- rearrange <dic1> using <rear> and write the dictionary to <dic2>
- <rear>: Rearrangement mode
    1: pack()
    2: rebuild()
Benchmark 5 <dic> <key> <pat>
- generate a random key set registered in <dic> to <key>
- given <pat>, generate the patterns of random sub key sets (optional)
```