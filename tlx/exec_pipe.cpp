/*******************************************************************************
 * tlx/exec_pipe.cpp
 *
 * The ExecPipe library provides a convenient C++ interface to execute child
 * programs connected via pipes on Linux/Unix. It is a front-end to the system
 * calls fork(), pipe(), select() and execv() and hides all the complexity of
 * these low-level functions. It allows a program to build a sequence of
 * connected children programs with input and output of the pipe sequence
 * redirected to a file, string or file descriptor. The library also allows
 * custom asynchronous data processing classes to be inserted into the pipe or
 * placed at source or sink of the sequence.
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2010-2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#include <tlx/exec_pipe.hpp>

#include <tlx/byte_ring_buffer.hpp>

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tlx {

/** \page tlx_execpipe C++ Interface for Executing Child Programs

The ExecPipe library provides a convenient C++ interface to execute child
programs connected via pipes. It is a front-end to the system calls `fork()`,
`pipe()`, `select()`, and `execv()` and hides all the complexity of these
low-level functions. It allows a program to build a sequence of connected
children programs with input and output of the pipe sequence redirected to a
file, string or file descriptor. The library also allows custom asynchronous
data processing classes to be inserted into the pipe or placed at source or sink
of the sequence.

An execution pipe consists of an input stream, a number of pipe stages and an
output stream. The input and output streams can be a plain file descriptor, a
file, a `std::string` or a special processing class. Each pipe stage is either
an executed child program or an intermediate function class. At the junction
between each stage in the pipeline the following program's stdin is connected to
the preceding stage's stdout. The input and output streams are connected to the
start and end of the pipe line.

<table style="margin: 0 auto; border: 0px"><tr><td>
<pre>
   Input Stream               Pipe Stages                     Output Stream
       none      |                                           |     none
        fd       |               exec()                      |      fd
       file      |-> stage ->      or      -> stage -> ... ->|     file
      string     |          ExecPipeFunction                 |    string
  ExecPipeSource |                                           | ExecPipeSink
</pre>
</td></tr></table>

All this functionality is wrapped into a flexible C++ class, which can be used
in an application to construct complex sequences of external programs similar
to shell piping. Some common operations would be calls of mkisofs or tar
coupled with gzip or gpg and possibly send the output to a remote host via ssh
or ncftpput.

\section sec_usage Library Usage Tutorial

The following tutorial shows some simple examples on how an execution pipe can
be set up.

To use the library a program must

\code
#include <tlx/exec_pipe.h>
\endcode

and later link against libtlx.a or include the corresponding .o / .cpp in the
project's dependencies.

To run a sequence of programs you must first initialize a new ExecPipe
object. The ExecPipe object is referenced counted so you can easily pass it
around without deep-duplicating the object.

\code
tlx::ExecPipe ep;               // creates new pipe

tlx::ExecPipe ep_ref1 = ep;     // reference to the same pipe.
\endcode

Once created the input stream source can be set using one of the four
`set_input_*()` functions. Note that these are mutually exclusive, you must call
at most one of the following functions!

\code
// you can designate an existing file as input stream
ep.set_input_file("/path/to/file");

// or directly assign an already opened file descriptor
int fd = ...;
ep.set_input_fd(fd);

// or pass the contents of a std::string as input
std::string str = ...;
ep.set_input_string(&str);

// or attach a data generating source class (details later).
tlx::ExecPipeSource source;
ep.set_input_source(&source);
\endcode

The input stream objects are _not_ copied. The fd, string or source object must
still exist when calling `run()`.

After setting up the input you specify the individual stages in the pipe by
adding children programs to `exec()` or function classes. The ExecPipe provides
different variants of `add_exec*()`, which are derived from the `exec*()` system
call variants.

\code
// add simple exec() call with full path.
ep.add_exec("/bin/cat");

// add exec() call with up to three direct parameters.
ep.add_exec("/bin/echo", "one", "two", "three");

// add exec() call with many parameters. the vector is _not_ copied.
std::vector<std::string> tarargs;
tarargs.push_back("/bin/tar");
tarargs.push_back("--create");
tarargs.push_back("--verbose");
tarargs.push_back("--gzip");
tarargs.push_back("--file");
tarargs.push_back("/path/to/file");
ep.add_exec(&tarargs);

// add execp() call which searches $PATH. see man 3 execvp.
ep.add_execp("cat");

// same with up to three parameters.
ep.add_execp("echo", "one", "two", "three");

// and also works with a vector of arguments.
ep.add_execp(&tarargs);

// most versatile function: call execve() with program name, argv[] arguments
// and a set of environment variables.
std::vector<std::string> gzipargs;
gzipargs.push_back("gunzip");           // this changes argv[0]

std::vector<std::string> gzipenvs;      // set environment variable
gzipenvs.push_back("GZIP=-d --name");

ep.add_exece("/bin/gzip", &gzipargs, &gzipenvs);

// insert an intermediate data processing class into the pipe (details later).
tlx::ExecPipeFunction function;
ep.add_function(&function);
\endcode

After configuring the pipe stages the user program can redirect the pipe's
output using one of the four `set_output_*()` functions. These correspond
directly the to input functions.

\code // designate a file as output, it will be over-written,
ep.set_output_file("/path/to/file");

// or directly assign an already opened file descriptor
int fd = ...;
ep.set_output_fd(fd);

// or save output in a std::string object
std::string str = ...;
ep.set_output_string(&str);

// or attach a sink class (details later).
tlx::ExecPipeSink sink;
ep.set_output_sink(&sink);
\endcode

The three steps above can be done in any order. Once the pipeline is configured
as required, a call to `run()` will set up the input and output file
descriptors, launch all children programs, wait until these finish and
concurrently process data passed between parent and children.

If any system calls fail while running the pipe, the `run()` function will
`throw()` a `std::runtime_error` exception. So wrap `run()` in a try-catch
block.

\code
try {
    ep.run();
}
catch (std::runtime_error &e) {
    std::cerr << "Pipe execution failed: " << e.what() << std::endl;
}
\endcode

After running all children their return status should be checked. These can be
inspected using the following functions. The integer parameter specifies the
exec stage in the pipe sequence.

\code
// get plain return status as indicated by wait().
int rs = ep.get_return_status(0)

// get return code for normally terminated program.
int rc = ep.get_return_code(1);

// get signal for abnormally terminated program (like segfault).
int rg = ep.get_return_signal(1);
\endcode

Most program have a return code of 0 when no error occurred. Therefore, a
convenience function is available which checks whether all program stages
returned zero. This is what would usually be used.

\code
// check all that program returned zero
if (ep.all_return_codes_zero()) {
    // run was ok.
}
else {
    // error handling.
}
\endcode

After checking the return error codes the pipe's results can be used.

Below are three simple examples and one complex one of using the different
`exec()` variants and input/output redirections.

\section sec_functionals Data Processing Classes

One of the useful features of the ExecPipe classes is the ability to insert
intermediate asynchronous C++ data processing classes into the pipe
sequence. The data of the pipe line is returned to the parent process and, after
arbitrary computations in your C++, can be sent on to the following execution
stages. Besides intermediate processing, the input and output stream can be
attached to source or sink classes.

This feature can be used to generate input data, e.g. binary data or file
listing, or peek at the data flowing between stages, e.g. to compute a SHA256
digest, or to directly processes output data while the children are running.

The data processing classes must be derived from one of the three abstract
classes: ExecPipeSource for generating input streams, ExecPipeFunction for
intermediate processing between stages or ExecPipeSink for receiving output.

For generating an input stream a class must derive from ExecPipeSource and
implement the \ref ExecPipeSource::poll "poll()" function. This function is
called when new data can be pushed into the pipe. When \ref
ExecPipeSource::poll "poll()" is called, new data must be generated and
delivered via the \ref ExecPipeSource::write "write()" function of
ExecPipeSource. If more data is available \ref ExecPipeSource::poll "poll()"
must return true, otherwise the input stream is terminated.

Intermediate data processing classes must derive from ExecPipeFunction and
implement the two pure virtual function \ref ExecPipeFunction::process
"process()" and \ref ExecPipeFunction::eof "eof()". As the name suggests, data
is delivered to the class via the \ref ExecPipeFunction::process "process()"
function. After processing the data it may be forwarded to the next pipe stage
via the inherited \ref ExecPipeFunction::write "write()" function. Note that the
library does not automatically forward data, so if you forget to write() data,
then the following stage does not receive anything. When the preceding
processing stage closes its data stream the function \ref ExecPipeFunction::eof
"eof()" is called.

To receive the output stream a class must derive from ExecPipeSink. Similar to
ExecPipeFunction, an output sink must implement the two pure virtual function
\ref ExecPipeFunction::process "process()" and \ref ExecPipeFunction::eof
"eof()". However, different from an intermediate class the ExecPipeSink does
not provide a write() function, so no data can be forwarded.

For a full example of using ExecPipeSource to iterate through a file list and
ExecPipeFunction to compute an intermediate SHA2561 digest see \ref
sec_example4.

\section sec_simple1 Simple Example 1

Shows a simple setup which calls "echo" and saves the output to a string.

\snippet exec_pipe_example.cpp example1

\section sec_simple2 Simple Example 2

Shows a simple setup which writes a string to "sha1sum" and saves the output to
a string.

\snippet exec_pipe_example.cpp example2

\section sec_simple3 Simple Example 3

Shows a simple setup which lists "/bin", pipes the outputs to "grep" and "sort"
and saves the output to a string.

\snippet exec_pipe_example.cpp example3

\section sec_example4 Functions Example 4

This example shows how to use ExecPipeSource to tar a list of file names
and inserts an intermediate processing class, which calculates the SHA1
digest of the uncompressed tarball before gzipping it..

\snippet exec_pipe_example.cpp example4

 */

