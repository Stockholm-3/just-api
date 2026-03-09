#!/usr/bin/env python3
"""
Validate that city datasets are properly sorted for binary search.
Uses the EXACT same normalization logic as C code.
"""

import json
import sys
import unicodedata

def normalize_city_name(name):
    """
    EXACT copy of normalization from generate_city_datasets.py
    """
    if not name:
        return ""

    # Special character transliteration map (before NFD decomposition)
    transliteration_map = {
        'Đ': 'D', 'đ': 'd',  # Vietnamese D with stroke
        'Ð': 'D', 'ð': 'd',  # Icelandic eth
        'Ø': 'O', 'ø': 'o',  # Nordic O with stroke
        'Æ': 'AE', 'æ': 'ae',  # Nordic/Latin AE ligature
        'Œ': 'OE', 'œ': 'oe',  # Latin OE ligature
        'ẞ': 'SS', 'ß': 'ss',  # German sharp S
        'Þ': 'TH', 'þ': 'th',  # Icelandic thorn
        'Ł': 'L', 'ł': 'l',  # Polish L with stroke
    }

    # Step 1: Apply special transliterations
    result = []
    for char in name:
        if char in transliteration_map:
            result.append(transliteration_map[char])
        else:
            result.append(char)
    transliterated = ''.join(result)

    # Step 2: NFD decomposition (é → e + accent, ñ → n + tilde)
    nfd = unicodedata.normalize('NFD', transliterated)

    # Step 3: Remove combining marks and keep only valid characters
    normalized = []
    for char in nfd:
        # Skip combining marks (accents, diacritics)
        if unicodedata.category(char) == 'Mn':
            continue

        # Convert to lowercase
        char_lower = char.lower()
        c = ord(char_lower)

        # Keep only: a-z, 0-9, space, hyphen, underscore
        if ord('a') <= c <= ord('z'):
            normalized.append(char_lower)
        elif ord('0') <= c <= ord('9'):
            normalized.append(char_lower)
        elif char_lower in (' ', '-', '_'):
            normalized.append(char_lower)

    return ''.join(normalized)

def validate_sorting(filepath):
    """
    Validate that cities in JSON file are sorted alphabetically
    by normalized name (case-insensitive, ASCII only).

    Returns: (is_valid, error_message, stats)
    """
    print(f"\nValidating: {filepath}")

    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            data = json.load(f)
    except Exception as e:
        return False, f"Failed to load JSON: {e}", None

    if 'cities' not in data or not isinstance(data['cities'], list):
        return False, "Invalid JSON format: missing 'cities' array", None

    cities = data['cities']

    if len(cities) == 0:
        return False, "Empty cities array", None

    print(f"  Total cities: {len(cities)}")

    # Check sorting
    errors = []
    prev_normalized = None
    prev_city = None

    for i, city in enumerate(cities):
        if 'name' not in city:
            errors.append(f"  City at index {i} missing 'name' field")
            continue

        normalized = normalize_city_name(city['name'])

        if prev_normalized is not None:
            if normalized < prev_normalized:
                errors.append(
                    f"  Sorting violation at index {i}:\n"
                    f"    Previous: '{prev_city['name']}' -> '{prev_normalized}'\n"
                    f"    Current:  '{city['name']}' -> '{normalized}'\n"
                    f"    ('{normalized}' should come AFTER '{prev_normalized}')"
                )

        prev_normalized = normalized
        prev_city = city

    if errors:
        return False, "\n".join(errors), None

    # Show first and last 5 cities
    print(f"  First 5 cities:")
    for city in cities[:5]:
        normalized = normalize_city_name(city['name'])
        print(f"    {city['name']:<30} -> '{normalized}'")

    print(f"  Last 5 cities:")
    for city in cities[-5:]:
        normalized = normalize_city_name(city['name'])
        print(f"    {city['name']:<30} -> '{normalized}'")

    stats = {
        'total': len(cities),
        'first': cities[0]['name'],
        'last': cities[-1]['name']
    }

    return True, None, stats

def main():
    hot_cities_path = "/home/oleksandr/Documents/proj/just-weather/data/hot_cities.json"
    all_cities_path = "/home/oleksandr/Documents/proj/just-weather/data/all_cities.json"

    print("=" * 70)
    print("City Dataset Sorting Validation")
    print("=" * 70)

    all_valid = True

    # Validate hot_cities.json
    valid, error, stats = validate_sorting(hot_cities_path)
    if valid:
        print(f"  ✓ VALID - Properly sorted")
    else:
        print(f"  ✗ INVALID - Sorting errors detected:")
        print(error)
        all_valid = False

    # Validate all_cities.json
    valid, error, stats = validate_sorting(all_cities_path)
    if valid:
        print(f"  ✓ VALID - Properly sorted")
    else:
        print(f"  ✗ INVALID - Sorting errors detected:")
        print(error)
        all_valid = False

    print("\n" + "=" * 70)

    if all_valid:
        print("✓ All datasets are properly sorted for binary search")
        return 0
    else:
        print("✗ Some datasets have sorting issues - binary search will NOT work correctly")
        return 1

if __name__ == "__main__":
    sys.exit(main())
