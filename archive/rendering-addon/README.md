# Archived rendering addon

This preserves the custom NR, DLAA, Super Resolution, frame generation and
D3D12-helper FSR experiments. The active Linux integration uses the original
TTWVR renderer, its D3D11-helper DLSS and native Vulkan FSR instead.

[Return to the current installation guide](../../docs/installation.md).

The full source, evidence and GPL-3.0-only license remain in [neural/](neural/README.md).
NVIDIA SDKs and runtime DLLs remain separate dependencies under NVIDIA's terms.
Older documents describe the historical setup and are not current installation
instructions. For historical commands containing `neural/`, first change into
`archive/rendering-addon` (or adjust that prefix). Do not install this addon in
the current author-renderer profile: it replaces the original upscaler controls.

[NR/DLAA source and build](neural/README.md) ·
[Custom SR/FG experiments](docs/sr-frame-generation.md) ·
[Custom helper FSR experiment](docs/fsr-integration.md)
