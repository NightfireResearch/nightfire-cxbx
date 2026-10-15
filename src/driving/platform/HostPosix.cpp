#define _FILE_OFFSET_BITS 64
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   // FNM_CASEFOLD, pipe2, tm_gmtoff
#endif

#include "Host.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// The host layer on a POSIX system (docs/driving/portable.md): pthreads, clock_gettime and POSIX files behind the
// Win32 shapes Host.h describes. Handles are objects of ours; errors are Win32's numbers, kept per thread.
//
// What is not Windows' exactly:
//   - thread priorities are accepted and not applied (an unprivileged process cannot raise its own);
//   - share modes are not enforced, and a file's attributes are ignored;
//   - IsBadWritePtr answers whether the memory can be read;
//   - RaiseException has no structured exception handling to raise into: it reports and aborts;
//   - the time zone is the C library's current offset, with no transition dates.

static_assert(sizeof(pthread_mutex_t) <= sizeof(HostLock), "a pthread mutex fits a HostLock");

// The Win32 numbers this file produces or takes
enum : unsigned long {
    kErrorFileNotFound = 2,
    kErrorPathNotFound = 3,
    kErrorTooManyOpenFiles = 4,
    kErrorAccessDenied = 5,
    kErrorInvalidHandle = 6,
    kErrorNotEnoughMemory = 8,
    kErrorNoMoreFiles = 18,
    kErrorWriteFault = 29,
    kErrorReadFault = 30,
    kErrorFileExists = 80,
    kErrorInvalidParameter = 87,
    kErrorDiskFull = 112,
    kErrorNegativeSeek = 131,
    kErrorAlreadyExists = 183,
    kErrorEnvironmentNotFound = 203,
};
enum : unsigned long {
    kGenericRead = 0x80000000u,
    kGenericWrite = 0x40000000u,
    kCreateNew = 1,
    kCreateAlways = 2,
    kOpenExisting = 3,
    kOpenAlways = 4,
    kTruncateExisting = 5,
    kTimePeriodic = 1,
    kTimeZoneUnknown = 0,
    kTimeZoneStandard = 1,
    kTimeZoneDaylight = 2,
};

// ---- errors

static thread_local unsigned long LastError;

unsigned long HostGetLastError(void) {
    return LastError;
}

void HostSetLastError(unsigned long error) {
    LastError = error;
}

static unsigned long ErrorFromErrno(int error) {
    switch (error) {
        case ENOENT:       return kErrorFileNotFound;
        case ENOTDIR:      return kErrorPathNotFound;
        case EMFILE:
        case ENFILE:       return kErrorTooManyOpenFiles;
        case EACCES:
        case EPERM:
        case EISDIR:
        case EROFS:        return kErrorAccessDenied;
        case EBADF:        return kErrorInvalidHandle;
        case ENOMEM:       return kErrorNotEnoughMemory;
        case EEXIST:       return kErrorFileExists;
        case ENOSPC:       return kErrorDiskFull;
        case EINVAL:       return kErrorInvalidParameter;
        default:           return kErrorInvalidParameter;
    }
}

// ---- objects

enum ObjectKind { kThread = 1, kEvent, kFile, kSearch, kHeap };

struct Object {
    ObjectKind kind;
};

// A condition with a flag: an event, or a thread's "finished"
struct Signal {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool manualReset;
    bool signaled;
};

static void InitSignal(Signal *signal, bool manualReset, bool signaled) {
    pthread_mutex_init(&signal->mutex, NULL);
    pthread_condattr_t attributes;
    pthread_condattr_init(&attributes);
    pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    pthread_cond_init(&signal->cond, &attributes);
    pthread_condattr_destroy(&attributes);
    signal->manualReset = manualReset;
    signal->signaled = signaled;
}

static void DestroySignal(Signal *signal) {
    pthread_cond_destroy(&signal->cond);
    pthread_mutex_destroy(&signal->mutex);
}

static void Raise(Signal *signal) {
    pthread_mutex_lock(&signal->mutex);
    signal->signaled = true;
    if (signal->manualReset)
        pthread_cond_broadcast(&signal->cond);
    else
        pthread_cond_signal(&signal->cond);
    pthread_mutex_unlock(&signal->mutex);
}

