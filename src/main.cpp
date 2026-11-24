#include <cerrno>
#include <csignal>
#include <linux/limits.h>
#include <signal.h>
#include <cstring>
#include <linux/fanotify.h>
#include <stdio.h>
#include <sys/fanotify.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "tasks.h"
#include "indexer.h"
#include <linux/mount.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <unordered_map>
#include <vector>
#include <limits.h>

#define strerrno (strerror(errno))

bool stop = false;
int sigints = 0;
int fd;

std::unordered_map<long, std::vector<long>> fsidToMFD;

#define MOUNT_BATCH_SIZE 16384
#define STATMOUNT_BUF_SIZE 4096

uint64_t mounts[MOUNT_BATCH_SIZE];

#ifndef SYS_statmount
    #ifdef __x86_64
    #define SYS_statmount 457 // architecture dependent!
    #else
    #error SYS_statmount was undefined, and it could not be automatically determined based on your architecture.
    #endif
#endif
#ifndef SYS_listmount
    #ifdef __x86_64
    #define SYS_listmount 458 // architecture dependent!
    #else
    #error SYS_listmount was undefined, and it could not be automatically determined based on your architecture.
    #endif
#endif

long listmount(mnt_id_req* req, uint64_t mnts[],size_t n,unsigned long flags) {
    return syscall(SYS_listmount,req,mnts,n,flags);
}
long statmount(mnt_id_req* req, statmount* smbuf, size_t bufsize, unsigned int flags) {
    return syscall(SYS_statmount,req,smbuf,bufsize,flags);
}

void cleanMounts() {
    for (auto i : fsidToMFD) {
        for (auto j : i.second) {
            close(j);
        }
    }
}

int enumerateMounts() {
    mnt_id_req req = {
        sizeof(mnt_id_req),
        0,
        LSMT_ROOT,
        0,
    };
    long n = listmount(
        &req,
        mounts,
        MOUNT_BATCH_SIZE,
        0
    );
    if (n < 0) {
        Task::fail("Error while enumerating mounts! Errno %d (%s)!",errno,strerrno);
        return errno;
    }
    if (n > MOUNT_BATCH_SIZE-1) {
        Task::fail("Listmount buffer clipping!");
        return 3;
    }
    // char smc[STATMOUNT_BUF_SIZE];
    union {
        struct statmount statmount;
        char buf[4096];
    } smc = {0};
    for (int i = 0;i < n;i++) {
        req.mnt_id = mounts[i];
        req.param = STATMOUNT_MNT_POINT;
        int result = statmount(&req,&smc.statmount,sizeof(smc),0);
        if (result == -1) {
            cleanMounts();
            Task::fail("Error while getting mount information! Errno %d (%s)!",errno,strerrno);
            return errno;
        }
        auto sm = &smc.statmount;
        uint32_t mountpoint = sm->mnt_point;
        if (offsetof(struct statmount,str) + mountpoint > STATMOUNT_BUF_SIZE) {
            cleanMounts();
            Task::fail("Statmount buffer clipping!");
            return 4;
        }
        // int mfd = open_tree(0,sm->str + mountpoint,OPEN_TREE_CLOEXEC);
        // int mfd = open(sm->str + mountpoint,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        int mfd = open(sm->str + mountpoint, O_PATH | O_CLOEXEC);
        if (mfd == -1) {
            cleanMounts();
            Task::fail("open_tree failed! Errno %d (%s)!",errno,strerrno);
            return errno;
        }
        printf("mfd=%d F_GETFD=%d\n", mfd, fcntl(mfd, F_GETFD));
        struct statvfs sv;
        result = fstatvfs(mfd,&sv);
        if (result == -1) {
            close(mfd);
            cleanMounts();
            Task::fail("Error while getting mount information! Errno %d (%s)!",errno,strerrno);
            return errno;
        }
        if (fsidToMFD.count(sv.f_fsid) == 0) {
            fsidToMFD[sv.f_fsid] = {};
        }
        fsidToMFD[sv.f_fsid].push_back(mfd);
    }
    return 0;
}

uint64_t fsidToKey(__kernel_fsid_t fsid) {
    static_assert(sizeof(fsid.val) == sizeof(uint64_t));
    #ifndef __GLIBC__
    #warning This implementation of `fsidToKey` depends on glibc, and has not been ported to other libcs. You may experience issues with indexd not being able to fetch the path of files. Feel free to make a port, I don't use musl.
    #endif
    #if __SIZEOF_LONG__ == 4
    #warning Potential FSID clipping; fstatvfs exposes f_fsid as unsigned long, which is only 32 bits on this platform. You may experience issues with indexd not being able to fetch the path of files.
    #endif
    uint64_t key;
    memcpy(&key,fsid.val,sizeof(fsid.val));
    return key;
}

void sig(int sig) {
    if (sig == SIGINT) {
        stop = true;
        sigints++;
        printf("[warn] SIGINT (keyboard interrupt %d more times to force quit!)\n",3-sigints);
        fflush(stdout);
        if (sigints == 3) {
            printf("KILLING!!!");
            _exit(0xFF);
        }
    } else if (sig == SIGTERM) {
        stop = true;
        Task::warn("Stopped by init process!");
    }
}

