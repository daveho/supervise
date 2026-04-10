# supervise

This program is intended to supervise a subprocess to make sure
that any child processes created by the subprocess are terminated
in a timely manner.

Each time the supervised subprocess forks a child process, it should
print a message to file descriptor 3 of the form

```
pid N
```

where `N` is the process id of the child process. In this way, the
`supervise` program knows the pids of all of the supervised process's
child processes.

When the supervised process exits, the `supervise` program sends a
`SIGKILL` to each of the supervised process's children.

If the the `supervise` program is sent a `SIGINT` or `SIGTERM` signal,
it responds by sending a `SIGKILL` to the supervised process. When the
process exits, its children will be terminated as usual. That means
that if you use `supervise` to launch a process, you can just send it
a `SIGINT` or `SIGTERM` signal, and have some confidence that the
supervised process and all of its children will be terminated.

## WTF?

I use this program in the context of automated testing where we want to
have a timeout asssociated with a test, and the test may involve multiple
processes. The `supervise` program is helpful to ensure that all processes
are terminated reliably if a timeout occurs.

## License

This work is in the public domain.