static unsigned long WaitFor(Signal *signal, unsigned long milliseconds) {
    timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += milliseconds / 1000;
    deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    unsigned long result = HOST_WAIT_OBJECT_0;
    pthread_mutex_lock(&signal->mutex);
    while (!signal->signaled) {
        if (milliseconds == HOST_INFINITE) {
            pthread_cond_wait(&signal->cond, &signal->mutex);
        } else if (pthread_cond_timedwait(&signal->cond, &signal->mutex, &deadline) == ETIMEDOUT) {
            result = signal->signaled ? HOST_WAIT_OBJECT_0 : HOST_WAIT_TIMEOUT;
            break;
        }
    }
    if (result == HOST_WAIT_OBJECT_0 && !signal->manualReset)
        signal->signaled = false;
    pthread_mutex_unlock(&signal->mutex);
    return result;
}

struct Event : Object {
    Signal signal;
};

// A thread object is shared by its handle and the running thread, and goes when both are done with it.
struct Thread : Object {
    pthread_t thread;
    HostThreadStart start;
    void *parameter;
    unsigned long id;
    unsigned long exitCode;
    Signal finished;             // manual reset: raised once, when the thread ends
    pthread_mutex_t resumeMutex;
    pthread_cond_t resumeCond;
    bool suspended;
    int references;
};

struct File : Object {
    int fd;
};

struct Search : Object {
    DIR *directory;
    char pattern[HOST_MAX_PATH];
};

struct Heap : Object {
    unsigned long flags;
};

static long Release(Thread *thread) {
    long left = __atomic_sub_fetch(&thread->references, 1, __ATOMIC_ACQ_REL);
    if (left == 0) {
        DestroySignal(&thread->finished);
        pthread_cond_destroy(&thread->resumeCond);
        pthread_mutex_destroy(&thread->resumeMutex);
        delete thread;
    }
    return left;
}

static Object *ObjectOf(HostHandle handle, ObjectKind kind) {
    if (handle == NULL || handle == HOST_INVALID_HANDLE || handle == HOST_CURRENT_THREAD) {
        LastError = kErrorInvalidHandle;
        return NULL;
    }
    Object *object = static_cast<Object *>(handle);
    if (object->kind != kind) {
        LastError = kErrorInvalidHandle;
        return NULL;
    }
    return object;
}

// ---- threads

static unsigned long NextThreadId = 0;
static thread_local unsigned long CurrentThreadId;
static thread_local Thread *CurrentThread;

static unsigned long NewThreadId(void) {
    return __atomic_add_fetch(&NextThreadId, 1, __ATOMIC_RELAXED) * 4;   // Win32's are multiples of four
}

static void FinishThread(Thread *thread, unsigned long exitCode) {
    thread->exitCode = exitCode;
    Raise(&thread->finished);
    Release(thread);
}

static void *ThreadMain(void *argument) {
    Thread *thread = static_cast<Thread *>(argument);
    CurrentThread = thread;
    CurrentThreadId = thread->id;
    pthread_mutex_lock(&thread->resumeMutex);
    while (thread->suspended)
        pthread_cond_wait(&thread->resumeCond, &thread->resumeMutex);
    pthread_mutex_unlock(&thread->resumeMutex);

    unsigned long exitCode = thread->start(thread->parameter);
    FinishThread(thread, exitCode);
    return NULL;
}

HostHandle HostCreateThread(size_t stackSize, HostThreadStart start, void *parameter, unsigned long flags,
                            unsigned long *threadId) {
    Thread *thread = new Thread();
    thread->kind = kThread;
    thread->start = start;
    thread->parameter = parameter;
    thread->id = NewThreadId();
    thread->exitCode = HOST_STILL_ACTIVE;
    InitSignal(&thread->finished, true, false);
    pthread_mutex_init(&thread->resumeMutex, NULL);
    pthread_cond_init(&thread->resumeCond, NULL);
    thread->suspended = (flags & HOST_CREATE_SUSPENDED) != 0;
    thread->references = 2;   // the handle and the thread

    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (stackSize != 0) {
        if (stackSize < (size_t)PTHREAD_STACK_MIN)
            stackSize = PTHREAD_STACK_MIN;
        pthread_attr_setstacksize(&attributes, stackSize);
    }
    int error = pthread_create(&thread->thread, &attributes, ThreadMain, thread);
    pthread_attr_destroy(&attributes);
    if (error != 0) {
        thread->references = 1;
        Release(thread);
        LastError = kErrorNotEnoughMemory;
        return NULL;
    }
    if (threadId != NULL)
        *threadId = thread->id;
    return thread;
}

