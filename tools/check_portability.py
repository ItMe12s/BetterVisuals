from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SHADERS = SRC / "shaders"
RENDER = SRC / "render"
SHADER_PROGRAM = RENDER / "ShaderProgram.cpp"
POST_PROCESS_PIPELINE = RENDER / "PostProcessPipeline.cpp"
SOURCE_SUFFIXES = {".cpp", ".hpp"}

BANNED_GL = {
    "GLEW symbol": re.compile(r"\bGLEW_[A-Z0-9_]+\b"),
    "extension GL entry point": re.compile(r"\bgl[A-Za-z0-9_]+(?:ARB|APPLE|EXT|OES)\b"),
    "extension GL enum": re.compile(r"\bGL_[A-Z0-9_]+_(?:ARB|APPLE|EXT|OES)\b"),
    "separate read/draw framebuffer": re.compile(
        r"\bGL_(?:READ|DRAW)_FRAMEBUFFER(?:_BINDING)?\b"
    ),
    "framebuffer blit": re.compile(r"\bglBlit(?:Named)?Framebuffer\b"),
    "alternate framebuffer copy": re.compile(
        r"\b(?:glCopyTexImage[12]D|glCopyTexSubImage[13]D|"
        r"glCopyTextureSubImage[123]D|glCopyImageSubData)\b"
    ),
    "framebuffer readback": re.compile(
        r"\b(?:glReadn?Pixels|glGetn?(?:Compressed)?TexImage|"
        r"glGet(?:Compressed)?Texture(?:Sub)?Image)\b"
    ),
    "framebuffer fetch": re.compile(
        r"\b(?:GL_[A-Za-z0-9]+_shader_framebuffer_fetch(?:_non_?coherent)?|"
        r"gl_LastFrag(?:Data|ColorARM))\b"
    ),
    "framebuffer sRGB": re.compile(r"\bGL_(?:FRAMEBUFFER_SRGB|SRGB[A-Z0-9_]*)\b"),
    "sized texture format": re.compile(
        r"\bGL_(?:R8|RG8|RGB8|RGBA8|LUMINANCE8|LUMINANCE8_ALPHA8)\b"
    ),
    "texture-level query": re.compile(
        r"\b(?:glGetTexLevelParameter(?:f|i)v|GL_TEXTURE_(?:WIDTH|HEIGHT))\b"
    ),
    "non-GLES2 vertex array": re.compile(
        r"\b(?:gl(?:Bind|Delete|Gen)VertexArrays?|GL_VERTEX_ARRAY_BINDING)\b"
    ),
    "non-GLES2 buffer mapping": re.compile(r"\bgl(?:MapBuffer|UnmapBuffer)\b"),
    "non-GLES2 immutable texture storage": re.compile(r"\bglTexStorage[23]D\b"),
}


def sources_below(directory: Path) -> list[Path]:
    return sorted(
        path for path in directory.rglob("*") if path.suffix in SOURCE_SUFFIXES
    )


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def main() -> int:
    failures: list[str] = []
    source_files = sources_below(SRC)
    shader_files = sources_below(SHADERS)
    renderer_files = sources_below(RENDER)

    for path in shader_files:
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), 1
        ):
            if "#version" in line:
                failures.append(
                    f"{relative(path)}:{line_number}: embedded shader #version directive"
                )

    shader_program = SHADER_PROGRAM.read_text(encoding="utf-8")
    required_prelude = (
        '"#version 100\\n"',
        '"precision highp float;\\n"',
        '"precision highp int;\\n"',
        '"#version 120\\n"',
        "#ifdef GEODE_IS_MOBILE",
    )
    for required in required_prelude:
        if required not in shader_program:
            failures.append(
                f"{relative(SHADER_PROGRAM)}: missing centralized prelude {required}"
            )
    if shader_program.count("#version") != 2:
        failures.append(
            f"{relative(SHADER_PROGRAM)}: expected exactly two shader preludes"
        )

    for path in source_files:
        text = path.read_text(encoding="utf-8")
        for label, pattern in BANNED_GL.items():
            for match in pattern.finditer(text):
                line_number = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{relative(path)}:{line_number}: banned {label}: {match.group(0)}"
                )

    render_and_shader_files = renderer_files + shader_files
    for path in render_and_shader_files:
        text = path.read_text(encoding="utf-8")
        platform_macros = re.finditer(r"\bGEODE_IS_[A-Z0-9_]+\b", text)
        for match in platform_macros:
            allowed = path == SHADER_PROGRAM and match.group(0) == "GEODE_IS_MOBILE"
            if not allowed:
                line_number = text.count("\n", 0, match.start()) + 1
                failures.append(
                    f"{relative(path)}:{line_number}: renderer platform branch: {match.group(0)}"
                )

    copy_sites: list[tuple[Path, int]] = []
    for path in source_files:
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r"\bglCopyTexSubImage2D\b", text):
            copy_sites.append((path, text.count("\n", 0, match.start()) + 1))
    if len(copy_sites) != 1 or copy_sites[0][0] != POST_PROCESS_PIPELINE:
        rendered_sites = (
            ", ".join(f"{relative(path)}:{line}" for path, line in copy_sites) or "none"
        )
        failures.append(
            "expected exactly one glCopyTexSubImage2D in "
            f"{relative(POST_PROCESS_PIPELINE)}, found {rendered_sites}"
        )

    if failures:
        print("Portability check failed:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(f"Portability check passed ({len(source_files)} source files checked).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
