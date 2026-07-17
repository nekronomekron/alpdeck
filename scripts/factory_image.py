"""Merges everything a blank device needs into a single flashable image.

`pio run -t upload` writes only the app partition, so a device flashed that way
comes up with an empty filesystem and no boot.lua — the data/ directory needs a
separate `uploadfs`. That split is fine on the bench but awkward to hand to a
user, so this target produces one binary they can write at offset 0:

    pio run -t factory
    esptool.py --chip esp32s3 write_flash 0x0 .pio/build/Alpdeck/factory.bin

Offsets and flash settings are taken from the build environment rather than
hardcoded, so they follow the partition table and board config automatically.

Note the merged image spans the whole flash up to the filesystem partition, and
esptool pads the gaps with 0xFF: expect a file around the flash size (16MB here)
regardless of how little real data it holds.
"""

Import("env")

from os.path import isfile, join

platform = env.PioPlatform()
board = env.BoardConfig()


def _partition_csv_offset(subtypes):
    """Offset of the first partition matching subtypes, per the build's CSV.

    The platform only computes FS_START for its own buildfs/uploadfs targets, so
    it isn't available here and the table is read directly instead.
    """
    csv_path = env.subst("$PARTITIONS_TABLE_CSV")
    if not isfile(csv_path):
        return None

    with open(csv_path) as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            tokens = [token.strip() for token in line.split(",")]
            if len(tokens) < 5:
                continue
            if tokens[1] == "data" and tokens[2] in subtypes:
                return tokens[3]
    return None


def build_factory_image(target, source, env):
    fs_image = env.subst(join("$BUILD_DIR", "${ESP32_FS_IMAGE_NAME}.bin"))
    fs_offset = _partition_csv_offset(("spiffs", "littlefs", "fat"))

    if fs_offset is None:
        print("factory: no filesystem partition in the table; aborting")
        env.Exit(1)
    if not isfile(fs_image):
        print("factory: %s missing; run 'pio run -t buildfs' first" % fs_image)
        env.Exit(1)

    # The bootloader, partition table and boot_app0 with the offsets the normal
    # upload path uses, so a merged flash matches an incremental one.
    segments = []
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        segments.append((offset, env.subst(image)))
    segments.append((env.subst("$ESP32_APP_OFFSET"),
                     env.subst(join("$BUILD_DIR", "${PROGNAME}.bin"))))
    segments.append((fs_offset, fs_image))
    segments.sort(key=lambda segment: int(str(segment[0]), 16))

    output = env.subst(join("$BUILD_DIR", "factory.bin"))
    command = [
        env.subst("$PYTHONEXE"),
        join(platform.get_package_dir("tool-esptoolpy") or "", "esptool.py"),
        "--chip", board.get("build.mcu", "esp32s3"),
        "merge_bin",
        "-o", output,
        "--flash_mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash_freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash_size", board.get("upload.flash_size", "4MB"),
    ]
    for offset, image in segments:
        command.extend([str(offset), image])

    print("factory: merging %d segments into %s" % (len(segments), output))
    for offset, image in segments:
        print("  %-10s %s" % (offset, image))

    env.Execute(" ".join('"%s"' % part for part in command))


# The platform only registers the FS image target for its own buildfs/uploadfs
# commands, so build the node here to make it a real dependency of factory.
dependencies = None
if "factory" in COMMAND_LINE_TARGETS:
    fs_node = env.DataToBin(
        join("$BUILD_DIR", "${ESP32_FS_IMAGE_NAME}"), "$PROJECT_DATA_DIR"
    )
    env.NoCache(fs_node)
    AlwaysBuild(fs_node)
    dependencies = [join("$BUILD_DIR", "${PROGNAME}.bin"), fs_node]

env.AddCustomTarget(
    name="factory",
    dependencies=dependencies,
    actions=[build_factory_image],
    title="Factory image",
    description="Merge firmware and LittleFS into a single flash-at-0x0 image",
    always_build=True,
)