unsigned long HostResumeThread(HostHandle handle) {
    Thread *thread = static_cast<Thread *>(ObjectOf(handle, kThread));
    if (thread == NULL)
        return 0xffffffffu;
    pthread_mutex_lock(&thread->resumeMutex);
    unsigned long previous = thread->suspended ? 1 : 0;
    thread->suspended = false;
    pthread_cond_signal(&thread->resumeCond);
    pthread_mutex_unlock(&thread->resumeMutex);
    return previous;
}

void HostExitThread(unsigned long exitCode) {
    if (CurrentThread != NULL)
        FinishThread(CurrentThread, exitCode);
    pthread_exit(NULL);
}

int HostSetThreadPriority(HostHandle thread, int priority) {
    (void)thread;
    (void)priority;
    return 1;
}

int HostGetExitCodeThread(HostHandle handle, unsigned long *exitCode) {
    Thread *thread = static_cast<Thread *>(ObjectOf(handle, kThread));
    if (thread == NULL)
        return 0;
    pthread_mutex_lock(&thread->finished.mutex);
    *exitCode = thread->finished.signaled ? thread->exitCode : HOST_STILL_ACTIVE;
    pthread_mutex_unlock(&thread->finished.mutex);
    return 1;
}

unsigned long HostGetCurrentThreadId(void) {
    if (CurrentThreadId == 0)
        CurrentThreadId = NewThreadId();
    return CurrentThreadId;
}

// ---- thread-local storage

unsigned long HostTlsAlloc(void) {
    pthread_key_t key;
    if (pthread_key_create(&key, NULL) != 0)
        return HOST_TLS_OUT_OF_INDEXES;
    return key;
}

void *HostTlsGetValue(unsigned long slot) {
    LastError = 0;
    return pthread_getspecific((pthread_key_t)slot);
}

int HostTlsSetValue(unsigned long slot, void *value) {
    return pthread_setspecific((pthread_key_t)slot, value) == 0;
}

// ---- locks

static pthread_mutex_t *MutexOf(HostLock *lock) {
    return reinterpret_cast<pthread_mutex_t *>(lock->storage);
}

void HostInitLock(HostLock *lock) {
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);   // a critical section is recursive
    pthread_mutex_init(MutexOf(lock), &attributes);
    pthread_mutexattr_destroy(&attributes);
}

void HostEnterLock(HostLock *lock) {
    pthread_mutex_lock(MutexOf(lock));
}

void HostLeaveLock(HostLock *lock) {
    pthread_mutex_unlock(MutexOf(lock));
}

long HostInterlockedIncrement(volatile long *value) {
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}

long HostInterlockedDecrement(volatile long *value) {
    return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}

// ---- events and waits

HostHandle HostCreateEvent(int manualReset, int initialState) {
    Event *event = new Event();
    event->kind = kEvent;
    InitSignal(&event->signal, manualReset != 0, initialState != 0);
    return event;
}

int HostSetEvent(HostHandle handle) {
    Event *event = static_cast<Event *>(ObjectOf(handle, kEvent));
    if (event == NULL)
        return 0;
    Raise(&event->signal);
    return 1;
}

int HostResetEvent(HostHandle handle) {
    Event *event = static_cast<Event *>(ObjectOf(handle, kEvent));
    if (event == NULL)
        return 0;
    pthread_mutex_lock(&event->signal.mutex);
    event->signal.signaled = false;
    pthread_mutex_unlock(&event->signal.mutex);
    return 1;
}

unsigned long HostWaitForObject(HostHandle handle, unsigned long milliseconds) {
    if (handle != NULL && handle != HOST_INVALID_HANDLE && handle != HOST_CURRENT_THREAD) {
        Object *object = static_cast<Object *>(handle);
        if (object->kind == kEvent)
            return WaitFor(&static_cast<Event *>(object)->signal, milliseconds);
        if (object->kind == kThread)
            return WaitFor(&static_cast<Thread *>(object)->finished, milliseconds);
    }
    LastError = kErrorInvalidHandle;
    return HOST_WAIT_FAILED;
}

