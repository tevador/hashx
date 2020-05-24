# HashX

HashX is an algorithm designed for client puzzles and proof-of-work schemes.
While traditional cryptographic hash functions use a fixed one-way compression
function, each HashX instance represents a unique pseudorandomly generated
one-way function.

HashX functions are generated as a carefully crafted sequence of integer
operations to fully saturate a 3-way superscalar CPU pipeline (modeled after
the Intel Ivy Bridge architecture). Extra care is taken to avoid optimizations
and to ensure that each function takes exactly the same number of CPU cycles
(currently 510 instructions over 170 cycles).

## Build

A C99-compatible compiler and `cmake` are required.

```
git clone https://github.com/tevador/hashx.git
cd hashx
mkdir build
cd build
cmake ..
make
```

## API

The API consists of 4 functions and is documented in the public header file
[hashx.h](include/hashx.h).

Example of usage:

```c
#include <hashx.h>
#include <stdio.h>

int main() {
    char seed[] = "this is a seed that will generate a hash function";
    char hash[HASHX_SIZE];
    hashx_ctx* ctx = hashx_alloc(HASHX_COMPILED);
    if (ctx == HASHX_NOTSUPP)
        ctx = hashx_alloc(HASHX_INTERPRETED);
    if (ctx == NULL)
        return 1;
    if (!hashx_make(ctx, seed, sizeof(seed))) /* generate a hash function */
        return 1;
    hashx_exec(ctx, 123456789, hash); /* calculate the hash of a nonce value */
    hashx_free(ctx);
    for (unsigned i = 0; i < HASHX_SIZE; ++i)
        printf("%02x", hash[i] & 0xff);
    printf("\n");
    return 0;
}
```

Because HashX is meant to be used in proof-of-work schemes and client puzzles,
the input is a 64-bit counter value. If you need to hash arbitrary data, build
with:

```
cmake .. -DHASHX_BLOCK_MODE=ON
```

This will change the API to accept `const void*, size_t` instead of `uint64_t`.
However, we strongly recommend to use the more efficient counter mode.

## Performance

HashX was designed for fast verification. Generating a hash function from seed
takes about 50 μs and a 64-bit nonce can be hashed in under 100 ns (in compiled
mode) or in about 1-2 μs (in interpreted mode).

## Protocols based on HashX

Here are two examples of how HashX can be used in practice:

### Interactive client puzzle

Client puzzles are protocols designed to protect server resources from abuse.
A client requesting a resource from a server may be asked to solve a puzzle
before the request is accepted.

One of the first proposed client puzzles is [Hashcash](https://en.wikipedia.org/wiki/Hashcash),
which requires the client to find a partial SHA-1 hash inversion. However,
because of the static nature of cryptographic hash functions, an attacker can
offload hashing to a GPU or FPGA to gain a significant advantage over legitimate
clients equipped only with a CPU.

In a HashX-based interactive client puzzle, the server sends each client
a 256-bit challenge used to generate a unique HashX function. The client then
has to find a 64-bit nonce value such that the resulting hash has a predefined
number of leading zeroes. An attacker cannot easily parallelize the workload
because each request would require a new GPU kernel or FPGA bistream.

### Non-interactive proof-of-work

In the absence of a central authority handing out challenges (for example in
a cryptocurrency), the client takes some public information `T` (for example
a block template) and combines it with a chosen 64-bit nonce `N1`.
The resulting string `X = T||N1` is then used to generate a HashX function
<code>H<sub>X</sub></code>. The client then tries to find a 16-bit nonce `N2`
such that <code>r = H<sub>X</sub>(N2)</code> meets the difficulty target of
the protocol. If no `N2` value is successful, the client increments `N1` and
tries with a different hash function.

In this protocol, each HashX function provides only 65536 attempts before it
must be discarded. This limits the parallelization advantage of GPUs and FPGAs.
A CPU core will be able to test about 200 different hash functions per second.




