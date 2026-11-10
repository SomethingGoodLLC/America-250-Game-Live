// PA250DiplomacyClient.h — HTTP + WebSocket client for the negotiation service (main:app, JSON control socket).
// The service only proposes; this client never mutates campaign state.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UObject/Object.h"
#include "PA250DiplomacyClient.generated.h"

class IWebSocket;
class FJsonObject;

UENUM()
enum class EPA250DiplomacyState : uint8
{
	Idle,
	Connecting,
	Talking,
	Unavailable
};

UCLASS()
class PLAYAMERICA250_API UPA250DiplomacyClient : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void(EPA250DiplomacyState)> OnStateChanged;
	/** Every control message from the service as parsed JSON (type, text, clauses, ...). */
	TFunction<void(const TSharedPtr<FJsonObject>&)> OnMessage;

	/** POST /v1/session with the sim's DiplomacyContext, then open the control socket (?format=json). */
	void Start(const FString& BaseUrl, const FString& ContextJson, const FString& EnvoyName, const FString& EnvoyTitle);
	void SendUtterance(const FString& Text);
	void Shutdown();

	EPA250DiplomacyState GetState() const { return State; }
	FString GetProvider() const { return Provider; }

	/** -PA250Service=host:port, env PA250_SERVICE, else http://127.0.0.1:8000 */
	static FString DefaultBaseUrl();
	FString GetBaseUrl() const { return BaseUrl; }

	float ConnectTimeoutSeconds = 8.f;
	float ReplyTimeoutSeconds = 40.f;

	virtual void BeginDestroy() override;

private:
	void SetState(EPA250DiplomacyState NewState);
	void OpenSocket(const FString& SessionId);
	void Fail(const FString& Why);
	bool Tick(float DeltaSeconds);

	EPA250DiplomacyState State = EPA250DiplomacyState::Idle;
	FString BaseUrl;
	FString Provider;
	TSharedPtr<IWebSocket> Socket;
	FTSTicker::FDelegateHandle TickHandle;
	float ConnectDeadline = 0.f;
	float ReplyDeadline = 0.f;
	float Clock = 0.f;
	bool bAwaitingReply = false;
};