// Nothing queues an asynchronous procedure call here, so an alertable wait is an ordinary one.
unsigned long HostWaitForObjectEx(HostHandle handle, unsigned long milliseconds, int alertable) {
    (void)alertable;
    return HostWaitForObject(handle, milliseconds);
}

int HostCloseHandle(HostHandle handle) {
    if (handle == HOST_CURRENT_THREAD)
        return 1;
    if (handle == NULL || handle == HOST_INVALID_HANDLE) {
        LastError = kErrorInvalidHandle;
        return 0;
    }
    Object *object = static_cast<Object *>(handle);
    switch (object->kind) {
        case kThread:
            Release(static_cast<Thread *>(object));
            return 1;
        case kEvent: {
            Event *event = static_cast<Event *>(object);
            DestroySignal(&event->signal);
            delete event;
            return 1;
        }
        case kFile: {
            File *file = static_cast<File *>(object);
            close(file->fd);
            delete file;
            return 1;
        }
        default:
            LastError = kErrorInvalidHandle;
            return 0;
    }
}

// ---- sleeping

void HostSleep(unsigned long milliseconds) {
    if (milliseconds == 0) {
        sched_yield();
        return;
    }
    timespec wait = { (time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000 };
    while (nanosleep(&wait, &wait) != 0 && errno == EINTR) {
    }
}

unsigned long HostSleepEx(unsigned long milliseconds, int alertable) {
    (void)alertable;
    HostSleep(milliseconds);
    return 0;
}

// ---- heaps: the C library's, with each block's size in front of it (eight bytes, keeping Win32's alignment)

// ---- heaps (a 64-bit host's are GameWindow.cpp's)

#if UINTPTR_MAX == 0xffffffffu
struct BlockHeader {
    size_t size;
    size_t unused;
};
static_assert(sizeof(BlockHeader) == 8, "a block header keeps eight-byte alignment");

static BlockHeader *HeaderOf(const void *block) {
    return const_cast<BlockHeader *>(static_cast<const BlockHeader *>(block) - 1);
}

HostHandle HostHeapCreate(unsigned long flags, size_t initialSize, size_t maximumSize) {
    (void)initialSize;
    (void)maximumSize;
    Heap *heap = new Heap();
    heap->kind = kHeap;
    heap->flags = flags;
    return heap;
}

void *HostHeapAlloc(HostHandle heap, unsigned long flags, size_t size) {
    (void)heap;
    BlockHeader *header = static_cast<BlockHeader *>((flags & HOST_HEAP_ZERO_MEMORY) != 0
                                                         ? calloc(1, sizeof(BlockHeader) + size)
                                                         : malloc(sizeof(BlockHeader) + size));
    if (header == NULL)
        return NULL;
    header->size = size;
    return header + 1;
}

int HostHeapFree(HostHandle heap, unsigned long flags, void *block) {
    (void)heap;
    (void)flags;
    if (block != NULL)
        free(HeaderOf(block));
    return 1;
}

// HEAP_REALLOC_IN_PLACE_ONLY (0x10): only shrinking can be done in place here.
void *HostHeapReAlloc(HostHandle heap, unsigned long flags, void *block, size_t size) {
    (void)heap;
    BlockHeader *header = HeaderOf(block);
    size_t old = header->size;
    if ((flags & 0x10) != 0) {
        if (size > old)
            return NULL;
        header->size = size;
        return block;
    }
    header = static_cast<BlockHeader *>(realloc(header, sizeof(BlockHeader) + size));
    if (header == NULL)
        return NULL;
    header->size = size;
    if ((flags & HOST_HEAP_ZERO_MEMORY) != 0 && size > old)
        memset(reinterpret_cast<char *>(header + 1) + old, 0, size - old);
    return header + 1;
}

size_t HostHeapSize(HostHandle heap, unsigned long flags, const void *block) {
    (void)heap;
    (void)flags;
    return HeaderOf(block)->size;
}
#endif

// ---- pages: the mapping's length is kept in a page before the block

#if UINTPTR_MAX > 0xffffffffu
// The lowest free range of `length` bytes at or above `lowest` that ends below 4 GB, mapped; MAP_FAILED if none.
// Tried at each 64 KB step: MAP_FIXED_NOREPLACE fails where anything is mapped already.
static void *MapLow(size_t length, uintptr_t lowest, int protection, int flags) {
    const uintptr_t step = 0x10000;
    for (uintptr_t address = (lowest + step - 1) & ~(step - 1); address + length <= (uintptr_t(1) << 32);
         address += step) {
        void *mapping = mmap(reinterpret_cast<void *>(address), length, protection,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | flags, -1, 0);
        if (mapping != MAP_FAILED)
            return mapping;
    }
    return MAP_FAILED;
}

void *HostReserveLow(size_t size, uintptr_t lowest) {
    void *mapping = MapLow(size, lowest, PROT_NONE, MAP_NORESERVE);
    return mapping != MAP_FAILED ? mapping : NULL;
}

int HostCommit(void *address, size_t size) {
    return mprotect(address, size, PROT_READ | PROT_WRITE) == 0;
}
#endif

void *HostAllocatePages(size_t size, uintptr_t lowest, uintptr_t limit) {
    (void)lowest;
    (void)limit;
    size_t page = size_t(sysconf(_SC_PAGESIZE));
    size_t length = page + size;
#if UINTPTR_MAX > 0xffffffffu   // in the game window
    void *mapping = MapLow(length, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC, 0);
#else
    void *mapping = mmap(NULL, length, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    if (mapping == MAP_FAILED)
        return NULL;
    *static_cast<size_t *>(mapping) = length;
    return static_cast<char *>(mapping) + page;
}

void HostFreePages(void *block) {
    if (block == NULL)
        return;
    char *mapping = static_cast<char *>(block) - size_t(sysconf(_SC_PAGESIZE));
    munmap(mapping, *reinterpret_cast<size_t *>(mapping));
}

// ---- files

HostHandle HostCreateFile(const char *path, unsigned long desiredAccess, unsigned long share,
                          unsigned long disposition, unsigned long attributes) {
    (void)share;
    (void)attributes;
    int mode;
    if ((desiredAccess & kGenericWrite) != 0)
        mode = (desiredAccess & kGenericRead) != 0 ? O_RDWR : O_WRONLY;
    else
        mode = O_RDONLY;
    switch (disposition) {
        case kCreateNew:        mode |= O_CREAT | O_EXCL; break;
        case kCreateAlways:     mode |= O_CREAT | O_TRUNC; break;
        case kOpenAlways:       mode |= O_CREAT; break;
        case kTruncateExisting: mode |= O_TRUNC; break;
        case kOpenExisting:     break;
        default:
            LastError = kErrorInvalidParameter;
            return HOST_INVALID_HANDLE;
    }

    bool existed = (disposition == kCreateAlways || disposition == kOpenAlways) && access(path, F_OK) == 0;
    int fd = open(path, mode | O_CLOEXEC, 0644);
    if (fd < 0) {
        LastError = ErrorFromErrno(errno);
        return HOST_INVALID_HANDLE;
    }
    struct stat status;
    if (fstat(fd, &status) == 0 && S_ISDIR(status.st_mode)) {
        close(fd);
        LastError = kErrorAccessDenied;
        return HOST_INVALID_HANDLE;
    }
    File *file = new File();
    file->kind = kFile;
    file->fd = fd;
    LastError = existed ? kErrorAlreadyExists : 0;
    return file;
}

int HostDeleteFile(const char *path) {
    if (unlink(path) != 0) {
        LastError = ErrorFromErrno(errno);
        return 0;
    }
    return 1;
}

int HostReadFile(HostHandle handle, void *buffer, unsigned long bytes, unsigned long *read) {
    File *file = static_cast<File *>(ObjectOf(handle, kFile));
    if (read != NULL)
        *read = 0;
    if (file == NULL)
        return 0;
    unsigned long done = 0;
    while (done < bytes) {
        ssize_t got = ::read(file->fd, static_cast<char *>(buffer) + done, bytes - done);
        if (got < 0) {
            if (errno == EINTR)
                continue;
            LastError = errno == EISDIR ? kErrorAccessDenied : kErrorReadFault;
            if (read != NULL)
                *read = done;
            return 0;
        }
        if (got == 0)
            break;   // the end of the file: success, with fewer bytes
        done += (unsigned long)got;
    }
    if (read != NULL)
        *read = done;
    return 1;
}

int HostWriteFile(HostHandle handle, const void *buffer, unsigned long bytes, unsigned long *written) {
    File *file = static_cast<File *>(ObjectOf(handle, kFile));
    if (written != NULL)
        *written = 0;
    if (file == NULL)
        return 0;
    unsigned long done = 0;
    while (done < bytes) {
        ssize_t put = write(file->fd, static_cast<const char *>(buffer) + done, bytes - done);
        if (put < 0) {
            if (errno == EINTR)
                continue;
            LastError = errno == ENOSPC ? kErrorDiskFull : kErrorWriteFault;
            if (written != NULL)
                *written = done;
            return 0;
        }
        done += (unsigned long)put;
    }
    if (written != NULL)
        *written = done;
    return 1;
}

unsigned long HostSetFilePointer(HostHandle handle, long distance, long *distanceHigh, unsigned long method) {
    File *file = static_cast<File *>(ObjectOf(handle, kFile));
    if (file == NULL)
        return 0xffffffffu;
    int64_t offset = distanceHigh != NULL ? (int64_t)((uint64_t)(uint32_t)*distanceHigh << 32 | (uint32_t)distance)
                                          : (int64_t)distance;
    int whence = method == 0 ? SEEK_SET : method == 1 ? SEEK_CUR : SEEK_END;
    off_t position = lseek(file->fd, (off_t)offset, whence);
    if (position < 0) {
        LastError = errno == EINVAL ? kErrorNegativeSeek : ErrorFromErrno(errno);
        return 0xffffffffu;
    }
    if (distanceHigh != NULL)
        *distanceHigh = (long)((uint64_t)position >> 32);
    LastError = 0;
    return (unsigned long)(uint32_t)position;
}

unsigned long HostGetFileSize(HostHandle handle, unsigned long *sizeHigh) {
    int64_t size;
    if (!HostGetFileSizeEx(handle, &size))
        return 0xffffffffu;
    if (sizeHigh != NULL)
        *sizeHigh = (unsigned long)((uint64_t)size >> 32);
    LastError = 0;
    return (unsigned long)(uint32_t)size;
}

int HostGetFileSizeEx(HostHandle handle, int64_t *size) {
    File *file = static_cast<File *>(ObjectOf(handle, kFile));
    if (file == NULL)
        return 0;
    struct stat status;
    if (fstat(file->fd, &status) != 0) {
        LastError = ErrorFromErrno(errno);
        return 0;
    }
    *size = status.st_size;
    return 1;
}

// The next entry whose name matches the pattern's last component
static bool NextMatch(Search *search, char *name) {
    while (dirent *entry = readdir(search->directory)) {
        if (fnmatch(search->pattern, entry->d_name, FNM_CASEFOLD) == 0) {
            snprintf(name, HOST_MAX_PATH, "%s", entry->d_name);
            return true;
        }
    }
    return false;
}

// The pattern is split at its last slash, of either kind: the directory, then what to match in it.
HostHandle HostFindFirstFile(const char *pattern, char *name) {
    char directory[HOST_MAX_PATH];
    snprintf(directory, sizeof(directory), "%s", pattern);
    char *slash = NULL;
    for (char *c = directory; *c != '\0'; c++)
        if (*c == '/' || *c == '\\')
            slash = c;
    const char *last = pattern;
    if (slash != NULL) {
        last = pattern + (slash - directory) + 1;
        *slash = '\0';
        if (directory[0] == '\0')
            snprintf(directory, sizeof(directory), "/");
    } else {
        snprintf(directory, sizeof(directory), ".");
    }

    DIR *opened = opendir(directory);
    if (opened == NULL) {
        LastError = errno == ENOENT ? kErrorPathNotFound : ErrorFromErrno(errno);
        return HOST_INVALID_HANDLE;
    }
    Search *search = new Search();
    search->kind = kSearch;
    search->directory = opened;
    snprintf(search->pattern, sizeof(search->pattern), "%s", last);
    if (!NextMatch(search, name)) {
        closedir(opened);
        delete search;
        LastError = kErrorFileNotFound;
        return HOST_INVALID_HANDLE;
    }
    return search;
}

int HostFindNextFile(HostHandle handle, char *name) {
    Search *search = static_cast<Search *>(ObjectOf(handle, kSearch));
    if (search == NULL)
        return 0;
    if (!NextMatch(search, name)) {
        LastError = kErrorNoMoreFiles;
        return 0;
    }
    return 1;
}

int HostFindClose(HostHandle handle) {
    Search *search = static_cast<Search *>(ObjectOf(handle, kSearch));
    if (search == NULL)
        return 0;
    closedir(search->directory);
    delete search;
    return 1;
}

static char *Trim(char *text) {
    while (*text == ' ' || *text == '\t')
        text++;
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        text[--length] = '\0';
    return text;
}

// GetPrivateProfileString's reading: [section] and key=value lines, both names without regard to case, the value
// trimmed and unquoted.
unsigned long HostGetProfileString(const char *section, const char *key, const char *defaultValue, char *value,
                                   unsigned long size, const char *file) {
    if (size == 0)
        return 0;
    const char *found = defaultValue != NULL ? defaultValue : "";
    char line[1024], copy[1024];
    FILE *ini = fopen(file, "r");
    bool inSection = false;
    while (ini != NULL && fgets(line, sizeof(line), ini) != NULL) {
        char *text = Trim(line);
        if (text[0] == '[') {
            char *end = strchr(text, ']');
            if (end != NULL)
                *end = '\0';
            inSection = strcasecmp(Trim(text + 1), section) == 0;
            continue;
        }
        char *equals = strchr(text, '=');
        if (!inSection || equals == NULL)
            continue;
        *equals = '\0';
        if (strcasecmp(Trim(text), key) != 0)
            continue;
        char *v = Trim(equals + 1);
        size_t length = strlen(v);
        if (length >= 2 && (v[0] == '"' || v[0] == '\'') && v[length - 1] == v[0]) {
            v[length - 1] = '\0';
            v++;
        }
        snprintf(copy, sizeof(copy), "%s", v);
        found = copy;
        break;
    }
    if (ini != NULL)
        fclose(ini);
    snprintf(value, size, "%s", found);
    return (unsigned long)strlen(value);
}

// ---- time

static int64_t MonotonicNs(void) {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

int HostQueryPerformanceCounter(int64_t *counter) {
    *counter = MonotonicNs();
    return 1;
}

int HostQueryPerformanceFrequency(int64_t *frequency) {
    *frequency = 1000000000;
    return 1;
}

unsigned long HostTimeMs(void) {
    return (unsigned long)(uint32_t)(MonotonicNs() / 1000000);
}

// A sleeping thread already wakes to the millisecond.
int HostRaiseTimerResolution(void) {
    return 1;
}

struct Timer {
    unsigned id;
    unsigned delayMs;
    bool periodic;
    HostTimerCallback callback;
    unsigned long user;
};

// Each due time follows the last by the period, as winmm's periodic timer keeps its rate; a timer that has fallen
// a second behind starts again from now rather than firing to catch up.
static void *TimerMain(void *argument) {
    Timer *timer = static_cast<Timer *>(argument);
    timespec due;
    clock_gettime(CLOCK_MONOTONIC, &due);
    for (;;) {
        due.tv_nsec += (long)timer->delayMs * 1000000;
        while (due.tv_nsec >= 1000000000) {
            due.tv_sec++;
            due.tv_nsec -= 1000000000;
        }
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, NULL) == EINTR) {
        }
        timer->callback(timer->id, 0, timer->user, 0, 0);
        if (!timer->periodic)
            break;
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > due.tv_sec + 1)
            due = now;
    }
    delete timer;
    return NULL;
}

