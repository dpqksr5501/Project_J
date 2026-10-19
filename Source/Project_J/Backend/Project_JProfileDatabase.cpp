#include "Backend/Project_JProfileDatabase.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"

namespace
{
// SQLite rollback locking protocol; interoperates with native SQLite tools.
// WAL deliberately unsupported (io_methods v1). No engine-global VFS override.
constexpr LONGLONG PendingByte = 0x40000000, ReservedByte = PendingByte + 1, SharedFirst = PendingByte + 2;
struct FFile
{
	sqlite3_file Base;
	HANDLE Handle;
	int Lock;
	bool ReservedHeld;
	bool PendingHeld;
	bool DeleteOnClose;
	bool FailNextWrite;
	bool CrashAfterSync;
	FString *Path;
};
bool Range(FFile *F, LONGLONG Offset, DWORD Length, bool Exclusive, bool Unlock = false)
{
	OVERLAPPED O = {};
	O.Offset = DWORD(Offset);
	O.OffsetHigh = DWORD(Offset >> 32);
	return Unlock ? !!UnlockFileEx(F->Handle, 0, Length, 0, &O)
				  : !!LockFileEx(F->Handle, LOCKFILE_FAIL_IMMEDIATELY | (Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0,
								 Length, 0, &O);
}
int Unlock(sqlite3_file *File, int Target)
{
	auto *F = reinterpret_cast<FFile *>(File);
	bool OK = true;
	if (F->Lock >= SQLITE_LOCK_SHARED && (Target == SQLITE_LOCK_NONE || F->Lock == SQLITE_LOCK_EXCLUSIVE))
	{
		OK = Range(F, SharedFirst, 510, false, true);
		if (Target == SQLITE_LOCK_SHARED)
			OK = Range(F, SharedFirst, 510, false) && OK;
	}
	if (F->PendingHeld)
	{
		OK = Range(F, PendingByte, 1, true, true) && OK;
		F->PendingHeld = false;
	}
	if (F->ReservedHeld)
	{
		OK = Range(F, ReservedByte, 1, true, true) && OK;
		F->ReservedHeld = false;
	}
	F->Lock = Target;
	return OK ? SQLITE_OK : SQLITE_IOERR_UNLOCK;
}
int Close(sqlite3_file *File)
{
	auto *F = reinterpret_cast<FFile *>(File);
	const bool OK = !!CloseHandle(F->Handle);
	if (F->DeleteOnClose)
		DeleteFileW(**F->Path);
	delete F->Path;
	F->Path = nullptr;
	return OK ? SQLITE_OK : SQLITE_IOERR_CLOSE;
}
int Read(sqlite3_file *File, void *Buffer, int N, sqlite3_int64 Offset)
{
	auto *F = reinterpret_cast<FFile *>(File);
	OVERLAPPED O = {};
	O.Offset = DWORD(Offset);
	O.OffsetHigh = DWORD(Offset >> 32);
	DWORD Done = 0;
	if (!ReadFile(F->Handle, Buffer, N, &Done, &O) && GetLastError() != ERROR_HANDLE_EOF)
		return SQLITE_IOERR_READ;
	if (Done < DWORD(N))
	{
		FMemory::Memzero(static_cast<uint8 *>(Buffer) + Done, N - Done);
		return SQLITE_IOERR_SHORT_READ;
	}
	return SQLITE_OK;
}
int Write(sqlite3_file *File, const void *Buffer, int N, sqlite3_int64 Offset)
{
	auto *F = reinterpret_cast<FFile *>(File);
	OVERLAPPED O = {};
	O.Offset = DWORD(Offset);
	O.OffsetHigh = DWORD(Offset >> 32);
	DWORD Done = 0;
#if WITH_DEV_AUTOMATION_TESTS
	if (F->FailNextWrite)
	{
		F->FailNextWrite = false;
		return SQLITE_IOERR_WRITE;
	}
#endif
	const bool OK = !!WriteFile(F->Handle, Buffer, N, &Done, &O);
	if (OK && Done == DWORD(N))
		return SQLITE_OK;
	const DWORD Error = GetLastError();
	return (OK || Error == ERROR_DISK_FULL || Error == ERROR_HANDLE_DISK_FULL) ? SQLITE_FULL : SQLITE_IOERR_WRITE;
}
int Truncate(sqlite3_file *File, sqlite3_int64 Size)
{
	auto *F = reinterpret_cast<FFile *>(File);
	LARGE_INTEGER P;
	P.QuadPart = Size;
	return SetFilePointerEx(F->Handle, P, nullptr, FILE_BEGIN) && SetEndOfFile(F->Handle) ? SQLITE_OK
																						  : SQLITE_IOERR_TRUNCATE;
}
int Sync(sqlite3_file *File, int)
{
	auto *F = reinterpret_cast<FFile *>(File);
	if (!FlushFileBuffers(F->Handle))
		return SQLITE_IOERR_FSYNC;
#if WITH_DEV_AUTOMATION_TESTS
	if (F->CrashAfterSync)
		FPlatformMisc::RequestExitWithStatus(true, 99,
											 TEXT("Owned recovery fixture inside COMMIT after database sync"));
#endif
	return SQLITE_OK;
}
int Size(sqlite3_file *File, sqlite3_int64 *Out)
{
	LARGE_INTEGER S;
	if (!GetFileSizeEx(reinterpret_cast<FFile *>(File)->Handle, &S))
		return SQLITE_IOERR_FSTAT;
	*Out = S.QuadPart;
	return SQLITE_OK;
}
int Lock(sqlite3_file *File, int Target)
{
	auto *F = reinterpret_cast<FFile *>(File);
	if (F->Lock >= Target)
		return SQLITE_OK;
	if (F->Lock == SQLITE_LOCK_NONE)
	{
		if (!Range(F, PendingByte, 1, false))
			return SQLITE_BUSY;
		const bool OK = Range(F, SharedFirst, 510, false);
		Range(F, PendingByte, 1, false, true);
		if (!OK)
			return SQLITE_BUSY;
		F->Lock = SQLITE_LOCK_SHARED;
	}
	if (Target == SQLITE_LOCK_SHARED)
		return SQLITE_OK;
	// Recovery requests EXCLUSIVE directly from SHARED and MUST NOT take
	// RESERVED (SQLite hot-journal protocol). A normal writer asks RESERVED first.
	if (Target == SQLITE_LOCK_RESERVED && !F->ReservedHeld)
	{
		if (!Range(F, ReservedByte, 1, true))
			return SQLITE_BUSY;
		F->ReservedHeld = true;
		F->Lock = SQLITE_LOCK_RESERVED;
	}
	if (Target == SQLITE_LOCK_RESERVED)
		return SQLITE_OK;
	if (F->Lock < SQLITE_LOCK_PENDING)
	{
		if (!Range(F, PendingByte, 1, true))
			return SQLITE_BUSY;
		F->PendingHeld = true;
		F->Lock = SQLITE_LOCK_PENDING;
	}
	if (!Range(F, SharedFirst, 510, false, true))
		return SQLITE_IOERR_UNLOCK;
	if (!Range(F, SharedFirst, 510, true))
	{
		if (!Range(F, SharedFirst, 510, false))
			return SQLITE_IOERR_LOCK;
		return SQLITE_BUSY;
	}
	F->Lock = SQLITE_LOCK_EXCLUSIVE;
	return SQLITE_OK;
}
int Reserved(sqlite3_file *File, int *Out)
{
	auto *F = reinterpret_cast<FFile *>(File);
	*Out = 1;
	if (F->ReservedHeld)
		return SQLITE_OK;
	if (Range(F, ReservedByte, 1, true))
	{
		*Out = 0;
		return Range(F, ReservedByte, 1, true, true) ? SQLITE_OK : SQLITE_IOERR_UNLOCK;
	}
	return GetLastError() == ERROR_LOCK_VIOLATION ? SQLITE_OK : SQLITE_IOERR_CHECKRESERVEDLOCK;
}
int Control(sqlite3_file *File, int Op, void *Arg)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (Op == 10001)
	{
		reinterpret_cast<FFile *>(File)->FailNextWrite = true;
		return SQLITE_OK;
	}
	if (Op == 10002)
	{
		reinterpret_cast<FFile *>(File)->CrashAfterSync = true;
		return SQLITE_OK;
	}
#endif
	if (Op == SQLITE_FCNTL_LOCKSTATE)
	{
		*static_cast<int *>(Arg) = reinterpret_cast<FFile *>(File)->Lock;
		return SQLITE_OK;
	}
	return SQLITE_NOTFOUND;
}
int Sector(sqlite3_file *)
{
	return 4096;
}
int Device(sqlite3_file *)
{
	return 0;
}
const sqlite3_io_methods Methods = {1,		 Close,	  Read,		Write,	 Truncate, Sync,   Size,
									Lock,	 Unlock,  Reserved, Control, Sector,   Device, nullptr,
									nullptr, nullptr, nullptr,	nullptr, nullptr};
int Open(sqlite3_vfs *, const char *Name, sqlite3_file *File, int Flags, int *OutFlags)
{
	auto *F = reinterpret_cast<FFile *>(File);
	FMemory::Memzero(F, sizeof(*F));
	F->Handle = INVALID_HANDLE_VALUE;
	FString Path =
		Name ? UTF8_TO_TCHAR(Name)
			 : FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LocalProfiles/Temp"), FGuid::NewGuid().ToString());
	if (!Name)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		Flags |= SQLITE_OPEN_CREATE | SQLITE_OPEN_EXCLUSIVE | SQLITE_OPEN_DELETEONCLOSE;
	}
	const bool RW = (Flags & SQLITE_OPEN_READWRITE) != 0;
	F->Handle = CreateFileW(
		*Path, GENERIC_READ | (RW ? GENERIC_WRITE : 0), FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		(Flags & SQLITE_OPEN_CREATE) ? ((Flags & SQLITE_OPEN_EXCLUSIVE) ? CREATE_NEW : OPEN_ALWAYS) : OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (F->Handle == INVALID_HANDLE_VALUE)
		return SQLITE_CANTOPEN;
	F->Path = new FString(MoveTemp(Path));
	F->DeleteOnClose = (Flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
	F->Base.pMethods = &Methods;
	if (OutFlags)
		*OutFlags = Flags;
	return SQLITE_OK;
}
int Delete(sqlite3_vfs *, const char *Name, int)
{
	if (DeleteFileW(UTF8_TO_TCHAR(Name)))
		return SQLITE_OK;
	return GetLastError() == ERROR_FILE_NOT_FOUND ? SQLITE_OK : SQLITE_IOERR_DELETE;
}
int Access(sqlite3_vfs *, const char *Name, int Flags, int *Out)
{
	const DWORD A = GetFileAttributesW(UTF8_TO_TCHAR(Name));
	*Out = A != INVALID_FILE_ATTRIBUTES && !(A & FILE_ATTRIBUTE_DIRECTORY) &&
		   (Flags != SQLITE_ACCESS_READWRITE || !(A & FILE_ATTRIBUTE_READONLY));
	return SQLITE_OK;
}
int FullPath(sqlite3_vfs *, const char *Name, int N, char *Out)
{
	const FString P = FPaths::ConvertRelativePathToFull(UTF8_TO_TCHAR(Name));
	FTCHARToUTF8 U(*P);
	if (U.Length() + 1 > N)
		return SQLITE_CANTOPEN;
	FMemory::Memcpy(Out, U.Get(), U.Length() + 1);
	return SQLITE_OK;
}
sqlite3_vfs VFS = {};
sqlite3_vfs *Parent = nullptr;
FCriticalSection Registration;
bool Register()
{
	FScopeLock Guard(&Registration);
	if (VFS.zName)
		return true;
	Parent = sqlite3_vfs_find(nullptr);
	if (!Parent)
		return false;
	VFS.iVersion = 1;
	VFS.szOsFile = sizeof(FFile);
	VFS.mxPathname = 32768;
	VFS.zName = "projectj-win32-rollback";
	VFS.xOpen = Open;
	VFS.xDelete = Delete;
	VFS.xAccess = Access;
	VFS.xFullPathname = FullPath;
	VFS.xRandomness = [](sqlite3_vfs *, int N, char *B) { return Parent->xRandomness(Parent, N, B); };
	VFS.xSleep = [](sqlite3_vfs *, int N) { return Parent->xSleep(Parent, N); };
	VFS.xCurrentTime = [](sqlite3_vfs *, double *T) { return Parent->xCurrentTime(Parent, T); };
	if (sqlite3_vfs_register(&VFS, 0) != SQLITE_OK)
	{
		VFS.zName = nullptr;
		return false;
	}
	return true;
}
} // namespace
#endif
bool FProject_JProfileDatabase::Open(const TCHAR *Filename, ESQLiteDatabaseOpenMode Mode)
{
	if (Handle)
		return false;
	FSQLiteDatabase InitializeLibrary; // Public constructor initializes SQLiteCore's allocator/VFS.
#if PLATFORM_WINDOWS
	if (!Register())
		return false;
	const int Flags =
		Mode == ESQLiteDatabaseOpenMode::ReadOnly
			? SQLITE_OPEN_READONLY
			: SQLITE_OPEN_READWRITE | (Mode == ESQLiteDatabaseOpenMode::ReadWriteCreate ? SQLITE_OPEN_CREATE : 0);
	if (sqlite3_open_v2(TCHAR_TO_UTF8(Filename), &Handle, Flags, "projectj-win32-rollback") != SQLITE_OK)
	{
		Close();
		return false;
	}
	return true;
#else
	return false; // Explicit unsupported platform; never silently use the unsafe VFS.
#endif
}
void FProject_JProfileDatabase::ShutdownVFS()
{
#if PLATFORM_WINDOWS
	FScopeLock Guard(&Registration);
	if (VFS.zName)
	{
		sqlite3_vfs_unregister(&VFS);
		VFS.zName = nullptr;
		Parent = nullptr;
	}
#endif
}
