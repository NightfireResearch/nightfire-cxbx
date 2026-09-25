# Functions to create by hand in DRIVING.ELF

Ghidra refused these creations through the MCP (batch P019). 'Unable to create function' has so far meant the
function's first word (00 00 ..) was taken for padding at the end of the previous function; 'defined data' means
the bytes are marked as data. Fix, create, and say so: a small batch will name them.

| address | name | Ghidra's reason |
|---|---|---|
| 0x0016ec40 | AttributeSystem::GetMetrics | Unable to create function |
| 0x00232088 | WWorldMath::Quatern_QuatToMat | Function entryPoint may not be created on defined data |
| 0x00237d18 | GGallery::LINE_SetState | Unable to create function |
| 0x00291150 | EAGL::RenderContextExtension::GetBackBufferInfo | Unable to create function |
| 0x00291198 | EAGL::RenderContextExtension::GetZBuffer | Unable to create function |
| 0x00293a38 | EAGL::Transform::ExtractRotTrans | Function entryPoint may not be created on defined data |
| 0x00295188 | EAGL::Transform::BuildQT | Function entryPoint may not be created on defined data |
| 0x00297ba8 | EAGL::VU0_MATRIX3x4_mult | Function entryPoint may not be created on defined data |
| 0x00297cd8 | EAGL::VU0_MATRIX4_multb | Unable to create function |