unsigned HostSetTimerEvent(unsigned delayMs, unsigned resolutionMs, HostTimerCallback callback, unsigned long user,
                           unsigned flags) {
    (void)resolutionMs;
    static unsigned nextId = 0;
    Timer *timer = new Timer();
    timer->id = __atomic_add_fetch(&nextId, 1, __ATOMIC_RELAXED);
    timer->delayMs = delayMs;
    timer->periodic = (flags & kTimePeriodic) != 0;
    timer->callback = callback;
    timer->user = user;

    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    int error = pthread_create(&thread, &attributes, TimerMain, timer);
    pthread_attr_destroy(&attributes);
    if (error != 0) {
        delete timer;
        return 0;
    }
    return timer->id;
}

static void ToSystemTime(const tm *local, long milliseconds, HostSystemTime *time) {
    time->year = (uint16_t)(local->tm_year + 1900);
    time->month = (uint16_t)(local->tm_mon + 1);
    time->dayOfWeek = (uint16_t)local->tm_wday;
    time->day = (uint16_t)local->tm_mday;
    time->hour = (uint16_t)local->tm_hour;
    time->minute = (uint16_t)local->tm_min;
    time->second = (uint16_t)local->tm_sec;
    time->milliseconds = (uint16_t)milliseconds;
}

