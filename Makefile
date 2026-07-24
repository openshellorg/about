CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CPPFLAGS ?= -DABOUT_VERSION=\"$(VERSION)\"
VERSION ?= 0.1.0

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRC = src/about.c
BIN = about

COSMOCC ?= cosmocc
COSMOCC_DIR ?= .cosmocc
# Fat amd64+arm64 by default. On WSL, prefer: make ape COSMOCC_FLAGS=-m64
COSMOCC_FLAGS ?=

.PHONY: all clean install uninstall ape ape-toolchain test smoke

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC)

ape-toolchain:
	@if [ ! -x "$(COSMOCC_DIR)/bin/$(COSMOCC)" ]; then \
		echo "Downloading cosmocc toolchain…"; \
		mkdir -p "$(COSMOCC_DIR)"; \
		curl -fsSL -o /tmp/cosmocc.zip https://cosmo.zip/pub/cosmocc/cosmocc.zip; \
		unzip -qo /tmp/cosmocc.zip -d "$(COSMOCC_DIR)"; \
		rm -f /tmp/cosmocc.zip; \
	fi

ape: ape-toolchain
	@if [ -f /proc/sys/fs/binfmt_misc/WSLInterop ]; then \
		echo "Note: on WSL, APE often needs: sudo sh -c 'echo -1 > /proc/sys/fs/binfmt_misc/WSLInterop'"; \
	fi
	$(COSMOCC_DIR)/bin/$(COSMOCC) $(COSMOCC_FLAGS) $(CPPFLAGS) -O2 -o $(BIN).com $(SRC)
	@echo "Built Actually Portable Executable: $(BIN).com"
	@echo "Runs on Linux, macOS, Windows, and BSDs (amd64 + arm64)."

smoke: $(BIN)
	./$(BIN) --version
	./$(BIN) -q
	./$(BIN) --tips >/dev/null

test: smoke

install: $(BIN)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 $(BIN) "$(DESTDIR)$(BINDIR)/$(BIN)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(BIN)"

clean:
	rm -f $(BIN) $(BIN).exe $(BIN).com $(BIN).com.dbg
