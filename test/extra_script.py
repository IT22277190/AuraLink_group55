Import("env")

# Ensure all necessary build flags are set
env.Append(
    CPPDEFINES=[
        ("ARDUINO_ARCH_ESP32", "1"),
        ("ESP32", "1"),
    ]
)

# Add include paths
env.Append(
    CPPPATH=[
        "#include",
        "#lib",
    ]
)