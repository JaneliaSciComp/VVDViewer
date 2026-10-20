# VVDViewer: wxWidgets → Qt port

VVDViewer now has two front ends over one shared, toolkit-neutral engine:

* **VVDViewer** — the original wxWidgets app (unchanged when Qt is off);
* **VVDViewerQt** — a Qt 6 app with the same pane layout and functions.

As of the `VVDViewer_QT` branch the Qt app is a complete port of the wx GUI
(windows, render views, tree, properties, tools, projects, movie export,
plugins). This document is the developer / reviewer reference: how it is
built and organised, what differs from wx, the bugs fixed on the way (many in
the shared engine, so they fix the wx app too), the speedups, the known
issues, and the technical notes and test hooks for each feature.

Why Qt: wxWidgets was hard to keep consistent across macOS / Windows / Linux
(native control heights differ per platform and per wx release; the bottom
row of the Properties pane was clipped on macOS with wx 3.3 because the pane
height was a hard-coded pixel value). Qt gives one layout engine, a native
Vulkan-capable window on every platform and a much larger pool of
maintainers.

## Building

```bash
bash build.sh               # wx app only (unchanged)
bash build.sh --enable-qt   # wx app + VVDViewerQt (Homebrew Qt on macOS)
```

`ENABLE_QT_GUI=ON` is the CMake option; `cmake/QtGui.cmake` defines the
target (it globs `fluorender/FluoRender/QtGui/` and `plugin_*/qt/`). Qt is not
built by vcpkg (too slow); it comes from Homebrew, the distro, or the Qt
installer (`-DCMAKE_PREFIX_PATH=<Qt dir>`). The Qt target needs
`QT_NO_KEYWORDS` because FLIVR shader classes have a method called `emit()`;
use `Q_SIGNALS/Q_SLOTS/Q_EMIT` in `QtGui/`. NRRD gzip decoding uses
libdeflate (also a CMake dependency of the engine).

On macOS both app bundles are self-contained and relocatable
(`cmake/macos_bundle_fixup.sh`, run after each build): MoltenVK and its ICD
json, the Vulkan loader and every library are copied into the bundle, install
names are rewritten, the only rpath is `@executable_path/../Frameworks`, the
build fails if anything still points outside the bundle, and the bundle is
signed (`VVD_CODESIGN_IDENTITY`, default ad hoc). Users need no Homebrew, Qt
or Vulkan SDK. The app sets `VK_DRIVER_FILES` to the bundled ICD unless the
user set it. Distribution outside your own Macs needs a Developer ID and
notarization (not set up).

Only macOS arm64 has been built and tested; Windows and Linux are not built
yet (see "Known issues").

## Architecture

```
                    ┌────────────────────────┐   ┌────────────────────────┐
                    │  wx GUI (VVDMainFrame) │   │  Qt GUI (VVDViewerQt)   │
                    │  VRenderFrame, VRender │   │  QtGui/MainWindow,      │
                    │  View, panels, dialogs │   │  RenderCanvas, panels   │
                    │  WxUIBackend           │   │  QtUIBackend            │
                    └───────────┬────────────┘   └───────────┬────────────┘
                                │  VVD_WX_STRING_INTEROP     │ VVD_QT_STRING_INTEROP
                    ┌───────────┴────────────────────────────┴────────────┐
                    │ toolkit-neutral engine + data layer (no wx, no Qt)   │
                    │  Core/  VVDString, Thread, FileUtil, IniFile, UI     │
                    │  DataManager, VolumeSelector/Calculator, KernelExec. │
                    │  FLIVR/ (renderer)   Vulkan/   Formats/ (readers)    │
                    │  Tracking/ Animator/ NV/ Converters/ RigidCellsWarp  │
                    └──────────────────────────────────────────────────────┘
```

* `fluorender/FluoRender/Core/` is the seam. `vvd::String` **is** a
  `std::wstring` with the wxString helpers the code used (`Mid`, `Find`,
  `Format`, ...). A GUI target that defines `VVD_WX_STRING_INTEROP` (all wx
  targets do, per target in `CMakeLists.txt`) gets implicit `wxString ⇄
  vvd::String`; `VVD_QT_STRING_INTEROP` does the same for `QString`. The
  engine object libraries are compiled with neither and are shared by both
  executables. `IniFile` reads / writes the wxFileConfig format, so settings,
  default files and projects are shared by both apps.
* `vvd::Thread` / `vvd::CriticalSection` replace `wxThread` /
  `wxCriticalSection` with the same semantics (detached threads delete
  themselves; `Delete()` waits). `vvd::UI::MessageBox / CreateProgress` go
  through a backend the GUI installs at start-up (`WxUIBackend`,
  `QtUIBackend`).
* The wx `VRenderVulkanView` was not translated into one widget: the Qt
  render view is `RenderCanvas` split by concern (`RenderCanvasCamera`,
  `…Layers`, `…Modes`, `…Streaming`, `…Capture`, `…Movie`, `…Traces`) plus
  tool objects (`BrushTool`, `RulerTool`, `ViewOverlays`) behind `CanvasTool`
  hooks. `MainWindow` (split into `MainWindow*.cpp`) is the wx VRenderFrame;
  project load / save is `ProjectIO`.
* Shared code used by both front ends for the new features:
  `WarpVolumeByRulers` (all Measurement transforms) and `RigidCellsWarp`.
* Rule for new code: **nothing under `Core/`, `FLIVR/`, `Formats/`,
  `Vulkan/`, `DataManager.*`, `VolumeSelector.*`, `VolumeCalculator.*`,
  `KernelExecutor.*` may include a wx or Qt header.** A Linux compile check
  used during the port (three object targets: `engine` without any toolkit,
  `gui` with wxGTK 3.2, `qtgui` with Qt 6.4) catches toolkit leaks into the
  engine immediately; worth turning into a CI job.
* `Main.cpp` (wx `wxApp`) stays for the wx build; `QtGui/QtMain.cpp` is its
  counterpart. When the wx GUI is retired: drop `VVD_WX_STRING_INTEROP`,
  `WxUIBackend`, `Main.cpp`, the wx block of `Core/VVDString.h`.

## Feature status

Everything below is ported and tested; the "Notes" column points to the
detailed sections. Open items are listed under "Known issues / not done".

| Area | State | Notes |
|---|---|---|
| Main window, docks, menus, toolbar | done | wx pane layout, wx toolbar bitmaps, menu bar, Show/Hide UI; fits the screen ("Window and layout") |
| Render views | done, several views | Vulkan canvas per view on one shared context ("Render views") |
| Render modes | done | Layered / Depth (depth peeling, transparent meshes) / Composite, fog, shadows, gradient background ("Rendering") |
| Streaming (memory swap) | done | wx mem-swap loop, empty-brick skip, progress guarantee ("Streaming engine") |
| View controls / keys | done | camera, clipping, keys as wx key polling ("View controls and keys") |
| View overlays | done | Center, Info, Legend drop-down, scale bar, colormap, annotations |
| Capture / movie export | done | tiled capture, TIFF writer, Record/Export with keyframes, 4D scripts |
| Workspace tree | done | full DataTreeCtrl port, drag & drop, context menus, visibility history |
| Properties | done | volume, mesh, mesh manipulate, annotation panes ("Panels and dialogs") |
| Output Adjustments / Clipping Planes | done | `AdjustPanel`, `ClipPanel` |
| Open / save volumes, meshes | done | all `DataManager` formats, Zarr / N5 dataset choice, companion masks, save / bake / export metadata |
| Projects (.vrp) | done | wx-compatible load / save, round trip loses no wx key ("Projects") |
| Start-up / command line / drop | done | `-d`, `-m`, `-p`, `vvd:` URLs, Finder open, folder drop |
| Paint brush / selection / masks | done | all strokes, undo / redo, erase / extract, calculations, components, index / NA volumes |
| Measurement | done + extensions | rulers, transforms (TPS ... Cells), Start / End Volume, Auto-select, Target: grid ("Measurement and warps") |
| Analyze dialogs | done | Noise Reduction, Counting and Volume, Colocalization, Convert, Information |
| Components & Tracking | done | `TraceDialog`, all pages ("Components & Tracking") |
| Settings | done | all SettingDlg pages, same `fluorender.set` file, test modes |
| Plugins | done | Fiji Interface, NBLAST Search, Neuron Annotator, built in; shared-library plugins ("Plugins") |
| Engine messages / progress | done | Qt dialogs via `QtUIBackend` |
| Windows / Linux builds | **not built** | see "Known issues" |
| Wayland surfaces | untested | run with `QT_QPA_PLATFORM=xcb` on Wayland |

## Changes compared with the wx app

New features and deliberate behaviour differences of the Qt app. Shared
engine changes that also change the wx app are under "Bug fixes".

### Window, panes and tree

* **The window fits the screen.** wx's minimum width was ~2100 px; the Qt
  minimum is ~700 x 535. Panes scroll below their content's size, the
  restored geometry is clamped to the screen, off-screen floating panes are
  moved back.
* **Tool windows stay on top** of the main window (Measurement, Analyze,
  Noise Reduction, ... are `Qt::Tool` windows that also stay visible while the
  app is inactive), so clicking in the view no longer sends them behind.
* **Legend drop-down** (`LegendMenu`) on the render view replaces the floating
  Legend dialog: rows / Show All / Hide All, the menu stays open; legend
  clicks redraw only the overlays (no brick-loop restart).
* **Components & Tracking menu entries**: wx has every entry commented out
  (the dialog is unreachable); the Qt app adds "Components && Tracking..." to
  the Analyze drop-down, the Tools menu and the tree's context menu.
* **Layers can be dragged between views**: a volume / mesh dropped on another
  view's tree item, group or layer moves to that view (wx keeps every drag
  inside one view).
