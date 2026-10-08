#!/usr/bin/env python3
"""Relocate an existing private xrdp runtime without rebuilding its encoders.

Compiled xrdp paths include the location of gfx.toml and are independent of
--config. Copying a private runtime and replacing its embedded prefix with an
equal-length prefix permits isolated codec configuration. Every copied ELF
.text section must remain byte-for-byte identical. Existing destinations are
never overwritten. This tool does not start a service or modify its source.

Create:
  prepare_rdp_server.py --source-runtime /tmp/xrdp-e2e/runtime \
      --destination /tmp/xrdp-fps/runtime --write-codec-config rfx

Between sessions, change only the private codec configuration:
  prepare_rdp_server.py --destination /tmp/xrdp-fps/runtime \
      --write-codec-config avc420
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct


CODECS = {
    "rfx": '[codec]\norder = [ "RFX" ]\n',
    "avc420": ('[codec]\norder = [ "H.264" ]\n'
               'h264_encoder = "OpenH264"\n'
               '[OpenH264.default]\nEnableFrameSkip = false\n'
               'TargetBitrate = 20000000\nMaxBitrate = 0\nMaxFrameRate = 60.0\n'),
}

INI_TEMPLATE = """[Globals]
ini_version=1
fork=false
port=tcp://127.0.0.1:@PORT@
security_layer=tls
certificate=@CERTIFICATE@
key_file=@KEY_FILE@
autorun=Bench
allow_channels=true
allow_multimon=false
max_bpp=32
use_fastpath=both
tcp_nodelay=true
tcp_keepalive=true
bitmap_cache=true
bitmap_compression=true
bulk_compression=true

[Logging]
LogFile=@LOG_FILE@
LogLevel=INFO
EnableSyslog=false

[Channels]
rdpdr=false
rdpsnd=false
drdynvc=true
cliprdr=false
rail=false

[Bench]
name=Bench
lib=libxup.so
username=ask
password=ask
ip=127.0.0.1
port=@XORG_SOCKET@
code=0
rfx_frame_interval=32
h264_frame_interval=16
normal_frame_interval=40
"""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def elf_text(data):
    """Return .text bytes from the ELF64 little-endian binaries used here."""
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("Runtime contains an unsupported ELF format")
    section_offset = struct.unpack_from("<Q", data, 40)[0]
    entry_size, count, names_index = struct.unpack_from("<HHH", data, 58)
    if entry_size < 64 or count == 0 or names_index >= count:
        raise ValueError("Invalid or unsupported ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data,
                                   section_offset + index * entry_size)
                for index in range(count)]
    names = sections[names_index]
    strings = data[names[4]:names[4] + names[5]]
    for section in sections:
        end = strings.find(b"\0", section[0])
        if strings[section[0]:end] == b".text":
            return data[section[4]:section[4] + section[5]]
    raise ValueError("Runtime ELF has no .text section")


def relocate(source, destination):
    source = source.resolve(strict=True)
    destination = destination.absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError("Destination already exists; refusing to overwrite it")
    old = str(source).encode()
    new = str(destination).encode()
    if len(old) != len(new):
        raise ValueError("Source and destination prefixes must have equal byte lengths")
    if source == destination or source in destination.parents:
        raise ValueError("Destination must be outside the source runtime")
    manifest = {"source_runtime": str(source),
                "destination_runtime": str(destination),
                "operation": "Equal-length embedded path-prefix replacement only",
                "elf_text_unchanged": True, "files": {}}
    shutil.copytree(source, destination, symlinks=True)
    for path in sorted(destination.rglob("*")):
        if path.is_symlink():
            target = path.readlink()
            replacement = str(target).replace(str(source), str(destination))
            if replacement != str(target):
                path.unlink()
                path.symlink_to(replacement)
            continue
        if not path.is_file():
            continue
        before = path.read_bytes()
        after = before.replace(old, new)
        record = {"before_sha256": sha256(before), "after_sha256": sha256(after),
                  "prefix_occurrences": before.count(old)}
        if before.startswith(b"\x7fELF"):
            original_text, copied_text = elf_text(before), elf_text(after)
            if original_text != copied_text:
                raise ValueError("Prefix replacement would alter ELF .text: " + str(path))
            record["text_sha256"] = sha256(original_text)
            record["text_unchanged"] = True
        if after != before:
            path.write_bytes(after)
        manifest["files"][str(path.relative_to(destination))] = record
    templates = destination / "benchmark-templates"
    templates.mkdir()
    for codec, contents in CODECS.items():
        (templates / ("gfx-" + codec + ".toml")).write_text(contents)
    (templates / "xrdp.ini.in").write_text(INI_TEMPLATE)
    manifest["templates"] = str(templates)
    manifest["frame_intervals_ms"] = {"rfx": 32, "avc420": 16, "normal": 40}
    manifest["launch"] = [str(destination / "sbin/xrdp-optimized"),
                          "--nodaemon", "--config", "TRIAL_XRDP_INI"]
    manifest["library_directory"] = str(destination / "lib/xrdp")
    manifest["codec_config"] = str(destination / "etc/xrdp/gfx.toml")
    (destination / "relocation-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-runtime", type=Path)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--write-codec-config", choices=CODECS)
    args = parser.parse_args()
    destination = args.destination.absolute()
    if args.source_runtime:
        manifest = relocate(args.source_runtime, destination)
    else:
        if not args.write_codec_config:
            parser.error("Supply --source-runtime or --write-codec-config")
        manifest = json.loads((destination / "relocation-manifest.json").read_text())
        if manifest["destination_runtime"] != str(destination):
            parser.error("Destination does not match its private relocation manifest")
    if args.write_codec_config:
        config = destination / "etc/xrdp/gfx.toml"
        config.write_text(CODECS[args.write_codec_config])
        # Codec settings are intentionally changed between isolated sessions;
        # retain the immutable binary relocation manifest separately.
        active = {"codec": args.write_codec_config, "path": str(config),
                  "sha256": sha256(config.read_bytes())}
        (destination / "active-codec.json").write_text(json.dumps(active, indent=2) + "\n")
    print(json.dumps({key: manifest[key] for key in
                      ("destination_runtime", "elf_text_unchanged", "templates",
                       "launch", "library_directory", "codec_config")}, indent=2))


if __name__ == "__main__":
    main()
