# Familiar UI native regression harness

`run.sh` compiles `src/ui/familiar_ui.cpp` on the host and exercises its public
touch gesture API. It needs only a C++17 compiler and Bash. The pytest wrapper
also needs pytest, which is already part of the host test environment.

The local stubs provide the small Arduino `String`, LovyanGFX drawing surface,
and ArduinoJson serialization API used by FamiliarUi. Drawing is intentionally
no-op; assertions observe real UI state transitions and emitted host commands.
The serializer stub covers the string-valued dispatch fields in these checks;
the harness does not validate ArduinoJson escaping or renderer appearance. No
PlatformIO libraries or board hardware are needed.

Run directly with `tests/native_ui/run.sh`, or through the host suite with
`python -m pytest tests/test_familiar_ui_native.py -q`.
