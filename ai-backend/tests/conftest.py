"""Test-wide fixtures. Forces TONEFORGE_PRIMARY_PROVIDER=mock before any
app.* module is imported, so the API test suite never depends on a running
Ollama instance - see app/config.py.
"""

import os

os.environ["TONEFORGE_PRIMARY_PROVIDER"] = "mock"
