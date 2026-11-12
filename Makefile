CC ?= gcc
CFLAGS ?= -Wall -Os -ffunction-sections -fdata-sections -Iinclude -Iprotocol -I/usr/include/freetype2
LDFLAGS ?= -Wl,--gc-sections -s
LIBS = -lwayland-client -lxkbcommon -lfreetype -lpng

TARGET = waylaunch
PACKAGE ?= waylaunch
VERSION ?= 1.1.0
MAINTAINER ?= Local build <root@localhost>
ARCH = $(shell dpkg --print-architecture)
DEB = $(PACKAGE)_$(VERSION)_$(ARCH).deb

SRCS = src/main.c \
       src/wayland_backend.c \
       src/graphics.c \
       src/ui.c \
       src/app.c \
       src/calc.c \
       src/util.c \
       protocol/xdg-shell-client-protocol.c \
       protocol/text-input-unstable-v3-protocol.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

build: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: build
	./$(TARGET) --debug

deb: $(TARGET)
	@set -eu; \
       workdir=$$(mktemp -d); \
       trap 'rm -rf "$$workdir"' EXIT; \
       mkdir -p "$$workdir/debian/$(PACKAGE)/usr/bin" "$$workdir/debian/$(PACKAGE)/DEBIAN"; \
              install -Dm755 "$(TARGET)" "$$workdir/debian/$(PACKAGE)/usr/bin/waylaunch"; \
       printf '%s\n' \
              "Source: $(PACKAGE)" \
              "Section: utils" \
              "Priority: optional" \
              "Maintainer: $(MAINTAINER)" \
              "" \
              "Package: $(PACKAGE)" \
              "Architecture: $(ARCH)" \
              'Depends: $${shlibs:Depends}' \
                     "Description: Waylaunch application launcher for Wayland" \
                     " A lightweight keyboard-driven application launcher for Wayland." \
              > "$$workdir/debian/control"; \
              (cd "$$workdir" && dpkg-shlibdeps -O -e "debian/$(PACKAGE)/usr/bin/waylaunch") > "$$workdir/shlibs"; \
       dependencies=$$(sed -n 's/^shlibs:Depends=//p' "$$workdir/shlibs"); \
       test -n "$$dependencies"; \
       printf '%s\n' \
              "Package: $(PACKAGE)" \
              "Version: $(VERSION)" \
              "Section: utils" \
              "Priority: optional" \
              "Architecture: $(ARCH)" \
              "Maintainer: $(MAINTAINER)" \
              "Depends: $$dependencies" \
                     "Description: Waylaunch application launcher for Wayland" \
                     " A lightweight keyboard-driven application launcher for Wayland." \
              > "$$workdir/debian/$(PACKAGE)/DEBIAN/control"; \
       dpkg-deb --root-owner-group --build "$$workdir/debian/$(PACKAGE)" "$(DEB)"

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all build run deb clean
