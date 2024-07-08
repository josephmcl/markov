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
- Grammar question: Markov's language would allow this `a = { a }, a ∈ a.` Since Markov does not syntactically delinate between variables and letters this could be ambigious. We could require `∈` take a letter literal. on the left hand side, but that seems limitting. We might extend this grammar to disambiguate letter literals and variables: `a = { a }, a ∈ :a.` s.t. the `:` is neccesary when a name is take by a variable and a letter. Thus in this case, the left-hand is the letter literal `a` and the right-hand side is a variable `a`. This also allows the left-hand side to be disambiguated as a variable as well. `a ∈ ; :a ∈ b; :a ∈ :b; a ∈ b,` would all be valid. Of course, we could flip this and require literal statments in some contexts be prefixed instead. This could be better since we would expect people to use more variables as their projects become complex. 
