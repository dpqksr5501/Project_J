#pragma once
#include "CoreMinimal.h"
#include "IncludeSQLite.h"
#include "SQLiteDatabase.h"

/** Narrow SQLiteCore C API owner. Selects our VFS per connection; never changes
 * the engine's default VFS. Same SQLite file format and library, no runtime DLL. */
class FProject_JProfileStatement
{
  public:
	explicit FProject_JProfileStatement(sqlite3_stmt *In = nullptr) : Handle(In)
	{
	}
	~FProject_JProfileStatement()
	{
		if (Handle)
			sqlite3_finalize(Handle);
	}
	FProject_JProfileStatement(const FProject_JProfileStatement &) = delete;
	FProject_JProfileStatement(FProject_JProfileStatement &&Other) : Handle(Other.Handle)
	{
		Other.Handle = nullptr;
	}
	bool IsValid() const
	{
		return Handle != nullptr;
	}
	bool SetBindingValueByIndex(int32 I, const FString &V)
	{
		return sqlite3_bind_text(Handle, I, TCHAR_TO_UTF8(*V), -1, SQLITE_TRANSIENT) == SQLITE_OK;
	}
	bool SetBindingValueByIndex(int32 I, const FGuid &V)
	{
		return SetBindingValueByIndex(I, &V, sizeof(V));
	}
	bool SetBindingValueByIndex(int32 I, int32 V)
	{
		return sqlite3_bind_int(Handle, I, V) == SQLITE_OK;
	}
	bool SetBindingValueByIndex(int32 I, int64 V)
	{
		return sqlite3_bind_int64(Handle, I, V) == SQLITE_OK;
	}
	bool SetBindingValueByIndex(int32 I, const void *V, int32 N)
	{
		return sqlite3_bind_blob(Handle, I, V, N, SQLITE_TRANSIENT) == SQLITE_OK;
	}
	ESQLitePreparedStatementStepResult Step()
	{
		const int R = sqlite3_step(Handle);
		return R == SQLITE_ROW	  ? ESQLitePreparedStatementStepResult::Row
			   : R == SQLITE_DONE ? ESQLitePreparedStatementStepResult::Done
								  : ESQLitePreparedStatementStepResult::Error;
	}
	bool Execute()
	{
		return Step() == ESQLitePreparedStatementStepResult::Done;
	}
	bool GetColumnValueByIndex(int32 I, int64 &V) const
	{
		if (sqlite3_column_type(Handle, I) != SQLITE_INTEGER)
			return false;
		V = sqlite3_column_int64(Handle, I);
		return true;
	}
	bool GetColumnValueByIndex(int32 I, int32 &V) const
	{
		int64 N = 0;
		if (!GetColumnValueByIndex(I, N) || N < MIN_int32 || N > MAX_int32)
			return false;
		V = int32(N);
		return true;
	}
	bool GetColumnValueByIndex(int32 I, FString &V) const
	{
		if (sqlite3_column_type(Handle, I) != SQLITE_TEXT)
			return false;
		V = UTF8_TO_TCHAR(reinterpret_cast<const char *>(sqlite3_column_text(Handle, I)));
		return true;
	}
	bool GetColumnValueByIndex(int32 I, TArray<uint8> &V) const
	{
		if (sqlite3_column_type(Handle, I) != SQLITE_BLOB)
			return false;
		V.Reset();
		const int N = sqlite3_column_bytes(Handle, I);
		if (N)
			V.Append(static_cast<const uint8 *>(sqlite3_column_blob(Handle, I)), N);
		return true;
	}

  private:
	sqlite3_stmt *Handle;
};
class FProject_JProfileDatabase
{
  public:
	~FProject_JProfileDatabase()
	{
		Close();
	}
	bool Open(const TCHAR *Filename, ESQLiteDatabaseOpenMode Mode);
	/** Module shutdown after GameInstance native jobs have drained. */
	static void ShutdownVFS();
	bool Close()
	{
		if (!Handle)
			return true;
		if (sqlite3_close(Handle) != SQLITE_OK)
			return false;
		Handle = nullptr;
		return true;
	}
	bool IsValid() const
	{
		return Handle != nullptr;
	}
	int32 GetLastResultCode() const
	{
		return Handle ? sqlite3_extended_errcode(Handle) : SQLITE_CANTOPEN;
	}
	bool Execute(const TCHAR *SQL)
	{
		return Handle && sqlite3_exec(Handle, TCHAR_TO_UTF8(SQL), nullptr, nullptr, nullptr) == SQLITE_OK;
	}
	FProject_JProfileStatement PrepareStatement(const TCHAR *SQL)
	{
		sqlite3_stmt *S = nullptr;
		if (Handle)
			sqlite3_prepare_v2(Handle, TCHAR_TO_UTF8(SQL), -1, &S, nullptr);
		return FProject_JProfileStatement(S);
	}
	bool GetApplicationId(int32 &V)
	{
		return Integer(TEXT("PRAGMA application_id"), V);
	}
	bool GetUserVersion(int32 &V)
	{
		return Integer(TEXT("PRAGMA user_version"), V);
	}
	bool SetApplicationId(int32 V)
	{
		return Execute(*FString::Printf(TEXT("PRAGMA application_id=%d"), V));
	}
	bool SetUserVersion(int32 V)
	{
		return Execute(*FString::Printf(TEXT("PRAGMA user_version=%d"), V));
	}
	bool PerformQuickIntegrityCheck()
	{
		auto S = PrepareStatement(TEXT("PRAGMA quick_check"));
		FString V;
		return S.IsValid() && S.Step() == ESQLitePreparedStatementStepResult::Row && S.GetColumnValueByIndex(0, V) &&
			   V == TEXT("ok");
	}
#if WITH_DEV_AUTOMATION_TESTS
	void FailNextWriteForTest()
	{
		sqlite3_file_control(Handle, "main", 10001, nullptr);
	}
	void CrashAfterNextSyncForTest()
	{
		sqlite3_file_control(Handle, "main", 10002, nullptr);
	}
#endif
  private:
	bool Integer(const TCHAR *SQL, int32 &V)
	{
		auto S = PrepareStatement(SQL);
		return S.IsValid() && S.Step() == ESQLitePreparedStatementStepResult::Row && S.GetColumnValueByIndex(0, V);
	}
	sqlite3 *Handle = nullptr;
};
