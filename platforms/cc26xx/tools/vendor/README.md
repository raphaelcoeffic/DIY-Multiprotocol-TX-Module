# Vendored Python packages

The flashing and file system scripts of `tools/` use these, so that they run
with Python alone (no pip, venv or uv).

| Package  | Version | License | Files |
|----------|---------|---------|-------|
| [pyserial](https://github.com/pyserial/pyserial) | 3.5 (PyPI wheel, SHA-256 c4451db6ba391ca6ca299fb3ec7bae67a5c55dde170964c7a14ceefec02f2cf0) | BSD-3-Clause (`serial/LICENSE.txt`) | the serial ports of Windows, macOS and Linux and their listing; left out: the URL handlers, RFC 2217, miniterm, the Java and .NET backends |
