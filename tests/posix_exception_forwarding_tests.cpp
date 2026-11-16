// SPDX-License-Identifier: GPL-2.0-only
// Real protected-page faults in subprocesses: a rejected fault must not loop.
#include "check.h"
#include <rex/exception_handler.h>
#include <csignal>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void* fault_page;
size_t page_size;
volatile sig_atomic_t handled = 0;

bool Reject(rex::arch::Exception*, void*) { return false; }
bool Recover(rex::arch::Exception* ex, void*) {
  if (ex->fault_address() != reinterpret_cast<uintptr_t>(fault_page)) _exit(91);
  ++handled;
  return mprotect(fault_page, page_size, PROT_READ | PROT_WRITE) == 0;
}
void Previous(int) { _exit(42); }
void PreviousInfo(int, siginfo_t* info, void*) {
  _exit(info->si_addr == fault_page ? 43 : 92);
}

int Run(int mode, bool unmapped = false) {
  const pid_t child = fork();
  if (child == 0) {
    const rlimit no_core{0, 0};
    setrlimit(RLIMIT_CORE, &no_core);
    alarm(3); // Regression: the old handler loops until SIGALRM kills the child.
    page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    fault_page = mmap(nullptr, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (fault_page == MAP_FAILED) _exit(93);
    if (unmapped && munmap(fault_page, page_size) != 0) _exit(95);
    struct sigaction previous{};
    sigemptyset(&previous.sa_mask);
    previous.sa_handler = mode == 1 ? Previous : mode == 3 ? SIG_IGN : SIG_DFL;
    if (mode == 2) {
      previous.sa_flags = SA_SIGINFO;
      previous.sa_sigaction = PreviousInfo;
    }
    sigaction(SIGSEGV, &previous, nullptr);
    sigaction(SIGBUS, &previous, nullptr);
    const auto handler = mode == 4 ? Recover : Reject;
    rex::arch::ExceptionHandler::Install(handler, nullptr);
    *static_cast<volatile unsigned char*>(fault_page) = 7;
    rex::arch::ExceptionHandler::Uninstall(handler, nullptr);
    _exit(mode == 4 && handled == 1 ? 0 : 94);
  }
  if (child < 0) return -1;
  int status = 0;
  return waitpid(child, &status, 0) == child ? status : -1;
}
}

int main() {
  using namespace rb_blitz::test;
  BeginCase("unhandled native faults terminate with their original signal");
  for (bool unmapped : {false, true}) for (int mode : {0, 3}) {
    const int status = Run(mode, unmapped);
    CHECK_TRUE(status >= 0 && WIFSIGNALED(status));
    CHECK_TRUE(WTERMSIG(status) == SIGBUS || WTERMSIG(status) == SIGSEGV);
  }
  BeginCase("previous handlers receive rejected faults and original fault addresses");
  for (bool unmapped : {false, true}) for (int mode : {1, 2}) {
    const int status = Run(mode, unmapped);
    CHECK_TRUE(status >= 0 && WIFEXITED(status));
    CHECK_EQ(WEXITSTATUS(status), mode == 1 ? 42 : 43);
  }
  BeginCase("handled page-protection faults resume the original instruction");
  const int status = Run(4);
  CHECK_TRUE(status >= 0 && WIFEXITED(status));
  CHECK_EQ(WEXITSTATUS(status), 0);
  return Finish();
}