#define LOG_OUTPUT(msg, level)                          \
    do {                                                \
        if (debug_level_ >= level) {                    \
            std::ostringstream oss__;                   \
            oss__ << msg;                               \
            if (debug_output_)                          \
                debug_output_(oss__.str().c_str());     \
            else                                        \
                std::cout << oss__.str() << std::endl;  \
        }                                               \
    } while (0)

#define LOG_ERROR(msg)  LOG_OUTPUT(msg, ExecPipe::DL_ERROR)
#define LOG_INFO(msg)   LOG_OUTPUT(msg, ExecPipe::DL_INFO)
#define LOG_DEBUG(msg)  LOG_OUTPUT(msg, ExecPipe::DL_DEBUG)
#define LOG_TRACE(msg)  LOG_OUTPUT(msg, ExecPipe::DL_TRACE)

/*!
 * Main library implementation (internal object)
 *
 * Implementation class for ExecPipe. See the documentation of the front-end
 * class for detailed information.
 */
class ExecPipeImpl
{
private:
    //! reference counter
    unsigned int refs_;

private:
    // *** Debugging Output ***

    //! currently set debug level
    enum ExecPipe::DebugLevel debug_level_;

    //! current debug line output function
    void (* debug_output_)(const char* line);

public:
    //! Change the current debug level. The default is DL_ERROR.
    void set_debug_level(enum ExecPipe::DebugLevel dl) {
        debug_level_ = dl;
    }

    //! Change output function for debug messages. If set to nullptr (the
    //! default) the debug lines are printed to stdout.
    void set_debug_output(void (*output)(const char* line)) {
        debug_output_ = output;
    }

private:
    //! Enumeration describing the currently set input or output stream type
    enum StreamType {
        ST_NONE = 0,    //!< no special redirection requested
        ST_FD,          //!< redirection to existing fd
        ST_FILE,        //!< redirection to file path
        ST_STRING,      //!< input/output directed by/to string
        ST_OBJECT       //!< input/output attached to program object
    };