void HostGetLocalTime(HostSystemTime *time) {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    tm local;
    localtime_r(&now.tv_sec, &local);
    ToSystemTime(&local, now.tv_nsec / 1000000, time);
}

static void Widen(const char *text, uint16_t *wide, size_t count) {
    size_t i = 0;
    for (; text != NULL && text[i] != '\0' && i + 1 < count; i++)
        wide[i] = (unsigned char)text[i];
    wide[i] = 0;
}

unsigned long HostGetTimeZoneInformation(HostTimeZone *zone) {
    memset(zone, 0, sizeof(*zone));
    time_t now = time(NULL);
    tm local;
    localtime_r(&now, &local);
    tzset();
    long offsetMinutes = local.tm_gmtoff / 60;
    Widen(tzname[0], zone->standardName, 32);
    Widen(tzname[1], zone->daylightName, 32);
    if (local.tm_isdst > 0) {
        zone->bias = -(offsetMinutes - 60);
        zone->daylightBias = -60;
        return kTimeZoneDaylight;
    }
    zone->bias = -offsetMinutes;
    return daylight ? kTimeZoneStandard : kTimeZoneUnknown;
}

// ---- the process

static int CommandLineCount;
static char **CommandLineArguments;

char **HostCommandLine(int *count) {
    *count = CommandLineCount;
    return CommandLineArguments;
}

