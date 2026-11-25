#include "Backend/Project_JHttpHandoverTransport.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJHandoverHttp, Log, All);

namespace
{
// Self-contained FIPS 180-4 SHA-256 (cross-platform Windows/Linux, zero external SSL linkage).
constexpr uint32 Sha256K[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
	0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
	0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
	0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
	0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
	0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
	0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

FORCEINLINE uint32 RotR32(uint32 V, uint32 Bits) { return (V >> Bits) | (V << (32u - Bits)); }

void ComputeSha256(const uint8* Data, int32 Len, uint8 OutDigest[32])
{
	uint32 H[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
	};

	const uint64 BitLen = static_cast<uint64>(FMath::Max(0, Len)) * 8ULL;
	const int32 SafeLen = FMath::Max(0, Len);
	const int32 PadZeroCount = ((56 - ((SafeLen + 1) % 64)) + 64) % 64;
	TArray<uint8> Padded;
	Padded.Reserve(SafeLen + 1 + PadZeroCount + 8);
	if (SafeLen > 0) Padded.Append(Data, SafeLen);
	Padded.Add(0x80u);
	Padded.AddZeroed(PadZeroCount);
	for (int32 i = 7; i >= 0; --i)
	{
		Padded.Add(static_cast<uint8>((BitLen >> (i * 8)) & 0xFFu));
	}

	uint32 W[64];
	for (int32 Offset = 0; Offset < Padded.Num(); Offset += 64)
	{
		const uint8* Block = Padded.GetData() + Offset;
		for (int32 i = 0; i < 16; ++i)
		{
			W[i] = (static_cast<uint32>(Block[i * 4]) << 24)
				| (static_cast<uint32>(Block[i * 4 + 1]) << 16)
				| (static_cast<uint32>(Block[i * 4 + 2]) << 8)
				| static_cast<uint32>(Block[i * 4 + 3]);
		}
		for (int32 i = 16; i < 64; ++i)
		{
			const uint32 S0 = RotR32(W[i - 15], 7) ^ RotR32(W[i - 15], 18) ^ (W[i - 15] >> 3);
			const uint32 S1 = RotR32(W[i - 2], 17) ^ RotR32(W[i - 2], 19) ^ (W[i - 2] >> 10);
			W[i] = W[i - 16] + S0 + W[i - 7] + S1;
		}
		uint32 a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
		for (int32 i = 0; i < 64; ++i)
		{
			const uint32 S1 = RotR32(e, 6) ^ RotR32(e, 11) ^ RotR32(e, 25);
			const uint32 Ch = (e & f) ^ ((~e) & g);
			const uint32 Temp1 = h + S1 + Ch + Sha256K[i] + W[i];
			const uint32 S0 = RotR32(a, 2) ^ RotR32(a, 13) ^ RotR32(a, 22);
			const uint32 Maj = (a & b) ^ (a & c) ^ (b & c);
			const uint32 Temp2 = S0 + Maj;
			h = g; g = f; f = e; e = d + Temp1;
			d = c; c = b; b = a; a = Temp1 + Temp2;
		}
		H[0] += a; H[1] += b; H[2] += c; H[3] += d;
		H[4] += e; H[5] += f; H[6] += g; H[7] += h;
	}

	for (int32 i = 0; i < 8; ++i)
	{
		OutDigest[i * 4] = static_cast<uint8>((H[i] >> 24) & 0xFFu);
		OutDigest[i * 4 + 1] = static_cast<uint8>((H[i] >> 16) & 0xFFu);
		OutDigest[i * 4 + 2] = static_cast<uint8>((H[i] >> 8) & 0xFFu);
		OutDigest[i * 4 + 3] = static_cast<uint8>(H[i] & 0xFFu);
	}
}

FString BytesToLowerHex(const uint8* Bytes, int32 Count)
{
	static constexpr TCHAR HexChars[] = TEXT("0123456789abcdef");
	FString Out;
	Out.Reserve(Count * 2);
	for (int32 i = 0; i < Count; ++i)
	{
		Out.AppendChar(HexChars[(Bytes[i] >> 4) & 0xF]);
		Out.AppendChar(HexChars[Bytes[i] & 0xF]);
	}
	return Out;
}
}

FProject_JHttpHandoverTransport::FProject_JHttpHandoverTransport(FString InSharedSecret, float InRequestTimeoutSeconds)
	: SharedSecret(MoveTemp(InSharedSecret))
	, RequestTimeoutSeconds(FMath::Clamp(InRequestTimeoutSeconds, 0.5f, 30.0f))
{
}

FProject_JHttpHandoverTransport::~FProject_JHttpHandoverTransport()
{
	for (auto& Pair : ActiveRequests)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->OnProcessRequestComplete().Unbind();
			Pair.Value->CancelRequest();
		}
	}
	ActiveRequests.Reset();
}

