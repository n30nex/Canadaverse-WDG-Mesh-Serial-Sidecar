"""Build only the pinned MeshCore pieces used by the sidecar."""

from os.path import commonpath, join, realpath

Import("env")  # type: ignore[name-defined]  # noqa: F821

meshcore_ed25519 = join(
    env.subst("$PROJECT_LIBDEPS_DIR"),  # type: ignore[name-defined]  # noqa: F821
    env.subst("$PIOENV"),  # type: ignore[name-defined]  # noqa: F821
    "MeshCore",
    "lib",
    "ed25519",
)
meshcore_builder = join(
    env.subst("$PROJECT_LIBDEPS_DIR"),  # type: ignore[name-defined]  # noqa: F821
    env.subst("$PIOENV"),  # type: ignore[name-defined]  # noqa: F821
    "MeshCore",
    "build_as_lib.py",
)
meshcore_ssd1306 = join(
    env.subst("$PROJECT_LIBDEPS_DIR"),  # type: ignore[name-defined]  # noqa: F821
    env.subst("$PIOENV"),  # type: ignore[name-defined]  # noqa: F821
    "MeshCore",
    "src",
    "helpers",
    "ui",
    "SSD1306Display.cpp",
)

# MeshCore's upstream library script builds every generic board, RTC, sensor,
# ESP-NOW and legacy BLE helper. The sidecar uses none of those. Replace that
# generated dependency script with a fail-closed source filter so all three
# targets build the same small, auditable MeshCore surface.
minimal_meshcore_builder = """Import(\"env\")  # type: ignore[name-defined]  # noqa: F821

menv = env  # type: ignore[name-defined]  # noqa: F821
src_filter = [
    \"+<*.cpp>\",
    \"+<helpers/AdvertDataHelpers.cpp>\",
    \"+<helpers/BaseChatMesh.cpp>\",
    \"+<helpers/StaticPoolPacketManager.cpp>\",
    \"+<helpers/TxtDataHelpers.cpp>\",
]
for item in menv.get(\"CPPDEFINES\", []):
    if isinstance(item, tuple) and item[0] == \"DISPLAY_CLASS\":
        src_filter.append(f\"+<helpers/ui/{item[1]}.cpp>\")
menv.Replace(SRC_FILTER=src_filter)
"""
try:
    with open(meshcore_builder, "r", encoding="utf-8") as source:
        upstream_builder = source.read()
    if upstream_builder != minimal_meshcore_builder:
        if "menv.Replace(SRC_FILTER=src_filter)" not in upstream_builder:
            raise RuntimeError("MeshCore build script format changed; refusing to patch")
        with open(meshcore_builder, "w", encoding="utf-8", newline="\n") as target:
            target.write(minimal_meshcore_builder)
except OSError as error:
    raise RuntimeError(f"Could not prepare pinned MeshCore dependency: {error}") from error

# The release screenshot is captured from the actual display buffer. Keep the
# upstream display driver unchanged during normal operation and emit one frame
# only after the explicit serial diagnostic command sets this global flag.
ssd1306_end_frame = """void SSD1306Display::endFrame() {
  display.display();
}
"""
ssd1306_diagnostic_end_frame = """extern volatile bool wdg_screenshot_requested;

void SSD1306Display::endFrame() {
  display.display();
  if (!wdg_screenshot_requested) return;
  wdg_screenshot_requested = false;

  const uint8_t* pixels = display.getBuffer();
  constexpr uint16_t width = 128;
  constexpr uint16_t height = 64;
  constexpr uint16_t length = width * height / 8;
  static uint32_t sequence = 0;
  uint32_t crc = 0xFFFFFFFF;
  for (uint16_t i = 0; i < length; i++) {
    crc ^= pixels[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
  }
  crc = ~crc;

  const char magic[] = {'N', 'P', 'O', 'F'};
  Serial.write(reinterpret_cast<const uint8_t*>(magic), sizeof(magic));
  Serial.write(reinterpret_cast<const uint8_t*>(&sequence), sizeof(sequence));
  Serial.write(reinterpret_cast<const uint8_t*>(&width), sizeof(width));
  Serial.write(reinterpret_cast<const uint8_t*>(&height), sizeof(height));
  Serial.write(reinterpret_cast<const uint8_t*>(&length), sizeof(length));
  Serial.write(reinterpret_cast<const uint8_t*>(&crc), sizeof(crc));
  Serial.write(pixels, length);
  sequence++;
}
"""
try:
    with open(meshcore_ssd1306, "r", encoding="utf-8") as source:
        ssd1306_source = source.read()
    if ssd1306_diagnostic_end_frame not in ssd1306_source:
        if ssd1306_source.count(ssd1306_end_frame) != 1:
            raise RuntimeError(
                "Pinned MeshCore SSD1306 driver changed; refusing to patch"
            )
        ssd1306_source = ssd1306_source.replace(
            ssd1306_end_frame, ssd1306_diagnostic_end_frame
        )
        with open(meshcore_ssd1306, "w", encoding="utf-8", newline="\n") as target:
            target.write(ssd1306_source)
except OSError as error:
    raise RuntimeError(f"Could not prepare SSD1306 diagnostics: {error}") from error

include_paths = [
    meshcore_ed25519,
    join(env.subst("$PROJECT_DIR"), "include"),  # type: ignore[name-defined]  # noqa: F821
]
if env.PioPlatform().name == "espressif32":  # type: ignore[name-defined]  # noqa: F821
    framework_dir = env.PioPlatform().get_package_dir(  # type: ignore[name-defined]  # noqa: F821
        "framework-arduinoespressif32"
    )
    include_paths.append(join(framework_dir, "libraries", "Network", "src"))
env.Append(CPPPATH=include_paths)  # type: ignore[name-defined]  # noqa: F821
# The Ed25519 sources are standalone C. Building them with the full Arduino
# include graph produces a response file longer than Windows can pass from GCC
# to its assembler. Override only these objects' include path while retaining
# the parent environment's toolchain, which PlatformIO configures after PRE
# scripts run.
def isolate_ed25519_includes(build_env, node):
    source_path = realpath(node.srcnode().get_abspath())
    if commonpath([realpath(meshcore_ed25519), source_path]) == realpath(
        meshcore_ed25519
    ):
        return build_env.Object(node, CPPPATH=[meshcore_ed25519])
    return node


env.AddBuildMiddleware(isolate_ed25519_includes)  # type: ignore[name-defined]  # noqa: F821
env.BuildSources(  # type: ignore[name-defined]  # noqa: F821
    join(env.subst("$BUILD_DIR"), "meshcore-ed25519"),  # noqa: F821
    meshcore_ed25519,
)
