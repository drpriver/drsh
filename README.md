# drsh

This is a basic unix-like shell. It is missing a lot of features, but is
functional enough to use as an interactive shell.

I mostly wrote it as I didn't like the shells that come with Windows and not
having readline-like key bindings was breaking my brain.
On Linux and MacOS, I currently prefer using fish, but maybe I'll hack on this
long enough to make it better than fish.

## Building

The moral equivelant of

    cc drsh.c -o drsh

should build. It should compile with clang, gcc and cl.

A basic makefile is provided.

## Builtin Commands

- cd
- echo
- set ENVVAR value

## Features

- Single file
- Tab-completion of paths
    - Uses custom algorithm so things like `dbg` can complete to `debugger`
- Readline-like key bindings
- Runs on Linux, MacOS, Windows
- globbing on linux, macos
    - Windows does not provide globbing as the command line is limited to 32k
      characters and as DOS and cmd did not expand wildcards it is expected
      programs will do their own wildcard expansion.
- environment variables
    - case sensitive on macos/linux, case insensitive (but case preserving) on
      windows
- prompt prints the date, etc.
- command history

## Missing Features

- Pipes
- File redirection
- background/foreground process
- command history search
- command completion
- exec
    - Not clear how to support this on windows.
    - Maybe we only support on unix-like?
- aliases

## Unplanned features

- functions
- control flow

## Bugs

- Uses glob(3), so brace-expansion doesn't work right.
- Needs tests
