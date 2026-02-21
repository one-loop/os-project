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
