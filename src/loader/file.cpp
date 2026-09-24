#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "file.h"
#include "../common/xboxPath.h"

// ---------------------------------------------------------------------------------------------------------------
// The file system, as the kernel sees it.
//
// The game's file layers - FILESYS, the async loader, the CRT's fopen - all end up in XAPI's CreateFile,
// ReadFile and friends, and those end up here, in the NT calls the XBE imports. Ten of them cover everything
// the driving engine opens; the action engine replaces the XAPI layer above instead (its own file code had to
// exist before the loader did), so for now this serves the driving engine, but nothing about it is specific
// to either.
//
// Handles are Win32 handles: a file the game opens is a file the host has open, and NtClose in kernel.cpp
// closes it. That is what makes this small - there is no object table to keep, no per-handle state, and the
// file pointer Win32 already maintains is the one the game is moving when it seeks.
//
// THE PATH. An Xbox path is a drive letter and a backslash path - "d:\driving\misc.viv" - reached through an
// OBJECT_ATTRIBUTES whose RootDirectory is the DOS devices directory (-3) and whose ObjectName is an
// ANSI_STRING that is not necessarily null-terminated. src/common/xboxPath.cpp maps the drive letter onto a
// host directory, the same mapping the action engine's file layer uses.
//
// WHAT IS NOT HERE. Overlapped I/O: every read completes before the call returns, and when the caller passed
// an event it is signalled on the way out, which is a legal way for an asynchronous read to behave and is what
// the game's async loader - which runs its own thread and waits on that event - sees. Also absent:
// NtDeviceIoControlFile and NtFsControlFile, which on the console are the DVD drive's media check; nothing
// reaches them now that XapiInitProcess is replaced.
// ---------------------------------------------------------------------------------------------------------------

#define XBOX_STATUS_SUCCESS                 ((LONG)0x00000000)
#define XBOX_STATUS_UNSUCCESSFUL            ((LONG)0xC0000001)
#define XBOX_STATUS_NOT_IMPLEMENTED         ((LONG)0xC0000002)
#define XBOX_STATUS_INVALID_PARAMETER       ((LONG)0xC000000D)
#define XBOX_STATUS_NO_SUCH_FILE            ((LONG)0xC000000F)
#define XBOX_STATUS_END_OF_FILE             ((LONG)0xC0000011)
#define XBOX_STATUS_ACCESS_DENIED           ((LONG)0xC0000022)
#define XBOX_STATUS_OBJECT_NAME_NOT_FOUND   ((LONG)0xC0000034)
#define XBOX_STATUS_OBJECT_PATH_NOT_FOUND   ((LONG)0xC000003A)
#define XBOX_STATUS_INVALID_INFO_CLASS      ((LONG)0xC0000003)

// The Xbox's OBJECT_ATTRIBUTES is three words: it has no Length or SecurityDescriptor, because the object
// namespace it addresses is much smaller than NT's.
struct XboxAnsiString {
    USHORT Length;
    USHORT MaximumLength;
    char  *Buffer;
};

struct XboxObjectAttributes {
    HANDLE           RootDirectory;
    XboxAnsiString  *ObjectName;
    ULONG            Attributes;
};

struct XboxIoStatusBlock {
    LONG   Status;
    ULONG  Information;
};

// CreateDisposition, as NtCreateFile takes it. mingw-w64's winnt.h already defines these as macros, with the
// same values; the Windows SDK's leaves them to winternl.h, which is not included here.
#ifndef FILE_SUPERSEDE
enum { FILE_SUPERSEDE = 0, FILE_OPEN = 1, FILE_CREATE = 2, FILE_OPEN_IF = 3,
       FILE_OVERWRITE = 4, FILE_OVERWRITE_IF = 5 };
#endif

// Information, as the IO_STATUS_BLOCK reports it after a create.
enum { FILE_SUPERSEDED = 0, FILE_OPENED = 1, FILE_CREATED = 2, FILE_OVERWRITTEN = 3 };

