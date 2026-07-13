# amazons

Simple 8×8 Amazons AI for Botzone.

## Build

```
g++ -std=c++17 -O2 main.cpp -o amazons
```

If `jsoncpp` is available in the environment (as on Botzone), it will be used for parsing the input protocol. Otherwise the AI falls back to a default first-move setup.