int main() {
    // setvbuf(stdout, nullptr, _IONBF, 0);
    printf("indexd starting...\n");
    Task::start("- enumerating mounts");
    int success = enumerateMounts();
    if (success != 0) {
        return success;
    }
    Task::succeed();
    Task::start("- adding signal handlers");
    struct sigaction sa = {};
    sa.sa_handler = sig;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Important: no SA_RESTART
    sigaction(SIGINT,&sa,nullptr);
    sigaction(SIGTERM,&sa,nullptr);
    Task::succeed();
    Task::start("- opening sqlite3 database");
    success = Index::init();
    if (success != 0) {
        Task::fail("failed to open database; errno %d!\n",success);
        return 1;
    }
    Task::succeed();
    Task::start("- setting up notifications");
    Task::step("init");
    fd = fanotify_init(FAN_CLOEXEC|FAN_REPORT_FID,O_RDONLY|O_LARGEFILE);
    if (fd == -1) {
        Index::quit();
        Task::fail("failed to init fanotify; errno %d (%s)!\n",errno,strerrno);
        return 1;
    }
    Task::step("mark");
    success = fanotify_mark(fd,FAN_MARK_ADD|FAN_MARK_FILESYSTEM,FAN_CREATE|FAN_DELETE|FAN_MOVED_FROM|FAN_MOVED_TO,0,"/home");
    if (success == -1) {
        Index::quit();
        close(fd);
        Task::fail("failed to mark filesystem; errno %d (%s)!\n",errno,strerrno);
        return 1;
    }
    Task::succeed();

    printf("started!\n");

    Task::start("- handling events...");
    char buf[65536];
    char* ptr = buf;
    int rootfd = open_tree(
        AT_FDCWD,
        "/",
        OPEN_TREE_CLOEXEC
    );
    while (not stop) {
        Task::step("listening");
        success = read(fd,&buf,sizeof(buf));
        if (success == -1) {
            if (errno == EINTR) {
                if (stop) break;
                continue;
            }
            Task::fail("failed; errno %d (%s)!\n",errno,strerrno);
            break;
        }
        Task::step("processing");
        auto metadata = ((fanotify_event_metadata*)buf);
        ptr = buf + metadata->metadata_len;
        while (ptr < buf + metadata->event_len) {
            Task::step("checking event!");
            auto header = (fanotify_event_info_header*)ptr;
            if (header->info_type == FAN_EVENT_INFO_TYPE_ERROR) {
                auto err = (fanotify_event_info_error*)ptr;
                Task::fail("error event of errno %d! (%s)\n",err->error,strerror(err->error));
                break;
            }
            if (header->info_type == FAN_EVENT_INFO_TYPE_FID) {
                Task::step("its info!");
                auto info = (fanotify_event_info_fid*)ptr;
                __kernel_fsid_t fsid = info->fsid;
                for (auto mfd : fsidToMFD[fsidToKey(fsid)]) {
                    struct stat st;
                    if (fstat(mfd, &st) == -1) {
                        printf("mfd %lu invalid: %s\n", mfd, strerror(errno));
                        continue;
                    }
                    printf("mfd=%lu dev=%lu ino=%lu\n",
                        mfd,
                        (unsigned long)st.st_dev,
                        (unsigned long)st.st_ino);
                    printf("ABOUT TO CALL: mfd=%d\n", (int)mfd);
                    fflush(stdout);
                    int fd1 = open_by_handle_at(mfd,(file_handle*)info->handle,O_RDONLY|O_CLOEXEC|O_NOATIME|O_PATH);
                    if (fd1 == -1) {
                        Task::fail("failed to open from fid; errno %d (%s)",errno,strerrno);
                        continue;
                    }
                    char proc_path[64];
                    char path[PATH_MAX+1];
                    snprintf(proc_path,sizeof(proc_path),"/proc/self/fd/%d",fd1);
                    ssize_t len = readlink(proc_path,path,PATH_MAX);
                    if (len == -1) {
                        Task::fail("failed to get path; errno %d (%s)",errno,strerrno);
                        continue;
                    }
                    if (len == PATH_MAX+1) {
                        Task::fail("Path clipping! (how? the buffer is PATH_MAX+1?!?!)");
                        continue;
                    }
                    if (metadata->mask == FAN_CREATE || metadata->mask == FAN_MOVED_TO) {
                        Task::step("adding to index!");
                        Index::add(path);
                    } else if (metadata->mask == FAN_DELETE || metadata->mask == FAN_MOVED_FROM) {
                        Task::step("removing from index!");
                        Index::remove(path);
                    }
                    close(fd1);
                }
            }
            ptr += header->len;
        }
    }
    Task::succeed();
    // Task::start("- listening for events...");
    // char buf[8192];
    // success = read(fd,&buf,sizeof(buf));
    // if (success == -1) {
    //     Index::quit();
    //     close(fd);
    //     Task::fail("\033[31mfailed; errno %d (%s)!\033[0m\n",errno,strerrno);
    //     return 2;
    // }
    // Task::succeed();
    // uint64_t mask = ((fanotify_event_metadata*)buf)->mask;

    printf("stopping...\n");
    Task::start("- cleaning up...");
    Task::step("closing sqlite3 database");
    Index::quit();
    Task::step("closing fanotify descriptor");
    close(fd);
    Task::succeed();
    cleanMounts();
    printf("stopped!\n");
}