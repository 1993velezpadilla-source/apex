# Preserved reference source

`signalsmith-stretch2/` preserves the complete exported source tree formerly at
`My DAW/DAW_Core/Source/ThirdParty/signalsmith-stretch2` in the Desktop workspace.
Its files retain their original bytes and internal directory structure.

This relocation honors the existing active dependency contract without deleting
the tree or treating similar names as interchangeable. APEX's application and
test projects do not reference it; their dependencies are the pinned
`signalsmith-stretch` and `signalsmith-linear` submodules in the active source.