void HostSetCommandLine(int count, char **arguments) {
    CommandLineCount = count;
    CommandLineArguments = arguments;
}

unsigned long HostGetEnvironment(const char *name, char *value, unsigned long size) {
    const char *text = getenv(name);
    if (text == NULL) {
        LastError = kErrorEnvironmentNotFound;
        return 0;
    }
    unsigned long length = (unsigned long)strlen(text);
    if (length + 1 > size)
        return length + 1;   // the size it needs, as Win32 answers
    memcpy(value, text, length + 1);
    return length;
}

void HostExitProcess(unsigned exitCode) {
    fflush(NULL);
    _exit((int)exitCode);
}

void HostDebugBreak(void) {
    raise(SIGTRAP);
}

void HostRaiseException(unsigned long code, unsigned long flags, unsigned long count, const unsigned long *arguments) {
    (void)flags;
    fprintf(stderr, "[host] exception 0x%08lx raised (%lu arguments, first 0x%08lx): no handler on this host\n", code,
            count, count > 0 ? arguments[0] : 0ul);
    fflush(NULL);
    abort();
}

// Whether every page of the range can be read: write() from it into a pipe fails with EFAULT where it cannot.
int HostIsBadReadPtr(const void *pointer, size_t bytes) {
    if (bytes == 0)
        return 0;
    if (pointer == NULL)
        return 1;
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static int pipeEnds[2] = { -1, -1 };
    pthread_mutex_lock(&mutex);
    if (pipeEnds[0] < 0 && pipe2(pipeEnds, O_CLOEXEC | O_NONBLOCK) != 0) {
        pthread_mutex_unlock(&mutex);
        return 0;
    }
    const long page = sysconf(_SC_PAGESIZE);
    uintptr_t first = (uintptr_t)pointer, last = first + bytes - 1;
    int bad = 0;
    for (uintptr_t at = first; !bad; at = (at | (page - 1)) + 1) {
        if (write(pipeEnds[1], (const void *)at, 1) != 1 && errno == EFAULT)
            bad = 1;
        char drain;
        while (read(pipeEnds[0], &drain, 1) == 1) {
        }
        if ((at | (page - 1)) >= last)
            break;
    }
    pthread_mutex_unlock(&mutex);
    return bad;
}

int HostIsBadWritePtr(void *pointer, size_t bytes) {
    return HostIsBadReadPtr(pointer, bytes);
}
