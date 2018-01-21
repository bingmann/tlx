/*******************************************************************************
 * tests/exec_pipe_example.cpp
 *
 * Part of tlx - http://panthema.net/tlx
 *
 * Copyright (C) 2010-2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#include <tlx/exec_pipe.hpp>

#include <tlx/die.hpp>
#include <tlx/digest/sha256.hpp>
#include <tlx/logger.hpp>

#include <iomanip>
#include <iostream>
#include <sstream>

//! [example1]
/*
 * This first example shows how to directly call a program, in this case
 * "/bin/echo" and save it's output in a std::string.
 */
void example1() {
    LOG1 << "example1()";

    tlx::ExecPipe ep;

    ep.add_exec("/bin/echo", "-n", "test123");

    std::string output;
    ep.set_output_string(&output);

    try {
        if (!ep.run().all_return_codes_zero()) {
            LOG1 << "Error calling echo: return code = "
                 << ep.get_return_code(0);
        }
        else {
            LOG1 << "echo output: " << output;
        }
    }
    catch (std::runtime_error& e) {
        LOG1 << "Error running children: " << e.what();
    }
}
//! [example1]

//! [example2]
/*
 * This second example shows how to directly call a program, but this time use
 * execp() variants, which search the PATH environment.
 */
void example2() {
    LOG1 << "example2()";

    tlx::ExecPipe ep;

    std::string input = "test123";
    ep.set_input_string(&input);

    ep.add_execp("sha1sum");

    std::string output;
    ep.set_output_string(&output);

    try {
        if (!ep.run().all_return_codes_zero()) {
            LOG1 << "Error calling echo: return code = "
                 << ep.get_return_code(0);
        }
        else {
            LOG1 << "sha1sum output: " << output;
        }
    }
    catch (std::runtime_error& e) {
        LOG1 << "Error running children: " << e.what();
    }
}
//! [example2]

//! [example3]
/*
 * This third example shows how to call a sequence of programs. The pipe
 * consists of "ls --size /bin" listing a direction, grepping out all 'shells'
 * and sorting these by file size.
 */
void example3() {
    LOG1 << "example3()";

    tlx::ExecPipe ep;

    ep.add_execp("ls", "--size", "/bin");

    ep.add_execp("grep", "sh");

    std::vector<std::string> sortargs;
    sortargs.push_back("sort");
    sortargs.push_back("--numeric-sort");
    sortargs.push_back("--ignore-leading-blanks");
    sortargs.push_back("--reverse");
    sortargs.push_back("--stable");
    ep.add_execp(&sortargs);

    std::string output;
    ep.set_output_string(&output);

    try {
        if (!ep.run().all_return_codes_zero()) {
            LOG1 << "Error calling programs: ";
            LOG1 << "  ls returned = " << ep.get_return_code(0);
            LOG1 << "  grep returned = " << ep.get_return_code(1);
            LOG1 << "  sort returned = " << ep.get_return_code(2);
        }
        else {
            LOG1 << "pipe output: " << output;
        }
    }
    catch (std::runtime_error& e) {
        LOG1 << "Error running children: " << e.what();
    }
}
//! [example3]

//! [example4]
/*
 * This example shows how to use the function classes tlx::ExecPipeSource and
 * tlx::ExecPipeFunction to insert custom processing into a pipe sequence. The
 * application calls tar to create an archive, calculates the SHA256 digest of
 * the uncompressed tarball and then pipes the data into gzip for compression.
 */
class FilelistSource : public tlx::ExecPipeSource
{
public:
    // List of files to send to tar.
    std::vector<std::string> list_;

    // Current position in list.
    size_t pos_;

    FilelistSource()
        : pos_(0) { }

    // Send one file name each time polled.
    bool poll() final {
        if (pos_ < list_.size())
        {
            write(list_[pos_].data(), list_[pos_].size());
            write("\n", 1);
            ++pos_;
        }

        return (pos_ < list_.size());
    }
};

class Sha256Function : public tlx::ExecPipeFunction
{
public:
    // Context of running SHA256 digest
    tlx::SHA256 ctx_;

    // Finished digest generated in eof().
    std::string digest_;

    // Update the sha256 digest context and pass on unmodified data.
    void process(const void* data, size_t datalen) final {
        ctx_.process(data, datalen);
        write(data, datalen);
    }

    // Calculate final SHA256 digest once the data stream closes.
    void eof() final {
        digest_ = ctx_.digest_hex();
    }
};

void example4() {
    LOG1 << "example4()";

    tlx::ExecPipe ep;

    // initialize a source object generating some file names. obviously in a
    // real application this list would be longer.
    FilelistSource source;

    source.list_.push_back("/bin/sh");
    source.list_.push_back("/bin/bash");
    source.list_.push_back("/bin/ls");
    source.list_.push_back("/bin/gzip");

    ep.set_input_source(&source);

    // add new exec stage calling tar with an option to read files from stdin
    std::vector<std::string> tarargs;

    tarargs.push_back("tar");
    tarargs.push_back("--create");
    tarargs.push_back("--verbose");
    tarargs.push_back("--no-recursion");
    tarargs.push_back("--files-from");
    tarargs.push_back("/dev/stdin");

    ep.add_execp(&tarargs);

    // insert an intermediate processing stage to save the SHA256 sum of the
    // uncompressed tarball.
    Sha256Function sha_tar;

    ep.add_function(&sha_tar);

    // add compression stage
    ep.add_execp("gzip", "-9");

    // set output stream to a temporary file
    ep.set_output_file("/tmp/tlx-execpipe-functions1.tar.gz");

    // run pipe
    try {
        if (!ep.run().all_return_codes_zero())
        {
            LOG1 << "Error calling programs: ";
            LOG1 << "  tar returned = " << ep.get_return_code(0);
            LOG1 << "  gzip returned = " << ep.get_return_code(2);
        }
        else
        {
            LOG1 << "SHA-256 of uncompress tar: " << sha_tar.digest_;

            LOG1 << "You can verify the digest using:";
            LOG1 << "    zcat /tmp/tlx-execpipe-functions1.tar.gz | sha256sum";
        }
    }
    catch (std::runtime_error& e)
    {
        LOG1 << "Error running children: " << e.what();
    }
}
//! [example4]

int main() {
    example1();
    example2();
    example3();
    example4();

    return 0;
}

/******************************************************************************/