    //! describes the currently set input stream type
    StreamType input_;

    //! \name Input Stream
    //! \{

    //! for ST_FD the input fd given by the user. for ST_STRING and ST_FUNCTION
    //! the pipe write fd of the parent process.
    int input_fd_;

    //! for ST_FILE the path of the input file.
    const char* input_file_;

    //! for ST_STRING a pointer to the user-supplied std::string input stream
    //! object.
    const std::string* input_string_;

    //! for ST_STRING the current position in the input stream object.
    std::string::size_type input_string_pos_;

    //! for ST_OBJECT the input stream source object
    ExecPipeSource* input_source_;

    //! for ST_OBJECT the input stream ring buffer
    ByteRingBuffer input_rbuffer_;

    //! \}

    //! \name Output Stream
    //! \{

    //! describes the currently set input stream type
    StreamType output_;

    //! for ST_FD the output fd given by the user. for ST_STRING and
    //! ST_FUNCTION the pipe read fd of the parent process.
    int output_fd_;

    //! for ST_FILE the path of the output file.
    const char* output_file_;

    //! for ST_FILE the permission used in the open() call.
    int output_file_mode_;

    //! for ST_STRING a pointer to the user-supplied std::string output stream
    //! object.
    std::string* output_string_;

    //! for ST_OBJECT the output stream source object
    ExecPipeSink* output_sink_;

    //! \}

    //! \name Pipe Stages
    //! \{

    /*!
     * Structure representing each stage in the pipe. Contains arguments,
     * buffers and output variables.
     */
    struct Stage {
        //! List of program and arguments copied from simple add_exec() calls.
        std::vector<std::string>      args;

        //! Character pointer to program path called
        const char                    * prog;

        //! Pointer to user list of program and arguments.
        const std::vector<std::string>* argsp;

        //! Pointer to environment list supplied by user.
        const std::vector<std::string>* envp;

        //! Pipe stage function object.
        ExecPipeFunction              * func;

        //! Output stream buffer for function object.
        ByteRingBuffer                outbuffer;

        // *** Exec Stages Variables ***

        //! Call execp() variants.
        bool                          withpath;

        //! Pid of the running child process
        pid_t                         pid;

        //! Return status of wait() after child exit.
        int                           retstatus;

        //! File descriptor for child stdin. This is dup2()-ed to STDIN.
        int                           stdin_fd;

        //! File descriptor for child stdout. This is dup2()-ed to STDOUT.
        int                           stdout_fd;

        //! Constructor reseting all variables.
        Stage()
            : prog(nullptr), argsp(nullptr), envp(nullptr), func(nullptr),
              withpath(false), pid(0), retstatus(0),
              stdin_fd(-1), stdout_fd(-1)
        { }
    };

    //! typedef of list of pipe stages.
    typedef std::vector<Stage> stagelist_type;

    //! list of pipe stages.
    stagelist_type stages_;

    //! general buffer used for read() and write() calls.
    char buffer_[4096];

    //! \}

public:
    //! Create a new pipe implementation with zero reference counter.
    ExecPipeImpl()
        : refs_(0),
          debug_level_(ExecPipe::DL_ERROR),
          debug_output_(nullptr),
          input_(ST_NONE),
          input_fd_(-1),
          output_(ST_NONE),
          output_fd_(-1)
    { }

    //! Return writable reference to counter.
    unsigned int& refs() {
        return refs_;
    }

    //! \name Input Selectors
    //! \{

    /*!
     * Assign an already opened file descriptor as input stream for the first
     * exec stage.
     */
    void set_input_fd(int fd) {
        assert(input_ == ST_NONE);
        if (input_ != ST_NONE) return;

        input_ = ST_FD;
        input_fd_ = fd;
    }

    /*!
     * Assign a file as input stream source. This file will be opened read-only
     * and read by the first exec stage.
     */
    void set_input_file(const char* path) {
        assert(input_ == ST_NONE);
        if (input_ != ST_NONE) return;

        input_ = ST_FILE;
        input_file_ = path;
    }

    /*!
     * Assign a std::string as input stream source. The contents of the string
     * will be written to the first exec stage. The string object is not copied
     * and must still exist when run() is called.
     */
    void set_input_string(const std::string* input) {
        assert(input_ == ST_NONE);
        if (input_ != ST_NONE) return;

        input_ = ST_STRING;
        input_string_ = input;
        input_string_pos_ = 0;
    }

    /*!
     * Assign a ExecPipeSource as input stream source. The object will be queried
     * via the read() function for data which is then written to the first exec
     * stage.
     */
    void set_input_source(ExecPipeSource* source) {
        assert(input_ == ST_NONE);
        if (input_ != ST_NONE) return;

        input_ = ST_OBJECT;
        input_source_ = source;
        source->impl_ = this;
    }

    //! \}

    /*!
     * Function called by ExecPipeSource::write() to push data into the ring
     * buffer.
     */
    void input_source_write(const void* data, size_t size) {
        input_rbuffer_.write(data, size);
    }

    //! \name Output Selectors
    //! \{

    /*!
     * Assign an already opened file descriptor as output stream for the last
     * exec stage.
     */
    void set_output_fd(int fd) {
        assert(output_ == ST_NONE);
        if (output_ != ST_NONE) return;

        output_ = ST_FD;
        output_fd_ = fd;
    }

    /*!
     * Assign a file as output stream destination. This file will be created or
     * truncated write-only and written by the last exec stage.
     */
    void set_output_file(const char* path, int mode = 0666) {
        assert(output_ == ST_NONE);
        if (output_ != ST_NONE) return;

        output_ = ST_FILE;
        output_file_ = path;
        output_file_mode_ = mode;
    }

