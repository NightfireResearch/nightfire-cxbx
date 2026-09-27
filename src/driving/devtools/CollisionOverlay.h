#pragma once

// A debug view of the level's collision geometry, drawn over the scene. F7 cycles it: off, solid (translucent,
// coloured by surface), wireframe. settings.ini: CollisionOverlay=off|solid|wire for the starting mode, and
// CollisionOverlayRadius=metres (default 150) for how far from the camera instances are drawn.
//
// Called once a frame from the world pass (RGlareManager::DrawGlares with inWorld), where the game's own
// renderer is set up with the camera and depth buffer. See docs/driving-collision.md for the data it reads.
void CollisionOverlay_Draw();
