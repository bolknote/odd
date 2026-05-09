# Odds — exploring a small inductive definition of odd numbers

This repository is a small **computational experiment** around a classic puzzle: define a set $X \subseteq \mathbb{N}$ by three rules, then ask whether $X$ coincides with **all positive odd integers**.

The rules are deliberately minimal:

$$
\begin{aligned}
&1 \in X \\
&3x \in X \implies x \in X \\
&x \in X \implies 2x + 1 \in X
\end{aligned}
$$

Equivalently: start from $1$, repeatedly apply $x \mapsto 2x+1$, and whenever you see a multiple of three you may “pull back” one division by $3$. The **order in which you apply these moves does not change the set** they generate—the closure is fixed by the rules—but your **implementation** must respect that if you want parallel code to match sequential code.

The open question (the one journal authors once threw at readers) is whether this inductive closure is exactly $\{\,2t+1 \mid t \in \mathbb{N}\,\}$. This code does **not** prove the theorem; it **enumerates** the elements that lie in the closure **inside a bounded numeric range** (determined by your chosen integer width), so you can stress hardware, compare implementations, and watch how far you can reach before you wrap, overflow, or simply run out of patience.

For background in Russian, see [«Нечётные числа» on bolknote.ru](https://bolknote.ru/all/nechyotnye-chisla/).

---

## What is implemented here?

Two C programs share the same **mathematical meaning**, but not the same engineering trade-offs:

| Program | Role |
|--------|------|
| **`phase1`** | Single-threaded enumerator. Simple stack-based worklist plus a bitset-backed visited set. |
| **`phase2`** | Multi-threaded enumerator with an atomic bitset-backed visited set and per-thread work queues with work stealing. Intended for larger domains, subject to memory and output-volume limits. |

Both programs print **one odd number per line** in hexadecimal (`0x…`), preceded by zero-padding to the selected fixed-width type (`uint16_t` prints four hex digits, `uint32_t` eight, `uint64_t` sixteen, `uint128_t` thirty-two). They only print a value when it is inserted into the set for the **first** time, so each line should be unique for a correct run.

Pass **`--count`** to suppress per-value output and print only the final number of discovered values. This is the fastest way to measure enumeration throughput without benchmarking the terminal or pipe.

The core closure step for each newly discovered $x$ is:

1. If $2x+1$ fits in the chosen type, enqueue it.
2. If $x$ is divisible by $3$, enqueue $x/3$.

Starting from seed $1$, this matches the inductive definition above **within the limits of your `numeric` type** (overflow stops a branch).

For `uint16_t` and `uint32_t`, the visited set is a dense bitset over odd values (`x >> 1`). For `uint64_t` and `uint128_t`, it switches to sparse paged bitsets so the program only allocates pages it actually touches.

---

## Building

Requires a C compiler with **`unsigned __int128`** (GCC/Clang on typical 64-bit Linux/macOS) or `_BitInt(128)` as a fallback. The code uses C11 features such as `_Generic`; MSVC is not a supported compiler target.

Two profiles are defined in the `Makefile`:

| Target | Binaries | Typical use |
|--------|-----------|-------------|
| **`make`** (default) | `phase1`, `phase2` | Development: strict warnings, `-O2`, stack protector, `_FORTIFY_SOURCE=2`. |
| **`make fast`** | `phase1-fast`, `phase2-fast` | Long runs / benchmarking: `-O3`, **`-march=native`**, LTO; no fortify/stack-hardening overhead. |

```bash
make              # phase1 + phase2 (strict “dev” flags)
make fast         # phase1-fast + phase2-fast (speed-oriented)
make clean        # removes all four binaries
```

Tune the CPU architecture for the fast build if needed (default `MARCH=native`):

```bash
make fast MARCH=znver3    # example: explicit AMD Zen 3
```

`phase2` and `phase2-fast` are linked with **POSIX threads** (`-pthread`).

---

## Choosing the integer width (`NUMERIC_TYPE`)

By default, both sources use `uint32_t`. Override it through the `Makefile`:

```bash
make NUMERIC_TYPE=uint16_t
make fast NUMERIC_TYPE=uint16_t
```

- **`uint16_t`** — small universe; runs finish quickly; good for regression checks (counts should match between `phase1` and `phase2`).
- **`uint32_t`** — dense visited bitset uses about **256 MiB**; output can still be **hundreds of millions of lines**. Pipe to `wc -l` if you only care how many distinct values appear.
- **`uint64_t` / `uint128_t`** — sparse paged visited bitsets avoid allocating the full universe, but long runs can still consume substantial memory as more pages are touched.

---

## Running `phase2` with more than one thread

Optional first argument: worker count. If omitted, the program picks a default from the OS (`sysconf(_SC_NPROCESSORS_ONLN)`). Explicit values must be integers in the range `1..1024`.

```bash
./phase1 --count      # single-threaded count-only run
./phase2              # default thread count
./phase2 8            # eight worker threads
./phase2 --count      # default thread count, print only the final count
./phase2 8 --count    # eight worker threads, print only the final count
```

---

## Practical notes

- **Determinism of the set:** For a fixed `NUMERIC_TYPE`, the **set** of emitted values should be the same regardless of thread count; only **print order** may differ in `phase2`.
- **Visited-set representation:** `uint16_t`/`uint32_t` use dense bitsets; wider types use sparse paged bitsets.
- **Counting output:** Use `--count` when you only need the number of discovered values. `wc -l` also counts lines, but then the program still formats and writes every value.
- **Disk and memory:** Saving the full hex listing for a 32-bit run is **enormous**. Prefer counting lines or hashing if you only need verification statistics.