    /*!
     * Assign a std::string as output stream destination. The output of the
     * last exec stage will be stored as the contents of the string. The string
     * object is not copied and must still exist when run() is called.
     */
    void set_output_string(std::string* output) {
        assert(output_ == ST_NONE);
        if (output_ != ST_NONE) return;

        output_ = ST_STRING;
        output_string_ = output;
    }

    /*!
     * Assign a ExecPipeSink as output stream destination. The object will receive
     * data via the process() function and is informed via eof()
     */
    void set_output_sink(ExecPipeSink* sink) {
        assert(output_ == ST_NONE);
        if (output_ != ST_NONE) return;

        output_ = ST_OBJECT;
        output_sink_ = sink;
    }

    //! \}

    //! \name Add Pipe Stages
    //! \{

    /*!
     * Return the number of pipe stages added.
     */
    size_t size() const {
        return stages_.size();
    }

    /*!
     * Add an exec() stage to the pipe with given arguments. Note that argv[0]
     * is set to prog.
     */
    void add_exec(const char* prog) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        stages_.push_back(newstage);
    }

    /*!
     * Add an exec() stage to the pipe with given arguments. Note that argv[0]
     * is set to prog.
     */
    void add_exec(const char* prog, const char* arg1) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        newstage.args.push_back(arg1);
        stages_.push_back(newstage);
    }

    /*!
     * Add an exec() stage to the pipe with given arguments. Note that argv[0]
     * is set to prog.
     */
    void add_exec(const char* prog, const char* arg1, const char* arg2) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        newstage.args.push_back(arg1);
        newstage.args.push_back(arg2);
        stages_.push_back(newstage);
    }

    /*!
     * Add an exec() stage to the pipe with given arguments. Note that argv[0]
     * is set to prog.
     */
    void add_exec(const char* prog, const char* arg1, const char* arg2,
                  const char* arg3) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        newstage.args.push_back(arg1);
        newstage.args.push_back(arg2);
        newstage.args.push_back(arg3);
        stages_.push_back(newstage);
    }

    /*!
     * Add an exec() stage to the pipe with given arguments. The vector of
     * arguments is not copied, so it must still exist when run() is
     * called. Note that the program called is args[0].
     */
    void add_exec(const std::vector<std::string>* args) {
        assert(args->size() > 0);
        if (args->size() == 0) return;

        struct Stage newstage;
        newstage.prog = (*args)[0].c_str();
        newstage.argsp = args;
        stages_.push_back(newstage);
    }

    /*!
     * Add an execp() stage to the pipe with given arguments. The PATH variable
     * is search for programs not containing a slash / character. Note that
     * argv[0] is set to prog.
     */
    void add_execp(const char* prog) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        newstage.withpath = true;
        stages_.push_back(newstage);
    }

    /*!
     * Add an execp() stage to the pipe with given arguments. The PATH variable
     * is search for programs not containing a slash / character. Note that
     * argv[0] is set to prog.
     */
    void add_execp(const char* prog, const char* arg1) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        newstage.args.push_back(arg1);
        newstage.withpath = true;
        stages_.push_back(newstage);
    }

    /*!
     * Add an execp() stage to the pipe with given arguments. The PATH variable
     * is search for programs not containing a slash / character. Note that
     * argv[0] is set to prog.
     */
    void add_execp(const char* prog, const char* arg1, const char* arg2) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        newstage.args.push_back(arg1);
        newstage.args.push_back(arg2);
        newstage.withpath = true;
        stages_.push_back(newstage);
    }

    /*!
     * Add an execp() stage to the pipe with given arguments. The PATH variable
     * is search for programs not containing a slash / character. Note that
     * argv[0] is set to prog.
     */
    void add_execp(const char* prog, const char* arg1, const char* arg2,
                   const char* arg3) {
        struct Stage newstage;
        newstage.prog = prog;
        newstage.args.push_back(prog);
        newstage.args.push_back(arg1);
        newstage.args.push_back(arg2);
        newstage.args.push_back(arg3);
        newstage.withpath = true;
        stages_.push_back(newstage);
    }

    /*!
     * Add an execp() stage to the pipe with given arguments. The PATH variable
     * is search for programs not containing a slash / character. The vector of
     * arguments is not copied, so it must still exist when run() is
     * called. Note that the program called is args[0].
     */
    void add_execp(const std::vector<std::string>* args) {
        assert(args->size() > 0);
        if (args->size() == 0) return;

        struct Stage newstage;
        newstage.prog = (*args)[0].c_str();
        newstage.argsp = args;
        newstage.withpath = true;
        stages_.push_back(newstage);
    }

    /*!
     * Add an exece() stage to the pipe with the given arguments and
     * environments. This is the most flexible exec() call. The vector of
     * arguments and environment variables is not copied, so it must still exist
     * when run() is called. The env vector pointer may be nullptr, the args
     * vector must not be nullptr. The args[0] is _not_ override with path, so
     * you can fake program name calls.
     */
    void add_exece(const char* path,
                   const std::vector<std::string>* argsp,
                   const std::vector<std::string>* envp) {
        assert(path && argsp);
        assert(argsp->size() > 0);
        if (argsp->size() == 0) return;

        struct Stage newstage;
        newstage.prog = path;
        newstage.argsp = argsp;
        newstage.envp = envp;
        stages_.push_back(newstage);
    }

    /*!
     * Add a function stage to the pipe. This function object will be called in
     * the parent process with data passing through the stage. See
     * ExecPipeFunction for more information.
     */
    void add_function(ExecPipeFunction* func) {
        assert(func);
        if (!func) return;

        func->impl_ = this;
        func->stage_id_ = stages_.size();

        struct Stage newstage;
        newstage.func = func;
        stages_.push_back(newstage);
    }

    //! \}

    /*!
     * Function called by ExecPipeSource::write() to push data into the ring
     * buffer.
     */
    void stage_function_write(size_t st, const void* data, size_t size) {
        assert(st < stages_.size());

        return stages_[st].outbuffer.write(data, size);
    }

    //! \name Run Pipe
    //! \{

    /*!
     * Run the configured pipe sequence and wait for all children processes to
     * complete. Returns a reference to *this for chaining.
     *
     * This function call should be wrapped into a try-catch block as it will
     * throw() if a system call fails.
     */
    void run();

    //! \}

    //! \name Inspection After Pipe Execution
    //! \{

    /*!
     * Get the return status of exec() stage's program run after pipe execution
     * as indicated by wait().
     */
    int get_return_status(size_t stage_id) const {
        assert(stage_id < stages_.size());
        assert(!stages_[stage_id].func);

        return stages_[stage_id].retstatus;
    }

    /*!
     * Get the return code of exec() stage's program run after pipe execution,
     * or -1 if the program terminated abnormally.
     */
    int get_return_code(size_t stage_id) const {
        assert(stage_id < stages_.size());
        assert(!stages_[stage_id].func);

        if (WIFEXITED(stages_[stage_id].retstatus))
            return WEXITSTATUS(stages_[stage_id].retstatus);
        else
            return -1;
    }

    /*!
     * Get the signal of the abnormally terminated exec() stage's program run
     * after pipe execution, or -1 if the program terminated normally.
     */
    int get_return_signal(size_t stage_id) const {
        assert(stage_id < stages_.size());
        assert(!stages_[stage_id].func);

        if (WIFSIGNALED(stages_[stage_id].retstatus))
            return WTERMSIG(stages_[stage_id].retstatus);
        else
            return -1;
    }

    /*!
     * Return true if the return code of all exec() stages were zero.
     */
    bool all_return_codes_zero() const {
        for (size_t i = 0; i < stages_.size(); ++i)
        {
            if (stages_[i].func) continue;

            if (get_return_code(i) != 0)
                return false;
        }

        return true;
    }

    //! \}

