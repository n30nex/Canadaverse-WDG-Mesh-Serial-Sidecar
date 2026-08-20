"""Create one-file factory images for ESP32 release downloads."""

from pathlib import Path
import subprocess


Import("env")  # type: ignore[name-defined]  # noqa: F821


def merge_factory_image(source, _target, build_env):
    output = Path(build_env.subst("$BUILD_DIR")) / "firmware-factory.bin"
    board = build_env.BoardConfig()
    application_offset = build_env.subst("$ESP32_APP_OFFSET")
    if not application_offset or application_offset in ("None", "$ESP32_APP_OFFSET"):
        application_offset = board.get("upload.offset_address", "0x10000")

    command = [
        build_env.subst("$PYTHONEXE"),
        build_env.subst("$UPLOADER"),
        "--chip",
        board.get("build.mcu"),
        "merge_bin",
        "-o",
        str(output),
    ]
    for offset, image in build_env.get("FLASH_EXTRA_IMAGES", []):
        command.extend((str(offset), build_env.subst(str(image))))
    command.extend((str(application_offset), str(source[0])))
    subprocess.run(command, check=True)
    print(f"Factory image: {output}")


if str(env.BoardConfig().get("build.mcu", "")).startswith("esp32"):  # type: ignore[name-defined]  # noqa: F821
    env.AddPostAction(  # type: ignore[name-defined]  # noqa: F821
        "$BUILD_DIR/${PROGNAME}.bin", merge_factory_image
    )
