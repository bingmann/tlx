/*******************************************************************************
 * tests/exec_pipe_test.cpp
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
#include <tlx/string/hexdump.hpp>
#include <tlx/logger.hpp>

#include <iomanip>
#include <iostream>
#include <sstream>

// methods to iron out wrinkles between operating systems
#if __linux__
static const char* md5prog = "md5sum";
static const char* sha256prog = "sha256sum";

static inline std::string digest_map(std::string s) {
    return s + "  -\n";
}
#else
static const char* md5prog = "md5";
static const char* sha256prog = "sha256";

static inline std::string digest_map(std::string s) {
    return s + "\n";
}
#endif

// Test pipe: none -> program -> string
void test_none_program_string() {
    LOG1 << "test_none_program_string()";

    tlx::ExecPipe ep;

    std::string output;
    ep.set_output_string(&output);

    ep.add_exec("/bin/echo", "test123");

    die_unless(ep.run().all_return_codes_zero());

    die_unequal(output, "test123\n");
}

// Test pipe: string -> program -> string
void test_string_program_string() {
    LOG1 << "test_string_program_string()";

    tlx::ExecPipe ep;

    std::string input = "test123";
    input += std::string(1024 * 1024, '\1');
    ep.set_input_string(&input);

    std::string output;
    ep.set_output_string(&output);

    ep.add_exec("/bin/cat");

    die_unless(ep.run().all_return_codes_zero());

    // std::cout << "o " << output.size() << " i " << input.size() << "\n";

    die_unequal(output, input);
}

// Test pipe: string -> program -> program -> string
void test_string_program_program_string() {
    LOG1 << "test_string_program_program_string()";

    tlx::ExecPipe ep;

    std::string input = "test123";
    ep.set_input_string(&input);

    std::string output;
    ep.set_output_string(&output);

    ep.add_exec("/bin/cat");
    ep.add_execp(md5prog);

    die_unless(ep.run().all_return_codes_zero());

    die_unequal(output, digest_map("cc03e747a6afbbcbf8be7668acfebee5"));
}

// Test pipe: file -> program -> string
void test_file_program_string() {
    LOG1 << "test_file_program_string()";

    tlx::ExecPipe ep;

    ep.set_input_file("/etc/hosts");

    std::string output;
    ep.set_output_string(&output);

    ep.add_execp("sort");

    die_unless(ep.run().all_return_codes_zero());

    die_unless(output.size());
}

// Test pipe: string -> program -> object

class TestSink : public tlx::ExecPipeSink
{
public:
    std::string save_;

    bool ok_;

    TestSink()
        : ok_(false)
    { }

    void process(const void* data, size_t datalen) final {
        save_.append(reinterpret_cast<const char*>(data), datalen);
    }

    void eof() final {
        ok_ = (save_ == digest_map("cc03e747a6afbbcbf8be7668acfebee5"));
    }
};

void test_string_program_object() {
    LOG1 << "test_string_program_object()";

    tlx::ExecPipe ep;

    std::string input = "test123";
    ep.set_input_string(&input);

    TestSink sink;
    ep.set_output_sink(&sink);

    ep.add_execp(md5prog);

    die_unless(ep.run().all_return_codes_zero());

    die_unless(sink.ok_);
}

class TestSource : public tlx::ExecPipeSource
{
public:
    size_t count_;

    std::string wrote_;

    TestSource()
        : count_(100 * 1024)
    { }

    bool poll() final {
        for (size_t i = 0; i < 1000 && count_ > 0; ++i, --count_)
        {
            write(&i, sizeof(unsigned char));

            wrote_.append(reinterpret_cast<const char*>(&i), sizeof(unsigned char));
        }

        return (count_ > 0);
    }
};

// Test pipe: object -> program -> string

void test_object_program_string() {
    LOG1 << "test_object_program_string()";

    tlx::ExecPipe ep;

    TestSource source;
    ep.set_input_source(&source);

    std::string output;
    ep.set_output_string(&output);

    ep.add_execp("cat");

    die_unless(ep.run().all_return_codes_zero());

    die_unequal(source.wrote_, output);
}

// Test pipe: object -> program -> function -> program -> string

class TestFunctionSHA256 : public tlx::ExecPipeFunction
{
public:
    tlx::SHA256 ctx_;

    std::string digest_;

    void process(const void* data, size_t datalen) final {
        ctx_.process(data, datalen);
        write(data, datalen);
    }

    void eof() final {
        digest_ = ctx_.digest_hex();
    }
};

void test_object_program_object_program_string() {
    LOG1 << "test_object_program_object_program_string()";

    tlx::ExecPipe ep;

    TestSource source;
    ep.set_input_source(&source);

    std::string output;
    ep.set_output_string(&output);

    ep.add_execp("cat");

    TestFunctionSHA256 func;
    ep.add_function(&func);

    ep.add_execp(sha256prog);

    die_unless(ep.run().all_return_codes_zero());

    die_unequal(func.digest_,
                "56ecf4a9d98115c3b2b47a5c0af9a156"
                "2c674e086bc05c095acbaaf4531359e5");
    die_unequal(output, digest_map("56ecf4a9d98115c3b2b47a5c0af9a156"
                                   "2c674e086bc05c095acbaaf4531359e5"));
}

void test_object_program_object_string() {
    LOG1 << "test_object_program_object_string()";

    tlx::ExecPipe ep;

    TestSource source;
    ep.set_input_source(&source);

    std::string output;
    ep.set_output_string(&output);

    ep.add_execp("cat");

    TestFunctionSHA256 func;
    ep.add_function(&func);

    die_unless(ep.run().all_return_codes_zero());

    die_unequal(func.digest_,
                "56ecf4a9d98115c3b2b47a5c0af9a156"
                "2c674e086bc05c095acbaaf4531359e5");
    die_unequal(output.size(), 100 * 1024u);
}

void test_none_program_set_string() {
#if __linux__
    LOG1 << "test_none_program_set_string()";

    tlx::ExecPipe ep;

    std::vector<std::string> args;
    args.push_back("/bin/bash");
    args.push_back("-c");
    args.push_back("set");

    std::vector<std::string> envs;
    envs.push_back("TEST=123");

    ep.add_exece("/bin/bash", &args, &envs);

    std::string output;
    ep.set_output_string(&output);

    die_unless(ep.run().all_return_codes_zero());

    die_unless(output.find("TEST=123") != std::string::npos);
#endif
}

void test_error_debug_output_null(const char*)
{ }

void test_error_none_program_none() {
    LOG1 << "test_error_none_program_none()";

    tlx::ExecPipe ep;
    ep.set_debug_level(tlx::ExecPipe::DL_INFO);
    ep.set_debug_output(test_error_debug_output_null);

    ep.add_exec("xyz-non-existing-program");

    ep.run();

    die_unless(!ep.all_return_codes_zero());

    die_unequal(ep.get_return_code(0), 255);
}

int main() {
    test_none_program_string();
    test_string_program_string();
    test_string_program_program_string();
    test_file_program_string();
    test_string_program_object();
    test_object_program_string();
    test_object_program_object_program_string();
    test_object_program_object_string();
    test_none_program_set_string();

    test_error_none_program_none();

    return 0;
}

/******************************************************************************/
