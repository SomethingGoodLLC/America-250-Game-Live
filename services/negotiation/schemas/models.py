"""Pydantic models generated from JSON schemas."""

from datetime import datetime
from typing import Any, Dict, List, Union, Optional, Literal
from pydantic import BaseModel, Field


class ErrorModel(BaseModel):
    code: str = Field(..., description="Error code")
    message: str = Field(..., description="Error message")
    details: Optional[Dict[str, Any]] = Field(default=None, description="Additional error details")


class SpeakerTurnModel(BaseModel):
    speaker_id: str = Field(..., description="Unique identifier for the speaker")
    text: str = Field(..., description="The spoken text")
    timestamp: datetime = Field(..., description="When this turn occurred")
    confidence: Optional[float] = Field(default=None, description="STT confidence score", ge=0, le=1)
    sentiment: Optional[Dict[str, Any]] = Field(default=None, description="Sentiment analysis")


class WorldContextModel(BaseModel):
    scenario_tags: List[str] = Field(..., description="Tags describing the scenario context")
    initiator_faction: Dict[str, Any] = Field(..., description="Initiator faction information")
    counterpart_faction: Dict[str, Any] = Field(..., description="Counterpart faction information")
    current_state: Optional[Dict[str, Any]] = Field(default=None, description="Current world state")


class ContentSafetyModel(BaseModel):
    is_safe: bool = Field(..., description="Whether the content is safe")
    flags: Optional[List[str]] = Field(default=None, description="Safety flags")
    severity: Optional[str] = Field(default=None, description="Severity level")
    reason: Optional[str] = Field(default=None, description="Reason for safety decision")


class ProposalModel(BaseModel):
    type: Literal["proposal"] = Field(..., description="Message type")
    speaker_id: str = Field(..., description="Speaker identifier")
    content: str = Field(..., description="The proposal content")
    intent_type: str = Field(..., description="Type of intent")
    terms: Dict[str, Any] = Field(..., description="Proposal terms")
    confidence: Optional[float] = Field(default=None, description="Confidence score", ge=0, le=1)
    timestamp: datetime = Field(..., description="When the proposal was made")


class ConcessionModel(BaseModel):
    type: Literal["concession"] = Field(..., description="Message type")
    speaker_id: str = Field(..., description="Speaker identifier")
    content: str = Field(..., description="The concession content")
    concession_type: str = Field(..., description="Type of concession")
    value: Optional[float] = Field(default=None, description="Relative value of the concession", ge=0, le=100)
    timestamp: datetime = Field(..., description="When the concession was made")


class CounterOfferModel(BaseModel):
    type: Literal["counter_offer"] = Field(..., description="Message type")
    speaker_id: str = Field(..., description="Speaker identifier")
    content: str = Field(..., description="The counter offer content")
    original_proposal_id: str = Field(..., description="Reference to the original proposal")
    counter_terms: Dict[str, Any] = Field(..., description="Counter offer terms")
    confidence: Optional[float] = Field(default=None, description="Confidence score", ge=0, le=1)
    timestamp: datetime = Field(..., description="When the counter offer was made")


class UltimatumModel(BaseModel):
    type: Literal["ultimatum"] = Field(..., description="Message type")
    speaker_id: str = Field(..., description="Speaker identifier")
    content: str = Field(..., description="The ultimatum content")
    deadline: datetime = Field(..., description="When the ultimatum expires")
    consequences: List[str] = Field(..., description="Consequences of not accepting")
    timestamp: datetime = Field(..., description="When the ultimatum was issued")


class SmallTalkModel(BaseModel):
    type: Literal["small_talk"] = Field(..., description="Message type")
    speaker_id: str = Field(..., description="Speaker identifier")
    content: str = Field(..., description="The small talk content")
    topic: Optional[str] = Field(default=None, description="Topic of small talk")
    timestamp: datetime = Field(..., description="When the small talk occurred")


class NegotiationReportModel(BaseModel):
    session_id: str = Field(..., description="Unique session identifier")
    start_time: datetime = Field(..., description="Session start time")
    end_time: datetime = Field(..., description="Session end time")
    initiator_faction: Dict[str, Any] = Field(..., description="Initiator faction info")
    counterpart_faction: Dict[str, Any] = Field(..., description="Counterpart faction info")
    transcript: List[SpeakerTurnModel] = Field(..., description="Full transcript of the negotiation")
    intents: List[Union[ProposalModel, ConcessionModel, CounterOfferModel, UltimatumModel]] = Field(..., description="Detected intents")
    justifications: List[Dict[str, Any]] = Field(..., description="Justifications for intents")
    outcome: Dict[str, Any] = Field(..., description="Negotiation outcome")
    content_safety_report: Optional[ContentSafetyModel] = Field(default=None, description="Content safety report")


# Union types for API requests/responses
IntentModel = Union[ProposalModel, ConcessionModel, CounterOfferModel, UltimatumModel, SmallTalkModel]
