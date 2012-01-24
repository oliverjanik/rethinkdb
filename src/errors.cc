#include "errors.hpp"

#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "utils.hpp"

void report_user_error(const char *msg, ...) {
    flockfile(stderr);

    va_list args;
    va_start(args, msg);
    //fprintf(stderr, "\nError: ");
    vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");
    va_end(args);

    funlockfile(stderr);
}

void report_fatal_error(const char *file, int line, const char *msg, ...) {
    flockfile(stderr);

    va_list args;
    va_start(args, msg);
    fprintf(stderr, "\nError in %s at line %d:\n", file, line);
    vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");
    va_end(args);

    /* Don't print backtraces in valgrind mode because valgrind issues lots of spurious
    warnings when print_backtrace() is run. */
#if !defined(VALGRIND)
    fprintf(stderr, "\nBacktrace:\n");
    print_backtrace();
#endif

    fprintf(stderr, "\nExiting.\n\n");

    funlockfile(stderr);
}

/* There has been some trouble with abi::__cxa_demangle.

Originally, demangle_cpp_name() took a pointer to the mangled name, and returned a
buffer that must be free()ed. It did this by calling __cxa_demangle() and passing NULL
and 0 for the buffer and buffer-size arguments.

There were complaints that print_backtrace() was smashing memory. Shachaf observed that
pieces of the backtrace seemed to be ending up overwriting other structs, and filed
issue #100.

Daniel Mewes suspected that the memory smashing was related to calling malloc().
In December 2010, he changed demangle_cpp_name() to take a static buffer, and fill
this static buffer with the demangled name. See 284246bd.

abi::__cxa_demangle expects a malloc()ed buffer, and if the buffer is too small it
will call realloc() on it. So the static-buffer approach worked except when the name
to be demangled was too large.

In March 2011, Tim and Ivan got tired of the memory allocator complaining that someone
was trying to realloc() an unallocated buffer, and changed demangle_cpp_name() back
to the way it was originally.

Please don't change this function without talking to the people who have already
been involved in this. */

#include <cxxabi.h>

std::string demangle_cpp_name(const char *mangled_name) {
    int res;
    char *name_as_c_str = abi::__cxa_demangle(mangled_name, NULL, 0, &res);
    if (res == 0) {
        std::string name_as_std_string(name_as_c_str);
        free(name_as_c_str);
        return name_as_std_string;
    } else {
        throw demangle_failed_exc_t();
    }
}

/* Handlers for various signals and for unexpected exceptions or calls to std::terminate() */

void generic_crash_handler(int signum) {
    if (signum == SIGSEGV) {
        crash("Segmentation fault.");
    } else {
        crash("Unexpected signal: %d", signum);
    }
}

void ignore_crash_handler(UNUSED int signum) { }

void terminate_handler() {
    std::type_info *t = abi::__cxa_current_exception_type();
    if (t) {
        std::string name;
        try {
            name = demangle_cpp_name(t->name());
        } catch (demangle_failed_exc_t) {
            name = t->name();
        }
        try {
            /* This will rethrow whatever unexpected exception was thrown. */
            throw;
        } catch (std::exception& e) {
            crash("Uncaught exception of type \"%s\"\n  what(): %s", name.c_str(), e.what());
        } catch (...) {
            crash("Uncaught exception of type \"%s\"", name.c_str());
        }
        unreachable();
    } else {
        crash("std::terminate() called without any exception.");
    }
}

void install_generic_crash_handler() {

    struct sigaction action;
    int res;

#ifndef VALGRIND
    bzero(&action, sizeof(action));
    action.sa_handler = generic_crash_handler;
    res = sigaction(SIGSEGV, &action, NULL);
    guarantee_err(res == 0, "Could not install SEGV handler");
#endif

    bzero(&action, sizeof(action));
    action.sa_handler = ignore_crash_handler;
    res = sigaction(SIGPIPE, &action, NULL);
    guarantee_err(res == 0, "Could not install PIPE handler");

    std::set_terminate(&terminate_handler);
}


/* Boost will call this function when an assertion fails. */

namespace boost {

void assertion_failed(char const * expr, char const * function, char const * file, long line) {
    report_fatal_error(file, line, "BOOST_ASSERT failure in '%s': %s", function, expr); 
    BREAKPOINT;
}

void assertion_failed_msg(char const * expr, char const * msg, char const * function, char const * file, long line) {
    report_fatal_error(file, line, "BOOST_ASSERT_MSG failure in '%s': %s (%s)", function, expr, msg);
    BREAKPOINT;
}

}
