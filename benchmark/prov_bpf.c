/*
 * Compile me with
 *
 *     gcc -Wall -Wextra prov_bpf.c -Og -DDEBUG -DBPFTRACE_EXE -DBPFTRACE_CODE -DCHECK_PERMS -o prov_bpf.exe
 *
 * DEBUG is optional
 *
 * CHECK_PERMS is optional. If this is enabled, the binary will check that permissions are "rigid" enough that this setuid binary can't be abused.
 *
 * BPFTRACE should point to the path the binary of https://github.com/bpftrace/bpftrace. It's better to bake this in to the binary than use the $PATH because this is ssetuid, and arbitrary users could set their own $PATH.
 *
 * BPFTRACE_CODE should point to a bpftrace code. Again, this should be baked in to the binary.
 *
 * My commmandline args:
 *
 *     env - PATH=$PWD/result/bin result/bin/gcc -Wall -Wextra prov_bpf.c -Og -DNDEBUG -DBPFTRACE_EXE=$PWD/result/bin/bpftrace -DBPFTRACE_CODE=$PWD/prov.bt -o prov_bpf.exe
 *
 */

#define _GNU_SOURCE
#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#ifdef DEBUG
#define DEBUG_PRINT(...) do { fprintf( stderr, "DEBUG: " __VA_ARGS__ ); } while( false )
#define DEBUG_VAR(var, kind) do { fprintf( stderr, "DEBUG: " #var " = " kind "\n", var ); } while( false )
#else
#define DEBUG_PRINT(...)
#define DEBUG_VAR(var, kind)
#endif

#define unlikely(x)    __builtin_expect(!!(x), 0)

#define EXPECT_NONNULL(expr) ({\
            void* ret = expr; \
            if (unlikely(ret == NULL)) { \
                fprintf(stderr, "failure on line %d: %s\nreturned NULL\nstrerror: %s\n", __LINE__, #expr, strerror(errno)); \
                abort(); \
            } \
            ret; \
    })

#define EXPECT_POSITIVE(expr) ({\
            int ret = expr; \
            if (unlikely(ret < 0)) { \
                fprintf(stderr, "failure on line %d: %s\nreturned a negative, %d\nstrerror: %s\n", __LINE__, #expr, ret, strerror(errno)); \
                abort(); \
            } \
            ret; \
    })

#define EXPECT_ZERO(expr) ({\
            int ret = expr; \
            if (unlikely(ret != 0)) { \
                fprintf(stderr, "failure on line %d: %s\nreturned a non-zero, %d\nstrerror: %s\n", __LINE__, #expr, ret, strerror(errno)); \
                abort(); \
            } \
            ret; \
    })

#define PIPE_SENDER 0
#define PIPE_RECVER 0

int wait_status(pid_t pid) {
    int wstatus;
    waitpid(pid, &wstatus, 0);
    if (WIFEXITED(wstatus)) {
        return WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        return WTERMSIG(wstatus);
    } else if (WIFSTOPPED(wstatus)) {
        return WSTOPSIG(wstatus);
    } else if (WIFCONTINUED(wstatus)) {
        return 1;
    } else {
        fprintf(stderr, "Impossible PID status");
        abort();
    }
}

bool file_exists(char* filename) {
    struct stat statbuf;
    int stat_result = stat(filename, &statbuf);
    if (stat_result == 0) {
        return true;
    } else if (errno == ENOENT) {
        return false;
    } else {
        fprintf(stderr, "Stat %s failed with %s\n", filename, strerror(errno));
        abort();
    }
}

