#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#define errExit(msg)    do {perror(msg); exit(EXIT_FAILURE);} while (0)

int unshare_env();
int setHostname(char *);
int setupEnv();
int setupRoot();
int mapFilesystems();
int run();
int cloneProcess(int (*run)());
int unmapFileSystems();
int configureCgroups();
int writeRule(const char*, const char*);
uint8_t* stackMemory();
static int pivot_root(const char*, const char*);

static int pivot_root(const char* newRoot, const char* oldRoot) {
    return syscall(SYS_pivot_root, newRoot, oldRoot);
}

int run() {
    //mapFilesystems();
    char *procName = "/bin/sh";
    char *procArgs[2] = {(char *)procName, (char *)0};
    printf("pid: %d , spwning new shell via execvp call ... \n", getpid());
    execvp(procName, procArgs);
}

uint8_t* stackMemory() {
    const int stackSize = 65536;
    uint8_t *stack = malloc(stackSize);
    if (!stack) {
        perror("malloc");
        exit(1);
    }
    return stack + stackSize;
}

int cloneProcess(int (*run)()) {
    int childStatus;
    if (clone(run, stackMemory(), SIGCHLD, 0) == -1) {
        perror("clone");
        exit(1);
    }
    wait(&childStatus);
}


int unshare_env() {
    int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS;
    return unshare(flags);
}

int setHostname(char *hostname) {
    return sethostname(hostname, strlen(hostname));
}

int setupEnv() {
    clearenv();
    return setenv("TERM", "xterm-256color", 0) &&
            setenv("PATH", "/bin/:/sbin/:usr/bin:/usr/sbin", 0);
}

int mapFilesystems() {
    int ret = mount("proc", "/proc", "proc", 0, 0);
    printf("mount returned: %d\n", ret);
    if (ret == -1) {
        errExit("mount");
    }
}

int unmapFileSystems() {
    umount("/proc");
}

int setupRoot() {
    char *newRoot = "./root";
    char *putOld = "/oldrootfs";
    char path[PATH_MAX];

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
        errExit("mount-MS_PRIVATE");
    }
    if (mount(newRoot, newRoot, NULL, MS_BIND, NULL) == -1) {
        errExit("mount-MS_BIND");
    }
    snprintf(path, sizeof(path), "%s%s", newRoot, putOld);
    if (mkdir(path, 0777) == -1) {
        errExit("mkdir");
    }
    if (pivot_root(newRoot, path) == -1) {
        errExit("pivot_root");
    }
    if (chdir("/") == -1) {
        errExit("chdir");
    }
    if (umount2(putOld, MNT_DETACH) == -1) {
        perror("umount2");
    }
    if (rmdir(putOld) == -1) {
        perror("rmdir");
    }
}

int writeRule(const char *path, const char* val) {
    int fp = open(path, O_WRONLY | O_APPEND);
    write(fp, val, strlen(val));
    close(fp);
}

char* concatPaths(char *container, char *stra, char *strb) {
    strcpy(container, stra);
    strcat(container, strb);
    return container;
}

int configureCgroups() {
    char *cgroupFolder = "/sys/fs/cgroup/pids/tinybox/";
    char pidsMax[PATH_MAX];
    char notifyRelease[PATH_MAX];
    char procs[PATH_MAX];

    mkdir(cgroupFolder, S_IRUSR | S_IWUSR);
    pid_t pid = getpid();
    char *pidStr = (char *)malloc(12);
    sprintf(pidStr, "%d", pid);

    writeRule(concatPaths(pidsMax, cgroupFolder, "pids.max"), "5");
    writeRule(concatPaths(notifyRelease, cgroupFolder, "notify_on_release"), "1");
    writeRule(concatPaths(procs, cgroupFolder, "cgroup.procs"), pidStr);
}

int setupRootChroot() {
    // this is an alternate approach. This changes root, not the filesystem
    // unlike pivot_root.
    chroot("./root");
    chdir("/");
} 

int jail() {
    if (unshare_env() == -1) {
        errExit("unshare");
    }
    configureCgroups();
    char *hostname = "tinybox";
    setHostname(hostname);
    setupEnv();
    setupRoot();
    mapFilesystems();
    cloneProcess(run);
    unmapFileSystems();
}

int main() {
    jail();
}

