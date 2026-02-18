#!/usr/bin/env python3
import csv
import sys

input_file = sys.argv[1]
output_file = "data/swedish_cities_locations.csv"

seen = set()  # to track duplicates

with open(input_file, encoding="utf-8") as f_in, \
     open(output_file, "w", newline="", encoding="utf-8") as f_out:

    reader = (line.strip().split("\t") for line in f_in)
    writer = csv.writer(f_out)
    writer.writerow(["name", "latitude", "longitude"])

    for cols in reader:
        if len(cols) >= 6:
            name = cols[1].strip()
            lat = cols[4].strip()
            lon = cols[5].strip()

            # Skip if any field is empty
            if not name or not lat or not lon:
                continue

            # Use (name, lat, lon) as unique key
            key = (name, lat, lon)
            if key in seen:
                continue  # skip duplicate
            seen.add(key)

            writer.writerow([name, lat, lon])

print(f"CSV file created: {output_file}")
