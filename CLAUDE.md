# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

fcitx5-lotus is a Vietnamese input method addon for Fcitx5 on Linux (C++20, CMake, plus a Python/Qt settings app). It is a fork of VMK, and its Vietnamese engine in `lotus/` is a C++ port of the Go engine [bamboo-core](https://github.com/BambooEngine/bamboo-core). `README.md` and `CONTRIBUTING.md` are in Vietnamese; `*.en.md` are the English versions.

## Build, test, lint

Dependencies: CMake ≥ 3.16, extra-cmake-modules, Fcitx5 ≥ 5.0.14 dev headers (including the Emoji module), libevdev, libinput, libudev, gettext, Python 3. Per-distro package lists are in `README.en.md`.

```sh
cmake -B build -DBUILD_TESTING=ON          # BUILD_TESTING defaults to OFF
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Running a subset of tests:

```sh
ctest --test-dir build -R lotus_key_processor --output-on-failure   # by ctest name
ctest --test-dir build -L integration      # by label: unit, integration, golden, preedit, apprule, smooth, ...
./build/test/lotus_unit_tests --run engine # engine unit groups share one binary; --run keys are listed in test/lotus-unit-test-main.cpp
./build/test/preedit_lifecycle             # each headless integration test is its own binary
```

- ctest sets `LOTUS_SOCKET_NAMESPACE` for the tests that use the uinput keyboard socket: the ones listed with `RESOURCE_LOCK lotus_kb_socket` in `test/CMakeLists.txt`. Add any new socket test to that list. If you run those binaries directly, set `LOTUS_SOCKET_NAMESPACE=<anything>` yourself. Otherwise they use the same abstract socket name as a real `fcitx5-lotus-server` running for your user.
- `LOTUS_GOLDEN_DIR=<dir>` points the golden replays (`--run engine`, `--run key_processor`, `--run golden_smoke`) at another corpus. `test/golden/README.md` still uses the old per-test binary names (`./build/test/lotus_engine_test`); the current binary is `lotus_unit_tests --run …`.

Formatting (CI checks both):

```sh
git ls-files '*.cpp' '*.h' | xargs clang-format -i   # LLVM-based, 4-space indent, 180 columns, namespace bodies indented
ruff check --fix settings-gui && ruff format settings-gui
```

`.clang-tidy` is present but CI does not run it. `compile_commands.json` is exported into the build directory.

Run the settings GUI from source with `python3 settings-gui/main.py`. The CMake-generated `paths.py` and `version.py` have import fallbacks. The GUI loads and saves config through the fcitx5 D-Bus controller, so it needs a running fcitx5 with the addon loaded.

## Architecture

One CMake tree builds four deliverables:

| Path | Target | Role |
|---|---|---|
| `lotus/` | `lotus_engine` (static lib, alias `Lotus::Engine`) | Pure C++ Vietnamese engine with no fcitx dependency |
| `src/` | `lotus` (fcitx5 `MODULE`, `lotus.so`) | The addon: typing modes, config, menus and actions |
| `server/` | `fcitx5-lotus-server` | Helper that injects keys through uinput and reports mouse clicks |
| `settings-gui/` | `fcitx5-lotus-settings` (installed Python app) | QtPy settings UI that talks to fcitx5 over D-Bus |

`common/` holds code shared by the addon and the server: the wire protocol (`KbMsg`/`KbOp`), socket naming, the peer executable check, and `FdGuard`. The server must not link the addon core, so keep `common/` header-only apart from the small `lotus_common_executable_path` object library.

### Engine (`lotus/`)

- This is a 1:1 port of bamboo-core. `engine.*` comes from `bamboo.go`. `key_processor.*` comes from fcitx5-bamboo's `fcitxbambooengine.go` and decides which keys reach the engine, when text is committed, macros, output charset, and auto-restore of non-Vietnamese words. Doc comments name the Go original ("like Go's X"); keep that mapping when you edit.
- Text is UTF-32 (`std::u32string`) throughout the engine and the addon. Conversion to UTF-8 happens only at the fcitx boundary (`src/lotus-fcitx-text.*`) and when loading config or dictionary data.
- The tables in `lotus/data/` were generated from the upstream Go sources. The generator is not in this repo.
- `test/golden/*.tsv` pins the engine's behavior. It was dumped from the Go engine and is the source of truth. It cannot be regenerated here; the dumper lives in the LotusInputMethod/bamboo repo. Never hand-edit the golden files to make a test pass.

### Addon (`src/`)

- `LotusEngine` (`lotus-engine.*`) implements fcitx's `InputMethodEngineV2`. It owns the config, actions and menus, the mode-menu and cycle-mode hotkeys, and per-app rules. It also holds the dictionary and macro table, which all input contexts share through `shared_ptr`.
- `LotusState` (`lotus-state.*`) is the per-`InputContext` property. It wraps a `lotus::KeyProcessor`, and `handleActiveMode` dispatches each key to the active mode.
- Each mode (`LotusMode` in `lotus-config.h`) is a different strategy for getting text into the application:
  - `Preedit`: classic fcitx preedit.
  - `SurroundingText`: reads the word before the cursor, rebuilds the engine from it, then calls `deleteSurroundingText` and commits.
  - The uinput modes `Smooth`, `SuperSmooth`, `Minecraft` and `Select` (`isUinputMode`): characters are committed immediately. To change text already typed, the addon asks the server to send real BackSpace presses (Shift+Left selection in `Select` mode). Those injected keys come back through fcitx as ordinary key events. `LotusState` counts the echoes (`expected_backspaces_`) and commits the replacement only after the last one. Keys typed in the meantime are held in `buffered_keys_` and replayed afterwards.
  - `Emoji` is the emoji picker. `Off` passes keys through and can still expand macros if configured.
- State shared by all input contexts is kept in process-wide atomics in `lotus-utils.cpp` (`realMode`, `is_deleting_`, `uinput_client_fd_`, `mouse_socket_fd`, …).
- Config:
  - `lotusConfig` is saved to `conf/lotus.conf`.
  - The sub-configs `custom_keymap`, `lotus-macro` and `app_rules` are exposed through `getSubConfig`/`setSubConfig` at `fcitx://config/addon/lotus/<path>`, and the settings GUI uses those paths.
  - Option keys, `FCITX_CONFIG_ENUM_NAME*` strings and the app-rule mode integers (`modeToInt`/`intToMode`) are a compatibility contract with the GUI and with users' saved configs. `test/config-enum-names.cpp` checks the enum names.
- `LOTUS_USE_MODERN_FCITX_API` is defined for Fcitx ≥ 5.1.13 (`StandardPaths` instead of the legacy `StandardPath`). Keep both branches compiling.
- The fcitx compiler settings build with hidden symbol visibility. Mark anything the headless tests call with `LOTUS_EXPORT` (`common/common-export.h`).

### Server and IPC

- `fcitx5-lotus-server -u <user>` runs as the system user `uinput_proxy` (group `input`). It is started by the `fcitx5-lotus-server@<user>.service` systemd template, or by the OpenRC/runit scripts in `misc/`. A udev rule grants that user read-write access to `/dev/uinput` through an ACL.
- Two `SOCK_SEQPACKET` sockets live in the abstract namespace. `LOTUS_SOCKET_NAMESPACE` overrides the `<user>` part of their names.
  - `lotussocket-<user>-kb_socket`: the addon sends `KbMsg` requests (backspace or select, a count, and pre/interval/post delays) to the server.
  - `lotussocket-<user>-mouse_socket`: the server sends one byte on every mouse button press, and the addon then drops its stale buffers.
- Peer checks:
  - The server accepts a client only if it has the target user's UID and its `/proc/<pid>/exe` matches `LOTUS_FCITX5_EXECUTABLE`.
  - The addon accepts the mouse-socket peer only if its `/proc/<pid>/cmdline` (argv[0]) matches `LOTUS_SERVER_EXECUTABLE`.
  - Both checks go through `isAllowedExecutablePath`, which also accepts binaries under `LOTUS_ALT_EXECUTABLE_PREFIX` (`/nix/store/` on NixOS).
- These paths, plus `LOTUS_SETFACL_EXECUTABLE` and `LOTUS_UINPUT_PROXY_USER`, are CMake cache variables that packagers override. Don't hard-code them.

### Tests (`test/`)

- Engine unit tests: every group builds into the single `lotus_unit_tests` executable, which links only `Lotus::Engine`. Each source file defines `int run_<name>()`. Register a new group in `lotus-unit-test-main.cpp` and with `add_lotus_unit_test(...)` in `test/CMakeLists.txt`, which creates one ctest entry per group. The target keeps `UNITY_BUILD OFF` because the test files define identical file-local helpers.
- Headless integration tests link `lotus_test_core`, a shared library built from the same `lotus_core_objects` as the module (all of `src/` except the `lotus.cpp` factory).
  - Each test creates a real `fcitx::Instance` and drives `LotusEngine` through `TestInputContext` (`test/test-input-context.h`), which records commits, deletions, forwarded keys and preedit updates.
  - Call `configureTestPaths()` first to sandbox `HOME` and the XDG directories.
  - `KbSocketListener` (`test/kb-socket-listener.h`) plays the server's role.
  - Register with `add_lotus_headless_test(...)`.
- A new `src/` file must be added to `fcitx_lotus_core_sources` in the top-level `CMakeLists.txt`, not in `src/CMakeLists.txt`, so that both the module and the tests compile it.

## Workflow

- PRs target `dev`, never `main`. Only maintainers merge into `main`, for releases. Commit messages use conventional prefixes (`feat:`, `fix:`, `refactor:`, `perf:`, `test:`, `chore:`, …).
- CI (`.github/workflows/build.yml`) builds against fcitx5 from git on Arch (glibc) and Alpine (musl), each with both gcc and clang, so code must build on musl and with both compilers. The Debian, RPM and Arch packaging builds also run `ctest`. The Nix workflow runs `nix flake check` and builds the HEAD package.
- A version bump touches `project(... VERSION ...)` in `CMakeLists.txt`, `packaging/debian/changelog`, and both `packaging/rpm/*/fcitx5-lotus.spec`. After a release, CI opens the PR that updates the Nix package's version and hash.
- Translations: run `./Messages.sh` to regenerate `po/fcitx5-lotus.pot` from the C++, Python, desktop and metainfo sources (`_()` / `N_()`), then `./update_translate.sh` to merge it into `po/*.po`.
- Source files carry SPDX headers. Files in `lotus/` and `lotus/data/` must keep the bamboo-core attribution (Luong Thanh Lam, 2018, MIT); see `CREDITS`.
