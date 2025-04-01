#
# Take a set of files, copied and pasted from the Google Sheet.
# Convert it to a C-style enum that Ghidra can parse.
#

# Load data from file
with open("sfx.txt", "r") as file:
    data = file.read()

enum_code = "typedef enum {\n"

lines = data.strip().split("\n")
for line in lines:
    parts = line.split("\t")
    if len(parts) == 2 and parts[0].isdigit() and parts[1] != "#N/A":
        enum_code += f"{parts[1]} = {parts[0]},\n"

enum_code += "} Nightfire_SFX;"

# Save enum_code to a file
with open("Nightfire_SFX.c", "w") as file:
    file.write(enum_code)


# Load data2 from file
with open("txt2lbl.txt", "r") as file:
    data2 = file.read()

# Extract the entries that have a non-empty name
filtered_entries = []

for line in data2.split("\n"):
    x = line.split("\t")
    if len(x) != 3:
        print(f"Skipping line: {line}")
        continue
    if x[1].strip() == "" and x[2].strip() == "#N/A":
        continue  # Skip lines with empty name and #N/A value
    if x[1].strip() == "":
        if x[2].strip() != "":
            filtered_entries.append((x[0], x[2]))
    else:
        filtered_entries.append((x[0], x[1]))

# If a duplicate name value is found, print a warning
if len(set([x[1] for x in filtered_entries])) != len(filtered_entries):
    print("WARNING: Duplicate name values found!")
    names = [x[1] for x in filtered_entries]
    duplicate_names = set([name for name in names if names.count(name) > 1])
    print("Duplicate names:", duplicate_names)


# Generate the C-style enum
enum_code = "typedef enum {\n"
for value, name in filtered_entries:
    enum_code += f"    {name} = 0x{value},\n"
enum_code += "} Action_TranslatedText;"

# Save enum_code to "Nightfire_Text.h"
with open("Nightfire_Text.h", "w") as f:
    f.write(enum_code)



