"""Read the plugin version from the same source used to compile the library."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def plugin_version():
    cmake = (ROOT / 'CMakeLists.txt').read_text()
    return '.'.join(re.search(r'set\(VERSION_' + part + r'\s+"(\d+)"\)', cmake)[1]
                    for part in ('MAJOR', 'MINOR', 'PATCH', 'TWEAK'))


VERSION = plugin_version()
