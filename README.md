# Waylaunch

[English](README.md) | [日本語](README.ja.md)

Waylaunch is a lightweight application launcher for Wayland desktops. It searches installed `.desktop` applications as you type and starts the selected application.

## Features

- Searches applications from `~/.local/share/applications`, `/usr/share/applications`, and `/usr/local/share/applications`.
- Supports keyboard navigation, mouse selection, and text input through the Wayland text-input-v3 protocol when available.
- Runs a command entered with the `>` prefix.
- Evaluates arithmetic expressions entered with the `=` prefix and copies the result to the Wayland clipboard on Enter. Supports decimals, parentheses, and unary `+` / `-`.
- Suppresses debug logs by default. Use `--debug` or `-d` to enable them.

## Requirements

- A Wayland session with the `wl_compositor`, `wl_shm`, and `xdg-shell` protocols.
- For building: GCC, Make, `dpkg-dev`, and the Wayland, xkbcommon, FreeType, and libpng development packages.

On Debian or Ubuntu, install build dependencies with:

```sh
sudo apt install build-essential dpkg-dev libwayland-dev libxkbcommon-dev libfreetype-dev libpng-dev
```

## Build and Run

```sh
make
./waylaunch
```

To enable debug logs:

```sh
./waylaunch --debug
```

`make run` also starts Waylaunch with debug logs enabled.

Build a Debian package with:

```sh
make deb
```

Install the generated package (the architecture suffix depends on your system):

```sh
sudo apt install ./waylaunch_1.0.0_$(dpkg --print-architecture).deb
```

## Controls

| Key or action | Function |
| --- | --- |
| Type text | Filter applications by name |
| Up / Down | Select an application |
| Enter or left click | Launch the selected application |
| `>` followed by a command, then Enter | Run a command |
| `=` followed by an expression | Preview the result; press Enter to copy it to the clipboard |
| Escape | Close Waylaunch |

Expressions support `+`, `-`, `*`, `/`, and parentheses. Invalid expressions are shown as `Invalid expression`.

## Releases

Push a version tag such as `v1.0.0` to build a version-matched Debian package and publish it with generated release notes on GitHub:

```sh
git tag v1.0.0
git push origin v1.0.0
```
