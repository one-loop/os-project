# Operating Systems Project

A custom remote shell/CLI written in **C** for the NYU Abu Dhabi Operating Systems course (Spring 2026). The goal is to build a minimal shell that simulates core OS services (processes, I/O redirection, pipes) and to practice Linux system calls: `fork()`, `exec()`, `wait()`, `dup2()`, and `pipe()`.

## What This Project Does

**myshell** is a command-line interpreter that:

- **Reads and parses** user input, then **runs commands as child processes** (parent shell forks and waits for children).
- **Executes programs** via `execvp()` — both simple commands (`ls`, `ps`) and commands with arguments (`ls -l`, `ps aux`), including executables by path (e.g. `./hello`).
- **Supports I/O redirection:**
  - Output: `command > output.txt`
  - Error: `command 2> error.log`
  - Input: `command < input.txt`
- **Supports pipes:** chains of commands with `|` (e.g. `cmd1 | cmd2 | cmd3`), with the shell waiting for the last process in the pipeline before showing the next prompt.
- **Handles composed combinations** of redirection and pipes (e.g. `cmd1 < in.txt | cmd2 > out.txt`, `cmd1 < in.txt | cmd2 2> err.log | cmd3 > out.txt`).
- **Validates and reports errors** for invalid or incomplete usage (missing files, missing commands after `|`, empty commands between pipes, unknown commands, etc.).
- **Presents a simple prompt:** a single `$` on a line; the `exit` command terminates the shell.

Phase 1 focuses on implementing this behavior in C on a Linux environment, with clear design, error handling, and a Makefile for building on the course’s remote Linux server.

A **networked front end** (`myshell_client`) is also provided: it shows the same `$` prompt, sends each command line to **`myshell_server`**, and prints whatever the server sends back. The server currently contains a **stub** that acknowledges commands; the real execution and response formatting should be implemented on the server by reusing the same parsing and execution logic as local `myshell`.

## How to compile

From the project directory (the one containing the `Makefile`):

```bash
make
```

This builds:

- **`myshell`** — local shell: `main.c`, `parser.c`, `executor.c`, `pipeline.c`, and `builtins.c`.
- **`myshell_client`** — TCP client: `myshell_client.c` (depends on `myshell_net.h`).
- **`myshell_server`** — TCP server stub: `myshell_server.c` (depends on `myshell_net.h`).

To remove the binaries and object files:

```bash
make clean
```

**Requirements:** `gcc` and `make`.

## How to run (local shell)

```bash
./myshell
```

You will see a `$` prompt. Type shell commands as you would in a normal Unix shell. To quit:

```bash
exit
```

You can also end input with **Ctrl-D** (EOF), which exits the shell.

## Remote shell: setup and usage

1. **Build** everything with `make` (see above).
2. **Start the server first** in one terminal so it is listening before the client connects:

   ```bash
   ./myshell_server
   ```

3. **Start the client** in a second terminal on the same machine (the client connects to `127.0.0.1`):

   ```bash
   ./myshell_client
   ```

4. At the `$` prompt, type a command line as you would in the local shell. The client sends it to the server; the server must **receive**, **process**, and **send one response** before the client shows the next prompt.
5. Type **`exit`** or press **Ctrl-D** on the client to quit. The stub server exits its loop when the client closes the connection or `recv` returns an error or zero.

**Port and firewall:** The default TCP port is **`8000`**, defined as `MYSHELL_PORT` in `myshell_net.h`. Nothing else should bind to that port while the server runs. If another process uses port 8000, change the macro in `myshell_net.h` and rebuild **both** client and server so sizes and port stay matched.

**Order of startup:** Always run **`myshell_server`** before **`myshell_client`**. If the client starts first, `connect` fails and the client exits.


## Notes for server implementation

These details matter so the client and server stay compatible and the UI behaves like a shell.

### Wire protocol (must stay aligned with the client)

Constants live in **`myshell_net.h`** (`MYSHELL_PORT`, `MYSHELL_CMD_MAX`, `MYSHELL_RESP_MAX`). **Change them in one place** and recompile both programs.

| Direction | Size | Semantics |
|-----------|------|-----------|
| Client → server | Exactly **`MYSHELL_CMD_MAX`** (256) bytes per command | Fixed-size “packet.” The client zero-pads the buffer and copies the trimmed command line (no trailing newline). Treat the payload as a C string: the meaningful text ends at the first `'\0'`, or use at most 255 characters plus termination. |
| Server → client | Exactly **`MYSHELL_RESP_MAX`** (4096) bytes per reply | Fixed-size “packet.” The client **`recv_all`**s **`MYSHELL_RESP_MAX`** bytes into a buffer one byte larger, then sets a trailing **`'\0'`** for `printf("%s", ...)`. The payload should be **NUL-terminated** early in the frame (for example `snprintf` then `memset` the whole buffer first, as the stub does). Include a final newline in the text if you want the next `$` to appear on a fresh line. |

**TCP is a byte stream:** One `send`/`recv` call may move only part of a fixed-size message. The reference **`myshell_client`** and **`myshell_server`** use small **`send_all` / `recv_all`** loops so each turn transfers exactly **`MYSHELL_CMD_MAX`** bytes (command) and **`MYSHELL_RESP_MAX`** bytes (response). If you change the server, keep the same all-or-nothing reads/writes (or adopt explicit length-prefix framing) so the two sides never misalign.

### Where to implement execution

- **File:** `myshell_server.c` — replace the body of the `for (;;)` loop where the stub builds the placeholder string.
- **Goal:** For each received command string, run the same behavior as local **`myshell`**: parsing (`parser.c`), single commands (`executor.c` / `run_command`), pipelines (`pipeline.c` / `run_multi_piped_command`), and builtins (`builtins.c`). The clean approach is to **link the server against the same object files** as `myshell` (or call shared functions) instead of duplicating logic, then capture **stdout/stderr** (and possibly stdin for interactive programs) into the **`MYSHELL_RESP_MAX`** response buffer, or extend the protocol if output can exceed 4096 bytes.

### Client address (remote deployment - don't worry about this for now)

The client currently connects to **`127.0.0.1`** in `myshell_client.c`. For a server on another host, you will need a configurable hostname or IP (for example `getaddrinfo` or `inet_pton` with a string from `argv` or an environment variable) **and** any firewall or VPN rules that allow TCP **`MYSHELL_PORT`** between the two machines.

### Stub behavior today

The stub **accepts one client**, **closes the listening socket** after accept, then loops: **recv** command packet → **send** one fixed-size response. You can extend this to multiple clients or keep the listener open, as long as the **per-command request/response sizes** (or an agreed new framing) remain consistent with **`myshell_client.c`**.
