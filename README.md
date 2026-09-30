Exyde is a Unix-compatible microkernel-based operating system for x86_64.

The kernel provides only fundamental mechanisms: threads and scheduling, address spaces and virtual memory primitives, physical memory management, IPC with capability transfer, synchronization, interrupts, capabilities/handles, process and thread primitives, and minimal hardware access.

Everything else lives in userspace: the VFS server, filesystem servers, console and device servers, the fd table, the libc allocator, the shell, and the service manager (init).

Unix and POSIX compatibility is a fundamental design goal: processes, file descriptors, stdin/stdout/stderr, pipes, signals, TTY, filesystem hierarchy, devices, sockets, environment variables, and exec-style process launching are all part of the design. Linux compatibility is a later layer built on top of the Unix foundation, translating Linux APIs into native Exyde primitives rather than turning the kernel into Linux.