bool FProject_JHttpHandoverTransport::IsAllowedEndpointUrl(const FString& Url)
{
	if (Url.IsEmpty() || Url.Len() > 2048) return false;
	for (TCHAR Ch : Url)
	{
		if (Ch <= 0x20 || Ch >= 0x7f || Ch == '\\' || Ch == '?' || Ch == '#' || Ch == '@') return false;
	}
	const bool bTls = Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	if (!bTls && !Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)) return false;
	const FString Rest = Url.Mid(bTls ? 8 : 7);
	int32 Slash = INDEX_NONE;
	const FString Authority = Rest.FindChar('/', Slash) ? Rest.Left(Slash) : Rest;
	FString Host = Authority;
	FString PortText;
	if (Authority.Split(TEXT(":"), &Host, &PortText))
	{
		int32 Port = 0;
		if (PortText.IsEmpty() || !PortText.IsNumeric() || !LexTryParseString(Port, *PortText) || Port < 1 || Port > 65535)
			return false;
	}
	else if (!bTls) return false;
	if (!bTls) return Host == TEXT("127.0.0.1");
	TArray<FString> Labels;
	Host.ParseIntoArray(Labels, TEXT("."), false);
	if (Labels.IsEmpty() || Host.Len() > 253) return false;
	for (const auto& Label : Labels)
	{
		if (Label.IsEmpty() || Label.Len() > 63 || Label.StartsWith(TEXT("-")) || Label.EndsWith(TEXT("-"))) return false;
		for (TCHAR Ch : Label) if (!FChar::IsAlnum(Ch) && Ch != '-') return false;
	}
	return true;
}

bool FProject_JHttpHandoverTransport::RegisterNodeEndpoint(const FString& NodeId, const FString& EndpointUrl)
{
	if (NodeId.IsEmpty() || !IsAllowedEndpointUrl(EndpointUrl))
	{
		return false;
	}
	NodeEndpoints.Add(NodeId, EndpointUrl);
	return true;
}

void FProject_JHttpHandoverTransport::UnregisterNodeEndpoint(const FString& NodeId)
{
	NodeEndpoints.Remove(NodeId);
}

FString FProject_JHttpHandoverTransport::ComputeHmacSha256Hex(const FString& Secret, const TArray<uint8>& MessageBytes)
{
	constexpr int32 BlockSize = 64;
	const FTCHARToUTF8 KeyUtf8(*Secret);
	uint8 KeyBlock[BlockSize];
	FMemory::Memzero(KeyBlock, BlockSize);
	if (KeyUtf8.Length() > BlockSize)
	{
		ComputeSha256(reinterpret_cast<const uint8*>(KeyUtf8.Get()), KeyUtf8.Length(), KeyBlock);
	}
	else if (KeyUtf8.Length() > 0)
	{
		FMemory::Memcpy(KeyBlock, KeyUtf8.Get(), KeyUtf8.Length());
	}

	uint8 InnerPad[BlockSize];
	uint8 OuterPad[BlockSize];
	for (int32 i = 0; i < BlockSize; ++i)
	{
		InnerPad[i] = KeyBlock[i] ^ 0x36;
		OuterPad[i] = KeyBlock[i] ^ 0x5c;
	}

	TArray<uint8> InnerInput;
	InnerInput.Reserve(BlockSize + MessageBytes.Num());
	InnerInput.Append(InnerPad, BlockSize);
	InnerInput.Append(MessageBytes);
	uint8 InnerHash[32];
	ComputeSha256(InnerInput.GetData(), InnerInput.Num(), InnerHash);

	uint8 OuterInput[BlockSize + 32];
	FMemory::Memcpy(OuterInput, OuterPad, BlockSize);
	FMemory::Memcpy(OuterInput + BlockSize, InnerHash, 32);
	uint8 FinalHash[32];
	ComputeSha256(OuterInput, sizeof(OuterInput), FinalHash);

	return BytesToLowerHex(FinalHash, 32);
}

bool FProject_JHttpHandoverTransport::ConstantTimeEquals(const FString& A, const FString& B)
{
	if (A.Len() != B.Len()) return false;
	uint32 Diff = 0;
	for (int32 i = 0; i < A.Len(); ++i)
	{
		Diff |= static_cast<uint32>(A[i] ^ B[i]);
	}
	return Diff == 0;
}

