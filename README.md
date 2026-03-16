# markov 

An implementation of Markov's algorithm. Built from a comprehensive reading of "Theory of Algorithms" A. A. Markov 1954. 

[Splash page](https://joseph.computer/markov/)

## About
 [Markov algorithms](https://en.wikipedia.org/wiki/Markov_algorithm) are simple string rewriting systems comprised of Alphabets and Productions --- similar to Turing machines. They are not widely used, but have applications in certain constructivist mathematics. 

## Progress 
- Basic parsing is complete for constructing Alphabets is complete. 
- [TODO] How to store Alphabets, and which operations are determined at compile time vs runtime.  We could store alphabets as bit fields, making comparison straight-forward. Additionally, each Alphabet could have an internal representation s.t. index *i* refers to the *ith* non-zero in the bit field.
  - [UPDATE] Added some basic storage for Letters. Letters get first class status similar to Variables and we need to figure out how to handle that exactly. There will be a few instances where it may be ambiguous whether a token is a Letter or a Variable. There's an issue discussing that in more detail.

## Big decisions
- LLVM or WebAssembly backend. (Likely WebAssembly.)
- An in-browser, interactive copy of the English translation of "Theory of Algorithms."
- The more I think about it, I think webassembly makes the most sense. Ideally this project compiles to a webassembly binary that can read inline code from the browser and compile it interactively. Maybe somewhat similar to a jypiter notebook. Compile markov -> fetch markov compiler.wasm in local browser session -> use it to compile browser content to wasm -> run it in the browser.

## Ongoing things 

Markov's grammar is very basic and mostly based on conventional mathematical notation. We're building some extensions on it to formalize it as a consistent and coherent grammar. For example, modules, scopes, and that sort of thing.

```
export module 010206a {
    A_0 = {a, b};
    A_1 = {a, b, c, d};
    A_2 = {a, b, c, d, e};
    A_3 = {a, b, c, d, e, f, g, h, i , j, k, l, m};
    Ч = {❚};
    С = {❚, ✱}; 
    Ц = {❚, —};
    М = {❚, —, ✱, ▢};
    Т = {❚, —, ✱, ▢, &}.
}
```

With a module system we can export and import code between code units, which will be helpful when we get this compiled into wasm and running as an web-based interactive book. 

```
import module 010206a
export module 010206b <010206a> {
    A_0 ⊂ A_1 ⊂ A_2 ⊂ A_3, 
    Ч ⊂ С ⊂ М ⊂ Т,
    Ч ⊂ Ц ⊂ Т.
}
```

And when we embed this in the browser maybe we can add some sugar to hide the module syntax somehow. Let's get to that later.  