int main(__attribute__((unused)) int argc, char** argv) {
    uid_t unprivileged_user = getuid();
    uid_t unprivileged_group = getgid();
    uid_t privileged_user = geteuid();
    uid_t privileged_group = getegid();

    // Partially de-escalate privileges
    // We'll keep the "saved" UID/GID as privileged, because that doesn't get inherited.
    // But we'll de-escalate the "real" and "effective" UID/GID, so that we have to explicitly re-escalate.
    EXPECT_ZERO(setresuid(unprivileged_user, unprivileged_user, -1));
    EXPECT_ZERO(setresgid(unprivileged_group, unprivileged_group, -1));

    char self[PATH_MAX - 10];
    EXPECT_POSITIVE(readlink("/proc/self/exe", self, PATH_MAX - 10));
    DEBUG_VAR(self, "%s");
#ifdef CHECK_PERMS
    struct stat self_stat;
    EXPECT_ZERO(stat(self, &self_stat));
    if (self_stat.st_uid != 0 || self_stat.st_gid != 0) {
        fprintf(stderr, "This binary must be root in order to use eBPF functionality.\n");
        fprintf(stderr, "Please `chown root:root %s`\n", self);
        abort();
    }
    if (!(self_stat.st_mode & 06000)) {
        fprintf(stderr, "This binary must be setuid/setgid in order to use eBPF functionality.\n");
        fprintf(stderr, "Please `chmod ug+s  %s`\n", self);
        abort();
    }
#endif

#ifdef CHECK_PERMS
    struct stat bpftrace_exe_stat;
    EXPECT_ZERO(stat(BPFTRACE_EXE, &bpftrace_exe_stat));
    if (bpftrace_exe_stat.st_uid != 0 || bpftrace_exe_stat.st_gid != 0) {
        fprintf(stderr, "The bpftrace binary must be root, otherwise someone might try to replace it.\n");
        fprintf(stderr, "Please `chown root:root %s`\n", BPFTRACE_EXE);
        abort();
    }
    if (self_stat.st_mode & 00022) {
        fprintf(stderr, "The bpftrace binary must be locked down, otherwise someone might try to replace it.\n");
        fprintf(stderr, "Please `chmod go-w  %s`\n", BPFTRACE_EXE);
        abort();
    }
#endif

#ifdef CHECK_PERMS
    struct stat bpftrace_code_stat;
    EXPECT_ZERO(stat(BPFTRACE_CODE, &bpftrace_code_stat));
    if (bpftrace_code_stat.st_uid != 0 || bpftrace_code_stat.st_gid != 0) {
        fprintf(stderr, "Please `chown root:root %s`\n", BPFTRACE_CODE);
        fprintf(stderr, "Otherwise unprivileged users can use this setuid binary to run arbitrary bpftrace code.\n");
        abort();
    }
    if (bpftrace_code_stat.st_mode & 00022) {
        fprintf(stderr, "Please `chmod go-w %s`\n", BPFTRACE_CODE);
        fprintf(stderr, "Otherwise unprivileged users can use this setuid binary to run arbitrary bpftrace code.\n");
        abort();
    }
#endif

    char* log_file = argv[1];
    DEBUG_VAR(log_file, "%s");

    FILE* file = EXPECT_NONNULL(fopen(log_file, "w+"));
    EXPECT_ZERO(fclose(file));

    char* tracee_exe = argv[2];
    DEBUG_VAR(tracee_exe, "%s");
    char** tracee_argv = &argv[2];

    int launcher2tracee[2];
    EXPECT_ZERO(pipe(launcher2tracee));
    pid_t tracee_pid = EXPECT_POSITIVE(fork());
    if (tracee_pid == 0) {
        // This is the tracee.
        // Fully de-escalate privileges
        EXPECT_ZERO(setresuid(unprivileged_user, unprivileged_user, unprivileged_user));
        EXPECT_ZERO(setresgid(unprivileged_group, unprivileged_group, unprivileged_group));

        // Close the write end of the pipe
        EXPECT_ZERO(close(launcher2tracee[1]));

        // I had this bug where the child would fork and start to run before bpftrace attached its probes.
        // Putting a sleep fixed that, but it felt hacky.
        // This actually waits for the right condition
        DEBUG_PRINT("Tracee: waiting for launcher to be ready");
        char buffer[1];
        EXPECT_POSITIVE(read(launcher2tracee[0], &buffer, 1));

        // This way the child proc doesn't get an unexpected open fd
        EXPECT_ZERO(close(launcher2tracee[0]));

        DEBUG_PRINT("Tracee: executing %s\n", tracee_exe);
        EXPECT_ZERO(execvp(tracee_exe, tracee_argv));
        // Even if exec fails, we would have aborted by now.
        __builtin_unreachable();
    }

    // Close the read end of the pipe
    EXPECT_ZERO(close(launcher2tracee[0]));

    DEBUG_VAR(tracee_pid, "%d");

    // Compute the args for bpftrace *before* escalating privilege
    char tracee_pid_str[PATH_MAX];
    EXPECT_POSITIVE(snprintf(tracee_pid_str, PATH_MAX, "%d", tracee_pid));
    char *bpftrace_argv[] = {BPFTRACE_EXE, "-B", "full", "-f", "json", "-o", log_file, BPFTRACE_CODE, tracee_pid_str, NULL};

    pid_t bpf_pid = EXPECT_POSITIVE(fork());
    if (bpf_pid == 0) {
        // "Explicitly" escalate privileges to use bpftrace
        // This is allowed since the "saved" UID/GID should be privileged_user/group
        EXPECT_ZERO(setresuid(privileged_user, privileged_user, privileged_user));
        EXPECT_ZERO(setresgid(privileged_group, privileged_group, privileged_group));

        // Many nix-store paths would otherwise be truncated.
        // However, BPF has a limit of 200
        // This limitation is because strings are currently stored on the 512 byte BPF stack.
        // See https://github.com/iovisor/bpftrace/issues/305
        EXPECT_ZERO(setenv("BPFTRACE_STRLEN", "200", 1));

        // Exec bpftrace
        EXPECT_ZERO(execv(BPFTRACE_EXE, bpftrace_argv));

        // Even if exec fails, we would have aborted by now.
        __builtin_unreachable();
    }

    // We did the only thing we need privileges for
    // De-escalate fully
    EXPECT_ZERO(setresuid(unprivileged_user, unprivileged_user, unprivileged_user));
    EXPECT_ZERO(setresgid(unprivileged_group, unprivileged_group, unprivileged_group));

    DEBUG_VAR(bpf_pid, "%d");

    // Test if bpftrace probes are ready
    while (true) {
        DEBUG_PRINT("Checking file existence\n");
        if (file_exists(log_file)) {
            DEBUG_PRINT("Checking file contents\n");
            FILE* file = EXPECT_NONNULL(fopen(log_file, "r"));
            #define MAX_LINE 1024
            char buffer[MAX_LINE];
            bool bpf_ready = false;
            while (fgets(buffer, MAX_LINE, file)) {
                DEBUG_PRINT("Checking line %s\n", buffer);
                if (strstr(buffer, "attached_probes")) {
                    DEBUG_PRINT("line matches :)\n");
                    bpf_ready = true;
                    break;
                }
            }
            EXPECT_ZERO(fclose(file));
            if (bpf_ready) {
                break;
            }
        }
        DEBUG_PRINT("Sleeping\n");
        EXPECT_ZERO(usleep(1000 * 10));
    }

    // Tell child we are ready
    EXPECT_POSITIVE(write(launcher2tracee[1], "\0", 1));
    EXPECT_ZERO(close(launcher2tracee[1]));

    // Wait on tracee
    int tracee_status = wait_status(tracee_pid);

    // Wait on BPF
    int bpf_status = wait_status(bpf_pid);

    DEBUG_VAR(tracee_status, "%d");
    DEBUG_VAR(bpf_status, "%d");
    return tracee_status | bpf_status;
}
