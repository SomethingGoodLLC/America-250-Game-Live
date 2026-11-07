"""YAML utilities for schema validation and serialization."""

import io
from pathlib import Path
from typing import Any

from ruamel.yaml import YAML


class YamlHelper:
    """YAML helper for loading, dumping, and validating YAML data."""

    def __init__(self):
        self.yaml = YAML()
        self.yaml.preserve_quotes = True
        self.yaml.width = 4096  # Prevent line wrapping

    def encode(self, data: dict[str, Any]) -> str:
        """
        Encode dictionary to YAML string.

        Args:
            data: Dictionary to encode

        Returns:
            str: YAML string
        """
        stream = io.StringIO()
        self.yaml.dump(data, stream)
        return stream.getvalue()

    def decode(self, yaml_string: str) -> dict[str, Any]:
        """
        Decode YAML string to dictionary.

        Args:
            yaml_string: YAML string to decode

        Returns:
            Dict[str, Any]: Parsed dictionary
        """
        return self.yaml.load(yaml_string)

    def load_schema(self, schema_path: str | Path) -> dict[str, Any]:
        """
        Load YAML schema from file.

        Args:
            schema_path: Path to YAML schema file

        Returns:
            Dict[str, Any]: Schema dictionary
        """
        with open(schema_path, encoding='utf-8') as f:
            return self.yaml.load(f)

    def save_schema(self, data: dict[str, Any], schema_path: str | Path) -> None:
        """
        Save dictionary as YAML schema file.

        Args:
            data: Dictionary to save
            schema_path: Path to save YAML file
        """
        with open(schema_path, 'w', encoding='utf-8') as f:
            self.yaml.dump(data, f)


# Global instance for easy access
yaml_helper = YamlHelper()