// The FILE_INFORMATION_CLASS values that are asked for.
enum { FileDirectoryInformation = 1, FileBasicInformation = 4, FileStandardInformation = 5,
       FilePositionInformation = 14, FileEndOfFileInformation = 20, FileNetworkOpenInformation = 34 };

// Seen in CreateOptions. mingw-w64 defines these too.
#ifndef FILE_DIRECTORY_FILE
#define FILE_DIRECTORY_FILE     0x00000001u
#define FILE_NON_DIRECTORY_FILE 0x00000040u
#endif

static LONG StatusFromLastError(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:    return XBOX_STATUS_OBJECT_NAME_NOT_FOUND;
        case ERROR_PATH_NOT_FOUND:    return XBOX_STATUS_OBJECT_PATH_NOT_FOUND;
        case ERROR_ACCESS_DENIED:     return XBOX_STATUS_ACCESS_DENIED;
        case ERROR_HANDLE_EOF:        return XBOX_STATUS_END_OF_FILE;
        default:                      return XBOX_STATUS_UNSUCCESSFUL;
    }
}

// The path out of an OBJECT_ATTRIBUTES, resolved onto the host. The ANSI_STRING's Length is authoritative -
// the buffer is not always null-terminated - so it is copied out before anything else touches it.
static bool ResolveObjectName(const XboxObjectAttributes *attributes, char *out, size_t outSize) {
    if (attributes == NULL || attributes->ObjectName == NULL || attributes->ObjectName->Buffer == NULL)
        return false;

    char xboxPath[MAX_PATH];
    size_t length = attributes->ObjectName->Length;
    if (length >= sizeof(xboxPath))
        length = sizeof(xboxPath) - 1;
    memcpy(xboxPath, attributes->ObjectName->Buffer, length);
    xboxPath[length] = '\0';

    return Xbox_ResolvePath(xboxPath, out, outSize);
}

// Win32's CreateFile arguments out of the NT ones. The access mask and share mode are the same bits on both
// systems; only the disposition has to be translated, and NT's four-way "open, create, either, replace"
// maps onto Win32's five-way set exactly.
static DWORD Win32DispositionOf(ULONG createDisposition) {
    switch (createDisposition) {
        case FILE_SUPERSEDE:      return CREATE_ALWAYS;
        case FILE_CREATE:         return CREATE_NEW;
        case FILE_OPEN_IF:        return OPEN_ALWAYS;
        case FILE_OVERWRITE:      return TRUNCATE_EXISTING;
        case FILE_OVERWRITE_IF:   return CREATE_ALWAYS;
        case FILE_OPEN:
        default:                  return OPEN_EXISTING;
    }
}