bool FProject_JHttpHandoverTransport::SerializeEnvelopeJson(const FProject_JHandoverEnvelope& Envelope, TArray<uint8>& OutUtf8Json)
{
	OutUtf8Json.Reset();
	auto Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("version"), Envelope.Version);
	Obj->SetStringField(TEXT("transfer_id"), Envelope.TransferId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Obj->SetStringField(TEXT("actor_class"), Envelope.ActorClassPath);
	Obj->SetStringField(TEXT("payload_b64"), FBase64::Encode(Envelope.Payload));
	Obj->SetStringField(TEXT("timestamp_ticks"), LexToString(Envelope.Timestamp.GetTicks()));
	Obj->SetStringField(TEXT("source_node"), Envelope.SourceNodeId);
	Obj->SetStringField(TEXT("target_node"), Envelope.TargetNodeId);
	Obj->SetNumberField(TEXT("checksum"), Envelope.PayloadChecksum);

	FString Text;
	if (!FJsonSerializer::Serialize(Obj, TJsonWriterFactory<>::Create(&Text)))
	{
		return false;
	}
	const FTCHARToUTF8 Utf8(*Text);
	OutUtf8Json.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return true;
}

bool FProject_JHttpHandoverTransport::VerifyAndParseSignedEnvelope(
	const FString& SharedSecret,
	const FString& SignatureHex,
	int64 TimestampUnixSeconds,
	const TArray<uint8>& BodyUtf8,
	int64 NowUnixSeconds,
	FProject_JHandoverEnvelope& OutEnvelope,
	FString& OutFailureReason)
{
	if (SharedSecret.Len() < 16)
	{
		OutFailureReason = TEXT("Handover shared secret not configured");
		return false;
	}
	const int64 MaxUnix = FDateTime::MaxValue().ToUnixTimestamp();
	if (NowUnixSeconds < 0 || NowUnixSeconds > MaxUnix || TimestampUnixSeconds < 0 || TimestampUnixSeconds > MaxUnix
		|| FMath::Abs(NowUnixSeconds - TimestampUnixSeconds) > MaxTimestampSkewSeconds)
	{
		OutFailureReason = TEXT("Handover timestamp outside allowed skew window");
		return false;
	}

	if (BodyUtf8.IsEmpty() || BodyUtf8.Num() > 1500000 || SignatureHex.Len() != 64)
	{
		OutFailureReason = TEXT("Invalid handover wire size");
		return false;
	}
	const FString Prefix = FString::Printf(TEXT("%lld."), TimestampUnixSeconds);
	const FTCHARToUTF8 PrefixUtf8(*Prefix);
	TArray<uint8> SignedInput;
	SignedInput.Reserve(PrefixUtf8.Length() + BodyUtf8.Num());
	SignedInput.Append(reinterpret_cast<const uint8*>(PrefixUtf8.Get()), PrefixUtf8.Length());
	SignedInput.Append(BodyUtf8);

	const FString ExpectedSig = ComputeHmacSha256Hex(SharedSecret, SignedInput);
	if (!ConstantTimeEquals(ExpectedSig, SignatureHex.ToLower()))
	{
		OutFailureReason = TEXT("Handover HMAC signature mismatch");
		return false;
	}

	const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(BodyUtf8.GetData()), BodyUtf8.Num());
	TSharedPtr<FJsonObject> Obj;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Obj) || !Obj.IsValid())
	{
		OutFailureReason = TEXT("Invalid handover JSON");
		return false;
	}

	FString TransferIdStr, PayloadB64, TimestampText;
	int64 TimestampTicks = 0;
	FProject_JHandoverEnvelope Parsed;
	if (!Obj->TryGetNumberField(TEXT("version"), Parsed.Version)
		|| !Obj->TryGetStringField(TEXT("transfer_id"), TransferIdStr) || !FGuid::Parse(TransferIdStr, Parsed.TransferId)
		|| !Obj->TryGetStringField(TEXT("actor_class"), Parsed.ActorClassPath)
		|| !Obj->TryGetStringField(TEXT("payload_b64"), PayloadB64) || !FBase64::Decode(PayloadB64, Parsed.Payload)
		|| !Obj->TryGetStringField(TEXT("timestamp_ticks"), TimestampText) || !LexTryParseString(TimestampTicks, *TimestampText)
		|| TimestampText != LexToString(TimestampTicks) || TimestampTicks < FDateTime::MinValue().GetTicks()
		|| TimestampTicks > FDateTime::MaxValue().GetTicks() || !Parsed.TransferId.IsValid() || Parsed.Payload.Num() > 1048576
		|| !Obj->TryGetStringField(TEXT("source_node"), Parsed.SourceNodeId)
		|| !Obj->TryGetStringField(TEXT("target_node"), Parsed.TargetNodeId)
		|| !Obj->TryGetNumberField(TEXT("checksum"), Parsed.PayloadChecksum))
	{
		OutFailureReason = TEXT("Missing required handover envelope field");
		return false;
	}
	Parsed.Timestamp = FDateTime(TimestampTicks);
	OutEnvelope = MoveTemp(Parsed);
	return true;
}

