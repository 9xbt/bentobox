import sys
import re

def classify_include(line):
    match = re.match(r'#include\s+<(.+)>', line.strip())
    if not match:
        return None
    path = match.group(1)
    if path.startswith("kernel/arch/"):
        return "arch"
    elif path.startswith("kernel/"):
        return "kernel"
    else:
        return "stdlib"

def sort_includes(lines):
    return sorted(lines, key=lambda l: (-len(l), l))

def main():
    if len(sys.argv) != 2:
        print("Usage: python format.py <file>")
        return

    input_file = sys.argv[1]

    stdlib, kernel, arch = [], [], []
    rest_of_file = []

    with open(input_file, 'r') as f:
        in_includes_section = True
        for line in f:
            stripped = line.strip()
            if in_includes_section:
                if stripped.startswith("#include"):
                    category = classify_include(stripped)
                    if category == "stdlib":
                        stdlib.append(stripped)
                    elif category == "kernel":
                        kernel.append(stripped)
                    elif category == "arch":
                        arch.append(stripped)
                    else:
                        in_includes_section = False
                        rest_of_file.append(line)
                else:
                    in_includes_section = False
                    rest_of_file.append(line)
            else:
                rest_of_file.append(line)

    for line in sort_includes(stdlib):
        print(line)
    for line in sort_includes(arch):
        print(line)
    for line in sort_includes(kernel):
        print(line)

    for line in rest_of_file:
        print(line, end='')

if __name__ == "__main__":
    main()