protected:
    //! \name Helper Function for run()
    //! \{

    //! Transform arguments and launch an exec stage using the correct exec()
    //! variant.
    void exec_stage(const Stage& stage);

    //! Print all arguments of exec() call.
    void print_exec(const std::vector<std::string>& args);

    //! Safe close() call and output error if fd was already closed.
    void sclose(int fd);

    //! \}
};

/******************************************************************************/
// ExecPipeImpl

void ExecPipeImpl::print_exec(const std::vector<std::string>& args) {
    std::ostringstream oss;
    oss << "Exec()";
    for (unsigned ai = 0; ai < args.size(); ++ai)
    {
        oss << " " << args[ai];
    }
    LOG_INFO(oss.str());
}

void ExecPipeImpl::exec_stage(const Stage& stage) {
    // select arguments vector
    const std::vector<std::string>& args = stage.argsp ? *stage.argsp : stage.args;

    // create const char*[] of prog and arguments for syscall.

    const char* cargs[args.size() + 1];

    for (unsigned ai = 0; ai < args.size(); ++ai)
    {
        cargs[ai] = args[ai].c_str();
    }
    cargs[args.size()] = nullptr;

    if (!stage.envp)
    {
        if (stage.withpath)
            execvp(stage.prog, const_cast<char* const*>(cargs));
        else
            execv(stage.prog, const_cast<char* const*>(cargs));
    }
    else
    {
        // create envp const char*[] for syscall.

        const char* cenv[args.size() + 1];

        for (unsigned ei = 0; ei < stage.envp->size(); ++ei)
        {
            cenv[ei] = (*stage.envp)[ei].c_str();
        }
        cenv[stage.envp->size()] = nullptr;

        execve(stage.prog, const_cast<char* const*>(cargs),
               const_cast<char* const*>(cenv));
    }

    LOG_ERROR("Error executing child process: " << strerror(errno));
}

void ExecPipeImpl::sclose(int fd) {
    int r = close(fd);

    if (r != 0) {
        LOG_ERROR("Could not correctly close fd: " << strerror(errno));
    }
}

/*----------------------------------------------------------------------------*/
// ExecPipeImpl::run()

