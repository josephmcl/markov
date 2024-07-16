# markov 

An implementation of Markov's algorithm. Built from a comprehensive reading of "Theory of Algorithms" A. A. Markov 1954. 

## About
 [Markov algorithms](https://en.wikipedia.org/wiki/Markov_algorithm) are **not** [Markov chains](https://en.wikipedia.org/wiki/Markov_chain).  They are simple string rewriting systems comprised of Alphabets and Productions --- more similar to Turing machines. They are not widely used, but have applications in certain constructivist mathematics. 

## Progress 
- Basic parsing is complete for constructing Alphabets is complete. 
- [TODO] How to store Alphabets, and which operations are determined at compile time vs runtime.  We could store alphabets as bit fields, making comparison straight-forward. Additionally, each Alphabet could have an internal representation s.t. index *i* refers to the *ith* non-zero in the bit field. 

## Big decisions
- LLVM or WebAssembly backend. (Likely WebAssembly.)
- An in-browser, interactive copy of the English translation of "Theory of Algorithms."
- The more I think about it, I think webassembly makes the most sense. Ideally this project compiles to a webassembly binary that can read inline code from the browser and compile it interactively. Maybe somewhat similar to a jypiter notebook. Compile markov -> fetch markov compiler.wasm in local browser session -> use it to compile browser content to wasm -> run it in the browser. 