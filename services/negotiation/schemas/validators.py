"""YAML schema validation for negotiation protocols."""

import yaml
from typing import Dict, Any, Union, Optional
from pathlib import Path
import json
import structlog
from jsonschema import validate, ValidationError, Draft7Validator
from ruamel.yaml import YAML

from .models import (
    IntentModel, SpeakerTurnModel, WorldContextModel,
    ProposalModel, ConcessionModel, CounterOfferModel,
    UltimatumModel, SmallTalkModel, ContentSafetyModel
)


class SchemaValidator:
    """Validates objects against YAML schemas.

    This class loads YAML schemas from the protocol/schemas directory
    and provides validation methods for different types of negotiation data.
    """

    def __init__(self, schema_dir: Optional[Path] = None):
        self.logger = structlog.get_logger(__name__)
        self.ruamel_yaml = YAML(typ='safe')

        # Default schema directory
        if schema_dir is None:
            schema_dir = Path(__file__).parent.parent.parent / "protocol" / "schemas"

        self.schema_dir = Path(schema_dir)
        self._schemas = {}
        self._validators = {}

        # Load all schemas
        self._load_schemas()

    def _load_schemas(self):
        """Load all YAML schemas from the schema directory."""
        if not self.schema_dir.exists():
            self.logger.error("Schema directory does not exist", path=str(self.schema_dir))
            return

        for schema_file in self.schema_dir.glob("*.yaml"):
            try:
                with open(schema_file, 'r', encoding='utf-8') as f:
                    schema_data = self.ruamel_yaml.load(f)

                schema_name = schema_file.stem
                self._schemas[schema_name] = schema_data
                self._validators[schema_name] = Draft7Validator(schema_data)

                self.logger.info("Loaded schema", name=schema_name)

            except Exception as e:
                self.logger.error(
                    "Failed to load schema",
                    schema_file=str(schema_file),
                    error=str(e)
                )

    def validate_or_raise(
        self,
        obj: Any,
        schema_name: str,
        version: str = "v1"
    ) -> Dict[str, Any]:
        """Validate an object against a schema and raise on failure.

        Args:
            obj: The object to validate
            schema_name: Name of the schema (e.g., "proposal", "intent")
            version: Schema version

        Returns:
            Validated object as dictionary

        Raises:
            ValidationError: If validation fails
            KeyError: If schema not found
        """
        # Try with version first, then without
        full_schema_name = f"{schema_name}.{version}"
        if full_schema_name not in self._schemas:
            # Try without version
            if schema_name in self._schemas:
                full_schema_name = schema_name
            else:
                available = list(self._schemas.keys())
                raise KeyError(f"Schema '{full_schema_name}' or '{schema_name}' not found. Available: {available}")

        schema = self._schemas[full_schema_name]
        validator = self._validators[full_schema_name]

        # Convert to dict if it's a Pydantic model
        if hasattr(obj, 'model_dump'):
            obj_dict = obj.model_dump()
        else:
            obj_dict = obj

        # Validate
        errors = list(validator.iter_errors(obj_dict))
        if errors:
            error_messages = [str(error.message) for error in errors]
            raise ValidationError(f"Schema validation failed: {'; '.join(error_messages)}")

        return obj_dict

    def validate_intent(
        self,
        intent: Union[IntentModel, Dict[str, Any]]
    ) -> Dict[str, Any]:
        """Validate a diplomatic intent against the appropriate schema.

        Args:
            intent: Intent object or dictionary

        Returns:
            Validated intent dictionary

        Raises:
            ValidationError: If validation fails
        """
        # Determine intent type for schema selection
        if isinstance(intent, dict):
            intent_type = intent.get('type', 'unknown')
        else:
            intent_type = getattr(intent, 'type', 'unknown')

        # Map intent types to schema names
        schema_map = {
            'proposal': 'proposal',
            'concession': 'concession',
            'counter_offer': 'counter_offer',
            'ultimatum': 'ultimatum',
            'small_talk': 'small_talk'
        }

        schema_name = schema_map.get(intent_type, 'intent')
        return self.validate_or_raise(intent, schema_name)

    def validate_speaker_turn(
        self,
        turn: Union[SpeakerTurnModel, Dict[str, Any]]
    ) -> Dict[str, Any]:
        """Validate a speaker turn against the schema.

        Args:
            turn: Speaker turn object or dictionary

        Returns:
            Validated turn dictionary

        Raises:
            ValidationError: If validation fails
        """
        return self.validate_or_raise(turn, "speaker_turn")

    def validate_world_context(
        self,
        context: Union[WorldContextModel, Dict[str, Any]]
    ) -> Dict[str, Any]:
        """Validate world context against the schema.

        Args:
            context: World context object or dictionary

        Returns:
            Validated context dictionary

        Raises:
            ValidationError: If validation fails
        """
        return self.validate_or_raise(context, "world_context")

    def validate_content_safety(
        self,
        safety_report: Union[ContentSafetyModel, Dict[str, Any]]
    ) -> Dict[str, Any]:
        """Validate content safety report against the schema.

        Args:
            safety_report: Safety report object or dictionary

        Returns:
            Validated safety report dictionary

        Raises:
            ValidationError: If validation fails
        """
        return self.validate_or_raise(safety_report, "content_safety")

    def validate_with_schema(
        self,
        obj: Any,
        schema_data: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Validate an object against a custom schema.

        Args:
            obj: Object to validate
            schema_data: Schema dictionary

        Returns:
            Validated object dictionary

        Raises:
            ValidationError: If validation fails
        """
        # Convert to dict if it's a Pydantic model
        if hasattr(obj, 'model_dump'):
            obj_dict = obj.model_dump()
        else:
            obj_dict = obj

        validate(obj_dict, schema_data)
        return obj_dict

    def is_valid(
        self,
        obj: Any,
        schema_name: str,
        version: str = "v1"
    ) -> bool:
        """Check if an object is valid against a schema without raising.

        Args:
            obj: Object to validate
            schema_name: Name of the schema
            version: Schema version

        Returns:
            True if valid, False otherwise
        """
        try:
            self.validate_or_raise(obj, schema_name, version)
            return True
        except (ValidationError, KeyError):
            return False

    def get_schema(self, schema_name: str, version: str = "v1") -> Optional[Dict[str, Any]]:
        """Get a schema by name and version.

        Args:
            schema_name: Name of the schema
            version: Schema version

        Returns:
            Schema dictionary or None if not found
        """
        full_schema_name = f"{schema_name}.{version}"
        return self._schemas.get(full_schema_name)


class NegotiationValidator:
    """High-level validator for negotiation data structures.

    This class provides convenience methods for validating complete
    negotiation workflows and data structures.
    """

    def __init__(self, schema_dir: Optional[Path] = None):
        self.schema_validator = SchemaValidator(schema_dir)
        self.logger = structlog.get_logger(__name__)

    async def validate_negotiation_report(
        self,
        report: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Validate a complete negotiation report.

        Args:
            report: Negotiation report dictionary

        Returns:
            Validated report dictionary

        Raises:
            ValidationError: If validation fails
        """
        # Validate transcript entries
        if "transcript" in report:
            for i, turn in enumerate(report["transcript"]):
                try:
                    self.schema_validator.validate_speaker_turn(turn)
                except ValidationError as e:
                    raise ValidationError(f"Invalid speaker turn at index {i}: {str(e)}")

        # Validate intents
        if "intents" in report:
            for i, intent in enumerate(report["intents"]):
                try:
                    self.schema_validator.validate_intent(intent)
                except ValidationError as e:
                    raise ValidationError(f"Invalid intent at index {i}: {str(e)}")

        # Validate world context
        if "initiator_faction" in report and "counterpart_faction" in report:
            context = {
                "scenario_tags": report.get("scenario_tags", []),
                "initiator_faction": report["initiator_faction"],
                "counterpart_faction": report["counterpart_faction"],
                "current_state": report.get("current_state")
            }
            self.schema_validator.validate_world_context(context)

        return report

    async def validate_provider_event(
        self,
        event: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Validate a provider event structure.

        Args:
            event: Provider event dictionary

        Returns:
            Validated event dictionary

        Raises:
            ValidationError: If validation fails
        """
        # Check required fields
        required_fields = ["type", "payload", "is_final"]
        for field in required_fields:
            if field not in event:
                raise ValidationError(f"Missing required field: {field}")

        # Validate based on event type
        event_type = event["type"]
        payload = event["payload"]

        if event_type == "intent":
            # Validate intent payload
            self.schema_validator.validate_intent(payload)
        elif event_type == "safety":
            # Validate safety payload
            self.schema_validator.validate_content_safety(payload)
        elif event_type in ["subtitle", "analysis"]:
            # These can have flexible payloads, just check basic structure
            if not isinstance(payload, dict):
                raise ValidationError(f"Payload must be a dictionary for {event_type} events")

        return event


# Global validator instance
validator = SchemaValidator()
