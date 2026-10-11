# Crib in a container: a small Alpine image with a static `crib` binary.
#
#   docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/crib exam.crib
#   docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/crib --html exam.html exam.crib
#
# Files are read from and written to the folder you mount at /work.
#
# Building it yourself needs gram. Either hand Docker your gram folder:
#
#   docker build --build-context gram=../gram -t crib .
#
# or let it clone gram from git:
#
#   docker build --build-arg GRAM_REPO=https://github.com/Husseinabdulameer11/gram.git -t crib .

ARG ALPINE=alpine:3.20

# ---- gram, when no `--build-context gram=...` is given: clone it ------------
FROM ${ALPINE} AS gram-git
ARG GRAM_REPO=
ARG GRAM_REF=main
RUN if [ -z "$GRAM_REPO" ]; then \
        echo "crib needs gram: build with --build-context gram=path/to/gram"; \
        echo "or --build-arg GRAM_REPO=https://github.com/<owner>/gram.git"; \
        exit 1; \
    fi \
 && apk add --no-cache git \
 && git clone --depth 1 --branch "$GRAM_REF" "$GRAM_REPO" /gram

# A stage named `gram` holding just gram's files. `--build-context gram=...`
# replaces this whole stage with the given folder.
FROM scratch AS gram
COPY --from=gram-git /gram /

# ---- build a static crib and run its tests ---------------------------------
FROM ${ALPINE} AS build
RUN apk add --no-cache build-base diffutils
COPY --from=gram / /src/gram
COPY . /src/crib
WORKDIR /src/crib
ARG VERSION=dev
RUN make clean \
 && make test GRAM=/src/gram VERSION="$VERSION" LDFLAGS=-static \
 && strip build/crib

# ---- the image people run ---------------------------------------------------
FROM ${ALPINE}
LABEL org.opencontainers.image.title="crib" \
      org.opencontainers.image.description="A language for the maths of a cryptography course: formulas mod n and step-by-step simulations (LFSR, RSA, ElGamal, DSA, elliptic curves, block modes)" \
      org.opencontainers.image.licenses="MIT"
COPY --from=build /src/crib/build/crib /usr/local/bin/crib
COPY --from=build /src/crib/examples /usr/share/crib/examples
WORKDIR /work
ENTRYPOINT ["crib"]
CMD ["--help"]