bool FProject_JHttpHandoverTransport::Send(
	const FProject_JHandoverTransportRequest& Request,
	FProject_JHandoverTransportCallback Completion)
{
	check(IsInGameThread());
	if (SharedSecret.Len() < 16 || !Completion || !Request.Envelope.TransferId.IsValid()
		|| Request.Envelope.Payload.Num() > 1048576 || ActiveRequests.Contains(Request.Envelope.TransferId))
	{
		return false;
	}
	const FString* Endpoint = NodeEndpoints.Find(Request.Envelope.TargetNodeId);
	if (!Endpoint || Endpoint->IsEmpty())
	{
		return false;
	}

	TArray<uint8> BodyUtf8;
	if (!SerializeEnvelopeJson(Request.Envelope, BodyUtf8))
	{
		return false;
	}

	const int64 NowUnix = FDateTime::UtcNow().ToUnixTimestamp();
	const FString Prefix = FString::Printf(TEXT("%lld."), NowUnix);
	const FTCHARToUTF8 PrefixUtf8(*Prefix);
	TArray<uint8> SignedInput;
	SignedInput.Reserve(PrefixUtf8.Length() + BodyUtf8.Num());
	SignedInput.Append(reinterpret_cast<const uint8*>(PrefixUtf8.Get()), PrefixUtf8.Length());
	SignedInput.Append(BodyUtf8);
	const FString SignatureHex = ComputeHmacSha256Hex(SharedSecret, SignedInput);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpReq = FHttpModule::Get().CreateRequest();
	HttpReq->SetURL(*Endpoint);
	HttpReq->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	HttpReq->SetVerb(TEXT("POST"));
	HttpReq->SetTimeout(RequestTimeoutSeconds);
	HttpReq->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpReq->SetHeader(TEXT("X-ProjectJ-Timestamp"), LexToString(NowUnix));
	HttpReq->SetHeader(TEXT("X-ProjectJ-Signature"), SignatureHex);
	HttpReq->SetContent(BodyUtf8);

	const FGuid TransferId = Request.Envelope.TransferId;
	const int32 Attempt = Request.Attempt;
	TWeakPtr<FProject_JHttpHandoverTransport> WeakSelf = AsShared();
	HttpReq->OnProcessRequestComplete().BindLambda(
		[WeakSelf, TransferId, Attempt, RegisteredUrl = *Endpoint, Callback = MoveTemp(Completion)]
		(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bConnected)
		{
			const auto Self = WeakSelf.Pin();
			if (!Self || Self->ActiveRequests.FindRef(TransferId) != Completed) return;
			Self->ActiveRequests.Remove(TransferId);
			FProject_JHandoverTransportResponse Out;
			Out.TransferId = TransferId;
			Out.Attempt = Attempt;
			if (!bConnected || !Response.IsValid())
			{
				Out.Outcome = EProject_JHandoverTransportOutcome::TransportError;
				Out.Message = TEXT("Handover HTTP transport unreachable");
			}
			else if (Response->GetEffectiveURL() != RegisteredUrl)
			{
				Out.Outcome = EProject_JHandoverTransportOutcome::Rejected;
				Out.Message = TEXT("Handover endpoint changed");
			}
			else if (Response->GetResponseCode() == 200)
			{
				Out.Outcome = EProject_JHandoverTransportOutcome::Accepted;
				Out.Message = TEXT("Accepted");
			}
			else if (Response->GetResponseCode() >= 400 && Response->GetResponseCode() < 500)
			{
				Out.Outcome = EProject_JHandoverTransportOutcome::Rejected;
				Out.Message = FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode());
			}
			else
			{
				Out.Outcome = EProject_JHandoverTransportOutcome::TransportError;
				Out.Message = FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode());
			}
			Callback(Out);
		});

	ActiveRequests.Add(TransferId, HttpReq);
	if (!HttpReq->ProcessRequest())
	{
		ActiveRequests.Remove(TransferId);
		return false;
	}
	return true;
}

void FProject_JHttpHandoverTransport::Cancel(const FGuid& TransferId)
{
	if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Req; ActiveRequests.RemoveAndCopyValue(TransferId, Req))
	{
		if (Req.IsValid())
		{
			Req->OnProcessRequestComplete().Unbind();
			Req->CancelRequest();
		}
	}
}

void FProject_JHttpHandoverTransport::Tick(float)
{
}
