#include "PA250DiplomacyClient.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WebSocketsModule.h"

FString UPA250DiplomacyClient::DefaultBaseUrl()
{
	FString V;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PA250Service="), V) || V.IsEmpty())
	{
		V = FPlatformMisc::GetEnvironmentVariable(TEXT("PA250_SERVICE"));
	}
	if (V.IsEmpty())
	{
		return TEXT("http://127.0.0.1:8000");
	}
	return V.StartsWith(TEXT("http")) ? V : TEXT("http://") + V;
}

void UPA250DiplomacyClient::SetState(EPA250DiplomacyState NewState)
{
	if (State != NewState)
	{
		State = NewState;
		if (OnStateChanged)
		{
			OnStateChanged(State);
		}
	}
}

void UPA250DiplomacyClient::Fail(const FString& Why)
{
	UE_LOG(LogTemp, Warning, TEXT("PA250 diplomacy client unavailable: %s"), *Why);
	bAwaitingReply = false;
	if (Socket.IsValid())
	{
		Socket->OnClosed().Clear();
		Socket->OnConnectionError().Clear();
		Socket->OnMessage().Clear();
		if (Socket->IsConnected())
		{
			Socket->Close();
		}
		Socket.Reset();
	}
	SetState(EPA250DiplomacyState::Unavailable);
}

bool UPA250DiplomacyClient::Tick(float DeltaSeconds)
{
	Clock += DeltaSeconds;
	if (State == EPA250DiplomacyState::Connecting && Clock > ConnectDeadline)
	{
		Fail(TEXT("connect timeout"));
	}
	else if (State == EPA250DiplomacyState::Talking && bAwaitingReply && Clock > ReplyDeadline)
	{
		Fail(TEXT("reply timeout"));
	}
	return true;
}

void UPA250DiplomacyClient::Start(const FString& InBaseUrl, const FString& ContextJson, const FString& EnvoyName, const FString& EnvoyTitle)
{
	BaseUrl = InBaseUrl;
	Clock = 0.f;
	ConnectDeadline = ConnectTimeoutSeconds;
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPA250DiplomacyClient::Tick));
	}
	SetState(EPA250DiplomacyState::Connecting);

	TSharedPtr<FJsonObject> Ctx;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ContextJson);
	if (!FJsonSerializer::Deserialize(Reader, Ctx) || !Ctx.IsValid())
	{
		Fail(TEXT("bad diplomacy context"));
		return;
	}
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), TEXT("game"));
	Body->SetObjectField(TEXT("diplomacy_context"), Ctx);
	TSharedRef<FJsonObject> Envoy = MakeShared<FJsonObject>();
	Envoy->SetStringField(TEXT("name"), EnvoyName);
	Envoy->SetStringField(TEXT("title"), EnvoyTitle);
	Body->SetObjectField(TEXT("envoy"), Envoy);
	FString BodyStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body, Writer);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(BaseUrl + TEXT("/v1/session"));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetContentAsString(BodyStr);
	Req->SetTimeout(ConnectTimeoutSeconds);
	TWeakObjectPtr<UPA250DiplomacyClient> Weak(this);
	Req->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk) {
		UPA250DiplomacyClient* Self = Weak.Get();
		if (!Self || Self->State != EPA250DiplomacyState::Connecting)
		{
			return;
		}
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
		{
			Self->Fail(FString::Printf(TEXT("session request failed (%d)"), Resp.IsValid() ? Resp->GetResponseCode() : 0));
			return;
		}
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Resp->GetContentAsString()), Json) || !Json.IsValid() || !Json->HasField(TEXT("session_id")))
		{
			Self->Fail(TEXT("bad session response"));
			return;
		}
		Self->Provider = Json->GetStringField(TEXT("provider"));
		Self->OpenSocket(Json->GetStringField(TEXT("session_id")));
	});
	if (!Req->ProcessRequest())
	{
		Fail(TEXT("could not start HTTP request"));
	}
}

void UPA250DiplomacyClient::OpenSocket(const FString& SessionId)
{
	FString WsBase = BaseUrl;
	WsBase.ReplaceInline(TEXT("https://"), TEXT("wss://"));
	WsBase.ReplaceInline(TEXT("http://"), TEXT("ws://"));
	const FString Url = FString::Printf(TEXT("%s/v1/session/%s/control?format=json"), *WsBase, *SessionId);
	Socket = FWebSocketsModule::Get().CreateWebSocket(Url, FString());

	TWeakObjectPtr<UPA250DiplomacyClient> Weak(this);
	Socket->OnConnected().AddLambda([Weak]() {
		if (UPA250DiplomacyClient* Self = Weak.Get())
		{
			Self->SetState(EPA250DiplomacyState::Talking);
		}
	});
	Socket->OnConnectionError().AddLambda([Weak](const FString& Error) {
		if (UPA250DiplomacyClient* Self = Weak.Get())
		{
			Self->Fail(TEXT("socket error: ") + Error);
		}
	});
	Socket->OnClosed().AddLambda([Weak](int32 Code, const FString& Reason, bool bClean) {
		if (UPA250DiplomacyClient* Self = Weak.Get())
		{
			if (Self->State != EPA250DiplomacyState::Idle && Self->State != EPA250DiplomacyState::Unavailable)
			{
				Self->Fail(TEXT("socket closed"));
			}
		}
	});
	Socket->OnMessage().AddLambda([Weak](const FString& Msg) {
		UPA250DiplomacyClient* Self = Weak.Get();
		if (!Self)
		{
			return;
		}
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Msg), Json) || !Json.IsValid())
		{
			return;
		}
		FString Type;
		Json->TryGetStringField(TEXT("type"), Type);
		if (Type == TEXT("diplomacy_proposal") || Type == TEXT("representative_unavailable"))
		{
			Self->bAwaitingReply = false;
		}
		if (Type == TEXT("representative_unavailable"))
		{
			Self->Fail(TEXT("service reported the envoy unavailable"));
		}
		if (Self->OnMessage)
		{
			Self->OnMessage(Json);
		}
	});
	Socket->Connect();
}

void UPA250DiplomacyClient::SendUtterance(const FString& Text)
{
	if (State != EPA250DiplomacyState::Talking || !Socket.IsValid() || !Socket->IsConnected())
	{
		return;
	}
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("type"), TEXT("player_utterance"));
	Obj->SetStringField(TEXT("text"), Text);
	FString Out;
	FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
	Socket->Send(Out);
	bAwaitingReply = true;
	ReplyDeadline = Clock + ReplyTimeoutSeconds;
}

void UPA250DiplomacyClient::Shutdown()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (Socket.IsValid())
	{
		Socket->OnClosed().Clear();
		Socket->OnConnectionError().Clear();
		Socket->OnMessage().Clear();
		Socket->OnConnected().Clear();
		if (Socket->IsConnected())
		{
			Socket->Close();
		}
		Socket.Reset();
	}
	State = EPA250DiplomacyState::Idle;
}

void UPA250DiplomacyClient::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}
