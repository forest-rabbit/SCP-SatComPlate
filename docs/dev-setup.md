# Development Setup

This project is based on ns-3.33 and uses waf for build management.

## Ubuntu prerequisites

Install the basic build tools and optional development helpers:

```bash
sudo apt update
sudo apt install -y build-essential gcc g++ python3 python3-dev python3-setuptools \
  pkg-config sqlite3 libsqlite3-dev libxml2 libxml2-dev libgsl-dev \
  tcpdump doxygen graphviz valgrind
```

For VS Code C++ navigation, install clangd:

```bash
sudo apt install -y clangd-15
```

## Python environment

Create a project-local Python environment with uv:

```bash
cd /path/to/SatCompute
uv venv --python /usr/bin/python3
source .venv/bin/activate
```

The `.venv/` directory is local and should not be committed.

## Configure and build

Configure only the SatCompute module and its ns-3 dependencies. Upstream
examples and test libraries are disabled for the normal development build:

```bash
./waf configure --disable-examples --disable-tests --enable-modules=satcompute
./waf build
```

The build writes the compilation database to:

```text
build/compile_commands.json
```

This file is generated locally. Rebuild it on every new machine instead of copying it from another machine.

## VS Code notes

Recommended WSL extensions:

```text
clangd
C/C++
Python
Pylance
ShellCheck
Markdown All in One
GitLens
Error Lens
```

Use clangd as the main C++ language server. If Ctrl+Click navigation does not work:

1. Make sure `clangd-15` is installed.
2. Run `./waf configure --disable-examples --disable-tests --enable-modules=satcompute`.
3. Run `./waf build`.
4. Reload VS Code with `Developer: Reload Window`.

The local VS Code workspace settings can point clangd to:

```text
/usr/bin/clangd-15
```

and use:

```text
build/compile_commands.json
```

for C++ code navigation.