static LONG OpenCommon(HANDLE *fileHandle, ACCESS_MASK desiredAccess,
                       const XboxObjectAttributes *objectAttributes, XboxIoStatusBlock *ioStatusBlock,
                       ULONG shareAccess, ULONG createDisposition, ULONG createOptions) {
    if (fileHandle == NULL)
        return XBOX_STATUS_INVALID_PARAMETER;

    char hostPath[MAX_PATH];
    if (!ResolveObjectName(objectAttributes, hostPath, sizeof(hostPath)))
        return XBOX_STATUS_INVALID_PARAMETER;

    // A directory is opened for enumeration, which Win32 needs told about explicitly.
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if ((createOptions & FILE_DIRECTORY_FILE) != 0)
        flags |= FILE_FLAG_BACKUP_SEMANTICS;

    DWORD access = (desiredAccess & (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)) != 0
                 ? desiredAccess
                 : (desiredAccess | GENERIC_READ);
    DWORD share = (shareAccess != 0) ? shareAccess : (FILE_SHARE_READ | FILE_SHARE_WRITE);

    HANDLE handle = CreateFileA(hostPath, access, share, NULL,
                                Win32DispositionOf(createDisposition), flags, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        // A miss is not news. The game looks for a loose file first and falls back to the archives, so
        // every file that lives in a .viv is reported missing here on its way to being found - see the
        // fallback in FUN_0010c070, which searches every open big file when the loose open fails.
        DWORD error = GetLastError();
        if (ioStatusBlock != NULL) {
            ioStatusBlock->Status = StatusFromLastError(error);
            ioStatusBlock->Information = 0;
        }
        return StatusFromLastError(error);
    }

    *fileHandle = handle;
    if (ioStatusBlock != NULL) {
        ioStatusBlock->Status = XBOX_STATUS_SUCCESS;
        ioStatusBlock->Information = (createDisposition == FILE_CREATE ||
                                      createDisposition == FILE_OVERWRITE_IF ||
                                      createDisposition == FILE_SUPERSEDE) ? FILE_CREATED : FILE_OPENED;
    }
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 190.
static LONG __stdcall Xbox_NtCreateFile(HANDLE *fileHandle, ACCESS_MASK desiredAccess,
                                        XboxObjectAttributes *objectAttributes,
                                        XboxIoStatusBlock *ioStatusBlock, LARGE_INTEGER *allocationSize,
                                        ULONG fileAttributes, ULONG shareAccess, ULONG createDisposition,
                                        ULONG createOptions) {
    (void)allocationSize;   // a hint about the eventual size; Win32 has no equivalent at create time
    (void)fileAttributes;
    return OpenCommon(fileHandle, desiredAccess, objectAttributes, ioStatusBlock,
                      shareAccess, createDisposition, createOptions);
}

// Ordinal 202. NtOpenFile is NtCreateFile with the disposition fixed at "it must already exist".
static LONG __stdcall Xbox_NtOpenFile(HANDLE *fileHandle, ACCESS_MASK desiredAccess,
                                      XboxObjectAttributes *objectAttributes,
                                      XboxIoStatusBlock *ioStatusBlock, ULONG shareAccess,
                                      ULONG openOptions) {
    return OpenCommon(fileHandle, desiredAccess, objectAttributes, ioStatusBlock,
                      shareAccess, FILE_OPEN, openOptions);
}

// A read or a write at an explicit offset, or at the file's own pointer when the offset is absent or is the
// "use the current position" sentinel.
static bool SeekIfAsked(HANDLE handle, const LARGE_INTEGER *byteOffset) {
    if (byteOffset == NULL)
        return true;
    // FILE_USE_FILE_POINTER_POSITION: the caller means "wherever the handle is".
    if (byteOffset->QuadPart == -2)
        return true;
    if (byteOffset->QuadPart < 0)
        return true;

    LARGE_INTEGER moved;
    return SetFilePointerEx(handle, *byteOffset, &moved, FILE_BEGIN) != 0;
}

// Ordinals 219 and 236. Both are synchronous here; see the note at the top about the event.
static LONG __stdcall Xbox_NtReadFile(HANDLE fileHandle, HANDLE event, void *apcRoutine, void *apcContext,
                                      XboxIoStatusBlock *ioStatusBlock, void *buffer, ULONG length,
                                      LARGE_INTEGER *byteOffset) {
    (void)apcRoutine; (void)apcContext;
    if (!SeekIfAsked(fileHandle, byteOffset))
        return StatusFromLastError(GetLastError());

    DWORD read = 0;
    BOOL ok = ReadFile(fileHandle, buffer, length, &read, NULL);
    LONG status = ok ? (read == 0 && length != 0 ? XBOX_STATUS_END_OF_FILE : XBOX_STATUS_SUCCESS)
                     : StatusFromLastError(GetLastError());

    if (ioStatusBlock != NULL) {
        ioStatusBlock->Status = status;
        ioStatusBlock->Information = read;
    }
    if (event != NULL)
        SetEvent(event);
    return status;
}

static LONG __stdcall Xbox_NtWriteFile(HANDLE fileHandle, HANDLE event, void *apcRoutine, void *apcContext,
                                       XboxIoStatusBlock *ioStatusBlock, void *buffer, ULONG length,
                                       LARGE_INTEGER *byteOffset) {
    (void)apcRoutine; (void)apcContext;
    if (!SeekIfAsked(fileHandle, byteOffset))
        return StatusFromLastError(GetLastError());

    DWORD written = 0;
    BOOL ok = WriteFile(fileHandle, buffer, length, &written, NULL);
    LONG status = ok ? XBOX_STATUS_SUCCESS : StatusFromLastError(GetLastError());

    if (ioStatusBlock != NULL) {
        ioStatusBlock->Status = status;
        ioStatusBlock->Information = written;
    }
    if (event != NULL)
        SetEvent(event);
    return status;
}

// Ordinal 211. Three classes are answered; anything else says so rather than filling the buffer with
// something plausible, because a wrong file size is a much harder bug to find than a refused query.
static LONG __stdcall Xbox_NtQueryInformationFile(HANDLE fileHandle, XboxIoStatusBlock *ioStatusBlock,
                                                  void *fileInformation, ULONG length,
                                                  ULONG fileInformationClass) {
    LONG status = XBOX_STATUS_SUCCESS;
    ULONG written = 0;

    switch (fileInformationClass) {
        case FileStandardInformation: {
            // { LARGE_INTEGER AllocationSize, EndOfFile; ULONG NumberOfLinks; BOOLEAN DeletePending, Directory; }
            struct { LARGE_INTEGER AllocationSize, EndOfFile; ULONG NumberOfLinks;
                     BOOLEAN DeletePending, Directory; } info;
            memset(&info, 0, sizeof(info));

            BY_HANDLE_FILE_INFORMATION handleInfo;
            if (!GetFileInformationByHandle(fileHandle, &handleInfo)) {
                status = StatusFromLastError(GetLastError());
                break;
            }
            info.EndOfFile.LowPart = handleInfo.nFileSizeLow;
            info.EndOfFile.HighPart = (LONG)handleInfo.nFileSizeHigh;
            info.AllocationSize = info.EndOfFile;
            info.NumberOfLinks = handleInfo.nNumberOfLinks;
            info.Directory = (handleInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;

            written = (length < sizeof(info)) ? length : sizeof(info);
            memcpy(fileInformation, &info, written);
            break;
        }
        case FilePositionInformation: {
            LARGE_INTEGER zero, position;
            zero.QuadPart = 0;
            if (!SetFilePointerEx(fileHandle, zero, &position, FILE_CURRENT)) {
                status = StatusFromLastError(GetLastError());
                break;
            }
            written = (length < sizeof(position)) ? length : sizeof(position);
            memcpy(fileInformation, &position, written);
            break;
        }
        case FileNetworkOpenInformation: {
            // { LARGE_INTEGER Creation, LastAccess, LastWrite, Change, AllocationSize, EndOfFile; ULONG Attributes; }
            struct { LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime,
                                   AllocationSize, EndOfFile; ULONG FileAttributes; } info;
            memset(&info, 0, sizeof(info));

            BY_HANDLE_FILE_INFORMATION handleInfo;
            if (!GetFileInformationByHandle(fileHandle, &handleInfo)) {
                status = StatusFromLastError(GetLastError());
                break;
            }
            info.CreationTime.LowPart = handleInfo.ftCreationTime.dwLowDateTime;
            info.CreationTime.HighPart = (LONG)handleInfo.ftCreationTime.dwHighDateTime;
            info.LastAccessTime.LowPart = handleInfo.ftLastAccessTime.dwLowDateTime;
            info.LastAccessTime.HighPart = (LONG)handleInfo.ftLastAccessTime.dwHighDateTime;
            info.LastWriteTime = info.ChangeTime = info.LastAccessTime;
            info.EndOfFile.LowPart = handleInfo.nFileSizeLow;
            info.EndOfFile.HighPart = (LONG)handleInfo.nFileSizeHigh;
            info.AllocationSize = info.EndOfFile;
            info.FileAttributes = handleInfo.dwFileAttributes;

            written = (length < sizeof(info)) ? length : sizeof(info);
            memcpy(fileInformation, &info, written);
            break;
        }
        default:
            printf("[loader] NtQueryInformationFile: class %lu is not implemented\n", fileInformationClass);
            fflush(stdout);
            status = XBOX_STATUS_INVALID_INFO_CLASS;
            break;
    }

    if (ioStatusBlock != NULL) {
        ioStatusBlock->Status = status;
        ioStatusBlock->Information = written;
    }
    return status;
}

// Ordinal 226. Seeking is a set of FilePositionInformation, which is how the CRT's fseek arrives here.
static LONG __stdcall Xbox_NtSetInformationFile(HANDLE fileHandle, XboxIoStatusBlock *ioStatusBlock,
                                                void *fileInformation, ULONG length,
                                                ULONG fileInformationClass) {
    LONG status = XBOX_STATUS_SUCCESS;

    switch (fileInformationClass) {
        case FilePositionInformation: {
            if (fileInformation == NULL || length < sizeof(LARGE_INTEGER)) {
                status = XBOX_STATUS_INVALID_PARAMETER;
                break;
            }
            LARGE_INTEGER position = *(const LARGE_INTEGER *)fileInformation, moved;
            if (!SetFilePointerEx(fileHandle, position, &moved, FILE_BEGIN))
                status = StatusFromLastError(GetLastError());
            break;
        }
        case FileEndOfFileInformation: {
            if (fileInformation == NULL || length < sizeof(LARGE_INTEGER)) {
                status = XBOX_STATUS_INVALID_PARAMETER;
                break;
            }
            LARGE_INTEGER end = *(const LARGE_INTEGER *)fileInformation, moved;
            if (!SetFilePointerEx(fileHandle, end, &moved, FILE_BEGIN) || !SetEndOfFile(fileHandle))
                status = StatusFromLastError(GetLastError());
            break;
        }
        default:
            printf("[loader] NtSetInformationFile: class %lu is not implemented\n", fileInformationClass);
            fflush(stdout);
            status = XBOX_STATUS_INVALID_INFO_CLASS;
            break;
    }

    if (ioStatusBlock != NULL) {
        ioStatusBlock->Status = status;
        ioStatusBlock->Information = 0;
    }
    return status;
}

// Ordinal 218. The console's answer describes the DVD or the hard disk; what the game does with it is decide
// how much it can cache, so the sector size is the part that has to be truthful-ish, and 2048 is the disc's.
static LONG __stdcall Xbox_NtQueryVolumeInformationFile(HANDLE fileHandle, XboxIoStatusBlock *ioStatusBlock,
                                                        void *volumeInformation, ULONG length,
                                                        ULONG volumeInformationClass) {
    (void)fileHandle;
    enum { FileFsSizeInformation = 3 };

    if (volumeInformationClass != FileFsSizeInformation) {
        printf("[loader] NtQueryVolumeInformationFile: class %lu is not implemented\n",
               volumeInformationClass);
        fflush(stdout);
        if (ioStatusBlock != NULL) {
            ioStatusBlock->Status = XBOX_STATUS_INVALID_INFO_CLASS;
            ioStatusBlock->Information = 0;
        }
        return XBOX_STATUS_INVALID_INFO_CLASS;
    }

    struct { LARGE_INTEGER TotalAllocationUnits, AvailableAllocationUnits;
             ULONG SectorsPerAllocationUnit, BytesPerSector; } info;
    memset(&info, 0, sizeof(info));
    info.TotalAllocationUnits.QuadPart = 1000000;
    info.AvailableAllocationUnits.QuadPart = 1000000;
    info.SectorsPerAllocationUnit = 1;
    info.BytesPerSector = 2048;

    ULONG written = (length < sizeof(info)) ? length : sizeof(info);
    memcpy(volumeInformation, &info, written);
    if (ioStatusBlock != NULL) {
        ioStatusBlock->Status = XBOX_STATUS_SUCCESS;
        ioStatusBlock->Information = written;
    }
    return XBOX_STATUS_SUCCESS;
}

// Ordinal 198.
static LONG __stdcall Xbox_NtFlushBuffersFile(HANDLE fileHandle, XboxIoStatusBlock *ioStatusBlock) {
    LONG status = FlushFileBuffers(fileHandle) ? XBOX_STATUS_SUCCESS : StatusFromLastError(GetLastError());
    if (ioStatusBlock != NULL) {
        ioStatusBlock->Status = status;
        ioStatusBlock->Information = 0;
    }
    return status;
}

// ---------------------------------------------------------------------------------------------------------------
// The string helpers that go with them.
//
// RtlInitAnsiString is how every path reaches NtCreateFile, so it belongs beside the file code rather than
// with the rest of Rtl. The others are here because the file layer is what calls them.
// ---------------------------------------------------------------------------------------------------------------

// Ordinal 289. Length excludes the terminator, MaximumLength includes it - the distinction matters, because
// the callers use Length to find the last character of the path.
static void __stdcall Xbox_RtlInitAnsiString(XboxAnsiString *destination, const char *source) {
    if (destination == NULL)
        return;
    destination->Buffer = (char *)source;
    if (source == NULL) {
        destination->Length = 0;
        destination->MaximumLength = 0;
        return;
    }
    size_t length = strlen(source);
    if (length > 0xFFFE)
        length = 0xFFFE;
    destination->Length = (USHORT)length;
    destination->MaximumLength = (USHORT)(length + 1);
}

// Ordinal 279. Case-insensitive comparison is an option rather than the default, as in NT.
static BOOLEAN __stdcall Xbox_RtlEqualString(const XboxAnsiString *first, const XboxAnsiString *second,
                                             BOOLEAN caseInsensitive) {
    if (first == NULL || second == NULL)
        return FALSE;
    if (first->Length != second->Length)
        return FALSE;
    if (first->Length == 0)
        return TRUE;

    if (caseInsensitive)
        return _strnicmp(first->Buffer, second->Buffer, first->Length) == 0 ? TRUE : FALSE;
    return memcmp(first->Buffer, second->Buffer, first->Length) == 0 ? TRUE : FALSE;
}

// Ordinal 269. Returns how many bytes from the start match the pattern, counting in whole dwords.
static ULONG __stdcall Xbox_RtlCompareMemoryUlong(const void *source, ULONG length, ULONG pattern) {
    const ULONG *words = (const ULONG *)source;
    ULONG count = length / sizeof(ULONG);
    for (ULONG i = 0; i < count; i++) {
        if (words[i] != pattern)
            return i * sizeof(ULONG);
    }
    return count * sizeof(ULONG);
}

// Ordinal 301. The game turns a failed NT call into a Win32 error for its own error reporting; only the
// handful that file operations actually produce are worth translating.
static ULONG __stdcall Xbox_RtlNtStatusToDosError(LONG status) {
    switch (status) {
        case XBOX_STATUS_SUCCESS:                 return ERROR_SUCCESS;
        case XBOX_STATUS_OBJECT_NAME_NOT_FOUND:   return ERROR_FILE_NOT_FOUND;
        case XBOX_STATUS_OBJECT_PATH_NOT_FOUND:   return ERROR_PATH_NOT_FOUND;
        case XBOX_STATUS_ACCESS_DENIED:           return ERROR_ACCESS_DENIED;
        case XBOX_STATUS_END_OF_FILE:             return ERROR_HANDLE_EOF;
        case XBOX_STATUS_INVALID_PARAMETER:       return ERROR_INVALID_PARAMETER;
        case XBOX_STATUS_NO_SUCH_FILE:            return ERROR_FILE_NOT_FOUND;
        default:                                  return ERROR_GEN_FAILURE;
    }
}

const KernelFileExport *Kernel_FileExports(unsigned *count) {
    static const KernelFileExport exports[] = {
        { 190, (void *)Xbox_NtCreateFile },
        { 198, (void *)Xbox_NtFlushBuffersFile },
        { 202, (void *)Xbox_NtOpenFile },
        { 211, (void *)Xbox_NtQueryInformationFile },
        { 218, (void *)Xbox_NtQueryVolumeInformationFile },
        { 219, (void *)Xbox_NtReadFile },
        { 226, (void *)Xbox_NtSetInformationFile },
        { 236, (void *)Xbox_NtWriteFile },
        { 269, (void *)Xbox_RtlCompareMemoryUlong },
        { 279, (void *)Xbox_RtlEqualString },
        { 289, (void *)Xbox_RtlInitAnsiString },
        { 301, (void *)Xbox_RtlNtStatusToDosError },
    };
    *count = sizeof(exports) / sizeof(exports[0]);
    return exports;
}
