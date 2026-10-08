# cloudIST

CloudIST simulates VM reservations and application execution across a set of
physical servers. It reads `.conf` command files from an input directory,
processes those files in alphabetical queue order, and runs up to a configured
number of worker threads.

## Build and run

```sh
make
./cloudIST <SERVERS> <RAM_GB> <DISK_GB> <CPU_CORES> <INPUT_DIR> <MAX_THREADS>
```

Example:

```sh
./cloudIST 4 16 500 8 ./configs 3
```

## Project layout

- `src/` contains the C implementation and the program entry point.
- `include/` contains the public headers and shared constants.
- `Makefile` builds the program from the source and header directories.
- `README.md` documents usage and runtime behavior.

The runtime requires `cpulimit` in `PATH`. `make clean` removes the executable
and object files produced by the build.

Each configuration file can contain `D`, `R`, `A`, `L`, and `E` commands. Lines
whose first non-whitespace character is `#`, and blank lines, are ignored. A
VM's input directory is copied recursively to
`/tmp/CloudIST/<reservation-id>/<vm-id>` before execution; the application is
started with that VM directory as its working directory.

The `A` command starts a separate process for each VM and returns to the
configuration reader. A reaper thread monitors those processes. When one exits,
its copied input directory is deleted and its resources are returned to the
server. Reservations are removed once all their VMs have exited.

RAM is limited with `RLIMIT_AS`, maximum file size with `RLIMIT_FSIZE`, and CPU
with `cpulimit`. The CPU percentage is computed from the VM vCPU request, the
physical server CPU count, and the number of servers in the data center. Since
`cpulimit` accepts integer percentages, the result is rounded to the nearest
whole percent (with a minimum limit of 1%).
