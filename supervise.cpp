// supervise by David Hovemeyer is marked CC0 1.0.
// To view a copy of this mark, visit https://creativecommons.org/publicdomain/zero/1.0/

#include <set>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>

// Exit codes

// Returned if an "unexpected" error (one not anticipated to
// ocurr in "normal" operation) occurs
#define UNEXPECTED_ERROR  121

// Returned if the main process was terminated by a signal
#define MAIN_PROC_CRASHED 122

// Pid of the "main" process (which is the process that's
// being supervised, and which will send us the pids
// of its child processes via a pipe.)
pid_t g_mainproc;

// Set to true if SIGTERM or SIGINT is received.
volatile bool g_signal_received;

void fatal( const std::string &msg, int exit_code = UNEXPECTED_ERROR );

void fatal( const std::string &msg, int exit_code ) {
  fprintf( stderr, "Error: %s\n", msg.c_str() );
  _exit( exit_code );
}

void install_handler( int signum, void (*handler)(int) ) {
  struct sigaction sa;

  sa.sa_handler = handler;
  sigemptyset( &sa.sa_mask );
  sa.sa_flags = 0;

  if ( sigaction( signum, &sa, NULL ) < 0 )
    fatal( "couldn't install handler for signal " + std::to_string(signum) );
}

// This actually handles both SIGTERM and SIGINT
void sigterm_handler( int ) {
  g_signal_received = true;

  // Send SIGKILL to the main process.
  // This should reliably cause it to stop running,
  // meaning it will close fd 3, so we'll read an
  // EOF on the pipe used to send child pids from
  // the main process.. That should guarantee that we know about
  // all child processes the main process created,
  // except in the (hopefully rare) case that the
  // main process died after creating a child
  // process but before sending its pid to the pipe.
  kill( g_mainproc, SIGKILL );
}

void usage( void ) {
  fprintf( stderr,
           "Usage: ./supervise [options] <program> [<args...>]\n"
           "Options:\n"
           "  --fd=N     supervised process writes child process info to fd N" );
  exit( 1 );
}

int main(int argc, char **argv) {
  // Pids of all processes that we know are running.
  std::set<pid_t> g_procs;

  // Pipe fds of the pipe through which we get reports from the supervised
  // process (about pids of child processes it creates)
  int g_pipe_fds[2];

  // The file descriptor the supervised process will use to
  // send information about child processes
  int notification_fd = 3;

  // Handle options (just one for now)
  if ( argc > 1 && strncmp( argv[1], "--fd=", 5 ) == 0 ) {
    const char *n = argv[1] + 5;
    if ( sscanf( n, "%d", &notification_fd ) != 1 )
      usage();

    // Fix up argv/argc because there is now one less command line argument
    // to pass on to the supervised command
    argv[1] = argv[0];
    argv++;
    argc--;
  }

  if ( argc < 2 )
    usage();

  // Install handler for SIGTERM (which is what timeout will send
  // if this process times out)
  install_handler( SIGTERM, sigterm_handler );

  // Handle SIGINT as well (useful in the case that
  // we're running in a terminal and the user uses control-C)
  install_handler( SIGINT, sigterm_handler );

  // Use dup2 to make notification fd a duplicate of stdin.
  // This prevents the pipe() system call from allocating
  // notification fd as the read end of the pipe.
  if ( dup2( 0, notification_fd ) < 0 )
    fatal( "dup2() failed (while reserving notification fd)" );

  // Create pipe for main process to use to let us know about
  // child processes it creates.
  if ( pipe( g_pipe_fds ) < 0)
    fatal( "pipe() failed" );

  // Use dup2 to make the write end of the pipe notification fd.
  // This will close the current notification fd, which is a duplicate
  // of stdin.
  if ( dup2( g_pipe_fds[1], notification_fd ) < 0 )
    fatal( "dup2() failed (creating notification fd as write end of pipe)" );

  // At this point we should be ready to creete tha main process.
  g_mainproc = fork();
  if ( g_mainproc < 0 )
    fatal( "fork() failed\n" );

  if ( g_mainproc == 0 ) {
    // In child: execute the supervised command
    extern char **environ;
    if ( execve( argv[1], argv + 1, environ ) < 0 ) {
      // execve failed: let the supervisor know
      std::stringstream ss;
      ss << "error " << errno << "\n";
      std::string msg = ss.str();
      if ( write( notification_fd, msg.data(), msg.size() ) != ssize_t( msg.size() ) )
        // This really shouldn't happen
        fatal( "in child, execve() failed, and write() to pipe failed" );
      _exit( UNEXPECTED_ERROR );
    }
  }

  // The parent can close the write end of the pipe
  close( g_pipe_fds[1] );
  close( notification_fd );

  // Read from the pipe
  FILE *pipe_in = fdopen( g_pipe_fds[0], "r" );
  char msg[1024];
  while ( fgets( msg, sizeof(msg), pipe_in ) ) {
    //fprintf( stderr, "supervise: read command %s\n", msg );
    std::stringstream ss( msg );
    std::string tag;
    int n;
    if ( !( ss >> tag >> n ) )
      fprintf( stderr, "Error: main process sent invalid message '%s'\n", msg );
    else {
      if ( tag == "error" )
        fprintf( stderr, "Unrecoverable error in child: %s\n", strerror( n ) );
      else if ( tag == "pid" )
        g_procs.insert( pid_t(n) );
      else if ( tag == "ignore" )
        // this just allows the supervised process to check whether it is
        // being supervised
        ;
      else
        fprintf( stderr, "Error: main process sent invalid message '%s'\n", msg );
    }
  }
  fclose( pipe_in );

  int wstatus;
  bool got_mainproc_wstatus = true;

  // Wait for main process to finish
  if ( waitpid( g_mainproc, &wstatus, 0 ) < 0 ) {
    fprintf( stderr, "Error: main process wait failed: %s\n", strerror( errno ) );
    got_mainproc_wstatus = false;
  }

  // Kill child processes created by main process.
  // Note that there is an ABA problem here, in that we don't
  // know whether the child pids we have actually represent
  // the same process as the child forked by the main process.
  // For our purposes (autograders which are extremely unlikely
  // to reuse pids), this shouldn't be a problem in practice.
  // Also note that we don't check for error return values because
  // it's possible (and likely) that some or all of these processes
  // no longer exist.
  for ( auto i = g_procs.begin(); i != g_procs.end(); ++i )
    kill( *i, SIGKILL );

  if ( !got_mainproc_wstatus )
    // We don't know anything about how the main process terminated
    // (or even if it terminated.) This should not happen.
    return UNEXPECTED_ERROR;

  // Check whether the main process exited.
  // It's very possible that it didn't, especially if WE killed it
  // when we received a SIGTERM.
  if ( !WIFEXITED( wstatus ) )
    return MAIN_PROC_CRASHED;

  // Main process did exit, so the supervisor exits with its
  // exit code
  return WEXITSTATUS( wstatus );
}
