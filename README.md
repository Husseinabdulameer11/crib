# Crib

[![Open in GitHub Codespaces](https://github.com/codespaces/badge.svg)](https://codespaces.new/Husseinabdulameer11/crib)

Crib is a small language for the maths of a cryptography course, built with
[gram](https://github.com/Husseinabdulameer11/gram), a separate parser library and
generator. You write formulas the way they are written on paper, and `sim`
statements run the textbook procedures (LFSR state diagrams,
square-and-multiply, extended Euclid, RSA, ElGamal, DSA, elliptic-curve
point arithmetic, block cipher modes) step by step, printing every
intermediate value.

With `--html` it also writes a single self-contained page where the
simulations are interactive: the LFSR register shifts bit by bit while its
state diagram lights up, the square-and-multiply steps walk through the
exponent's bits, elliptic-curve sums are plotted on the point grid together
with the wrapped line through P and Q, and the modes grid shows which blocks
a one-bit error destroys.

(A *crib* is a piece of known plaintext in cryptanalysis, and also a crib
sheet.)

## Install

**Windows:** download **crib-windows-setup.exe** from the
[latest release](https://github.com/Husseinabdulameer11/crib/releases/latest) and run it.
It installs for your user only (no admin needed) and adds `crib` to your
PATH. Open a *new* terminal afterwards. The Start menu also gets a "Crib
command prompt" and the examples folder.

**macOS and Linux:**

```
curl -fsSL https://raw.githubusercontent.com/Husseinabdulameer11/crib/main/install.sh | sh
```

This puts `crib` in `~/.local/bin` and the examples in
`~/.local/share/crib/examples`, after checking the download's checksum.
`CRIB_VERSION=0.1.0` picks a release, and `sh -s -- --uninstall` removes it
again. Prefer doing it by hand? Every release also has plain `.tar.gz`
archives.

**Docker**, on any OS. Run it in the folder with your `.crib` files:

```
docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/crib exam.crib
docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/crib --html exam.html exam.crib
```

Works the same in PowerShell, bash and zsh. On Linux, add
`-u "$(id -u):$(id -g)"` so the files Crib writes belong to you, not root.

**GitHub Codespaces:** click the badge above. You get VS Code in the browser
with Crib, gram and a C compiler ready, so you can use Crib *and* change it.

## Use

```
crib examples/exam.crib                        # terminal
crib --html exam.html examples/exam.crib       # + interactive browser page
crib -t file.crib                              # syntax tree
crib --version
```

`examples/exam.crib` works the recurring Part II exam problems and
checks each answer; `examples/language.crib` is a tour of the language.

## The language

```
p = 17; alpha = 3; a = 6          # variables; semicolons are optional
show alpha^a mod p                # prints "alpha^a mod p = 15"
m = t * r^-a mod p                # see "mod" below
check beta^k * m mod p == t       # [ok], or [FAIL] with both sides' values
g, x, y = egcd(7, 192)            # tuples, destructuring
P = (3, 2)                        # a point is a tuple
blocks = [0x4154, 0x5441]         # lists; blocks[0], len(blocks)
fn h(x) = alpha^x mod p           # one-line function
fn f(a, b) {                      # block function
  if a > b { return a } else { return b }
}
for i in 1..10 { print i, i^2 }   # inclusive range
while x != 1 { x = x * a mod n }
f = poly(x^4 + x + 1)             # polynomial over GF(2)
# comment   // comment
```

Numbers are 64-bit integers, written in decimal, `0x` hex or `0b` binary.
Arithmetic that would overflow stops with an error instead of wrapping.

### `mod`

`mod` binds more loosely than `+ - * / ^`, so `t * r^-a mod p` means
`(t * r^-a) mod p`. Everything on its left is then computed modulo p:

| Written | Means |
|---|---|
| `a^e mod n` | square-and-multiply; `3^200 mod 101` never builds 3^200 |
| `a^-1 mod n` | the inverse of a (an error if gcd(a, n) is not 1) |
| `a^-k mod n` | (a^-1)^k |
| `x / y mod p` | x · y^-1, as in the ECC λ formulas |
| `+ - *` | reduced as they go |

Exponents themselves are never reduced. Without `mod`, `/` is integer
division, `%` is the remainder, and a negative power is an error.

`poly(...) mod poly(...)` is polynomial remainder over GF(2).

### Operators, loosest first

`or` · `and` · `not` · `== != < <= > >=` · `mod` · `xor` · `+ -` ·
`* / %` · unary `-` · `^` (right-associative) · `x[i]`

`+` also joins strings and lists. On polynomials `+` and `-` are XOR and
`*` is carry-less multiplication.

### Statements

| | |
|---|---|
| `show e1, e2` | prints each expression with its value: `phi(221) = 192` |
| `print e1, e2` | prints the values separated by spaces |
| `check cond` | `[ok] cond`, or `[FAIL] cond (left side = .., right side = ..)` |
| `sim name(args)` | a step-by-step simulation (below) |
| a bare expression | at top level, printed like `show` |

### Built-in functions

| Function | Result |
|---|---|
| `gcd(a, b)`, `egcd(a, b)` | gcd; `(g, x, y)` with ax + by = g |
| `inv(a, n)`, `modpow(a, e, n)` | inverse; power mod n |
| `phi(n)`, `factor(n)`, `isprime(n)` | totient; prime factors as a list; 1 or 0 |
| `ord(a, n)` | multiplicative order of a mod n |
| `crt([r1, r2..], [m1, m2..])` | Chinese remainder theorem |
| `root(c, e)` | integer e-th root (textbook RSA with small e) |
| `ecadd(P, Q, a, p)`, `ecmul(k, P, a, p)` | point arithmetic on y² = x³ + ax + b; `O` is the point at infinity |
| `lfsr(f, seed, n)` | first n output bits of the LFSR for polynomial f |
| `deg(f)` | degree of a polynomial |
| `bin(x, width)`, `hex(x)` | strings |
| `len(x)`, `str(x)`, `abs(x)`, `min(..)`, `max(..)` | |

## Simulations

Arguments can be named or positional (in the order shown).

| Statement | Shows |
|---|---|
| `sim lfsr(f, seed = s, steps = n)` | recurrence, register, factorisation over GF(2), every cycle of the state diagram, cycle lengths, period, primitivity; with a seed, a step table and the keystream |
| `sim modpow(a, e, n)` | Euler reduction of the exponent when gcd(a, n) = 1, then the square-and-multiply table bit by bit |
| `sim egcd(a, b)` / `sim inverse(a, n)` | the extended Euclid table (q, r, s, t) and the check line |
| `sim phi(n)` | factorisation and the product formula |
| `sim rsa(p, q, e, m = .., c = .., h = ..)` | n, φ(n), d via extended Euclid, encryption, decryption, signature |
| `sim dh(p, g, x, y)` | both sides of the exchange and what Eve sees |
| `sim elgamal(p, alpha, a, k = .., m = ..)` or `(..., r = .., t = ..)` | encryption and/or decryption with r^a, its inverse, and the check |
| `sim elgamal_sign(p, alpha, a, k, m)` | r, k⁻¹ mod p−1, s, verification |
| `sim dsa(p, q, h = .. or g = .., x, k, H)` | signing, verification, and recovery of x from a leaked k |
| `sim ecc(a, b, p, P = .., Q = ..)` | P + Q (or 2P when Q is left out) with λ; `k = ..` lists the multiples of P and its order; all points of the curve |
| `sim modes(text = "..", flip = (block, bit), at = "transit" or "encrypt")` | runs ECB, CBC, CFB, OFB and CTR on a toy 16-bit block cipher and reports which plaintext blocks Bob gets back intact, with one bit wrong, or garbled |

The modes simulation really encrypts and decrypts; the effects are measured,
not looked up. The block cipher is a 4-round Feistel network on 16-bit
blocks so whole blocks fit on screen. It is a toy and not secure.

## How it is put together

| File | Role |
|---|---|
| `src/crib.gram` | the grammar; `gramgen` turns it into `build/crib_grammar.h` (rule ids) |
| `src/crib.c` | values, `mod`-aware evaluation, statements, built-ins, number theory |
| `src/crib_sim.c` | the simulations |
| `src/crib_out.c` | output: terminal text, plus the JSON record for `--html` |
| `src/crib_view.html` | the page template; `tools/embed` bakes it into `build/crib_view.h` |

The `mod` trick is in `eval(node, m)`: `m` is the modulus in force, and the
`modx` rule evaluates its left side with `m` set to its right side. To add a
simulation, write a `sim_x(simargs *)` in `src/crib_sim.c` using `sim_step`,
`sim_note`, `sim_table` and `sim_result`, and add it to `run_sim`. It then
appears in the terminal and the page with no viewer changes; a custom
visualisation goes in `src/crib_view.html`, keyed on the sim's type.

## Building

You only need this to change Crib; to use it, see [Install](#install).
The easiest way to work on Crib is Codespaces (above), where all of this is
already done.

Crib needs **[gram](https://github.com/Husseinabdulameer11/gram)**: `gramgen` turns `src/crib.gram` into the rule ids the
interpreter switches on, and gram's parser is linked into the binary. So to
build Crib, or to change its grammar, get gram first and then do one of:

```
# 1. install gram once, then build Crib anywhere
cd gram && make install            # or: make install PREFIX=~/.local
cd crib && make

# 2. keep gram next to Crib, as ../gram; it is found automatically
cd crib && make

# 3. point at gram explicitly
make GRAM=path/to/gram             # a gram checkout
make GRAM_PREFIX=/opt/gram         # an installed gram not on PATH
```

`make` prints which gram it used, or tells you how to get it if none is
found. `make test` runs the exam and compares the output.

On Windows, build in an MSYS2 shell (UCRT64) with:

```
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make diffutils
```

### Extending the language

Edit `src/crib.gram`, then `make`: the grammar is run through `gramgen`
again and the new rule ids appear in `build/crib_grammar.h` as
`CRIB_R_<rule>`. Handle them in `eval` / `exec` in `src/crib.c`. See gram's
README for the grammar syntax.

## Releases

Maintainers: see [RELEASING.md](RELEASING.md). Pushing a `v*` tag builds
the installer, the archives and the Docker image and publishes them.

## License

MIT, see `LICENSE`.