void ExecPipeImpl::run() {
    if (stages_.size() == 0)
        throw (std::runtime_error("No stages to in exec pipe."));

    // *** Phase 1: prepare all file descriptors ************************* //

    // set up input stream accordingly
    switch (input_)
    {
    case ST_NONE:
        // no file change of file descriptor after fork.
        stages_[0].stdin_fd = -1;
        break;

    case ST_STRING:
    case ST_OBJECT: {
        // create input pipe for strings and function objects.
        int pipefd[2];

        if (pipe(pipefd) != 0)
            throw (std::runtime_error(std::string("Could not create an input pipe: ") + strerror(errno)));

        if (fcntl(pipefd[1], F_SETFL, O_NONBLOCK) != 0)
            throw (std::runtime_error(std::string("Could not set non-block mode on input pipe: ") + strerror(errno)));

        input_fd_ = pipefd[1];
        stages_[0].stdin_fd = pipefd[0];
        break;
    }
    case ST_FILE: {
        // open input file

        int infd = open(input_file_, O_RDONLY);
        if (infd < 0)
            throw (std::runtime_error(std::string("Could not open input file: ") + strerror(errno)));

        stages_[0].stdin_fd = infd;
        break;
    }
    case ST_FD:
        // assign user-provided fd to first process
        stages_[0].stdin_fd = input_fd_;
        input_fd_ = -1;
        break;
    }

    // create pipes between exec stages
    for (size_t i = 0; i < stages_.size() - 1; ++i)
    {
        int pipefd[2];

        if (pipe(pipefd) != 0)
            throw (std::runtime_error(std::string("Could not create a stage pipe: ") + strerror(errno)));

        stages_[i].stdout_fd = pipefd[1];
        stages_[i + 1].stdin_fd = pipefd[0];

        if (stages_[i].func)
        {
            if (fcntl(stages_[i].stdout_fd, F_SETFL, O_NONBLOCK) != 0)
                throw (std::runtime_error(std::string("Could not set non-block mode on a stage pipe: ") + strerror(errno)));
        }
        if (stages_[i + 1].func)
        {
            if (fcntl(stages_[i + 1].stdin_fd, F_SETFL, O_NONBLOCK) != 0)
                throw (std::runtime_error(std::string("Could not set non-block mode on a stage pipe: ") + strerror(errno)));
        }
    }

    // set up output stream accordingly
    switch (output_)
    {
    case ST_NONE:
        // no file change of file descriptor after fork.
        stages_.back().stdout_fd = -1;
        break;

    case ST_STRING:
    case ST_OBJECT: {
        // create output pipe for strings and objects.
        int pipefd[2];

        if (pipe(pipefd) != 0)
            throw (std::runtime_error(std::string("Could not create an output pipe: ") + strerror(errno)));

        if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) != 0)
            throw (std::runtime_error(std::string("Could not set non-block mode on output pipe: ") + strerror(errno)));

        stages_.back().stdout_fd = pipefd[1];
        output_fd_ = pipefd[0];
        break;
    }
    case ST_FILE: {
        // create or truncate output file

        int outfd = open(output_file_, O_WRONLY | O_CREAT | O_TRUNC, output_file_mode_);
        if (outfd < 0)
            throw (std::runtime_error(std::string("Could not open output file: ") + strerror(errno)));

        stages_.back().stdout_fd = outfd;
        break;
    }
    case ST_FD:
        // assign user-provided fd to last process
        stages_.back().stdout_fd = output_fd_;
        output_fd_ = -1;
        break;
    }

    // *** Phase 2: launch child processes ******************************* //

    for (size_t i = 0; i < stages_.size(); ++i)
    {
        if (stages_[i].func) continue;

        print_exec(stages_[i].args);

        pid_t child = fork();
        if (child == 0)
        {
            // inside child process

            // move assigned file descriptors and close all others
            if (input_fd_ >= 0)
                sclose(input_fd_);

            for (size_t j = 0; j < stages_.size(); ++j)
            {
                if (i == j)
                {
                    // dup2 file descriptors assigned for this stage as stdin and stdout

                    if (stages_[i].stdin_fd >= 0)
                    {
                        if (dup2(stages_[i].stdin_fd, STDIN_FILENO) == -1) {
                            LOG_ERROR("Could not redirect file descriptor: " << strerror(errno));
                            exit(255);
                        }
                    }

                    if (stages_[i].stdout_fd >= 0)
                    {
                        if (dup2(stages_[i].stdout_fd, STDOUT_FILENO) == -1) {
                            LOG_ERROR("Could not redirect file descriptor: " << strerror(errno));
                            exit(255);
                        }
                    }
                }
                else
                {
                    // close file descriptors of other stages

                    if (stages_[j].stdin_fd >= 0)
                        sclose(stages_[j].stdin_fd);

                    if (stages_[j].stdout_fd >= 0)
                        sclose(stages_[j].stdout_fd);
                }
            }

            if (output_fd_ >= 0)
                sclose(output_fd_);

            // run program
            exec_stage(stages_[i]);

            exit(255);
        }

        stages_[i].pid = child;
    }

    // parent process: close all unneeded file descriptors of exec stages.

    for (stagelist_type::const_iterator st = stages_.begin();
         st != stages_.end(); ++st)
    {
        if (st->func) continue;

        if (st->stdin_fd >= 0)
            sclose(st->stdin_fd);

        if (st->stdout_fd >= 0)
            sclose(st->stdout_fd);
    }

    // *** Phase 3: run select() loop and process data ******************* //

    while (1)
    {
        // build file descriptor sets

        int max_fds = -1;
        fd_set read_fds, write_fds;

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        if (input_fd_ >= 0)
        {
            if (input_ == ST_OBJECT)
            {
                assert(input_source_);

                if (!input_rbuffer_.size() && !input_source_->poll() && !input_rbuffer_.size())
                {
                    sclose(input_fd_);
                    input_fd_ = -1;

                    LOG_INFO("Closing input file descriptor: " << strerror(errno));
                }
                else
                {
                    FD_SET(input_fd_, &write_fds);
                    if (max_fds < input_fd_) max_fds = input_fd_;

                    LOG_DEBUG("Select on input file descriptor");
                }
            }
            else
            {
                FD_SET(input_fd_, &write_fds);
                if (max_fds < input_fd_) max_fds = input_fd_;

                LOG_DEBUG("Select on input file descriptor");
            }
        }

        for (size_t i = 0; i < stages_.size(); ++i)
        {
            if (!stages_[i].func) continue;

            if (stages_[i].stdin_fd >= 0)
            {
                FD_SET(stages_[i].stdin_fd, &read_fds);
                if (max_fds < stages_[i].stdin_fd) max_fds = stages_[i].stdin_fd;

                LOG_DEBUG("Select on stage input file descriptor");
            }

            if (stages_[i].stdout_fd >= 0)
            {
                if (stages_[i].outbuffer.size())
                {
                    FD_SET(stages_[i].stdout_fd, &write_fds);
                    if (max_fds < stages_[i].stdout_fd) max_fds = stages_[i].stdout_fd;

                    LOG_DEBUG("Select on stage output file descriptor");
                }
                else if (stages_[i].stdin_fd < 0 && !stages_[i].outbuffer.size())
                {
                    sclose(stages_[i].stdout_fd);
                    stages_[i].stdout_fd = -1;

                    LOG_INFO("Close stage output file descriptor");
                }
            }
        }

        if (output_fd_ >= 0)
        {
            FD_SET(output_fd_, &read_fds);
            if (max_fds < output_fd_) max_fds = output_fd_;

            LOG_DEBUG("Select on output file descriptor");
        }

        // issue select() call

        if (max_fds < 0)
            break;

        int retval = select(max_fds + 1, &read_fds, &write_fds, nullptr, nullptr);
        if (retval < 0)
            throw (std::runtime_error(std::string("Error during select() on file descriptors: ") + strerror(errno)));

        LOG_TRACE("select() on " << retval << " file descriptors: " << strerror(errno));

        // handle file descriptors marked by select() in both sets

        if (input_fd_ >= 0 && FD_ISSET(input_fd_, &write_fds))
        {
            if (input_ == ST_STRING)
            {
                // write string data to first stdin file descriptor.

                assert(input_string_);
                assert(input_string_pos_ < input_string_->size());

                ssize_t wb;

                do
                {
                    wb = write(input_fd_,
                               input_string_->data() + input_string_pos_,
                               input_string_->size() - input_string_pos_);

                    LOG_TRACE("Write on input fd: " << wb);

                    if (wb < 0)
                    {
                        if (errno == EAGAIN || errno == EINTR)
                        { }
                        else
                        {
                            LOG_DEBUG("Error writing to input file descriptor: " << strerror(errno));

                            sclose(input_fd_);
                            input_fd_ = -1;

                            LOG_INFO("Closing input file descriptor: " << strerror(errno));
                        }
                    }
                    else if (wb > 0)
                    {
                        input_string_pos_ += wb;

                        if (input_string_pos_ >= input_string_->size())
                        {
                            sclose(input_fd_);
                            input_fd_ = -1;

                            LOG_INFO("Closing input file descriptor: " << strerror(errno));
                            break;
                        }
                    }
                } while (wb > 0);
            }
            else if (input_ == ST_OBJECT)
            {
                // write buffered data to first stdin file descriptor.

                ssize_t wb;

                do
                {
                    wb = write(input_fd_,
                               input_rbuffer_.bottom(),
                               input_rbuffer_.bottom_size());

                    LOG_TRACE("Write on input fd: " << wb);

                    if (wb < 0)
                    {
                        if (errno == EAGAIN || errno == EINTR)
                        { }
                        else
                        {
                            LOG_INFO("Error writing to input file descriptor: " << strerror(errno));

                            sclose(input_fd_);
                            input_fd_ = -1;

                            LOG_INFO("Closing input file descriptor: " << strerror(errno));
                        }
                    }
                    else if (wb > 0)
                    {
                        input_rbuffer_.advance(wb);
                    }
                } while (wb > 0);
            }
        }

        if (output_fd_ >= 0 && FD_ISSET(output_fd_, &read_fds))
        {
            // read data from last stdout file descriptor

            ssize_t rb;

            do
            {
                errno = 0;

                rb = read(output_fd_,
                          buffer_, sizeof(buffer_));

                LOG_TRACE("Read on output fd: " << rb);

                if (rb <= 0)
                {
                    if (rb == 0 && errno == 0)
                    {
                        // zero read indicates eof

                        LOG_INFO("Closing output file descriptor: " << strerror(errno));

                        if (output_ == ST_OBJECT)
                        {
                            assert(output_sink_);
                            output_sink_->eof();
                        }

                        sclose(output_fd_);
                        output_fd_ = -1;
                    }
                    else if (errno == EAGAIN || errno == EINTR)
                    { }
                    else
                    {
                        LOG_ERROR("Error reading from output file descriptor: " << strerror(errno));
                    }
                }
                else
                {
                    if (output_ == ST_STRING)
                    {
                        assert(output_string_);
                        output_string_->append(buffer_, rb);
                    }
                    else if (output_ == ST_OBJECT)
                    {
                        assert(output_sink_);
                        output_sink_->process(buffer_, rb);
                    }
                }
            } while (rb > 0);
        }

        for (size_t i = 0; i < stages_.size(); ++i)
        {
            if (!stages_[i].func) continue;

            if (stages_[i].stdin_fd >= 0 && FD_ISSET(stages_[i].stdin_fd, &read_fds))
            {
                ssize_t rb;

                do
                {
                    errno = 0;

                    rb = read(stages_[i].stdin_fd,
                              buffer_, sizeof(buffer_));

                    LOG_TRACE("Read on stage fd: " << rb);

                    if (rb <= 0)
                    {
                        if (rb == 0 && errno == 0)
                        {
                            // zero read indicates eof

                            LOG_INFO("Closing stage input file descriptor: " << strerror(errno));

                            stages_[i].func->eof();

                            sclose(stages_[i].stdin_fd);
                            stages_[i].stdin_fd = -1;
                        }
                        else if (errno == EAGAIN || errno == EINTR)
                        { }
                        else
                        {
                            LOG_ERROR("Error reading from stage input file descriptor: " << strerror(errno));
                        }
                    }
                    else
                    {
                        stages_[i].func->process(buffer_, rb);
                    }
                } while (rb > 0);
            }

            if (stages_[i].stdout_fd >= 0 && FD_ISSET(stages_[i].stdout_fd, &write_fds))
            {
                while (stages_[i].outbuffer.size() > 0)
                {
                    ssize_t wb = write(stages_[i].stdout_fd,
                                       stages_[i].outbuffer.bottom(),
                                       stages_[i].outbuffer.bottom_size());

                    LOG_TRACE("Write on stage fd: " << wb);

                    if (wb < 0)
                    {
                        if (errno == EAGAIN || errno == EINTR)
                        { }
                        else
                        {
                            LOG_INFO("Error writing to stage output file descriptor: " << strerror(errno));
                        }
                        break;
                    }
                    else if (wb > 0)
                    {
                        stages_[i].outbuffer.advance(wb);
                    }
                }

                if (stages_[i].stdin_fd < 0 && !stages_[i].outbuffer.size())
                {
                    LOG_INFO("Closing stage output file descriptor: " << strerror(errno));

                    sclose(stages_[i].stdout_fd);
                    stages_[i].stdout_fd = -1;
                }
            }
        }
    }

    // *** Phase 4: call wait() for all children processes *************** //

    unsigned int donepid = 0;

    for (size_t i = 0; i < stages_.size(); ++i)
    {
        if (!stages_[i].func) continue;
        ++donepid;
    }

    while (donepid != stages_.size())
    {
        int status;
        int p = wait(&status);

        if (p < 0)
        {
            LOG_ERROR("Error calling wait(): " << strerror(errno));
            break;
        }

        bool found = false;

        for (size_t i = 0; i < stages_.size(); ++i)
        {
            if (p == stages_[i].pid)
            {
                stages_[i].retstatus = status;

                if (WIFEXITED(status))
                {
                    LOG_INFO("Finished exec() stage " << p << " with retcode " << WEXITSTATUS(status));
                }
                else if (WIFSIGNALED(status))
                {
                    LOG_INFO("Finished exec() stage " << p << " with signal " << WTERMSIG(status));
                }
                else
                {
                    LOG_ERROR("Error in wait(): unknown return status for pid " << p);
                }

                ++donepid;
                found = true;
                break;
            }
        }

        if (!found)
        {
            LOG_ERROR("Error in wait(): syscall returned an unknown child pid.");
        }
    }

    LOG_INFO("Finished running pipe.");
}

