#!/usr/bin/env python3
"""Generate pydantic models from JSON schemas."""

import json
import os
from pathlib import Path
from typing import Any, Dict

import pydantic
from pydantic import BaseModel, Field


def load_schema(schema_name: str) -> Dict[str, Any]:
    """Load a JSON schema from the protocol/schemas directory."""
    schema_path = Path(__file__).parent.parent.parent.parent / "protocol" / "schemas" / f"{schema_name}.json"
    with open(schema_path, 'r') as f:
        return json.load(f)


def create_pydantic_model_from_schema(schema_name: str) -> str:
    """Create a pydantic model from a JSON schema."""
    schema = load_schema(schema_name)

    class_name = "".join(word.capitalize() for word in schema_name.split('_')) + "Model"

    # Create field definitions
    fields = {}
    required_fields = set(schema.get('required', []))

    for field_name, field_schema in schema.get('properties', {}).items():
        field_type = get_pydantic_type(field_schema)
        default = ... if field_name in required_fields else None
        description = field_schema.get('description', '')

        if description:
            fields[field_name] = Field(default=default, description=description)
        else:
            fields[field_name] = Field(default=default)

    # Create the model class
    model_code = f"""
class {class_name}(BaseModel):
    {chr(10).join(f'    {field}: {field_type}' for field, field_type in fields.items())}
"""

    return model_code.strip()


def get_pydantic_type(schema: Dict[str, Any]) -> str:
    """Convert JSON schema type to pydantic type."""
    schema_type = schema.get('type', 'object')

    if schema_type == 'string':
        if schema.get('format') == 'date-time':
            return 'datetime'
        return 'str'
    elif schema_type == 'number':
        return 'float'
    elif schema_type == 'integer':
        return 'int'
    elif schema_type == 'boolean':
        return 'bool'
    elif schema_type == 'array':
        items_schema = schema.get('items', {})
        if 'oneOf' in items_schema:
            # Union type
            union_types = []
            for ref in items_schema['oneOf']:
                if '$ref' in ref:
                    ref_name = ref['$ref'].split('/')[-1].replace('.json', '')
                    union_types.append("".join(word.capitalize() for word in ref_name.split('_')) + "Model")
            return f'Union[{", ".join(union_types)}]'
        else:
            item_type = get_pydantic_type(items_schema)
            return f'List[{item_type}]'
    elif schema_type == 'object':
        if 'additionalProperties' in schema:
            return 'Dict[str, Any]'
        return 'Dict[str, Any]'

    return 'Any'


def main():
    """Generate all pydantic models from schemas."""
    schema_dir = Path(__file__).parent.parent.parent.parent / "protocol" / "schemas"
    output_file = Path(__file__).parent / "models.py"

    schema_files = [
        'error',
        'speaker_turn',
        'world_context',
        'content_safety',
        'proposal',
        'concession',
        'counter_offer',
        'ultimatum',
        'small_talk',
        'negotiation_report'
    ]

    imports = [
        "from datetime import datetime",
        "from typing import Any, Dict, List, Union, Optional",
        "from pydantic import BaseModel, Field",
        "",
    ]

    model_code = []

    for schema_name in schema_files:
        model_code.append(create_pydantic_model_from_schema(schema_name))
        model_code.append("")

    with open(output_file, 'w') as f:
        f.write("\n".join(imports))
        f.write("\n".join(model_code))

    print(f"Generated pydantic models in {output_file}")


if __name__ == "__main__":
    main()
