#pragma once

enum class PlayState  { Stopped, Playing, Paused };
enum class GizmoMode  { Translate, Rotate, Scale };

// Where EderPlayer renders when you press Play.
//   Embedded  — child window inside the Viewport panel (like UE "Simulate").
//   Standalone — separate OS window outside the editor.
enum class PlayTarget { Embedded, Standalone };