/******************************************************************************/
// ExecPipe

ExecPipe::ExecPipe()
    : impl_(new ExecPipeImpl) {
    ++impl_->refs();
}

ExecPipe::~ExecPipe() {
    if (--impl_->refs() == 0)
        delete impl_;
}

ExecPipe::ExecPipe(const ExecPipe& ep)
    : impl_(ep.impl_) {
    ++impl_->refs();
}

ExecPipe& ExecPipe::operator = (const ExecPipe& ep) {
    if (this != &ep)
    {
        if (--impl_->refs() == 0)
            delete impl_;

        impl_ = ep.impl_;
        ++impl_->refs();
    }
    return *this;
}

void ExecPipe::set_debug_level(enum DebugLevel dl) {
    return impl_->set_debug_level(dl);
}

void ExecPipe::set_debug_output(void (* output)(const char* line)) {
    return impl_->set_debug_output(output);
}

void ExecPipe::set_input_fd(int fd) {
    return impl_->set_input_fd(fd);
}

void ExecPipe::set_input_file(const char* path) {
    return impl_->set_input_file(path);
}

void ExecPipe::set_input_string(const std::string* input) {
    return impl_->set_input_string(input);
}

void ExecPipe::set_input_source(ExecPipeSource* source) {
    return impl_->set_input_source(source);
}

