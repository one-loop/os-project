# Phase 2 Worklog (Client/Server Remote Shell)

## Goal
Implement server-side execution so `myshell_client` sends a command and `myshell_server` executes it using the existing shell logic, then returns output.

## Plan (step-by-step)
1. Inspect teammate networking baseline (`myshell_client.c`, `myshell_server.c`, `myshell_net.h`).
2. Reuse existing local shell modules (`parser`, `executor`, `pipeline`, `builtins`) instead of rewriting command logic.
3. In server loop, replace stub response with real command execution output.
4. Keep wire protocol unchanged (fixed-size `MYSHELL_CMD_MAX` request and `MYSHELL_RESP_MAX` response).
5. Compile, run server+client, and verify output/behavior against assignment screenshots.
6. Clean up output formatting/messages only if mismatch appears.

## Changes Done (this session)

### 1) Server now executes real commands
- File: `myshell_server.c`
- Added module includes:
  - `executor.h`
  - `pipeline.h`
- Added helper: `execute_and_capture()`
  - Forks a child
  - Redirects child `stdout` and `stderr` into a capture pipe
  - Dispatches command using same path as local shell:
    - `run_multi_piped_command()` if command contains `|`
    - `run_command()` otherwise
  - Parent reads captured output into `response`
  - Sends newline when command output is empty (prompt stays clean)
- Replaced old server stub reply with real captured output.
- Added handling for `exit` command from client side session (`bye\n` then break).

### 2) Build integration
- File: `Makefile`
- Updated `myshell_server` target to link with:
  - `parser.o`
  - `executor.o`
  - `pipeline.o`
  - `builtins.o`
- This keeps command behavior consistent between local and remote shell.

### 3) First smoke test + fix
- Built with `make clean && make` successfully.
- Ran server/client end-to-end with commands:
  - `echo hello-remote`
  - `ls | wc -l`
- Found one issue: server startup logs were leaking into the first command response.
- Root cause: buffered stdio data duplicated across `fork()` in capture helper.
- Fix: added `fflush(NULL);` before `fork()` in `execute_and_capture()`.

### 4) Output-format tuning for assignment screenshots
- Removed client-side connection chatter so the client behaves like a normal shell.
- Replaced server socket-status chatter with screenshot-style logs:
  - `[INFO] Server started, waiting for client connections...`
  - `[INFO] Client connected.`
  - `[RECEIVED] ...`
  - `[EXECUTING] ...`
  - `[OUTPUT] ...`
  - `[ERROR] ...`
- Trimmed server-side logged error text so quoted messages match the screenshot style.

## Why this approach
- No duplication of shell parsing/execution logic.
- Server behavior stays aligned with already-tested local shell behavior.
- Fastest path to a working remote shell for Phase 2.

## Pending Next Verification
- Rebuild (`make clean && make`)
- Run `./myshell_server` and `./myshell_client`
- Compare output format against assignment screenshots and adjust message formatting if needed.