* **A `.vrp` dropped on the render view opens the project** (wx refuses it
  there: "For project files, drag and drop to regions other than render
  views.").
* **Plugin panes** dock beside the Workspace column (wx: stacked under
  Workspace / Record), start at most a third of the window wide and are
  tabbed with each other, so they never widen the window.
* **After deleting the last item of a group the previous sibling is
  selected** in the Workspace tree (the scroll position is kept, as in wx; see
  "Workspace tree jumped to the top" under Bug fixes).

### Render view

* **Meshes stay in every frame of a multi-frame streaming pass** (wx draws
  them only in the pass's first frame).
* **The Qt-only "R resets the view" key is removed** (wx has no R binding).
* **View keys work whatever pane has the focus** (as wx's global key
  polling), except while a text box / spin box / editable combo has it; each
  key acts once per press.
* **With several views, frame-level key actions run once, for the active
  view** (d / space / a, s / w, Alt+V, Ctrl+B, Ctrl+Z / Ctrl+Y, f / l, Esc);
  wx ran them once per view (two views stepped the movie twice).
* **s / w with two or more linked axes** step each axis by its own slab
  width (wx read the width back from the previous text box).
* **Ctrl+B** runs Measurement "Apply Transform" with the dialog's current
  transform (wx sends it to the Fiji Interface plugin).
* **Captures at sizes other than the view size are always tiled** (wx: only
  when both sides > 4000).
* **Volume shadow depth map is drawn without depth peeling** (in Depth mode
  with meshes wx keeps the peel, the shader then fails to compile and the
  brick loop never ends).
* **Pen pressure** from Qt tablet events on every platform (wx reads Wintab on
  Windows only, and that code is commented out).
* **Virtual sphere rotation** (new; **on by default**, Settings ->
  Rotations "Virtual sphere: drag near the edge to roll (Z).", saved as
  `/sphere rotation`): within half the view radius (R = half the shorter
  side) a left drag turns exactly like the wx trackball (same speed and
  direction). Further out, the part of the drag that goes around the centre
  turns the data about the view axis (roll), following the mouse angle 1:1;
  its share rises smoothly to 1 at R, so near the rim a drag around the
  centre only rolls. A drag towards or away from the centre always turns
  like the trackball, also beyond R (the left / right sides of a wide view).
  Off = the wx trackball; "45 Increments" always uses the wx trackball.
* **Reduced sampling rate while dragging stays on until the mouse rests**
  (Settings "reduce sample rate during mouse interaction"): wx returns to full
  quality after one frame, so every short pause in a drag started a
  full-quality pass (~2x the slices) and the drag went stop-and-go. The Qt
  app returns to full quality only after 300 ms without input
  (`VVD_INT_SETTLE_MS`), also after the button is released.

### Measurement

* **Start Volume / End Volume columns** (new): the volume each ruler click
  hit is recorded per point; the list shows it for the first and last points
  (a locator fills Start only; a View Plane click leaves it empty). Saved as
  an extra `volume=` key per ruler point (wx ignores it and still opens the
  files); the text export appends `start_volume` / `end_volume` to each line
  (existing columns and the BigWarp CSV format unchanged). Column order: ID
  (wx "Name", half width, in-place rename), Start Volume, End Volume, then the
  wx columns (Color, Length, Angle/Pitch, Start/End Points, Time,
  Volumes/Score).
* **Auto-select** check box (new, off by default, saved as
  `/ruler auto select`): each ruler click, Edit-mode drag and Shift-paint
  ruler press first selects in the tree the displayed volume with the
  brightest voxel on the click ray (Maximum Intensity on the value after the
  transfer function, inside the clipping planes, across all groups; ties keep
  the current volume) and places the point on it with the pointing method
  (View Plane: on the view plane, the volume still recorded). Properties and
  Clipping follow the selection. No hit = the normal behaviour.
* **Transform "Cells: shape preserving"** (new): applies BigWarp landmarks to a
  segmented (label or binary) volume without distorting the cells. A thin
  plate spline that matches every landmark exactly can stretch, shear or fold
  the space between landmarks a few micrometres apart that want different
  displacements, so a compact cell body comes out as a streak or breaks up.
  The cell warp fits the same moving → target TPS (with the dialog's
  stiffness), splits the volume into objects (26-connected components or one
  object per label value) and places each object with its own local
  transform: **Adaptive** (the TPS displacement smoothed over the object with
  a width set by the local thickness, so cell bodies move as a rigid block
  while thin fibres still follow the TPS) or one **Translation / Rigid /
  Similarity** transform per object. Every object keeps its shape and volume;
  nearest neighbour, label values kept. A results window lists per object how
  much the TPS wanted to deform it, folded samples and nearby landmark error,
  and per landmark a leave-one-out error (to find conflicting landmark
  pairs), each saveable as CSV, plus warnings. Options: rigidity factor,
  minimum smoothing, neurite radius, samples per object, minimum object size,
  landmark search radius, CPU or OpenCL GPU. It is a C++ port of the author's
  own, not yet published Fiji plugin `Apply_BigWarp_RigidCells`, checked voxel
  for voxel against it (see "Cell (shape preserving) warp").
* **"Target:" output grid for Apply Transform** (new; BigWarp's target
  image): wx always writes the warp result on the current volume's own grid,
  so warping a small moving volume toward a larger target volume put its data
  outside its own box and the result was empty. **Auto** (default) uses the
  volume the rulers end on, **Same as source** is the wx behaviour
  (byte-identical), or any shown volume. The result gets the target's size,
  spacing and position with the source's bit depth, for every transform
  including Cells (see "Warp output grid").
* **Delete removes the selected ruler** (wx deletes by display name, which can
  remove several rulers that share a name).
* Ported wx behaviour that had been missing in Qt: annotation rows in the list
  and the text export, Shift-paint ruler mode.

### Panels, tools and projects

* Annotation "Number of Particles" is counted after the new Min Score is
  applied (wx showed the previous value's count).
* Opening a project first closes the further render views (wx's code for that
  is commented out, so wx kept adding views and filled the last one with
  view 0's data).
* Plugin keys are written to projects only for plugins whose pane exists (wx
  writes them, and creates the `_files` folder, for every plugin), so
  projects saved without using a plugin are byte-identical to before.
* Neuron Annotator: the setting file stores each segment's actual mask state
  (wx wrote its own copy, which missed changes made with the view's menu).
* NBLAST: R runs as a child process with its output in a progress dialog
  (Cancel kills it) instead of a Terminal window; every selected database is
  checked for existence (wx skipped the last).
* Fiji Interface: the server listens on loopback only; a connection times out
  only if Fiji has not connected within 35 s (wx killed Fiji whenever the
  connection took 15 s, even when connected by then); see "Fiji Interface"
  for the wire-level differences.
* Cells: the Translation local mode is a pure translation (the original
  plugin also fitted a rotation; see "Cell (shape preserving) warp").
* **Properties pane separator lines**: a vertical line between the left
  (Gamma ... Shading) and right (Extract Boundary ... Colormap) parameter
  columns, a horizontal line above the bottom row (Max Value, Color, spacing,
  scale bar, mask, group) and a vertical line before the Inv / Index / MIP /
  Smoothing column, with narrower gaps than before so the pane is no wider.

### Settings files (both apps)

* **Settings are saved in the per-user folder, never inside the app**: macOS
  `~/Library/Application Support/VVDViewer/`, Windows `%APPDATA%\VVDViewer`,
  Linux `$XDG_CONFIG_HOME/VVDViewer` (default `~/.config/VVDViewer`); one
  folder shared by the wx and Qt apps. The copies in the app are read-only
  factory defaults. See "Settings files" under Technical notes.

## Bug fixes

### Shared engine (affects the wx app too)

* **Graphics-memory setting ignored**: the Settings value never reached the
  Vulkan device, so the texture cache could grow to the whole GPU heap; on
  large projects this exhausted RAM and the view never finished drawing.
  The limit applies while streaming is on (the Settings dialog greys the
  value out otherwise); with streaming off every brick must stay resident,
  so the device maximum is used, as in wx.
* **Black 3D view in the wx app** (regression from `495e8b5`, "2 frames in
  flight"): after a volume loaded, a swapchain recreation left the MoltenVK
  drawable at 1x1 and every acquire returned `VK_SUBOPTIMAL_KHR` without the
  swapchain ever being rebuilt, so only the background colour showed.
  `prepareFrame()` now rebuilds the swapchain on a suboptimal acquire (with a
  wait on the acquire semaphore first, so it is not left signalled);
  `suboptimalPersists` stops a loop when the rebuild cannot help.
* **Settings saved inside the app bundle** (both apps): `fluorender.set`, all
  `default_*.dft`, `catalog_list.set`, the plugin settings files and the brick
  download cache were written to `Contents/Resources` (next to the exe
  elsewhere). The first save broke the code signature, and saving failed when
  the app was read-only or in `/Applications` without write permission. Now
  written only to the per-user folder (see "Settings files").
* **wx dropped the Qt app's settings**: wx's save rebuilt `fluorender.set`
  from scratch, so the Qt-only groups (`[rigid cells]`, `[warp target]`,
  `[ruler auto select]`, `[sphere rotation]`) were lost. Both apps now start
  the save from the existing file and keep keys they do not know.
* **Kernel panic on Apply Transform (TPS / affine / similarity)**:
  `VolumeRenderer::warp` ran entirely between frames, but released textures
  are only destroyed when the next frame completes, so every source tile and
  every texture evicted from the brick cache stayed allocated until the warp
  ended. Volumes wider than the 2048 3D-texture limit take the tiled path,
  whose tiles were capped per axis only (up to 2048³ voxels). With several
  large volumes loaded the process wired ~104 GB and macOS panicked in
  IOGPUFamily (`IOGPUGroupMemory.cpp:220`). Now `VulkanDevice::
  DrainDeferredDestroys()` (wait idle + run the deferred destroys) runs at the
  start, after each tile / brick and at the end, `TrimUploadStaging()` trims
  the staging buffers, each source tile is capped at a quarter of the
  graphics-memory limit (256 MB – 1 GB, regions split up to 1024 per brick)
  and the whole-source texture is used only when it fits the cap. Output is
  byte-identical; memory stays bounded (growth was ~26–70x the volume size).
* **Colocalization always empty**: the shader bound volume A's label texture
  where volume B's mask was expected (since commit 27d5cb2, 2020).
* **Noise Removal removed nothing** (`VolumeSelector::NoiseRemoval`): the
  FLT_NR filter writes the data bricks but `draw_mask` marked only the mask
  dirty, so `return_volume` never read the filtered data back (the `_NR`
  volume was an unchanged copy); it now marks the component it wrote. The
  filter also only lowered a voxel to a value `> 0`, so noise surrounded by
  exact-zero background was never removed; the test is now "a lower neighbour
  was found" (`>= 0`).
* **Hide Inside** showed a speckled layer instead of a clean cut-out, and in
  Depth mode used a wrong mask transform for multi-brick volumes (see
  "Streaming engine").
* **Meshes cut off by their clipping planes** (`FLIVR/glm.cpp`):
  `glmBoundingBox` looped over vertices 1 .. n-1 of the 1-based arrays, so a
  mesh whose last vertex was an extreme one got too small a box.
* **Random mesh colours changed between launches**: the generator was the
  unseeded `rand()`, also used first by a system thread; now a deterministic
  generator (minstd_rand0 seeded with 1, i.e. what macOS `rand()` gives).
* **Streaming could stall**: when a frame's fixed overhead exceeded the time
  budget no brick was drawn and the loop never advanced; every frame now draws
  at least one brick.
* **Depth-mode capture tile hang**: a tile in which every brick is culled or
  provably empty never ended its brick loop (`MultiVolumeRenderer::draw`
  returned before marking it done), so a 6x capture of a large volume hung;
  the loop is now marked done there and the target cleared, as
  `VolumeRenderer::draw_volume_ray` does (else the tile showed the previous
  tile).
* **"Skip empty bricks" setting** did nothing; it now computes the brick
  maxima at load time (see "Streaming engine").
* **Crash in the label-volume rescale** (`NrrdScaler::Run`): it waited for its
  worker threads with a counter the threads incremented themselves; when none
  had started yet the wait ended at once and the threads then used the
  destroyed scaler (`recursive_mutex lock failed`). The counter is now
  incremented before each thread starts.
* **Null-mask crashes** in `VolumeSelector::CompExportRandomColor` /
  `CompExportMultiChann` when `!select` (the 4D-script random_colors task after
  noise_reduction crashed).
* Fixes from the port's phase-1 review: miniz FILE handling on Windows,
  C-locale INI parsing, curl timeouts, `%f` formatting in annotation text
  files, hidden-file filter in zip extraction.

### Qt port

* **Transparent meshes in Depth mode hid the volumes behind them**:
  `DrawVolumesDP` draws the volume in slabs between the mesh's peeled depth
  layers and calls `StartLoopUpdate(false)` between slabs; the Qt
  `StartLoopUpdate` ends with the camera-only modelview (wx leaves the full
  camera + object matrix), so every slab after the first was ray-cast without
  the object transform and came out empty. The frame's modelview /
  projection are now kept across that call; volumes in front of, inside and
  behind a semi-transparent mesh show, attenuated by (1 − alpha) per mesh
  layer, matching wx; opaque meshes, no-mesh scenes and Layered / Composite
  are byte-identical to before.
* **Mouse-wheel zoom up to 12x faster than wx on macOS** (2x on a trackpad):
  both apps add wheel/1000 to the zoom, but on macOS Qt's `angleDelta` reports
  120 per wheel-mouse event and 2x the pixel delta for trackpads, while wx
  reports lines x 10 and the raw pixel delta; since the slice count grows with
  zoom the rendered slices shot up too. `RenderCanvas::WxWheelRotation()`
  returns what wx would report (macOS: `pixelDelta`, halved for a wheel mouse;
  elsewhere `angleDelta`) for the camera zoom and the brush-size wheel; the
  zoom slider / text / spin / reset request an interactive redraw like wx.
* **Workspace tree jumped to the top** on every rebuild (delete, visibility,
  rename, ...): the scroll value was restored before the rows were laid out;
  it is now restored after the selection and the layout (and once more after
  the pending layout).
* **Stop-and-go while dragging** with the reduced sampling rate on: see
  "Render view" under Changes (the interactive flag was cleared after every
  frame).
* Crash on the first frame when data was loaded before the window was exposed
  (the layer was not attached yet).
* The window could not fit the screen (minimum width 2102 px).
* Legend text half size and the name gaps too wide on Retina (see "HiDPI
  text"); the Legend menu cut off volumes added later.
* s / w (clipping planes), a / d (time step) and the other view keys worked
  only while the 3D view had the focus.
* X rotation mirrored in projects written by earlier Qt builds; projects saved
  from Qt dropped camera, scale-bar, view-adjustment and clipping keys.
* Tool windows fell behind the main window on macOS.
* 4D script: the frame check uses the volume's time instead of the reader's
  (wx: `reader->GetCurTime()`; prefetched time points left the reader time
  behind, so going back to the start frame was skipped).
* `SelLabelSegVolumeMax` mode 1 never returns (its loop over the active ids
  does not advance the iterator); fixed in the port (each active id set to 1
  once). wx only calls mode 0.

## Performance (compared with the original wx app)

Original = the wx VVDViewer before this branch. New = this branch's Qt app,
and the rebuilt wx app where noted. Apple M4 Max, 128 GB.

| | Original wx | This branch |
|---|---|---|
| Open a 243-volume NRRD project (2953×625×625 each) | ~33 s until idle (~24 s parallel decode + ~6 s follow-up) | **~21 s** (Qt) |
| Paint **Diffuse** stroke, 1 large volume | 3.65–3.72 s | **1.08 s** (Qt), 1.06–1.13 s (rebuilt wx): **~3.4x faster** |
| Paint **Diffuse** stroke, 4 large volumes loaded | 3.68–3.72 s | **1.18 s** (Qt): **~3.1x faster** |
| Paint **Select** / **Unselect** stroke | ~185 ms / ~190–230 ms | **139 ms / 69 ms** |

How:

* **Loading**: volumes are read and decoded on worker threads
  (`VolumePrefetcher` = wx `ProjectDataLoader` in prefetch mode) and finished
  on the GUI thread in file order; gzip NRRD is decoded with libdeflate
  (~4x faster per volume, byte-identical data; `VVD_NRRD_ZLIB=1` for A/B);
  the 16³ block-max grid for the empty-brick skip is computed in the loader
  workers.
* **Painting**: the painted volume's bricks stay resident on the GPU during
  the stroke, the diffusion dispatches are batched (one submit for many
  bricks), and bricks outside the stroke's 2D footprint are skipped once they
  can no longer change (A/B switches `VVD_SEG_NO_PIN`, `VVD_SEG_NO_BATCH`,
  `VVD_SEG_NO_SKIP`, `VVD_SEG_VERIFY_SKIP`).
* **Drawing**: bricks are skipped only when they provably cannot change the
  image (exact empty-brick skip), texture reuse waits only for the frame that
  last used a texture, and two frames are in flight (`VVD_FRAMES_IN_FLIGHT=1`
  restores the fully serialized behaviour).
* The first full draw of the 243-volume project took 34 s in the Qt app; it
  was **not timed in the original wx app**, which has the graphics-memory bug
  described under "Bug fixes".

Verification: renders are byte-identical to the previous build in Layered,
Depth and Composite whenever the new features are off; opening and saving
wx projects from Qt loses no wx key and the reopened view renders
byte-identically; Thin Plate Spline output is byte-identical to the wx app.

## Known issues / not done

* **Windows and Linux are not built or tested** (macOS arm64 only); CI is
  recommended (including the toolkit-free engine compile check).
* **Wayland**: untested; run with `QT_QPA_PLATFORM=xcb`.
* **A Depth group, a normal group and a mesh** in a Layered or Composite view:
  the normal group disappears. A direct port of the wx code, so wx may behave
  the same.
* **Zarr**: a hand-made test file loads but renders black; no real Zarr data
  was available to check against.
* **Remote NBLAST upload** (`runNBLASTremote`) is untested (no server); wx
  has no UI calling it either.
* **Double click on a ruler point**: the animated move centres the point
  horizontally but moves it away from the centre vertically (the wx formula,
  kept; the Vulkan projection is y-down).
* As wx: trace drawing (`DrawTraces` body commented out in wx) draws nothing;
  "Convert to: Rulers" in Components & Tracking adds no rulers
  (`TrackMapProcessor::GetMappedRulers` is commented out); trace groups are
  not saved in projects.
* `Formats/png_resource.h` / `img/` are wx bitmap arrays, used by the Qt app
  through `IconFromPng`; a `.qrc` would be the Qt way (cleanup only).

---

# Technical notes

## Window and layout

* **Menus and panes** = the wx frame: wxAUI layers emulated with a nested
  `QMainWindow`. Menu bar = wx m_top_menu: File (Open Volume / Mesh /
  Project, Save Project, Open URL, Quit; About in the macOS application
  menu), Tools (Analysis Tools ... Convert, Settings), Windows (Show/Hide
  Toolbar, Show/Hide UI, Datasets, the six panes as check items, Open New
  VVDViewer, Full Screen except on macOS). Show/Hide UI (`MainWindowUI.cpp`)
  = wx ToggleAllTools / m_tb_menu_ui: the button hides all panes remembering
  which were shown and brings exactly those back; its drop-down has Toggle All
  Panels + one check item per pane; check marks follow the real pane
  visibility (title-bar close included). Projects save / restore /ui_layout.
* **Window size** (`MainWindow::FitToScreen`, MainWindowLayout.cpp, after
  restoreState and once shown): the restored geometry is clamped to the
  screen's available area, floating docks restored off-screen are moved back.
  The old minimum width (~2100 px) came from the Properties volume page
  (1708 px) + the left column (393 px), the render view control rows (1073 /
  914 px) and the plugin panes (NBLAST 947, Fiji 514). Now ~700 x 535: the
  Properties pages, the render view's top and bottom control rows and
  Record/Export scroll horizontally below their content's width, Output
  Adjustments / Clipping Planes vertically, plugin panes both ways
  (`PaneScrollArea.h`: no frame, no scroll bars and the content's own size
  hint at or above its minimum, so the layout at the usual sizes is unchanged
  — window pictures at 2102x900 are pixel-identical to before). Without a
  saved state the left column starts at max(320, Record/Export's width).
* **Tool windows** (`MainWindow::MakeFloatingTool`): `Qt::Tool` +
  `WA_MacAlwaysShowToolWindow` (macOS hides tool windows of an inactive app;
  wx floating panes stay).
* **HiDPI text**: on Retina the Qt swapchain images are physical pixels (Qt's
  CAMetalLayer has contentsScale 2; the wx one keeps 1) while overlays lay
  text out in logical units. `RenderCanvas` passes the logical size to
  `TextRenderer::SetSwapchainLayoutSize`; glyphs are rasterized at size x
  scale on swapchain images, so text has the wx apparent size and
  `RenderTextDims` stays logical (legend, Info, scale bar, ruler, colormap,
  annotation and brush labels). The wx view never sets it.
* **Legend drop-down** (`QtGui/LegendMenu`): QMenu caches its action rects, so
  `LegendMenu::Reload` re-lays the menu out when the list changes size and
  before it is shown. Legend clicks call `RenderCanvas::RefreshOverlayChange`
  (no brick-loop restart; a full redraw only when no frame is on the
  surface). Each view's menu acts on that view only.
* **Workspace tree** (`TreePanel`, full wx DataTreeCtrl port): view → volume
  groups / meshes / mesh groups / annotations / segment trees (volumes) and
  SWC segment trees (meshes, built on expansion as wx), wx bulb icons,
  visibility history (PushVisHistory / Undo / Redo, 10 steps),
  ShowAllDatasets / HideOtherDatasets / HideSelectedItem, single selection,
  toggle (double click / Enter / button), rename, Delete key, every wx context
  menu, drag & drop as wx OnEndDrag (a volume dropped on the view or empty
  space gets a new group through OrganizeLayers). Real layer list in
  `RenderCanvasLayers.cpp` (wx m_layer_list API); groups drawn per group with
  their blend mode. Scroll position: restored after the selection and the
  layout (see "Bug fixes").

## Rendering

* **Render modes** (`RenderCanvasModes.cpp`): Layered / Depth (multi-volume +
  mesh depth peeling) / Composite, depth attenuation, quality; volumes, meshes
  and groups as wx, the wx mask pass of DrawVolumesComp in every blend mode and
  in the streaming loop.
* **Meshes during multi-frame streaming passes**: Layered / Composite keep the
  first frame's mesh image as the base of the first volume channel
  (`SaveFinalToTemp`), Depth composites the mesh layers of the unfinished
  slabs behind the presented frame (`DrawDPMeshPreview`).
* **Transparent meshes in Depth mode**: the volume is split into
  peeling-layers + 1 slabs between the mesh depth layers and each slab is
  composited behind the mesh layers in front of it (as wx); see "Bug fixes"
  for the modelview fix in `DrawVolumesDP`.
* **Volume shadows** (wx DrawOLShadows from DrawOVER / DrawMIP /
  DrawVolumesMulti): depth map of the shadow volumes, gradient shifted by the
  Settings shadow direction, fb darkened by the shadow intensity; while
  streaming a brick loop of its own, so (as in wx) only with memory swap on.
  The depth-map pass is drawn without depth peeling (deliberate). Mesh shadows
  use the Settings shadow direction too (was 0).
* **Overlays** (`ViewOverlays`): Center, Info, Legend, scale bar, colormap,
  annotation labels (`DrawAnnotations`); an annotation layer that carries a
  mesh (the `Mesh:` line of its file) is drawn as that mesh, ADDed with the
  layer's alpha (`RenderCanvas::DrawAnnotationMesh`). Settings gradient
  background (wx DrawGradBg).
* **Capture** (`CaptureDialog` = wx OnCapture save dialog + Additional
  Options, project save; `RenderCanvasCapture.cpp`): tile rendering, PostDraw
  readback, group capture, the wx TIFF writer (`CaptureWriter.h`). Sizes other
  than the view size are always tiled.
* **Test modes** (Settings [test mode], wx VRenderFrame ctor): speed = the view
  redraws on every idle; param = the Tester dialog (wx TesterDlg: a "Click Me"
  button, all handlers commented out); wiref = m_vol_test_wiref, m_draw_bounds
  (DrawBounds is empty in wx) and the grid (wx DrawGrid).

## Streaming engine (shared FLIVR / Vulkan code)

* The wx mem-swap loop is ported (`RenderCanvasStreaming.cpp`).
* **Empty-brick skip (always on).** `VolumeRenderer::invisible_brick_max`
  derives, from the VRay shader's transfer function, the largest brick
  maximum for which every sample of the data pass is exactly `vec4(0)`
  (below `lo - boundary`, above `hi + boundary` when inverted, or all-zero
  with `1/gamma >= 1` and `offset > 0`; not in solid, depth-map, index,
  mask/label, NA-label or highlight modes, nor for pyramid data). Such bricks
  are counted as drawn but never uploaded or rasterized; since a zero fragment
  leaves the OVER / MIP / ADD blends bit-identical, captures are
  byte-identical (checked in all three render methods, rotated, inverted, MIP,
  shading on/off, colormap, fog, thresholds, gamma). Brick maxima come from a
  16³ block-max grid computed right after decoding (`VL_Nrrd::computeBlockMax`,
  in the loader workers) or, failing that, from a scan on first use; they are
  cached on `TextureBrick`, survive transfer-function changes and are dropped
  when the data change (`set_modified` / `set_dirty` /
  `eraseBricksFromTexpools` / new buffer). `VVD_NO_BRICK_SKIP=1` disables it.
* **"Skip empty bricks" setting**: computes the maxima at load time and gives
  all-zero bricks priority 1, which are then never drawn even where the exact
  test would draw them (legacy FluoRender behaviour; may differ visually with
  gamma > 1 or inverted data).
* **Progress guarantee.** `TextureRenderer::frame_budget_exhausted()` only
  reports a used-up budget once the frame has drawn a brick (volume,
  multi-volume and mask/label passes, wx and Qt canvases).
* **Texture reuse.** Overwriting a pooled brick texture no longer drains the
  queue: textures carry the serial of the last frame using them, idle
  textures are preferred, else the least recently used one, waiting only for
  its frame's fence; a drain remains only when every candidate is used by the
  frame being recorded.
* **Noise removal**: on zero-background data `_NR` equals the Noise Reduction
  dialog's Erase result voxel for voxel; on data with a non-zero background
  the noise is lowered to the surrounding values (wx algorithm).
* **Hide Inside.** The ray caster skipped samples whose interpolated mask was
  > 0.5, which leaves half-voxel shells of every surface inside the hidden
  region. For intensity data the sample is now the trilinear interpolation of
  data * (1 - mask) (8+8 texel fetches, also for the gradient), i.e. what the
  Erase volume shows (`VVD_HIDE_INSIDE_LEGACY=1` restores the old test). In
  Depth mode the MultiVolumeRenderer passed an identity mask transform and
  overwrote the gradient's z step (only right for single-brick volumes); it
  now uses the brick box as VolumeRenderer.
* **Warp memory**: see the kernel-panic fix under "Bug fixes";
  `VVD_WARP_DEBUG=1` prints tile counts and the memory footprint.

## View controls and keys

* FreeFly, 45° increments, rotation link (also Settings "Link all rendering
  views' rotations" = wx VRenderView::OnRotLink(bool)), Save as Default,
  Output Adjustments applied; clip planes drawn and applied to meshes; camera
  = wx quaternion trackball (degrees). `ViewPanel`: top options bar, depth-int
  / zoom columns, rotation bar.
* **Keyboard** = wx VRenderVulkanView::OnIdle key polling
  (`RenderCanvasCamera.cpp` ViewKeyFilter / OnViewKey): an application-wide
  event filter, so the keys work whatever pane has the focus (wx reads the
  global key state whenever any window of the app is focused) except while a
  text box / spin box / editable combo has it; keys are never consumed; each
  acts once per press (auto-repeat ignored, the wx latches).
  * Ctrl+arrows pan; d / space and a step the time sequence; V held hides the
    masks; Esc ends tile rendering.
  * **s / w move the linked clipping planes** (ClipPanel::
    MoveLinkedClippingPlanes): only axes whose Link is checked move (nothing
    when no axis is linked, as wx); s = up / dir 1, w = down / dir 0; the step
    is the slab width x2 - x1, clamped at 0 and the box size with the slab
    kept.
  * Alt+V = wx CopyLevel(cur vol) of a pyramid volume (the copy is discarded
    as in wx) + selector volume. Ctrl+B = Measurement Apply Transform.
  * f / l = Components & Tracking CellFull / CellLink(false).
* **Clip sliders**: ',' / '.' on a focused slider move its linked slab (wx
  OnSliderKeyDown); a right click = wx OnSliderRClick (low thumb: x2 = x1 + 1,
  high thumb: x1 = x2 - 1, the axis linked, the other two unlinked and reset;
  as wx the new value in box units is compared with the voxel size). The
  Clipping Planes pane saves its defaults on exit (wx ~ClippingView; not in
  scripted runs). Hovering the V. AOV slider shows the clipping planes (wx
  OnAovSldrIdle, 100 ms poll; not with "Display Planes" or while capturing).
* View default `mouse_focus` (no control, as wx) is read / written: when set,
  1:1 / Reset Zoom / Reset Rot. give the canvas the focus back. "Intrp." is
  view-wide (wx SetIntp: pushed to every shown volume in StartLoopUpdate).
  macOS: a clipping slab of <= 3 voxels (one shown volume or Sync All Chan.)
  puts the volume renderers in slice mode (wx StartLoopUpdate _DARWIN).
* **Double click on a ruler point** (any mode) = wx StartManipulation: a
  6-frame animated move (300 ms of streaming per frame, coarsest pyramid level
  while moving) to the object translation Point(cx - px, -(cy - py), -(cz -
  pz)) (wx formula; see "Known issues").
* **Context menu** (wx OnContextMenu): Show all, Show only selected, Hide
  selected, Join / Split selected segments, Undo / Redo visibility (NA-mode
  volumes: Show all / Deselect all / Show only selected / Hide selected
  fragments); a right click opens it, a right drag zooms without opening it
  (wx opens it on every right release).

## Render views

`MainWindowViews.cpp`.

* **Where views come from (as wx).** wx creates further views only in
  OpenProject (/views/num > 1 → CreateView); "Open New VVDViewer" starts a
  new process. The tree's view item has "Close" for every view but the first
  (wx OnCloseView → DeleteVRenderView: the layers leave the view, the data
  stay in the DataManager). `MainWindow::CreateView` / `DeleteView` are public
  (tests).
* **Layout** = wx CreateView: view 0 is the centre of the nested
  `QMainWindow` (`m_view_area`, wxAUI layer 0), every further view a dock to
  its right (floatable, no close button, floating size 600 x 400, min 300 x
  200) split left-right with equal widths (OrganizeVRenderViews(1)); Output
  Adjustments / Clipping Planes stay outside (layer 1).
* **Per view** (wx members of VRenderVulkanView): canvas (camera, layer list,
  overlays, clipping state, blend mode, streaming loop), ViewPanel bars,
  BrushTool, RulerTool (+ ruler list), ViewOverlays, VolumeSelector,
  VolumeCalculator, trace group. New views take the Settings render values
  (peeling layers, micro blending, mouse interactivity, gradient background,
  shadow direction, ruler point mode / transform / time dependence, font,
  wireframe test mode) and the Output Adjustments defaults; a view created
  while the Settings rotation link is on joins it.
* **Active view.** The panes follow the view of the tree selection or the view
  last clicked: Properties, Output Adjustments, Clipping Planes, Measurement,
  the brush dialog, Components & Tracking, the Analyze dialogs
  (`AnalysisContext` = the active view's canvas / selector / calculator).
  Record/Export records the view of its own "Capture" combo (wx m_views_cmb;
  `MoviePanel::ResolveView`). Open Volume / Mesh / URL load into the first
  view (wx GetView(0)); a drop on a view loads into that view.
* **Keys**: every view polls the keys (Ctrl+arrows pan each view, V hides
  every view's masks); frame-pane actions run once for the active view
  (`RenderCanvas::SetKeyCanvas`). Linked rotations act on every view.
* **Tree**: one "Render View:<n>" item per view (`TreePanel::SetCanvases`,
  `CanvasOf(item)`); drags into another view are a Qt extension (see
  "Changes"). View names: n = highest in use + 1 (wx counts every view ever
  created).
* **Vulkan.** wx creates a VVulkan per view while the engine's static
  renderer state points at the last one; the port shares view 0's context:
  `VVulkan::beginSurface / finishSurface / activateSurface / removeSurface`
  keep one swapchain (+ frame buffers, present semaphores, depth buffer, size,
  window) per view and swap it into the VulkanExampleBase members before a
  view draws (shared code; the wx app does not call it). The static brick-loop
  state of TextureRenderer (start / done_update_loop, brick counters, time
  budget) is saved / restored per canvas (`RenderCanvas::MakeCurrent`), so two
  views stream independently.

## Projects and files

* **Projects (.vrp)** (`ProjectIO`): wx-compatible format, all volume + mesh
  properties, every view (/views/num, /views/<i>/layers, /views/<i>/properties
  incl. painting parameters and unmodelled keys kept, /views/<i>/rulers, in the
  wx order; group ids reset once before the views), real volume / mesh groups,
  annotations (`/data/annotations/N` + type-4 layers; a `.txt` is written to
  `<project>_files` for annotations without a file). Every
  `/views/0/properties` key wx writes is filled / applied as
  VRenderFrame::SaveProject / OpenProject (`MainWindow::CaptureProjectView` /
  `ApplyProjectView`, wx order): camera translation / rotation (wx X
  convention, 360 - m_rotx) / centre / centre-eye distance / radius / init
  distance / zoom, object centre / translation / rotation, persp, free fly,
  AOV, near / far clip, min_dpi, drawtype, overlays, scale bar, view 2D
  adjustment, st / ed / cur_time, view clipping (mode, rotations, sync,
  distances, links, linked params, fix parameters); `/prop_panel` = wx
  m_cur_sel_type / _vol / _mesh + plane_mode + the legacy chann_link keys;
  `/memory settings` written from the Settings (not applied on load: wx has
  that code commented out); the "saved by a newer version" message; keyframes
  (/interpolator), /movie_panel, /brush_diag, plugin state. `/data/volume` and
  `/data/mesh` in DataManager order; doubles written like wxConfig ("%g").
  Ruler points carry the Qt-only `volume=` key.
* **Round trip of a wx project** (Qt open + save): identical keys and values
  except `translation` / `centereyedist` (recomputed by the first frame, as
  wx), `/memory settings` (current settings, as wx), the order of
  `/tree_panel/exp_states` (wx: an unordered map) and dropped
  FLKEY_TYPE_INT keyframes (wx skips them too); the Qt save is a fixed point
  and renders identically. With one view the files are byte-identical to the
  previous Qt build.
* **Open Volume**: every `DataManager` format, multi-select. Dialog = wx
  OnOpenVolume + CreateExtraControlVolume (`FileDialogs`): Z-slice /
  time-series sequence (exclusive, session), compress data and skip empty
  bricks (settings), sequence identifier; the sequence options apply to every
  load (command line, drag & drop) like wx LoadVolumes. Volumes are decoded in
  parallel and finished in file order (all load paths). A volume file's
  companion `<name>.msk` / `<name>.lbl` are loaded with it (wx m_load_mask).
* **Zarr / N5** as wx LoadVolumes: a `.zarr` / `.zgroup` / `.zattrs` or `.n5`
  / `.json` root expands into its datasets (`BRKXMLReader::
  GetZarrChannelPaths` / `GetN5ChannelPaths`, `<dataset>.zfs_ch` / `.n5fs_ch`,
  the opened `.zattrs` / `.json` as metadata file); several Zarr datasets and
  every N5 root go through the wx DatasetSelectionDialog
  (`FileDialogs::SelectDatasets`: unchecked rows, a click toggles, Select All
  / Deselect All); Open URL on a Zarr / N5 folder URL loads its `.zattrs` /
  `attributes.json`.
* **Save dialogs** (`FileDialogs`): Save / Save segmented / Bake volume +
  Export metadata (.vvd) with the wx options (metadata as an individual file,
  crop to the clipping planes, pyramid scale level; session), Save Project
  with embed / compression. Open Mesh / Open Project have no options in wx.
* **Meshes** (`MeshIO`: obj / swc / ply / nml / csv): Open Mesh, start-up and
  drop put several mesh files (or a folder) into one new mesh group, an `.nml`
  into a group of its own (wx LoadMeshes).
* **Drag & drop** (wx DnDFile::OnDropFiles): a `.vrp` as the first file opens
  that project only — on the window and, unlike wx, on a render view (same
  code path; a later `.vrp` is ignored); files sorted into volumes / meshes by
  the wx suffix lists (others ignored); folders expanded (their files by
  suffix, or the folder as an N5 dataset when it has an
  `s<N>/attributes.json`; a folder's meshes go into a mesh group named after
  the folder); each batch sorted.
* **Start-up** (wx Main.cpp / StartupLoad): `-d/--desc` name prefixes,
  `-m/--macro` tasks (`hideui`, `showui`, `basic`, `advanced`,
  `record,<file>`; wx SetTasks + OnTimer at 100 ms, the window closes when
  done), `vvd:path1,path2` arguments, `-p/--plugin "<name> <options>"`, macOS
  QFileOpenEvent (Finder / `open -a`, vvd: URLs); a `.vrp` as the first file
  opens only the project. On exit the curl handles are released and the
  cached remote-brick files deleted (`TextureBrick::delete_all_cache_files`).
* **Settings** (`SettingsDialog`): all SettingDlg pages, the same
  `fluorender.set`. Scripted runs (`TestRun.h`) never write the settings file,
  the window state or the default_*.dft files.

## Settings files

* `Core/FileUtil` (toolkit-neutral, used by both GUIs and all plugins):
  `vvd::GetUserSettingsDir()` (macOS `~/Library/Application Support/VVDViewer`,
  Windows `%APPDATA%\VVDViewer`, Linux `$XDG_CONFIG_HOME/VVDViewer` or
  `~/.config/VVDViewer`; follows `$HOME`; `VVD_SETTINGS_DIR` overrides it),
  `vvd::SettingsReadPath(name)` (the user folder, then the factory copy in
  `Contents/Resources` / the exe folder, then on Windows / Linux the legacy
  user config folder), `vvd::SettingsWritePath(name)` (the user folder,
  created when needed), `vvd::GetFactorySettingsDir()`,
  `vvd::GetUserCacheDir()` (macOS `~/Library/Caches/VVDViewer`; the brick
  download cache `vvd_cache/`).
* Files: `fluorender.set`, `default_volume_settings.dft`,
  `default_brush_settings.dft`, `default_view_settings.dft`,
  `default_2d_adjustment_settings.dft`, `default_clip_settings.dft`,
  `default_movie_settings.dft`, `catalog_list.set` (wx only),
  `fiji_interface_settings.dft`, `NBLAST_plugin_settings.dft`,
  `NBLAST_database_list.txt`, `NA_plugin_settings.dft`. Not moved (read-only
  or already per-user): fonts, Fiji macros / jars, `nblast_search.R`,
  `term.sh`, project-local plugin `.set` files, the Qt window state
  (QSettings), `~/.cache/VVDViewer/pipeline_cache*`.
* A bundle copy modified by an older build is still read (fallback) and moves
  to the user folder on the first save. Both apps' `fluorender.set` save
  starts from the existing file, so each keeps the other's keys; all other
  files use the same keys in both apps.
* Scripted Qt runs (`TestRun.h`) write nothing and do not create the folder.
  Verified with read-only bundle copies and a temporary `HOME`: files appear
  only in the user folder, the bundles stay byte-identical and
  `codesign --verify --deep --strict` passes.

## Panels and dialogs

* **Volume properties** (`VolumePropPanel`): all VPropView sliders / checks,
  colour, spacing. Save as Default = wx OnSaveDefault (the shown values →
  default_volume_settings.dft next to the executable / in the bundle's
  Resources, else the user config dir; same keys and order; also the
  DataManager m_vol_* defaults; scripted runs write it only when
  `VVD_QT_VOLUME_DFT=<path>` names one). Reset = wx OnResetDefault (gamma,
  boundary, offset, thresholds, low / high shading via SetMaterial, alpha,
  sample rate, luminance as the HSV value of the colour, colormap low / high,
  shadow intensity; Alpha / Shading / Inv / MIP / Smoothing / Shadow on the
  group when Sync is on). Enable states as wx: MIP disables alpha, right
  threshold, boundary and luminance (gamma / saturation too with a plain
  colormap) and renames "Threshold:" to "Shade Threshold:"; in Depth mode MIP
  is refused ("MIP is not supported in Depth mode."). Shadow, Smoothing and
  Sample Rate (and shadow intensity) go to every volume of the view in Depth
  mode, otherwise to the group (Sync, or Smoothing / Sample Rate / intensity
  of a group in depth blend) or the volume. Max Value (wx UpdateMaxValue):
  scalar scale 65535 / value for 16-bit, 1 / value for float, 1.0 for 8-bit;
  the box shows max(255 | 1, max) and float sliders run 0 .. max x 100.
  Index volumes: "Segment:" row (name, colour, display mode).
* **Mesh properties** (`MeshPropPanel`): Sync (wx MPropView::UpdateSync) = a
  mesh in a mesh group syncs its group, a view-level mesh the shown meshes in
  no group. **Mesh Manipulate** (`MeshManipPanel`, wx MManipulator, tree
  "Manipulate"): shown in the Properties dock ("Manipulations - <mesh>"), the
  previous mesh loses its box, m_cur_sel_type = 6; translation / rotation /
  scaling boxes ("%.2f" from GetData, "%.3f" after Enter or a spin arrow, ±1
  per arrow).
* **Annotation properties** (`AnnotationPropPanel`, wx APropView): memo +
  Update (read-only and disabled with `Memo Update: 1`); for annotations with a
  mesh Transparency (0..255 → "%.2f"), Min Score (slider max = MaxScore + 1),
  "Number of Particles" (the Measurement count of the texts above the
  threshold), Color (mesh diffuse, ambient 0.3 x diffuse); without a mesh
  those controls are hidden (labels stay, as wx). The Clipping pane follows
  the annotation's volume / mesh.
* **Record/Export** (`MoviePanel` = wx VMovieView): Basic (rotation X/Y/Z,
  degrees, Linear/Smooth, rewind, time sequence / 3D batch with start / end /
  current and ±, movie length), Advanced (`RecorderPanel` = wx RecorderDlg:
  keyframe list with in-place Inbetweens / Interpolation / Description
  editing, drag reorder, Add / Delete / Del. All, Record volume properties),
  Auto Key (nC1..nC3), Cropping (frame drawn on the view, centre / size,
  Reset), footer (FPS, repeat, Capture view, play / pause, rewind, time slider
  / text, Save...). Playback and recording are the wx timer logic; a recorded
  step waits until the frame of the current state is completely streamed and
  read back. Save...: wx dialog + Additional Options (LZW, bitrate, estimated
  size, embed project), .mov / .mp4 (QVideoEncoder) or TIFF sequence, cropped,
  project saved with the movie when the setting is on. Engine side in
  `RenderCanvasMovie.cpp`.
* **4D script** (`MainWindowScript.cpp`, wx Run4DScript + task handlers): with
  Settings > 4D Script on and the file present, Play runs the script on every
  shown volume at the start and once per new time point (the timer stops
  meanwhile; playback ends after one pass); the view takes the run flag / file
  at each Set4DSeqFrame. Tasks: noise_reduction (component analysis threshold
  / voxel size, Calculate(6) without adding, saved as
  `<savepath><name>_T<t>.tif|.nrrd`, mask + label deleted),
  selection_tracking (labels of the previous time point under the mask,
  size_limit, the same values selected in the new .lbl; with a track map the
  view's trace group gives the mapped cells), random_colors (new .lbl,
  CompExportRandomColor, three `_COMP<i>.tif`), separate_channels,
  external_exe (empty in wx), fetch_mask (.msk / .lbl of the new time point).
* **Paint brush / selection** (`BrushTool` + `BrushToolDialog`): Select /
  Diffuse / Unselect / Solid strokes, undo / redo (Ctrl+Z / Ctrl+Y, depth =
  Settings > paint history), wheel brush size, Reset All, Erase, Extract,
  Calculations (types 1-4, 9), component analysis + exports, noise analysis /
  removal (result in a new group), brxml operands copied at their mask level
  (wx CopyLevel), "Enhance selection" (mask colour HDR boost x Mask
  Intensity), the selected volume's own image in `m_fbo_wt2` for the brush's
  2D weight, Hide Outside / Inside, shared mask / label volumes
  (`SetVolumeLookup`). The brush dialog exists from start-up (holds the NR /
  CA defaults, saved on exit). Noise Reduction Erase = wx BrushErase:
  Calculate(11) with the NA-mode label volumes of the group for a volume in
  NA mode, else Calculate(6). Fill Holes (type 9) marks A > threshold x 255
  plus the enclosed holes, binary 0 / 255.
* **Index and NA-mode volumes**: click = Pick (PickVolume / SelSegVolume(3),
  then PickMesh: a click on a mesh selects it / its SWC segment), double click
  = SelSegVolume(0); NA-mode volumes: double click = SelLabelSegVolumeMax(0)
  (`RulerTool::GetPointAndLabelMax`: the label of the shown segments where the
  NA-mode volumes are brightest along the ray; the segment toggles between
  highlighted 2 and shown 1 in every NA-mode volume).
* **Paint-ruler mode** (wx int modes 7 / 8): the brush and ruler tools mirror
  one interaction mode: ruler creation (5) puts the brush in 5, Shift gives
  mode 7 (selector mode 1, 8 for ruler type 3); a press clears the selection
  (paint mode 6), the release segments and adds the selection centre
  (VolumeSelector::ProcessSel(0.01), `RulerTool::AddPaintRulerPoint`: info
  "v<n>" = selected size; for ruler type 3 the box point) then 8 → 7;
  releasing Shift returns both to 5. Painting from ruler-edit mode (6) or
  camera mode ends the ruler mode as in wx.
* **Pen pressure**: QTabletEvent pressure x 2 (wx's pkNormalPressure / 512
  for a 1024-level pen) while a pen touches the view; with brush_use_pres
  (project key) the brush radii, stamp spacing and cursor use it per drag
  segment; mouse strokes and brush_use_pres off keep 1.0 (identical masks).
* **Analyze dialogs**: Noise Reduction, Counting and Volume, Colocalization,
  Convert, Information; `MainWindow` implements `AnalysisContext`.

## Measurement and warps

* **Dialog** (`MeasureDialog` + `RulerTool`): all wx controls — ruler tools,
  pointing method, Transient, Use Volume Properties, Scatter Rulers,
  Transform / Interpolation / Stiffness / Apply Transform, export / import, the
  ruler list — plus the Qt additions described under "Changes".
* **Annotation rows** (wx RulerListCtrl::UpdateRulers / OnGetItemText /
  Export, m_show_anno): after the rulers, one row per text of every shown
  annotations layer (for mesh annotations only the texts whose score is above
  Min Score; sorted list, 10000-step bracketing as wx); name
  "<text>  (<layer>)", points (x, y, z) x the volume resolution, info = score
  or "<n> voxels". Export writes them with the wx columns (+ the two empty
  start / end volume columns). They cannot be edited / dragged / deleted.
* **Transforms** go through the shared `WarpVolumeByRulers` (TPS with
  stiffness, affine, similarity, rigid, translation, Cells). Landmarks: a
  2-point ruler = point 0 moving, point 1 target; a locator = a
  zero-displacement anchor; physical units (the BigWarp CSV coordinates).

### Cell (shape preserving) warp

C++ port of the Fiji plugin `Apply_BigWarp_RigidCells.java` ("Apply BigWarp to
Cells (shape preserving)"), in the toolkit-neutral `RigidCellsWarp.h/.cpp`
(compiled into both apps; the Qt Measurement dialog is the only UI).

* Output grid = the Target: grid (see below; "Same as source" = the current
  volume's grid, the plugin with moving = target image). Label values and bit
  depth kept, nearest neighbour, first object written wins; name +
  `_CELLS_ADAPTIVE` / `_CELLS_TRANS` / `_CELLS_RIGID` / `_CELLS_SIM`.
* Voxel convention is the plugin's: physical = index * spacing (origin 0). The
  GPU resampler of the other transforms puts voxel centres at (index + 0.5) *
  spacing, half a voxel off; both read the same ruler coordinates.
* Stiffness = the dialog's slider, the plugin's dimensionless value: lambda =
  stiffness * mean squared distance of the TPS source landmarks from their
  centroid, on the kernel diagonal. The cell warp's TPS maps moving → target
  (source = moving landmarks), the resampling TPS of "Thin Plate Spline" maps
  target → moving (source = target landmarks), as in "Apply BigWarp with
  Stiffness"; same convention, but the two radii come from different landmark
  sets.
* Options (saved under `/rigid cells`): local transform (Adaptive /
  Translation / Rigid / Similarity), objects (26-connected components per
  label / each label value), rigidity factor, minimum smoothing, neurite
  radius, TPS samples per object, minimum object voxels, landmark search
  radius, compute device (CPU threads or an OpenCL GPU, loaded at run time:
  OpenCL.framework / OpenCL.dll / libOpenCL.so.1; `VVD_NO_OPENCL=1` hides it),
  leave-one-out diagnostics.
* Results dialog (`RigidCellsReportDialog`): the plugin's objects and
  landmarks tables (same column names, row label "Label"), Save CSV (full
  precision), the plugin's log lines and warnings.
* Every expression follows the Java order and `-ffp-contract=off` is set for
  the file, so results are bit-identical to the plugin's: 29 configurations
  (all local modes, both object modes, CPU and GPU, 8- and 16-bit labels),
  object and landmark tables included. Deliberate difference: the plugin's
  `Procrustes.fit` tested `mode > 0` (TRANSLATION is 1), so "Translation"
  also fitted a rotation and equalled Rigid; the port tests
  `mode > TRANSLATION` (identical to the plugin with the same one-line fix;
  Adaptive / Rigid / Similarity unchanged).

### Warp output grid ("Target:")

* Measurement "Target:" next to Transform / Interpolation, saved under
  `/warp target` ("auto", "same", "vol:<name>"). Auto = the End Volume of the
  2-point rulers when every one of them starts on the current volume and all
  end on the same other shown volume, else Same as source (the item shows what
  it resolves to). The list is refilled when it opens and before each Apply
  Transform. Same as source is byte-identical to before (16 cases checked).
* Why: the transform maps ruler start → end points in world space, and the
  result used the source's grid. With rulers from a small volume (1305 x 712 x
  782 at spacing 0.024 = 31.3 x 17.1 x 18.8) to a large one (2953 x 625 x 625
  at 0.041671 = 123 x 26 x 26; both boxes start at the world origin, world z =
  -physical z) all end points (x 59..75) lie outside the small volume's box,
  so warping it gave an empty volume, and warping the large volume on its own
  grid moved its data away from the start points.
* With a target, `NewWarpResultVolume` makes the result on the target's grid
  (size, spacing, BigDataViewer additional transform; bit depth of the source;
  refused above half of the installed memory). The map is still fit in the
  source's normalized box; `WarpTargetGridMap` gives the per-axis map from the
  result's normalized box into it (refused when the two boxes are rotated /
  flipped against each other) and `VolumeRenderer::warp(..., out_scale,
  out_off)` folds it into the shader's u = o * gscale + goff (no shader
  change; the tile estimates use it too, so the tiled source path and outputs
  larger than the source work). Cells: the plugin's target grid `tg` = the
  target's size / spacing with its origin in the source's physical frame (CPU
  and OpenCL).
* Reverse: when every 2-point ruler starts on the chosen target and ends on
  the current volume (`WarpRulersReversed`), the end points are the moving
  landmarks.
* Result on a two-neuron test project (TPS, stiffness 0): small → large gives
  465630 nonzero voxels on the 2953 x 625 x 625 grid with every ruler end
  point on the warped object; the reverse puts every start point on it.

## Components & Tracking

`TraceDialog.cpp`, port of TraceDlg / TraceListCtrl, every page and handler:

* Track Map (Load / Save / Save As .track, Generate with "X" iterations,
  Refine T, Refine All, progress), Selection (ID text shared with the Linkage
  page, X, FullCompt, Replace, Append ("all" = every label), Clear, component
  size), Linkage (Append, Clear, Auto Link check tool, Excl. Link, Link IDs,
  Isolate, Unlink IDs), Modify (New ID text coloured by id hue, X, Append,
  Clear, Auto Assign ID check tool, Assign ID, Add ID, Replace ID, Combine,
  Divide; Segment hidden and empty as wx), Analysis (Rulers, UniIDs
  (consistent ids written to a folder), Compnts, Links, Save As), Ghosts
  (Tail, number, Lead), ID lists of the current / previous time point (tree
  glyphs, id colours, multi-selection, Copy / Delete menu, Delete /
  Backspace, Ctrl+C), Backward / Forward (VMovieView Down / UpFrame), Output
  text.
* The dialog exists hidden from start-up (wx: a hidden floating pane); the Qt
  app shows it from the Analyze drop-down, the Tools menu and the tree's
  context menu (`MainWindow::ShowTraceDlg`; all commented out in wx). The f /
  l keys and the brush hooks work while it is hidden, as in wx.
* View hooks: after every brush Segment, auto ID (selector mode 1 / 2 / 4:
  CellNewID(true), 3: CellEraseID) and manual assist (CellUpdate unless auto
  ID, CellLink(true)) (`BrushTool::segmented`); GetTraces
  (`RenderCanvasTraces.cpp`); the trace group per view (Create / Load /
  Save); the dialog's GetSettings on volume selection.
* As wx: nothing is drawn by DrawTraces, "Convert to: Rulers" adds no rulers,
  trace groups are not saved in projects; Measure() reads 8 / 16-bit data
  only (other types: value 0 instead of wx's uninitialized value).

## Plugins

### Plugin system (`QtGui/PluginApi.h`, `QtGui/QtPluginManager.*`, `QtGui/MainWindowPlugins.cpp`)

* **Interface.** `vvd::Plugin` is the toolkit-neutral part of wx
  `wxGuiPluginBase` / `wxNonGuiPluginBase`: `GetName` (the id of RunPlugin /
  `-p` / PluginExists), `GetDisplayName` (menu, pane caption, project keys),
  `GetId`, `IsGui`, `OnInit`, `OnDestroy`, `OnTreeUpdate`, `OnRun(options)`,
  `Work` (non-GUI plugins), `Load/SaveProjectSettingFile`.
  `vvd::QtGuiPlugin` adds `QWidget* CreatePanel(QWidget*)`. `vvd::PluginHost`
  is what a plugin may call in the application (the wx plugins cast
  GetVVDMainFrame() to VRenderFrame*): data manager, current selection,
  views, tree, brush, adjust / properties panes, AddVolume,
  ReplaceVolumeData, RemoveGroup, UpdateTree / UpdateTreeIcons /
  RefreshVRenderViews, OnSelection, StartupLoad(files, datasize, descs),
  LoadMeshes, UploadFileRemote, the plugin window calls and the interop
  message (wx `wxEVT_GUI_PLUGIN_INTEROP` → `QtPluginManager::interop`,
  queued). `MainWindow` implements it. The wx ABI stays for the wx app.
* **Built in.** The three wx plugins call into the frame, the views and the
  engine all over; as shared libraries they would need the executable's
  symbols at load time and would duplicate engine statics. They are compiled
  into VVDViewerQt (`plugin_fiji/qt`, `plugin_nblast/qt`, `plugin_NA/qt`) and
  registered in `QtPluginManager::LoadAllPlugins` in the wx loader's order:
  Fiji Interface, NBLAST Search, Neuron Annotator Plugin.
  **Shared-library plugins** are still loaded from the wx plugins folder
  (macOS `<bundle>/Contents/PlugIns`, else `<exe dir>/plugins`) but its
  subfolder `qt` (wx: `gui` / `nongui`), so neither front end loads the
  other's libraries: every `*.dylib` / `.so` / `.dll` exporting
  `vvdCreateQtPlugin` / `vvdDeleteQtPlugin` (C linkage; may rely on the
  virtual interfaces and Qt only).
* **UI.** The toolbar's Plugins button pops up one entry per plugin (GUI
  plugins first; wx OnPlugins / OnPluginMenuSelect): a GUI plugin's pane is
  shown (created on first use), a non-GUI plugin runs `Work()`. Panes are
  `QDockWidget`s (caption = display name, object name `PluginDock_<name>`);
  placement see "Changes". ToggleVisibilityPluginWindow(name, false) on a
  pane that does not exist does nothing (wx: unreachable creation code).
  RunPlugin(name, options, show), PluginExists, IsCreated /
  IsShownPluginWindow, CreatePluginWindow as wx. `-p "<name> <options>"` runs
  RunPlugin(name, options + " -d <desc>"..., true) after the start-up files
  (the name is the text before the first blank, so only plugins without a
  blank in their name: NAPlugin).
* **OnTreeUpdate**: `TreePanel::treeUpdated` (end of UpdateTree,
  UpdateTreeIcons, UpdateTreeColors).
* **Projects**: on save, for every GUI plugin whose pane exists:
  `/ui_layout/<display name>` (shown), `<display name>_float`, and
  `<display name>_setting` = `<project>_files/<display name>.set` when the
  plugin wrote that file (only the NA plugin does). On open: the pane is
  shown / hidden, floated / docked, the setting file loaded. Keys of a wx
  project are kept through the passthrough; a capture with and without the
  plugin system is byte-identical.
* **Settings files** (wx Load/SaveConfigFile): `fiji_interface_settings.dft`,
  `NBLAST_plugin_settings.dft`, `NBLAST_database_list.txt`,
  `NA_plugin_settings.dft`, same keys / format / place as wx; written when the
  panes are destroyed, never in scripted runs.

### Fiji Interface (`plugin_fiji/qt/FijiInterfacePluginQt.*`, `WxIpcServer.*`)

* **Server = wxTCPServer on QTcpServer, byte for byte** (full wire format in
  `WxIpcServer.h`, from wx 3.3 `src/common/sckipc.cpp`): little-endian int32,
  strings as int32 length + UTF-8; IPC_CONNECT(10) + topic → IPC_CONNECT /
  IPC_FAIL(9); IPC_POKE(3) item, format byte, int32 size, data; ADVISE_START /
  ADVISE_STOP reply 4 / 7, REQUEST, unknown codes and zero-size data →
  IPC_FAIL. `Poke(item, wxString)` = wxIPC_UTF8TEXT (17) with the terminating
  NUL (vvd_run.txt splits on ",\0" because of it). Port 8002, topic
  "FijiVVDPlugin", loopback only.
* **Messages as wx OnPoke**: "version", "confirm" (echoed back; 128 MB
  buffers), "pid", "com_finish"; private "settimeout", "setrcvbufsize",
  "setsndbufsize", "volume". SendCurrentVolume = header (name length, name,
  nx, ny, nz, bits, r, g, b, double spacings) + voxels of the current volume
  (its mask with "Process mask"), preceded by "setrcvbufsize" and 500 ms;
  SendCommand = the volume, then Poke("com", command); the temporary voxel
  override (`;send_mask;override_vox`) and the group sync flags are restored
  at com_finish. A received volume with the same name replaces the data
  (ReplaceVolumeData), else is added; 16-bit gets max value + scalar scale; an
  8-bit image named after the volume whose mask was sent becomes that
  volume's mask on a copy (wx); then the previous selection, the interop
  message "Fiji Interface,finished,<com>", VVDViewer to the front.
* **StartFiji / CloseFiji** as wx: vvd_listener.txt / vvd_run.txt /
  vvd_quit.txt and vvd_listener.jar (now bundled in Resources by
  `cmake/QtGui.cmake`; the wx bundle has none) are copied into Fiji's macros /
  plugins folders (an existing file is removed first, so a symbolic link is
  replaced, never written through), `LSUIElement 1` set in Fiji's Info.plist,
  `ImageJ-macosx -macro vvd_listener.txt` started detached; CloseFiji resets
  LSUIElement, sends SIGTERM, removes the macros. Window as wx (info line
  "Connectiong to Fiji..." [sic], Fiji path, "Launch Fiji at startup",
  command, "Process mask", "Send event", progress dialogs with Cancel).
* Wire-level differences: between the volume and "com" the server waits until
  the volume is handed to the OS + 300 ms (vvd_listener reads a block with
  read() calls of the block's full size, so a following message could be read
  into the volume); the name is sent as UTF-8 bytes with their count (wx: the
  character count); osascript activation is detached and skipped in scripted
  runs (it needs the accessibility permission).
* A duplicated plugin jar in Fiji's `plugins/` folder makes Fiji show a modal
  "Duplicate command" dialog that blocks every -macro start (both apps).
* Verified with a real Fiji (a copy of the installation): Invert / Gaussian
  Blur on 8- and 16-bit volumes return voxels identical to the same commands
  in a headless Fiji; mask round trip; clean exit. A logging proxy showed the
  wire layout identical to the wx server's; a Python client checked the
  protocol edge cases.

### NBLAST Search (`plugin_nblast/qt/NBLASTPluginQt.*`)

* Port of NBLASTGuiPlugin + window: toolbar Save / Import / Database /
  Setting, scoring Forward / Mean, "Target Neurons" check boxes, Run NBLAST,
  the results list (Name, Database, Skeleton / Volume Data thumbnails, Score),
  the two preview panes (with "Overlay search query"; the MIP pane only with
  the same integer aspect ratio, as wx), Delete / Backspace, undo / redo (300
  steps), double click loads `swc/<name>.swc` / `volume/<name>.nrrd`, Save
  with "Additional Options" (export skeletons / MIPs / previews / volumes,
  name prefix score x 100000 / database), Import, the database editor (list
  file `name\tpath\ttrue|false\tuser\tpassword`, no final newline), the
  settings dialog (Rscript, temporary directory, Show Top N).
* runNBLAST: the selected volume's mask (else the volume) as vvdnbtmpv.nrrd or
  the selected SWC mesh as vvdnbtmpsk.swc, then Rscript nblast_search.R
  (bundled in Resources); a volume without "skeleton" in its name is first
  skeletonized in Fiji ("NBLAST Skeletonize;true;false" through the Fiji
  Interface; the search starts on the interop message). runNBLASTremote and
  LoadSWC are ported (no UI calls them, as wx). Rscript default
  `/Library/Frameworks/R.framework/.../Rscript` (Windows: the newest R-*
  under Program Files).
* Verified with R 4 + nat + nat.nblast on two synthetic databases (40 + 30
  random SWC neurons; `nblast_search.R` needs at least as many neurons per
  database as worker threads): a jittered copy of a neuron ranks it first;
  all export options; a volume query skeletonized through Fiji.

### Neuron Annotator (`plugin_NA/qt/NAPluginQt.*`)

* Port of NAGuiPlugin + window. runNALoader: label .v3dpbd (V3DPBDReader),
  volume through StartupLoad with 80 % of the graphics memory as the size
  budget (`MainWindow::m_load_datasize`), chspec signals / reference, `-s
  XxYxZ`, `-d`; the label scaled to the signal size, shared label / mask, NA
  mode with every segment shown, previous NA groups removed, the reference
  moved out of the signal group, masks hidden, bounds / centre / translation
  reset. MIP worker threads (wx partitioning; joined instead of a counter +
  detached threads), the reference MIP, the fragment list (check = active,
  segment mask 2; uncheck hides; selection through the 30 ms timer with one
  visibility-history step; double click toggles; thumbnails follow the width;
  undo / redo of deletions; the Delete key stays off as in wx), the 100 ms
  idle refresh, SwitchPropPanel, Import (label, then `<label>.h5j` or the
  volume dialog), export (SaveCombinedFragment: shown fragments, maximum of
  the signals, gzip NRRD), OnRun (-l -v -c -s -d), project / default settings
  (Reference, Signal1-3, s_count, x/y/zspc, id_path, vol_path, prefix,
  seg_num, seg_<i> = "id,visibility"), LoadNrrd, runNALoaderRemote, LoadSWC.
  The view's NA context menu and the list stay in step through OnTreeUpdate.
* Verified on synthetic PBD8 data (6 fragments, 4 channels): segment MIPs,
  reference MIP and the exported volume identical to an independent numpy
  computation; project reopen restores segment visibility.

## Testing aids

Scripted runs (`VVD_QT_TEST`, `VVD_QT_CAPTURE`, `VVD_QT_GRAB*`,
`VVD_QT_MOVIE`, `VVD_QT_FILEDLG_RUN`; `TestRun.h`) never write the settings
file, the window state, the plugin settings or the default_*.dft files. Test
runs print `[paint]` / `[b3]` / `[b4]` / `[layout]` / `[legend]` lines with
PASS / FAIL on stderr, save pictures in `VVD_QT_TEST_DIR` (or next to
`VVD_QT_GRAB`) and quit by themselves. Most kinds take their data on the
command line.

### `VVD_QT_TEST=<kind>`

| Kind | What it checks (extra variables) |
|---|---|
| `layout` | window / pane / toolbar minimum sizes against the screen, resizeDocks to small sizes, 1280x800 and 1024x768 windows, all plugin panes, an off-screen floating dock; `layout_*.png` (`VVD_QT_LAYOUT_SIZE=WxH`, `VVD_QT_LAYOUT_NO_PLUGINS=1`) |
| `ui` | Show/Hide UI, menus, check marks, /ui_layout |
| `tree` | tree, groups, per-group capture (`VVD_QT_GRAB_DIR`, `VVD_QT_GROUP_DEPTH`) |
| `treescroll` | scroll position and selection after a delete |
| `sphererot` | virtual sphere rotation: centre drags right / down against the wx trackball, a 45° drag around the centre near the rim (pure roll), drags left / down near the right side of the view; `sph_0.png` / `sph_1.png` beside `VVD_QT_GRAB` |
| `drop` | drag & drop of `VVD_QT_DROP=a;b` onto the render view (`VVD_QT_DROP_TARGET=frame`: the main window); prints the volumes / rulers opened |
| `ruler`, `brush`, `dialogs` | ruler tool, brush tool, the analysis dialogs |
| `paint` | brush strokes, undo / redo, keys, blend modes, Extract / Erase voxel by voxel, mask counts from the GPU; saves `p19_annotations.vrp` (`VVD_QT_PAINT_UNDO` = history depth, default 5; `VVD_QT_TEST_DIFFUSE`, `VVD_QT_TEST_EXTRACT`, `VVD_QT_TEST_ONLY`, `VVD_QT_COLOR`) |
| `calc`, `seg`, `index` | Calculations against the shader formulas (Fill Holes: every voxel above the threshold is 255), segmentation, index-segment picking |
| `diffuse` | timed Select / Diffuse / Unselect strokes with mask counts, captures d00-d03 (`VVD_QT_DIFFUSE_REPS`, `_ITERS` 20/50/100, `_DRAG=1` one drag event per 50 ms tick, `_PERSP=1`, `_ROT=rx,ry,rz`) |
| `nr` | both noise-removal paths with raw dumps of masks / results (`VVD_QT_NR_SIZE`, `VVD_QT_NR_THRESH`) |
| `hide` | Hide Inside / Outside against the Erase / Extract volumes (`VVD_QT_TEST_SHADING=0|1`) |
| `ctx` | the view's context menu, PickMesh, right click vs drag, visibility undo / redo (`VVD_QT_CTX_INDEX=1`: join / split on an index volume) |
| `na` | NA-mode double click (a volume with a .lbl) |
| `view3d` | volA + volB + a mesh: s / w with the focus on tree / check box / canvas, auto-repeat, text box, clamping, ',' / '.', Ctrl+arrows, V, R (no action), Alt+V / Ctrl+B, clip-slider right click, shadows in the three blend modes, mesh shadow direction, gradient background, Intrp., slice mode, AOV hover, mouse_focus, annotation mesh, rotation link, ruler-point double click |
| `rulervol` | 2-point ruler from volume 0 to volume 1, Start / End Volume, text export; project saved to `VVD_QT_SAVE` or `<dir>/rulervol.vrp` |
| `rulerlist` | the opened project's Measurement list (`VVD_QT_EXPECT_START`, `VVD_QT_EXPECT_END`) |
| `rulerauto` | Auto-select on two separated volumes: clicks, Edit drag, View Plane locator, empty space, paint-ruler press, off = unchanged; `a01_measure_list_auto.png` |
| `warp` | Import `VVD_QT_WARP_CSV` + Apply Transform (`VVD_QT_WARP_TTYPE`, `_LAMBDA`, `_INTERP`), result voxels to `VVD_QT_WARP_OUT` (raw) |
| `cells` | Import `VVD_QT_CELLS_CSV`, one "Cells: shape preserving" run per entry of `VVD_QT_CELLS_RUNS="local,objects,stiffness,device,loo,outprefix;..."` (+ `VVD_QT_CELLS_PARAMS="beta,sigma_min,neurite,samples,min_voxels,radius"`); `<outprefix>.raw` (`VVD_QT_CELLS_RAW=0`: md5 only), both tables via Save CSV, `VVD_QT_CELLS_GRAB=<dir>` dialog pictures |
| `warptarget` | project on the command line, `VVD_QT_WT_RUNS="src,target,ttype,interp,stiffness,tag;..."` (src / target = index or name, target also auto / same; ttype 5 = Cells, `VVD_QT_WT_CELLDEV`); `VVD_QT_WT_GRAB=1` (`<tag>_before.png`, `<tag>_after.png`, `measure_dialog_target.png`); `VVD_QT_WT_RAW=1` grids, boxes, ruler points, per result nonzero count / bbox / md5 and per ruler the distance from the end point to the nearest nonzero voxel |
| `legend2` | Legend drop-down after volumes are added while running (volA.nrrd on the command line, volB / b1 / b2.nrrd beside it): row count, rows inside the menu, clicks / Show All / Hide All checked on the next presented frame (`RenderCanvas::CaptureNextFrame`) and by `screencapture`; second view's menu |
| `b3_panels`, `b3_maxval`, `b3_paint`, `b3_nrna`, `b3_script4d`, `b3_modes` | panels and dialogs (`MainWindowBatch3Test.cpp`; `VVD_QT_B3_DATA` = folder with octa2.obj / octa3.obj, `VVD_QT_B3_DATA4D` = the ts_T1..3 series with .lbl / .msk, `VVD_QT_VOLUME_DFT`, `VVD_QT_TEST_MODE`) |
| `b4_views` | volA.nrrd volB.nrrd mesh.obj: second view, drag onto it, linked rotations, active view, ruler in view 2, Record/Export view combo, captures v0 / v1.tif, 2-view project p2.vrp, "Close" |
| `b4_viewsrt` | p2.vrp on the command line (`VVD_QT_SAVE`): views, names and rulers back, saved again (fixed point after one round trip) |
| `b4_track` | tk_T1.nrrd + `VVD_QT_TIME_SEQ=1` (3 time points, 3 moving blobs): Generate, Refine All, track file I/O, UniIDs, selection / linkage / modify actions, f / l keys, selection_tracking, auto ID; `trace_*.png` (`VVD_QT_B4_NOAUTO`) |
| `b4b_ui`, `b4b_uirt` | plugin menu / panes / project keys and their round trip |
| `b4b_lib` | a sample shared-library non-GUI plugin (`VVD_SAMPLE_OUT`) |
| `b4b_fiji` | real Fiji (fiji8.nrrd, fiji16.nrrd): commands, 16-bit fix-up, RunPlugin, `VVD_QT_FIJI_MASK=1` mask round trip (`VVD_QT_FIJI_PATH`, `VVD_QT_FIJI_PORT`, `VVD_QT_FIJI_CMD`) |
| `b4b_ipc` | IPC server for an external client (`VVD_QT_IPC_WAIT`) |
| `b4b_nblast`, `b4b_nbvol`, `b4b_nbdlg` | NBLAST with R (mesh query, volume query through Fiji, dialogs) (`VVD_QT_NBLAST_DBLIST`, `_DBLIST_SAVE`, `_OUTDIR`, `_RSCRIPT`, `_MEAN`) |
| `b4b_na`, `b4b_nart`, `b4b_naimp` | Neuron Annotator: `-p "NAPlugin -l ... -v ... -s 1x1x2"`, project reopen, Import |

### Other variables

| Variable | Effect |
|---|---|
| `VVD_QT_GRAB=<png>` | save the window (widgets only) after start-up and quit; with `VVD_QT_TEST` runs the test first |
| `VVD_QT_GRAB_VIEW=<png>` | also save the 3D view's own pixels |
| `VVD_QT_SAVE=<vrp>` | also save the project (round trips) |
| `VVD_QT_CAPTURE=<tif>` | run Capture without its dialog (`_SIZE` WxH / `<k>x` / 1:1 / disp, `_COMPRESS`, `_GROUP`, `_PROJECT`, `_DELAY` ms, default 4000), then quit |
| `VVD_QT_GRAB_CAPDLG` | picture of the capture dialog |
| `VVD_QT_MOVIE=<mp4/mov/tif>` | Record/Export Save... without the dialog (`_LEN`, `_FPS`, `_AXIS`, `_DEG`, `_STYLE`, `_REWIND`, `_ROT`, `_SEQ`, `_CROP`, `_KEYS`, `_KEYDUR`, `_MODE`, `_KEEP`, `_PRJ`, `_DELAY`; see QtMain.cpp) |
| `VVD_QT_GRAB_MOVPANEL=<dir>` | pictures of the four Record/Export tabs and the save dialog (`VVD_QT_GRAB_MOVDLG`) |
| `VVD_QT_FILEDLG_RUN="step,..."` | file dialogs / tree save actions in order, then quit: `openvolume`, `openmesh`, `openproject`, `saveproject`, `savevolume`, `savesegmented`, `bake`, `exportmeta`, `select:<i>`, `clip:x0:x1:y0:y1:z0:z1`, `wait:<ms>`; per dialog `VVD_QT_FILEDLG_FILE_<NAME>=a;b`, `VVD_QT_FILEDLG_OPTS_<NAME>=key=value,...` (option controls by object name, `filter=<i>`); `VVD_QT_GRAB_FILEDLG=<dir>` pictures; `VVD_QT_FILEDLG_DELAY` (see FileDialogs.h) |
| `VVD_QT_DATASETS=all\|none\|cancel\|<i,j>` | answers the Zarr / N5 dataset dialog (picture `filedlg_datasets.png`) |
| `VVD_QT_TIME_SEQ=1` | Open Volume option "Read a sequence as time series" for the session |
| `VVD_QT_GRAB_LEGEND=<dir>` | Legend drop-down clicks, `legend_menu_<step>.png`, `legend_view.png` (`_DELAY`, default 5000 ms) |
| `VVD_QT_GRAB_MIDPASS=<dir>` | presented frames of unfinished streaming passes as `midpass_<n>.png` (`_MAX`, default 60) |
| `VVD_QT_STATE_HEX=<file>` | lines `geometry\|windowState2\|innerState2 <hex>` replace the saved window state (empty file = none) |
| `VVD_QT_VOL_METHOD`, `VVD_QT_FOG`, `VVD_QT_OVERLAYS` | blend mode, fog, overlays at start-up |
| `VVD_QT_MESH_ALPHA=<a>` | alpha of every mesh 2.5 s after start-up (transparent meshes / depth peeling) |
| `VVD_QT_ROT=rx,ry,rz` | view rotation 2.5 s after start-up |
| `VVD_QT_VOLPROPS="invert=1,mip=1,shading=0,solid=1,colormap=1,gamma=..,offset=..,lo=..,hi=..,boundary=..,alpha=..,lum=.."` | volume properties for all volumes 2.4 s after start-up |
| `VVD_QT_SPIN="deg,steps,delay_ms"` | scripted rotation, prints fps |
| `VVD_QT_UP_TIME=<ms>` | streaming time budget for the run |
| `VVD_QT_TEST_MODE=speed,param,wiref` | Settings test modes for the run |
| `VVD_QT_VOLUME_DFT=<path>` | where a scripted run may write default_volume_settings.dft |
| `VVD_QT_NO_PLUGINS=1` | skip the plugin system |
| `VVD_QT_LOAD_TIMING` | load timings on stderr |
| `VVD_QT_STREAM_DEBUG` | per-second brick-loop statistics (frames, bricks, loop progress, texture pool, reuse waits) and one `loop done` line per brick loop with its wall time |
| `VVD_PAINT_TIMING=1` / `VVD_QT_PAINT_TIMING=1` | engine (wx too): `[ptime]` lines per stroke (events, frames while held, FlushStrokes, frame-slot wait, Select split into undo push / init / diffusion passes with bricks, skips, uploads, swaps, submits, GPU time, mask readback, the redraw until its loop is done) |
| `VVD_SEG_NO_PIN`, `VVD_SEG_NO_BATCH`, `VVD_SEG_NO_SKIP`, `VVD_SEG_VERIFY_SKIP` | engine A/B for the mask passes (no resident textures / one submit per brick / no footprint skip / run skippable bricks and count changed texels, expected 0) |
| `VVD_NO_BRICK_SKIP=1` | engine: disable the exact empty-brick skip |
| `VVD_HIDE_INSIDE_LEGACY=1` | engine: old Hide Inside test |
| `VVD_NRRD_ZLIB=1` | decode gzip NRRD with zlib instead of libdeflate |
| `VVD_FRAMES_IN_FLIGHT=1` | engine: one frame in flight (serialized frame ends) |
| `VVD_WARP_DEBUG=1` | warp tile counts and memory footprint |
| `VVD_NO_OPENCL=1` | hide the OpenCL devices of the cell warp |
| `VVD_SETTINGS_DIR=<dir>` | both apps and plugins: settings folder instead of the per-user one |
| `VVD_INT_SETTLE_MS=<ms>` | Qt: how long the input must rest before full quality returns (default 300) |