void ExecPipe::set_output_fd(int fd) {
    return impl_->set_output_fd(fd);
}

void ExecPipe::set_output_file(const char* path, int mode) {
    return impl_->set_output_file(path, mode);
}

void ExecPipe::set_output_string(std::string* output) {
    return impl_->set_output_string(output);
}

void ExecPipe::set_output_sink(ExecPipeSink* sink) {
    return impl_->set_output_sink(sink);
}

size_t ExecPipe::size() const {
    return impl_->size();
}

void ExecPipe::add_exec(const char* prog) {
    return impl_->add_exec(prog);
}

void ExecPipe::add_exec(const char* prog, const char* arg1) {
    return impl_->add_exec(prog, arg1);
}

void ExecPipe::add_exec(const char* prog, const char* arg1, const char* arg2) {
    return impl_->add_exec(prog, arg1, arg2);
}

void ExecPipe::add_exec(const char* prog, const char* arg1, const char* arg2,
                        const char* arg3) {
    return impl_->add_exec(prog, arg1, arg2, arg3);
}

void ExecPipe::add_exec(const std::vector<std::string>* args) {
    return impl_->add_exec(args);
}

void ExecPipe::add_execp(const char* prog) {
    return impl_->add_execp(prog);
}

void ExecPipe::add_execp(const char* prog, const char* arg1) {
    return impl_->add_execp(prog, arg1);
}

void ExecPipe::add_execp(const char* prog, const char* arg1, const char* arg2) {
    return impl_->add_execp(prog, arg1, arg2);
}

void ExecPipe::add_execp(const char* prog, const char* arg1, const char* arg2,
                         const char* arg3) {
    return impl_->add_execp(prog, arg1, arg2, arg3);
}

void ExecPipe::add_execp(const std::vector<std::string>* args) {
    return impl_->add_execp(args);
}

void ExecPipe::add_exece(const char* path,
                         const std::vector<std::string>* args,
                         const std::vector<std::string>* env) {
    return impl_->add_exece(path, args, env);
}

void ExecPipe::add_function(ExecPipeFunction* func) {
    return impl_->add_function(func);
}

ExecPipe& ExecPipe::run() {
    impl_->run();
    return *this;
}

int ExecPipe::get_return_status(size_t stage_id) const {
    return impl_->get_return_status(stage_id);
}

int ExecPipe::get_return_code(size_t stage_id) const {
    return impl_->get_return_code(stage_id);
}

int ExecPipe::get_return_signal(size_t stage_id) const {
    return impl_->get_return_signal(stage_id);
}

bool ExecPipe::all_return_codes_zero() const {
    return impl_->all_return_codes_zero();
}

/******************************************************************************/
// ExecPipeSource

ExecPipeSource::ExecPipeSource()
    : impl_(nullptr)
{ }

void ExecPipeSource::write(const void* data, size_t size) {
    assert(impl_);
    return impl_->input_source_write(data, size);
}

/******************************************************************************/
// ExecPipeFunction

ExecPipeFunction::ExecPipeFunction()
    : impl_(nullptr), stage_id_(0)
{ }

void ExecPipeFunction::write(const void* data, size_t size) {
    assert(impl_);
    return impl_->stage_function_write(stage_id_, data, size);
}

} // namespace tlx

/******************************************************************************/
