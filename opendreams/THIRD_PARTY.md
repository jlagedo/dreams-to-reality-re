# Third-party notices

OpenDreams builds these pinned dependencies from source or uses the pinned host
tool listed in [README.md](README.md). Keep their upstream license texts with
any future distribution that includes their code or binaries.

| Component | License and notice | Role |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL/blob/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c/LICENSE.txt) | zlib; Copyright 1997–2026 Sam Lantinga | Statically linked runtime library |
| [Dear ImGui](https://github.com/ocornut/imgui/blob/f1cc2ae15e53a861a874c3034aae6798fde194ab/LICENSE.txt) | MIT; Copyright 2014–2026 Omar Cornut | Statically linked UI library and SDL3 platform backend |
| [sokol](https://github.com/floooh/sokol/blob/2e75443dbd4940b5aa8d76a8e479f8e4b270b9a3/LICENSE) | zlib; Copyright 2018 Andre Weissflog | GPU and ImGui renderer source |
| [sokol-tools-bin](https://github.com/floooh/sokol-tools-bin/blob/11d0cf678105d614d675e6d9bd2aaf3eeff12f8c/LICENSE) | MIT; Copyright 2019 Andre Weissflog | Build-time shader compiler only |
| [lib9660](https://github.com/erincandescent/lib9660/tree/17704f87e833f3de46b1546864d47ef4a6d12b97) | ISC variant; Copyright 2014, 2025 Erin Shepherd; [vendored license](third_party/lib9660/LICENSE) | ISO 9660 records over ODShared's bounded sector callback; local fixes listed in the vendored README |
| [Emscripten SDK](https://github.com/emscripten-core/emsdk/blob/dfb9d1a46c3bb8f52e1e6324be23123b9d73c190/LICENSE) | MIT | Browser build toolchain only |

The Python toolkit remains a separate reference project. The retired Babylon
viewer and its packer are no longer part of this repository. No original game
content is part of the spec 001 applications.
