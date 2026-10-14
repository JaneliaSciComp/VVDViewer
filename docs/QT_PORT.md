# VVDViewer: wxWidgets → Qt port

Status document for the GUI-toolkit migration. Phase 1 is done on the
`qt-port` branch; this file records what was done, how the two front ends
coexist, and what remains, file by file, with effort estimates.

## Why

wxWidgets has been hard to keep consistent across macOS / Windows / Linux
(native control heights differ per platform and per wx release; the bottom
row of the Properties pane was clipped on macOS with wx 3.3 because the pane
height was a hard-coded pixel value). Qt gives one layout engine, a native
Vulkan-capable window on every platform, and a much larger pool of
maintainers.

## Architecture after phase 1

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
                    │  Core/  VVDString, Thread, FileUtil, UICallbacks     │
                    │  DataManager, VolumeSelector/Calculator, KernelExec. │
                    │  FLIVR/ (renderer)   Vulkan/   Formats/ (readers)    │
                    │  Tracking/ Animator/ NV/ Converters/ teem pole       │
                    └──────────────────────────────────────────────────────┘
```

* `fluorender/FluoRender/Core/` is the seam. `vvd::String` **is** a
  `std::wstring` with the wxString helpers the code used (`Mid`, `Find`,
  `Format`, ...). A GUI target that defines `VVD_WX_STRING_INTEROP` (all wx
  targets do, per-target in `CMakeLists.txt`) gets implicit `wxString ⇄
  vvd::String`; `VVD_QT_STRING_INTEROP` does the same for `QString`. The
  engine object libraries are compiled with neither and are shared by both
  executables.
* `vvd::Thread` / `vvd::CriticalSection` replace `wxThread` /
  `wxCriticalSection` with the same semantics (detached threads delete
  themselves; `Delete()` waits). `vvd::UI::MessageBox / CreateProgress` go
  through a backend the GUI installs at start-up.
* Rule for new code: **nothing under `Core/`, `FLIVR/`, `Formats/`,
  `Vulkan/`, `DataManager.*`, `VolumeSelector.*`, `VolumeCalculator.*`,
  `KernelExecutor.*` may include a wx or Qt header.** The Linux compile
  check builds those files without either toolkit on the include path.

## Building

```bash
bash build.sh               # wx app only (unchanged)
bash build.sh --enable-qt   # wx app + VVDViewerQt (Homebrew Qt on macOS)
```

`ENABLE_QT_GUI=ON` is the CMake option; `cmake/QtGui.cmake` defines the
target. Qt is not built by vcpkg (too slow); it comes from Homebrew, the
distro, or the Qt installer (`-DCMAKE_PREFIX_PATH=<Qt dir>`). The Qt target
needs `QT_NO_KEYWORDS` because FLIVR shader classes have a method called
`emit()`; use `Q_SIGNALS/Q_SLOTS/Q_EMIT` in `QtGui/`.

## What the Qt app does today (phase 1b)

| Area | State |
|---|---|
| Main window, docks, menus, toolbar | same pane layout as the wx frame (wxAUI layers emulated with a nested QMainWindow), wx toolbar bitmaps. Menu bar = wx m_top_menu: File (Open Volume / Mesh / Project, Save Project, Open URL, Quit + About in the macOS application menu), Tools (Analysis Tools ... Convert, Settings), Windows (Show/Hide Toolbar, Show/Hide UI, Datasets, the six panes as check items, Open New VVDViewer, Full Screen except on macOS). Show/Hide UI (`MainWindowUI.cpp`) = wx ToggleAllTools / m_tb_menu_ui: the button hides all panes remembering which were shown and brings exactly those back, its drop-down arrow has Toggle All Panels + one check item per pane; all check marks follow the real pane visibility (title-bar close included). Projects save / restore /ui_layout as wx |
| Output Adjustments / Clipping Planes | `AdjustPanel`, `ClipPanel` |
| Record/Export | `MoviePanel` = wx VMovieView: Basic (rotation X/Y/Z, degrees, Linear/Smooth, rewind, time sequence / 3D batch with start/end/current and +/-, movie length), Advanced (`RecorderPanel` = wx RecorderDlg: keyframe list with in-place Inbetweens/Interpolation/Description editing, drag reorder, Add/Delete/Del. All, Record volume properties), Auto Key (nC1..nC3), Cropping (frame drawn on the view, centre/size, Reset), footer (FPS, repeat, Capture view, play/pause, rewind, time slider/text, Save...). Playback and recording are the wx timer logic (OnPrev/OnTimer/SetRendering/SetProgress); a recorded step waits until the frame of the current state is completely streamed and read back. Save...: wx dialog + Additional Options (LZW, bitrate, estimated size, embed project), .mov/.mp4 (QVideoEncoder) or TIFF sequence, cropped, project saved with the movie when the setting is on. Engine side in `RenderCanvasMovie.cpp` (SetParams, AddKeyFrame, Set4DSeqFrame, Set3DBatFrame, CalcFrame, recording). Keyframes (/interpolator) and /movie_panel are saved / loaded with projects. 4D script runner (Settings > 4D Script; `MainWindowScript.cpp`, see "Batch 3" below) |
| Render views | several views as wx (`MainWindowViews.cpp`, see "Batch 4a"): one canvas + bars + layers + camera + overlays + brush + rulers + selector per view, a Vulkan surface each on one shared context, projects with /views/num > 1, tree "Close", panes follow the active view |
| Components & Tracking | `TraceDialog` = wx TraceDlg (all five pages, ghosts, ID lists, output), hidden as in wx; f / l keys, auto ID / manual assist after brush strokes, selection_tracking with the trace group (see "Batch 4a") |
| Render View chrome | `ViewPanel`: top options bar, depth-int / zoom columns, rotation bar (AOV/ortho, zoom, rotations, background wired) |
| Render canvas | volumes in composite mode; orbit / pan / zoom; perspective toggle; background colour; resize |
| Workspace tree | full wx DataTreeCtrl port (`TreePanel`): view → volume groups / meshes / mesh groups / annotations / segment trees (volumes) and SWC segment trees (meshes, built on expansion as wx), wx bulb icons, visibility history (PushVisHistory / Undo / Redo, 10 steps), ShowAllDatasets / HideOtherDatasets / HideSelectedItem, single selection, toggle (double-click / Enter / button), rename, Delete key, every wx context menu, drag & drop exactly as wx OnEndDrag (a volume dropped on the view or empty space gets a new group through OrganizeLayers). Real layer list in `RenderCanvas` (`RenderCanvasLayers.cpp`, wx m_layer_list API); groups drawn per group with their blend mode; projects load/save real groups; per-group capture. Test: `VVD_QT_TEST=tree` (+ `VVD_QT_GRAB_DIR`, `VVD_QT_GROUP_DEPTH`) |
| Properties (volume) | all VPropView sliders/checks, colour, spacing, max value (wx UpdateMaxValue scalar-scale rules), Reset / Save as Default (default_volume_settings.dft + DataManager m_vol_*), wx enable states (Alpha, Shading, MIP: "Shade Threshold:"; MIP refused in Depth mode), Shadow / Smoothing / Sample Rate on every volume in Depth mode |
| Open Volume | every format `DataManager` supports, multi-select, drag & drop. Dialog = wx OnOpenVolume + CreateExtraControlVolume (`FileDialogs`): Z-slice / time-series sequence (exclusive, session), compress data and skip empty bricks (settings), sequence identifier (settings time id); the sequence options apply to every load (command line, drag & drop) like wx LoadVolumes. Zarr / N5 as wx LoadVolumes: a `.zarr` / `.zgroup` / `.zattrs` or `.n5` / `.json` root expands into its datasets (`BRKXMLReader::GetZarrChannelPaths` / `GetN5ChannelPaths`, `<dataset>.zfs_ch` / `.n5fs_ch`, the opened `.zattrs` / `.json` as metadata file); several Zarr datasets and every N5 root go through the wx DatasetSelectionDialog (`FileDialogs::SelectDatasets`: "Choose Zarr/N5 datasets", unchecked rows, a click toggles, Select All / Deselect All); Open URL on a Zarr / N5 folder URL loads its `.zattrs` / `attributes.json`. A volume file's companion `<name>.msk` / `<name>.lbl` are loaded with it (wx m_load_mask) |
| Drag & drop | wx DnDFile::OnDropFiles: a `.vrp` as the first file opens that project only (on the window; refused on the render view with the wx message), files sorted into volumes / meshes by the wx suffix lists (others ignored), folders expanded (their files by suffix, or the folder as an N5 dataset when it has an `s<N>/attributes.json`; a folder's meshes go into a mesh group named after the folder), each batch sorted |
| Start-up | wx Main.cpp / StartupLoad: `-d/--desc` name prefixes (LoadVolumeData prefix, wx index rule), `-m/--macro` tasks (`hideui`, `showui`, `basic`, `advanced`, `record,<file>`; wx SetTasks + OnTimer at 100 ms, the window closes when done), `vvd:path1,path2` arguments, `-p/--plugin "<name> <options>"` = wx RunPlugin(name, options, true) after the start-up files (see "Batch 4b"), macOS QFileOpenEvent (Finder / `open -a`, vvd: URLs) -> StartupLoad; a `.vrp` as the first file opens only the project. Open Mesh / start-up / drop: several mesh files (or a folder) go into one new mesh group, an `.nml` into a group of its own (wx LoadMeshes). On exit the curl handles are released and the cached remote-brick files deleted (wx ~VRenderFrame, `TextureBrick::delete_all_cache_files`) |
| Save dialogs | `FileDialogs`: Save / Save segmented / Bake volume + Export metadata (.vvd) with the wx DataTreeCtrl options (metadata as an individual file, crop to the clipping planes, pyramid scale level; session), Save Project with embed / compression. Open Mesh / Open Project have no options in wx |
| Open / Save Project (.vrp) | `ProjectIO`: wx-compatible format, all volume + mesh properties, every view (/views/num, /views/<i>/layers, properties incl. painting parameters, rulers; `ProjectView::extra_views`), real volume / mesh groups, annotations (`/data/annotations/N` + type-4 layers; a `.txt` is written to `<project>_files` for annotations without a file). Every `/views/0/properties` key wx writes is filled from / applied to the view as VRenderFrame::SaveProject / OpenProject (`MainWindow::CaptureProjectView` / `ApplyProjectView`, wx order): camera translation / rotation (wx X convention, 360 - m_rotx) / centre / centre-eye distance / radius / init distance / zoom, object centre / translation / rotation, persp, free fly, AOV, near / far clip, min_dpi, drawtype, Center / Info / Legend overlays, scale bar, view 2D adjustment, st / ed / cur_time, view clipping (mode, rotations, sync, distances, links, linked params, fix parameters); `/prop_panel` = wx m_cur_sel_type / _vol / _mesh + plane_mode (Clipping Planes display mode) + the legacy chann_link keys; `/memory settings` written from the Settings (not applied on load: wx has that code commented out); the "saved by a newer version" message. `/data/volume` and `/data/mesh` in DataManager order; doubles written like wxConfig ("%g"). Volumes are read/decoded in parallel (`VolumePrefetcher` = wx `ProjectDataLoader` in prefetch mode) and finished on the GUI thread in file order; Open Volume with several files / drag & drop / command line use the same path. Round trip of a wx project (Qt open + save): identical keys and values except `translation` / `centereyedist` (recomputed by the first frame, as wx does), `/memory settings` (current settings, as wx), the order of `/tree_panel/exp_states` (wx: an unordered map) and dropped FLKEY_TYPE_INT keyframes (wx skips them too); the Qt save of that file is a fixed point and renders identically |
| Meshes | `MeshIO` (obj/swc/ply/nml/csv), drawn in composite mode like wx, `MeshPropPanel` (Sync = the mesh group, or the ungrouped shown meshes), Mesh Manipulate pane (`MeshManipPanel`, tree "Manipulate") |
| Annotations | `AnnotationPropPanel` = wx APropView (memo + Update, Transparency, Min Score, Number of Particles, Color); annotation rows in the Measurement list and its text export |
| Settings | `SettingsDialog`: all SettingDlg pages, same `fluorender.set` file |
| Analyze menu | Paint brush (`BrushTool` + `BrushToolDialog`), Measurement (`RulerTool` + `MeasureDialog`, rulers saved in projects; list columns ID (wx "Name", in-place rename), Start Volume, End Volume, then the wx columns -- the text export keeps the wx order + start / end volume appended; Qt-only "Auto-select" option (`/ruler auto select`, default off): each click / edit drag / paint-ruler stroke press first selects in the tree the displayed volume with the brightest voxel on the click ray (Maximum Intensity on the value after the transfer function, inside the clipping planes; ties keep the current volume) and places the point on it with the pointing method (View Plane: on the view plane, the volume still recorded), no hit = the normal behaviour; Transform "Cells: shape preserving" = the ImageJ plugin Apply_BigWarp_RigidCells, see below), Noise Reduction, Counting and Volume, Colocalization, Convert, Information (MainWindow implements `AnalysisContext`) |
| Selection / masks | wx mask pass of DrawVolumesComp in every blend mode and in the streaming loop (`m_draw_mask` on; hold V to hide), "Enhance selection" (mask colour HDR boost x Mask Intensity), the selected volume's own image in `m_fbo_wt2` for the brush's 2D weight, Hide Outside / Inside, shared mask / label volumes (`SetVolumeLookup`). Brush: Select / Diffuse / Unselect / Solid strokes, undo / redo (Ctrl+Z / Ctrl+Y one step each; depth = Settings > paint history), wheel brush size, Reset All, Erase, Extract, Calculations (types 1-4, 9), component analysis + exports, annotation labels drawn in the view (`ViewOverlays::DrawAnnotations`; an annotation layer that carries a mesh -- the `Mesh:` line of its file -- is drawn as that mesh, ADDed with the layer's alpha: `RenderCanvas::DrawAnnotationMesh`), noise analysis / removal (result in a new group, wx NoiseRemoval), brxml operands copied at their mask level (wx CopyLevel). The brush dialog exists from start-up (holds the NR / CA defaults, saved on exit) and its view settings + /brush_diag are read from / written to projects. Index volumes: Properties "Segment:" row (name, colour, display mode), click = Pick (PickVolume / SelSegVolume(3), then PickMesh: a click on a mesh selects it / its SWC segment), double click = SelSegVolume(0); NA-mode volumes: double click = SelLabelSegVolumeMax(0) (`RulerTool::GetPointAndLabelMax`: the label of the shown segments where the NA-mode volumes are brightest along the ray; the segment toggles between highlighted 2 and shown 1 in every NA-mode volume). wx mode 1 of SelLabelSegVolumeMax never returns (its loop over the active ids does not advance the iterator); the port fixes the loop (each active id set to 1 once); wx only calls mode 0. The view's right-click menu (wx OnContextMenu): Show all, Show only selected, Hide selected, Join / Split selected segments, Undo / Redo visibility (NA-mode volumes: Show all / Deselect all / Show only selected / Hide selected fragments); a right click opens it, a right drag zooms without opening it (wx opens it on every right release). Paint-ruler mode (wx int modes 7 / 8): Shift in a 2pt / 2+pt / locator ruler mode paints, the selection centre becomes the next ruler point. Pen pressure from Qt tablet events (project key brush_use_pres). 4D-script masks: fetch_mask / selection_tracking |
| Canvas tools | `CanvasTool` hooks (mouse first, per-frame overlays); camera = wx quaternion trackball (degrees) |
| Streaming | wx mem-swap loop ported (`RenderCanvasStreaming.cpp`) |
| Render modes | Layered / Depth (multi-volume + mesh depth peeling) / Composite, depth attenuation, quality (`RenderCanvasModes.cpp`). Meshes stay in every frame of a streaming pass that takes several frames (not so in wx, where they are drawn only in the pass's first frame): Layered / Composite keep the first frame's mesh image as the base of the first volume channel (`SaveFinalToTemp`), Depth composites the mesh layers of the unfinished slabs behind the presented frame (`DrawDPMeshPreview`). Gap: transparent meshes in Depth mode hide volumes behind them Volume shadows (wx DrawOLShadows from DrawOVER / DrawMIP / DrawVolumesMulti: depth map of the shadow volumes, gradient shifted by the Settings shadow direction, fb darkened by the shadow intensity; while streaming a brick loop of its own, so -- as in wx -- only with memory swap on). Deliberate difference: the depth-map pass is drawn without depth peeling (in Depth mode with meshes wx keeps the peel, the VRay shader then declares dmap_ray twice, fails to compile and the brick loop never ends). Mesh shadows use the Settings shadow direction too (was 0). |
| View overlays | Center, Info, Legend (+ Legend dialog), scale bar, colormap (`ViewOverlays`) |
| View controls | FreeFly, 45 increments, rotation link (also Settings "Link all rendering views' rotations" = wx VRenderView::OnRotLink(bool)), Save as Default, Output Adjustments applied; clip planes drawn and applied to meshes. Keyboard = wx VRenderVulkanView::OnIdle key polling (`RenderCanvasCamera.cpp` ViewKeyFilter / OnViewKey): an application-wide event filter, so the keys work whatever pane has the focus (wx reads the global key state whenever any window of the app is focused) except while a text box / spin box / editable combo has it, and never consume the key; each key acts once per press (auto-repeat ignored, the wx latches): Ctrl+arrows pan, d / space and a step the time sequence, **s / w move the linked clipping planes** (ClipPanel::MoveLinkedClippingPlanes: only axes whose Link is checked move -- nothing moves when no axis is linked, as wx; s = up / dir 1, w = down / dir 0; the step is the slab width x2 - x1 of the slider, clamped at 0 and the box size with the slab kept), V held hides the masks, Alt+V = wx CopyLevel(cur vol) of a pyramid volume (the copy is discarded as in wx) + selector volume, Ctrl+B = Measurement "Apply Transform" with the dialog's current transform (wx: WarpCurrentVolume through the Fiji Interface plugin; kept on the native transform), Esc ends tile rendering. Not reproduced from wx: its "moving higher" y / z branches read the slab width back from the previous text box (`str.ToLong(&dist)`), so with two or more axes linked wx stepped y / z by the x2 / y2 value; the port uses each axis's own width. The Qt-only "R resets the view" key is gone (wx has no R binding). ',' / '.' on a focused clip slider move its linked slab (wx OnSliderKeyDown); a right click on a clip slider = wx OnSliderRClick (low thumb: x2 = x1 + 1, high thumb: x1 = x2 - 1, the axis linked, the other two unlinked and reset; as wx the new value in box units is compared with the voxel size). The Clipping Planes pane saves its defaults on exit (wx ~ClippingView; not in scripted runs). Hovering the V. AOV slider shows the clipping planes (wx OnAovSldrIdle, 100 ms poll; not with "Display Planes" or while capturing). view default `mouse_focus` (no control, as wx) is read / written: when set, 1:1 / Reset Zoom / Reset Rot. give the canvas the focus back. "Intrp." is view-wide (wx SetIntp: pushed to every shown volume in StartLoopUpdate, used by the multi-volume renderer). macOS: a clipping slab of <= 3 voxels (one shown volume or Sync All Chan.) puts the volume renderers in slice mode (wx StartLoopUpdate _DARWIN). Double click on a ruler point (any mode) = wx StartManipulation: a 6-frame animated move (300 ms of streaming per frame, coarsest pyramid level while moving) to the object translation Point(cx - px, -(cy - py), -(cz - pz)); as in wx the point ends up centred horizontally, but the y term moves it away from the centre vertically with the Vulkan y-down projection (wx formula kept). Settings gradient background (wx DrawGradBg) |
| Capture button | `CaptureDialog` (wx OnCapture save dialog + Additional Options, project save) and `RenderCanvasCapture.cpp` (tile rendering, PostDraw readback, group capture, wx TIFF writer in `CaptureWriter.h`). Sizes other than the view size are always tiled (wx: only when both > 4000). Movie export: Record/Export |
| Plugins | Plugins toolbar button + drop-down, a dock pane per GUI plugin, RunPlugin / -p, OnTreeUpdate, plugin state in projects; Fiji Interface, NBLAST Search, Neuron Annotator (see "Batch 4b") |
| Testing aids | window size: `VVD_QT_TEST=layout` (MainWindowLayout.cpp: window / pane / toolbar minimum sizes against the screen, resizeDocks to small sizes, 1280x800 and 1024x768 windows, all plugin panes shown, a floating dock moved off-screen; `[layout]` lines with PASS/FAIL, `layout_*.png`; `VVD_QT_LAYOUT_SIZE=WxH` sets the window size first, `VVD_QT_LAYOUT_NO_PLUGINS=1`), `VVD_QT_STATE_HEX=<file>` (lines `geometry|windowState2|innerState2 <hex>` replace the saved window state for the run; an empty file = no saved state); batch 4b: `VVD_QT_TEST=b4b_ui|b4b_uirt|b4b_fiji|b4b_ipc|b4b_nblast|b4b_nbvol|b4b_nbdlg|b4b_na|b4b_naimp|b4b_nart|b4b_lib` (see "Batch 4b"); batch 4a: `VVD_QT_TEST=b4_views|b4_viewsrt|b4_track` (see "Batch 4a"); batch 3: `VVD_QT_TEST=b3_*` (see "Batch 3" below); `VVD_QT_GRAB`, `VVD_QT_GRAB_VIEW` (3D view pixels), `VVD_QT_SAVE`, `VVD_QT_TEST=ruler|brush|dialogs`, `VVD_QT_TEST=paint|calc|seg|index|nr|hide|ctx|ui|diffuse|rulervol|rulerauto|rulerlist|warp|view3d|na` (MainWindowPaintTest.cpp; rulervol: 2-point ruler from volume 0 to volume 1 (Maximum Intensity), Start / End Volume columns, text export, project saved to `VVD_QT_SAVE` or `<dir>/rulervol.vrp`; rulerlist: the opened project's Measurement list, `VVD_QT_EXPECT_START/_END`; rulerauto (two separated volumes): Auto-select check box, clicks on volume 1 then 0 with 0 current (tree / Properties follow, points = the Maximum Intensity points, Start / End Volume), Edit drag, View Plane locator, empty space, paint-ruler press, Auto-select off unchanged, `a01_measure_list_auto.png`; warp: Import `VVD_QT_WARP_CSV` + Apply Transform with `VVD_QT_WARP_TTYPE/_LAMBDA/_INTERP`, result voxels to `VVD_QT_WARP_OUT` (raw); cells: Import `VVD_QT_CELLS_CSV`, Transform "Cells: shape preserving", one Apply Transform per entry of `VVD_QT_CELLS_RUNS="local,objects,stiffness,device,loo,outprefix;..."` (+ `VVD_QT_CELLS_PARAMS="beta,sigma_min,neurite,samples,min_voxels,radius"`), result voxels to `<outprefix>.raw` (`VVD_QT_CELLS_RAW=0`: md5 only) and both tables through the results dialog's Save CSV, `VVD_QT_CELLS_GRAB=<dir>` pictures of the Measurement and results dialogs; view3d (volA + volB + a mesh): s / w with the focus on the tree / a clipping-pane check box / the canvas, auto-repeat, text box, clamping, ',' / '.', Ctrl+arrows, V, R, Alt+V / Ctrl+B, clip-slider right click, shadows in the three blend modes (+ MIP, direction, off = identical), mesh shadow direction, gradient background, Intrp., slice mode, AOV hover (`ViewPanel::SetCursorOverride`), mouse_focus, annotation mesh, Settings rotation link, ruler-point double click; na: NA-mode double click (a volume with a .lbl); paint: with several volumes shown the object box comes from the stroked (current, last loaded) volume alone and is moved onto its pixels when the default stroke line misses them (two failing checks with a ring-shaped last volume were the test's stroke placement, not a selection bug); nr: both noise-removal paths with raw dumps of the masks / results for an independent check, `VVD_QT_NR_SIZE`, `VVD_QT_NR_THRESH` (raw); hide: Hide Inside / Outside against the Erase / Extract volumes, `VVD_QT_TEST_SHADING=0|1`; ctx: the view's context menu, PickMesh, right click vs drag, visibility undo / redo, `VVD_QT_CTX_INDEX=1` join / split on an index volume; ui: Show/Hide UI, menus, check marks, /ui_layout; scripted brush strokes, undo/redo, keys, blend modes, Extract/Erase checked voxel by voxel, Calculations against the shader formulas, analysis dialogs, index-segment picking; mask counts read back from the GPU; `[paint]` lines with PASS/FAIL on stderr, captures in `VVD_QT_TEST_DIR` or next to `VVD_QT_GRAB`; `VVD_QT_PAINT_UNDO` = history depth for the run, default 5; quits by itself), `VVD_QT_VOL_METHOD`, `VVD_QT_FOG`, `VVD_QT_OVERLAYS`, `VVD_QT_CAPTURE` (+ `_SIZE`, `_COMPRESS`, `_GROUP`, `_PROJECT`), `VVD_QT_LOAD_TIMING` (load timings on stderr), `VVD_PAINT_TIMING=1` / `VVD_QT_PAINT_TIMING=1` (shared engine, wx too: `[ptime]` lines per brush stroke -- press / drag events, frames while held, FlushStrokes, frame-slot wait, VolumeSelector::Select split into undo push, init / diffusion passes (bricks, skipped bricks, uploads, texture-pool swaps, submits, GPU time), mask readback, and the redraw after the release until its brick loop is done), `VVD_QT_TEST=diffuse` (timed Select / Diffuse / Unselect strokes with mask counts, `VVD_QT_DIFFUSE_REPS`, `VVD_QT_DIFFUSE_ITERS` (20/50/100), `VVD_QT_DIFFUSE_DRAG=1` (one drag event per 50 ms tick, frames drawn while held), `VVD_QT_DIFFUSE_PERSP=1`, `VVD_QT_DIFFUSE_ROT=rx,ry,rz`; captures d00-d03), engine A/B switches for the mask passes: `VVD_SEG_NO_PIN=1` (do not keep the segmented volume's textures resident), `VVD_SEG_NO_BATCH=1` (one submit per brick), `VVD_SEG_NO_SKIP=1` (no 2D-footprint brick skip), `VVD_SEG_VERIFY_SKIP=1` (run the skippable bricks anyway and count the texels they change; expected 0), `VVD_QT_STREAM_DEBUG` (per-second brick-loop statistics: frames, bricks drawn, loop progress, texture pool, texture-reuse waits; one `loop done` line per completed brick loop with its wall time), `VVD_NRRD_ZLIB=1` (decode gzip NRRD with zlib instead of libdeflate, for A/B checks), `VVD_QT_VOLPROPS="invert=1,mip=1,shading=0,solid=1,colormap=1,gamma=..,offset=..,lo=..,hi=..,boundary=..,alpha=..,lum=.."` (volume properties applied to all volumes 2.4 s after start-up), `VVD_QT_SPIN="deg,steps,delay_ms"` (scripted rotation, prints fps), `VVD_QT_UP_TIME=<ms>` (streaming time budget for this run), `VVD_NO_BRICK_SKIP=1` (engine: disable the exact empty-brick skip, for A/B checks), `VVD_QT_MOVIE=<file.mp4/.mov/.tif>` (Record/Export Save... without the dialog; `_LEN`, `_FPS`, `_AXIS`, `_DEG`, `_STYLE`, `_REWIND`, `_ROT`, `_SEQ`, `_CROP`, `_KEYS`, `_KEYDUR`, `_MODE`, `_KEEP`, `_PRJ`, `_DELAY`; see QtMain.cpp), `VVD_QT_GRAB_MOVPANEL=<dir>` (pictures of the four Record/Export tabs and the save dialog), `VVD_QT_TIME_SEQ=1` (selects the Open Volume option "Read a sequence as time series" for the session), `VVD_QT_GRAB_MIDPASS=<dir>` (+ `_MAX`, default 60: the presented frames of unfinished streaming passes as `midpass_<n>.png`), `VVD_QT_DATASETS=all|none|cancel|<i,j>` (answers the Zarr/N5 dataset dialog; picture `filedlg_datasets.png` in `VVD_QT_GRAB_FILEDLG`), `VVD_QT_TEST=drop` + `VVD_QT_DROP=a;b` (+ `VVD_QT_DROP_TARGET=frame`: onto the main window instead of the render view), `VVD_QT_TEST=paint` also saves `p19_annotations.vrp` (annotations in projects), `VVD_QT_FILEDLG_RUN="step,..."` (file dialogs / tree save actions in order, then quit: `openvolume`, `openmesh`, `openproject`, `saveproject`, `savevolume`, `savesegmented`, `bake`, `exportmeta`, `select:<i>`, `clip:x0:x1:y0:y1:z0:z1`, `wait:<ms>`; each dialog takes `VVD_QT_FILEDLG_FILE_<NAME>=a;b` and `VVD_QT_FILEDLG_OPTS_<NAME>=key=value,...` (option controls by object name, `filter=<i>`), `VVD_QT_GRAB_FILEDLG=<dir>` saves pictures; see FileDialogs.h). Scripted runs (`VVD_QT_CAPTURE/GRAB/MOVIE/FILEDLG_RUN/TEST`, `TestRun.h`) do not write the settings file, the window state or the brush / movie default_*.dft files |
| Engine messages / progress | Qt dialogs via `QtUIBackend` |

Not yet: meshes, depth peeling / "Depth" blend mode, streaming (mem-swap)
rendering of very large data, rulers, brush, capture/movie, legends, scale
bar, colormap bar, save project, settings persistence, Wayland surfaces
(run with `QT_QPA_PLATFORM=xcb` on Wayland). (Historical list; most of it
is done, see the table above; plugins: batch 4b.)

## Remaining work, per file

Line counts are the wx-dependent `.h + .cpp` today. Effort is a
best-guess for one engineer familiar with the code base, including
testing against the wx app.

### The render view — the big one

| File | Lines | Plan | Effort |
|---|---|---|---|
| `VRenderView.cpp/.h` (`VRenderVulkanView`) | 26 177 | Do **not** translate 1:1 into a Qt widget. Extract a toolkit-neutral `RenderScene` (camera, layer list, draw passes: composite / depth / multi, depth peeling, streaming loop, overlays, rulers, brush strokes, capture) that takes matrices and framebuffers and knows nothing about windows or events. `QtGui/RenderCanvas` then shrinks to event translation + swapchain. Start by moving `Draw*`, `HandleProjection/Camera`, `InitView`, `PopVolumeList/PopMeshList`, `Prep*Buffer` into the scene class while the wx view still calls them; that keeps the wx app working throughout. | 8–10 weeks |
| — of which `LMTreeCtrl`, `LMSeacher`, `LegendListCtrl`, `LegendPanel` (in the same header) | ~1 200 | Plain Qt widgets, low risk. | 1 week |

### Frame and panels

| File | Lines | Plan | Effort |
|---|---|---|---|
| `VRenderFrame.cpp/.h` | 8 954 | Split into (a) application logic that is toolkit-neutral — project load/save (`OpenProject`/`SaveProject`, ~2 000 lines of `wxFileConfig` ⇄ data model, port to `vvd::IniFile`), settings, batch/script running — and (b) `QtGui/MainWindow`. Move (a) into a `ProjectIO` / `AppController` class shared by both GUIs first. | 4–5 weeks |
| `TreePanel.cpp/.h` | 7 134 | Full workspace tree: views, groups, meshes, annotations, drag/drop reordering, context menus, multi-selection, icons. Extend `QtGui/TreePanel` (QTreeWidget → QTreeView + model when drag/drop lands). | 3 weeks |
| `ClippingView.cpp/.h` | 4 294 | Clipping planes pane: 6 linked sliders, rotations, sync options. Mechanical. | 1.5 weeks |
| `AdjustView.cpp/.h` | 3 122 | Output adjustments (gamma/luminance/eq per channel). Mechanical. | 1 week |
| `VPropView.cpp/.h` | 3 643 | Remaining bits beyond `QtGui/VolumePropPanel`: colormap combos, ID colour modes, ROI names, group sync, save/load defaults (`vvd::IniFile`). | 1 week |
| `VAnnoView` | ~700 | Annotation text view (MPropView / MManipulator / APropView are ported: batch 3). | 2 days |
| `VMovieView.cpp/.h` | 1 908 | Ported (`MoviePanel`, 4D script batch mode included). | - |
| `ListPanel`, `DragDrop` | 420 | small | 2 days |

### Dialogs (mechanical, mostly QGridLayout + slots)

| File | Lines | Effort |
|---|---|---|
| `TraceDlg` | 3 764 | ported (`TraceDialog`, batch 4a) |
| `BrushToolDlg` | 2 653 | 1 week |
| `SettingDlg` | 2 186 | 1 week (also move settings I/O to `Core`) |
| `MeasureDlg` | 1 859 | 1 week |
| `RecorderDlg` | 1 339 | 3 days |
| `DatabaseDlg` | 1 314 | 3 days (its `wxThread` download workers → `vvd::Thread` + curl in `Core`) |
| `CountingDlg`, `ConvertDlg`, `NoiseCancellingDlg`, `ColocalizationDlg`, `HelpDlg`, `Tester` | ~1 800 | 1 week total |

### Plugins

| File | Lines | Plan | Effort |
|---|---|---|---|
| `PluginController/*`, `PluginManager`, `InterprocessCommunication` | ~550 | Done (batch 4b): `QtGui/PluginApi.h` (`vvd::Plugin`, `vvd::QtGuiPlugin`, `vvd::PluginHost`), `QtGui/QtPluginManager`. The wx ABI stays for the wx app. | - |
| `plugin_fiji`, `plugin_nblast`, `plugin_NA` | 7 382 | Ported: `plugin_*/qt/` (compiled into VVDViewerQt). | - |

### Odds and ends

* `Main.cpp` (wx `wxApp`) stays for the wx build; `QtGui/QtMain.cpp` is its counterpart.
* `Formats/png_resource.h` and `img/` are wx bitmap resources; the Qt build should use a `.qrc`.
* `Core/VVDString.h` still supports the wx interop; delete that block when the wx GUI is retired.

### Total

Roughly **30–35 engineer-weeks** after phase 1, dominated by the render-view
extraction and `VRenderFrame`. Doing the `RenderScene` extraction first is
strongly recommended: it is the one step that pays off even if the Qt effort
pauses, because it also removes 20k lines of GUI/engine entanglement from
the wx app.

## Suggested order

1. `RenderScene` extraction (wx app keeps working; Qt canvas gains every render feature at once).
2. `ProjectIO` extraction from `VRenderFrame` → Qt gets full project load/save.
3. Full `TreePanel`, `ClippingView`, `AdjustView`, `VMovieView`.
4. Dialogs, in order of user demand (Brush and Measure first).
5. Plugin interface, then plugins.
6. Retire the wx build; drop `VVD_WX_STRING_INTEROP`, `WxUIBackend`, `Main.cpp`.

## Window size

* The main window fits the screen: the restored geometry is clamped to the
  screen's available area (shrunk and moved on-screen), floating docks
  restored off-screen are moved back (`MainWindow::FitToScreen`,
  MainWindowLayout.cpp, after restoreState and once shown). The window's
  minimum width used to be ~2100 px (Properties' volume page 1708 px + the
  left column 393 px; render view control rows 1073 / 914 px; NBLAST pane
  947, Fiji 514), wider than a laptop screen. Now it is ~700 x 535: the
  Properties pages, the render view's top and bottom control rows and
  Record/Export scroll horizontally below their content's width,
  Output Adjustments / Clipping Planes vertically below their height,
  plugin panes both ways (`PaneScrollArea.h`: no frame, no scroll bars and
  the content's own size hint at or above its minimum, so the layout at the
  usual sizes is unchanged: window pictures at 2102x900 are pixel-identical
  to before). Without a saved state the left column starts at
  max(320, Record/Export's width) once shown (the old effective width).

## Testing notes

* `VVD_QT_GRAB_LEGEND=<dir>` (+ `_DELAY`, default 5000 ms): opens the render
  view's Legend drop-down (`QtGui/LegendMenu`, which replaced the floating
  LegendDialog), clicks rows / Show All / Hide All, checks that the menu stays
  open and the SetLegend flags (`[legend]` PASS/FAIL lines), saves
  `legend_menu_<step>.png` and `legend_view.png`, quits.
* `VVD_QT_TEST=legend2` (+ `VVD_QT_TEST_DIR`; volA.nrrd on the command line,
  volB.nrrd / b1.nrrd / b2.nrrd in the same folder): the Legend drop-down
  after volumes are added while running (LoadVolumes, LoadVolumes with the
  menu open, drag & drop) -- row count, every row inside the menu (QMenu
  caches its action rects: `LegendMenu::Reload` re-lays the menu out when the
  list changes size, also before it is shown); row clicks / Show All / Hide
  All with the menu open, each checked by a passive capture of the next
  presented frame (`RenderCanvas::CaptureNextFrame`, no frame requested of
  its own) and by `screencapture` of the canvas (what is on the screen),
  compared with a base picture (bottom band = legend); a second view's menu
  acts on that view only. `[b4]` PASS/FAIL lines, pictures in the test dir.
  Legend clicks redraw overlays only (`RenderCanvas::RefreshOverlayChange`:
  no brick-loop restart; a full redraw only when no frame is on the surface).
* HiDPI text: on Retina the Qt swapchain images are physical pixels (Qt's
  CAMetalLayer has contentsScale 2; the wx one keeps 1) while overlays lay
  text out in logical units. `RenderCanvas` passes the logical size to
  `TextRenderer::SetSwapchainLayoutSize`, and TextRenderer rasterizes glyphs at
  size x scale on swapchain images, so text has the wx apparent size and
  `RenderTextDims` stays logical (legend, Info, scale bar, ruler, colormap,
  annotation and brush labels). The wx view never sets it (unchanged).

* Linux compile check used during phase 1: a small out-of-tree CMakeLists.txt
  style harness — three object targets (`engine` without any toolkit,
  `gui` with wxGTK 3.2, `qtgui` with Qt 6.4). Worth turning into a CI job:
  it catches toolkit leaks into the engine immediately.
* Behavioural risks introduced in phase 1a were reviewed and fixed
  (miniz FILE handling on Windows, C-locale INI parsing, curl timeouts,
  `%f` formatting in annotation text files, hidden-file filter in zip
  extraction). Regression-test: open/save project, `.idi` and zipped TIFF
  loading, export segments, remote (URL) volumes.

## Streaming engine notes (shared FLIVR / Vulkan code)

* **Empty-brick skip (always on).** `VolumeRenderer::invisible_brick_max`
  derives, from the VRay shader's transfer function, the largest brick
  maximum for which every sample of the data pass is exactly `vec4(0)`
  (below `lo - boundary`, above `hi + boundary` when inverted, or all-zero
  with `1/gamma >= 1` and `offset > 0`; not in solid, depth-map, index,
  mask/label, NA-label or highlight modes, nor for pyramid data). Such bricks
  are counted as drawn but never uploaded or rasterized; since a zero
  fragment leaves the OVER / MIP / ADD blends bit-identical, captures are
  byte-identical (checked in all three render methods, rotated, inverted,
  MIP, shading on/off, colormap, fog, thresholds, gamma). Brick maxima come
  from a 16^3 block-max grid computed right after decoding
  (`VL_Nrrd::computeBlockMax`, in the loader workers) or, failing that, from
  a scan on first use; they are cached on `TextureBrick` and survive
  transfer-function changes, and are dropped when the data change
  (`set_modified`/`set_dirty`/`eraseBricksFromTexpools`/new buffer).
* **"Skip empty bricks" setting.** Now repaired: computes the maxima at load
  time and gives all-zero bricks priority 1, which are then never drawn even
  where the exact test would draw them (legacy FluoRender behaviour; may
  differ visually with gamma > 1 or inverted data).
* **Progress guarantee.** `TextureRenderer::frame_budget_exhausted()` only
  reports a used-up budget once the frame has drawn a brick, so a frame
  whose fixed overhead exceeds the budget still advances the loop (volume,
  multi-volume and mask/label passes, wx and Qt canvases).
* **Noise removal (Analyze > Noise Removal > Remove, `VolumeSelector::NoiseRemoval`).**
  The FLT_NR filter writes the data bricks, but `draw_mask` marked only the
  mask dirty, so `return_volume` never read the filtered data back (the
  `_NR` volume was the unchanged copy); it now marks the component it wrote.
  The filter also only lowered a voxel to a value `> 0`, so noise surrounded
  by exact-zero background (binary / cropped data) was never removed; the
  test is now "a lower neighbour was found" (`>= 0`). On zero-background data
  `_NR` equals the Noise Reduction dialog's Erase result voxel for voxel; on
  data with a non-zero background the noise is lowered to the surrounding
  values (wx algorithm).
* **Hide Inside.** The ray caster skipped samples whose interpolated mask was
  > 0.5, which leaves half-voxel shells of every surface inside the hidden
  region (speckled layer). For intensity data the sample is now the
  trilinear interpolation of data * (1 - mask) (8+8 texel fetches, also for
  the gradient), i.e. what the Erase volume shows (`VVD_HIDE_INSIDE_LEGACY=1`
  restores the old test for A/B checks). In Depth mode the
  MultiVolumeRenderer passed an identity mask transform and overwrote the
  gradient's z step (only right for single-brick volumes); it now uses the
  brick box as VolumeRenderer.
* **Texture reuse.** Overwriting a pooled brick texture no longer drains the
  queue: textures carry the serial of the last frame using them, idle
  textures are preferred, else the least recently used one, waiting only for
  its frame's fence; a drain remains only when every candidate is used by the
  frame being recorded.

## Batch 3: panels and dialogs (wx parity)

* **Mesh Manipulate** (`MeshManipPanel` in `MeshPropPanel.cpp`, wx
  MManipulator / OnSelection case 6): the tree's "Manipulate" shows it in the
  Properties dock ("Manipulations - <mesh>"), the previous mesh loses its
  box, m_cur_sel_type = 6. Translation / rotation / scaling boxes ("%.2f"
  from GetData, "%.3f" after Enter or a spin arrow, +-1 per arrow) set the
  mesh transform and redraw.
* **Annotation properties** (`AnnotationPropPanel`, wx APropView, OnSelection
  case 4): memo + Update (read-only and disabled with `Memo Update: 1`);
  for annotations with a mesh Transparency (0..255 -> "%.2f"), Min Score
  (slider max = MaxScore + 1), "Number of Particles" (the Measurement count of
  the texts above the threshold), Color (mesh diffuse, ambient 0.3 x diffuse);
  without a mesh those controls are hidden (labels stay, as wx). The Clipping
  pane follows the annotation's volume / mesh (cleared when it has neither).
  Deliberate difference: wx counted the particles before applying the new
  threshold (the label showed the previous value's count); the port applies
  the threshold first.
* **Measurement annotation rows** (wx RulerListCtrl::UpdateRulers /
  OnGetItemText / Export, m_show_anno): after the rulers, one row per text of
  every shown annotations layer -- for mesh annotations only the texts whose
  score (first info field) is above Min Score (sorted list, 10000-step
  bracketing as wx) -- name "<text>  (<layer>)", points (x, y, z) x the
  volume resolution, info = score or "<n> voxels". Export writes them with the
  wx columns (+ the two empty start / end volume columns of the Qt header).
  Annotation rows cannot be edited / dragged / deleted (no ruler).
* **Volume pane**: Save as Default = wx OnSaveDefault (the shown values ->
  default_volume_settings.dft next to the executable / in the bundle's
  Resources, else the user config dir; same keys and order as wx; also the
  DataManager m_vol_* defaults); scripted test runs write the file only when
  `VVD_QT_VOLUME_DFT=<path>` names one. Reset = wx OnResetDefault (the
  DataManager defaults: gamma, boundary, offset, thresholds, low / high
  shading via SetMaterial, alpha, sample rate, luminance as the HSV value of
  the colour, colormap low / high, shadow intensity on the volume; Alpha /
  Shading / Inv / MIP / Smoothing / Shadow on the group when Sync is on).
  Enable states as wx GetSettings / OnAlphaCheck / OnShadingEnable /
  OnMIPCheck: MIP disables alpha, right threshold, boundary and luminance
  (gamma / saturation too with a plain colormap) and renames "Threshold:" to
  "Shade Threshold:"; in Depth mode MIP is refused with "MIP is not supported
  in Depth mode.". Shadow, Smoothing and Sample Rate (and shadow intensity)
  go to every volume of the view in Depth mode (wx m_vrv->GetVolMethod()),
  otherwise to the group (Sync, or Smoothing / Sample Rate / intensity of a
  group in depth blend) or the volume. Max Value (wx UpdateMaxValue):
  scalar scale 65535 / value for 16-bit, 1 / value for float, 1.0 for 8-bit
  (was 65535 / value for all); the box shows max(255 | 1, max) and float
  sliders run 0 .. max x 100 as wx.
* **Mesh Sync** (wx MPropView::UpdateSync): a mesh in a mesh group syncs its
  group, a view-level mesh the shown meshes that are in no group.
* **Paint-ruler mode** (wx int modes 7 / 8): the brush and ruler tools mirror
  one interaction mode (MainWindow): ruler creation (5) puts the brush in 5,
  Shift then gives mode 7 (wx SetBrush; selector mode 1, 8 for ruler type 3)
  and the ruler tool waits in 7; a press clears the selection (paint mode 6),
  the release segments and adds the selection centre (VolumeSelector::
  ProcessSel(0.01), RulerTool::AddPaintRulerPoint: info "v<n>" = selected
  size) -- for ruler type 3 the box point AddRulerPoint(x, y) -- then 8 -> 7;
  releasing Shift returns both to 5. Painting from ruler-edit mode (6) or
  camera mode ends the ruler mode as in wx.
* **Noise Reduction Erase** = wx TreePanel::BrushErase: Calculate(11) with the
  NA-mode label volumes of the group for a volume in NA mode, else
  Calculate(6).
* **Pen pressure**: wx reads Wintab packets on Windows only (and that code is
  commented out); the port takes QTabletEvent pressure on every platform
  (pressure x 2, i.e. wx's pkNormalPressure / 512 for a 1024-level pen) while a
  pen touches the view. With brush_use_pres (project key) the brush radii,
  the stamp spacing and the cursor use it per drag segment; mouse strokes and
  brush_use_pres off keep 1.0 (identical masks, checked).
* **4D script** (`MainWindowScript.cpp`, wx Run4DScript + task handlers,
  VMovieView batch mode): with Settings > 4D Script on and the file present,
  Play runs the script on every shown volume at the start and once per new
  time point (the timer stops meanwhile; the playback ends after one pass).
  As in wx the view takes the run flag / file at each Set4DSeqFrame. Tasks:
  noise_reduction (component analysis threshold / voxelsize, Calculate(6)
  without adding, saved as <savepath><name>_T<t>.tif|.nrrd, mask + label
  deleted), selection_tracking (labels of the previous time point under the
  mask, size_limit, select the same values in the new .lbl; no trace group
  in the Qt app), random_colors (new .lbl, CompExportRandomColor, three
  _COMP<i>.tif), separate_channels, external_exe (empty in wx), fetch_mask
  (.msk / .lbl of the new time point). Fixes found on the way: the 4D frame
  check uses the volume's time instead of the reader's (prefetched time
  points left the reader time behind, so going back to the start frame was
  skipped), and VolumeSelector::CompExportRandomColor / CompExportMultiChann
  no longer dereference a missing mask when !select (shared engine; the
  random_colors task after noise_reduction crashed).
* **Test modes** (Settings [test mode], wx VRenderFrame ctor): speed = the
  view redraws on every idle; param = the Tester dialog (wx TesterDlg: a
  "Click Me" button, all handlers commented out); wiref = m_vol_test_wiref,
  m_draw_bounds (DrawBounds is empty in wx) and the grid (wx DrawGrid).
  `VVD_QT_TEST_MODE=speed,param,wiref` turns them on for a run.
* **Mesh clipping fix** (shared engine, `FLIVR/glm.cpp`): glmBoundingBox looped
  over vertices 1 .. n-1 (the arrays are 1-based), so a mesh whose last vertex
  was an extreme one got too small a box, and the mesh clipping planes (in
  that box's normalized coordinates) cut it -- the test octahedron lost its
  half beyond the last vertex in every blend mode (wx too). The annotation
  path draws without those planes. Real meshes (SWC and OBJ test meshes) render
  byte-identically before / after.
* **Fill Holes check**: VolumeCalculator::FillHoles (type 9) marks A > threshold
  x 255 (8-bit scale) plus the enclosed holes, binary 0 / 255; soft edges at
  or below the threshold are not part of it. The old test expectation
  ("every nonzero voxel of A") was wrong; the calc test now checks that every
  voxel above the threshold is 255 in a binary result.
* Tests: `VVD_QT_TEST=b3_panels|b3_maxval|b3_paint|b3_nrna|b3_script4d|b3_modes`
  (`MainWindowBatch3Test.cpp`; `VVD_QT_B3_DATA` = folder with octa2.obj /
  octa3.obj, `VVD_QT_B3_DATA4D` = the ts_T1..3 series with .lbl / .msk,
  `VVD_QT_VOLUME_DFT`, `VVD_QT_TEST_MODE`), "[b3] PASS/FAIL" lines.

## Cell (shape preserving) warp

Measurement > Transform "Cells: shape preserving" is a C++ port of the ImageJ
plugin `Apply_BigWarp_RigidCells.java` ("Apply BigWarp to Cells (shape
preserving)"), in the toolkit-neutral `RigidCellsWarp.h/.cpp` (compiled into
both apps; the Qt dialog is the only UI so far).

* Landmarks = the ruler list exactly as `WarpVolumeByRulers` (2-point ruler:
  point 0 moving, point 1 target; locator: zero-displacement anchor), in
  physical units, i.e. the BigWarp CSV coordinates. Output grid = the current
  volume's grid (the plugin with moving = target image); label values and bit
  depth kept, nearest neighbour, first object written wins; name + `_CELLS_ADAPTIVE`
  / `_CELLS_TRANS` / `_CELLS_RIGID` / `_CELLS_SIM`.
* Voxel convention is the plugin's: physical = index * spacing (origin 0).
  The GPU resampler of the other transforms puts voxel centres at
  (index + 0.5) * spacing, half a voxel off; both read the same ruler
  coordinates.
* Stiffness = the dialog's slider, the plugin's dimensionless value:
  lambda = stiffness * mean squared distance of the TPS source landmarks
  from their centroid, on the kernel diagonal. The cell warp's TPS maps
  moving -> target (source = moving landmarks), the resampling TPS of
  "Thin Plate Spline" maps target -> moving (source = target landmarks), as
  in "Apply BigWarp with Stiffness"; same convention, but the two radii come
  from different landmark sets.
* Options (Measurement dialog, saved under `/rigid cells` in the settings):
  local transform (Adaptive / Translation / Rigid / Similarity), objects
  (26-connected components per label / each label value), rigidity factor,
  minimum smoothing, neurite radius, TPS samples per object, minimum object
  voxels, landmark search radius, compute device (CPU threads or an OpenCL
  GPU, loaded at run time: OpenCL.framework / OpenCL.dll / libOpenCL.so.1;
  `VVD_NO_OPENCL=1` hides it), leave-one-out diagnostics.
* Results dialog (`RigidCellsReportDialog`): the plugin's objects and
  landmarks tables (same column names, row label as "Label"), Save CSV (full
  precision), the plugin's log lines and warnings.
* Every expression follows the Java order and `-ffp-contract=off` is set for
  the file, so results are bit-identical to the plugin's (checked: all modes,
  both object modes, CPU and GPU). Deliberate difference from the original
  plugin: its `Procrustes.fit` tested `mode > 0` (TRANSLATION is 1), so
  "Translation" also fitted a rotation and gave the same result as Rigid; the
  port tests `mode > TRANSLATION`, so Translation is a pure translation
  (identical to the plugin with the same one-line fix; Adaptive / Rigid /
  Similarity unchanged).


## Batch 4a: several render views, Components & Tracking

### Render views (`MainWindowViews.cpp`)

* **Where views come from (as wx).** wx creates further views only in
  OpenProject (/views/num > 1 -> CreateView); "Open New VVDViewer" starts a
  new process (unchanged). The tree's view item has "Close" for every view
  but the first (wx DataTreeCtrl::OnCloseView -> DeleteVRenderView: the
  layers leave the view, the data stay in the DataManager).
  `MainWindow::CreateView` / `DeleteView` are public (tests).
* **Layout** = wx CreateView: view 0 is the centre of a nested
  `QMainWindow` (`m_view_area`, the wxAUI layer 0), every further view a
  dock to its right (Dockable, floatable, no close button, floating size
  600 x 400, min 300 x 200) split left-right with equal widths
  (OrganizeVRenderViews(1)); the Output Adjustments / Clipping Planes panes
  stay outside (layer 1).
* **Per view** (wx members of VRenderVulkanView): canvas (camera, layer
  list, overlays, clipping state, blend mode, streaming loop), ViewPanel
  bars, BrushTool, RulerTool (+ ruler list), ViewOverlays, VolumeSelector,
  VolumeCalculator, trace group. New views take the Settings render values
  (peeling layers, micro blending, mouse interactivity, gradient background,
  shadow direction, ruler point mode / transform / time dependence, font,
  wireframe test mode) and the Output Adjustments defaults, as wx CreateView;
  a view created while the Settings rotation link is on joins it.
* **Active view.** The panes follow the view of the tree selection (wx
  OnSelection(type, vrv, ...)) or the view last clicked: Properties, Output
  Adjustments, Clipping Planes, Measurement (its ruler tool), the brush
  dialog (its brush), Components & Tracking, the Analyze dialogs
  (AnalysisContext = the active view's canvas / selector / calculator).
  Record/Export records the view of its own "Capture" combo (wx
  m_views_cmb; `MoviePanel::ResolveView`), the combo follows CreateView /
  DeleteView (wx AddView / DeleteView). Open Volume / Mesh / URL load into the
  first view (wx GetView(0)); a drop on a view loads into that view (wx
  DnDFile(frame, vrv)).
* **Keys.** As wx every view polls the keys (Ctrl+arrows pan each view, V
  hides every view's masks). Deliberate difference: the actions forwarded to
  frame panes -- d / space / a (time step), s / w (linked clip planes), Alt+V,
  Ctrl+B, Ctrl+Z / Ctrl+Y, f / l, Esc -- run once, for the active view
  (`RenderCanvas::SetKeyCanvas`); wx ran them once per view (with two views a
  press stepped the movie twice).
* **Linked rotations**: the view bar's Link and the Settings link (wx
  LinkAllRotations / OnRotLink) act on every view.
* **Tree**: one "Render View:<n>" item per view (`TreePanel::SetCanvases`,
  `CanvasOf(item)`), every tree action on the item's view. wx keeps every
  drag inside one view (MoveLayertoView = out of a group to the view level);
  Qt extension: a volume / mesh dropped on another view's item, group or
  layer moves to that view (wx does nothing there).
* **Vulkan.** wx creates a VVulkan per view (the shared-context argument is
  ignored), while the engine's static renderer state points at the last one;
  the port shares view 0's context: `VVulkan::beginSurface / finishSurface /
  activateSurface / removeSurface` keep one swapchain (+ frame buffers,
  present semaphores, depth buffer, size, window) per view and swap it into
  the VulkanExampleBase members before a view draws (shared engine code; the
  wx app does not call it). The static brick-loop state of TextureRenderer
  (start / done_update_loop, brick counters, time budget) is saved / restored
  per canvas (`RenderCanvas::MakeCurrent`), so two views stream
  independently. View names: "Render View:<n>", n = highest in use + 1 (wx
  counts every view ever created).
* **Projects**: /views/num = number of views; /views/<i>/layers,
  /views/<i>/properties (all wx keys, the view's painting parameters, its
  unmodelled keys kept) and /views/<i>/rulers per view, in the wx order;
  group ids reset once before the views (wx). Deliberate difference: opening
  a project first closes the further views (wx's code for that is commented
  out, so wx kept adding views and filled the last one with view 0's data).
  With one view the files are byte-identical to before.

### Components & Tracking (`TraceDialog.cpp`)

* Port of TraceDlg / TraceListCtrl, every page and handler: Track Map (Load /
  Save / Save As .track, Generate with "X" iterations, Refine T, Refine All,
  progress), Selection (ID text shared with the Linkage page, X, FullCompt,
  Replace, Append ("all" = every label), Clear, component size), Linkage
  (Append, Clear, Auto Link check tool, Excl. Link, Link IDs, Isolate,
  Unlink IDs), Modify (New ID text coloured by id hue, X, Append, Clear,
  Auto Assign ID check tool, Assign ID, Add ID, Replace ID, Combine, Divide;
  Segment hidden and empty as wx), Analysis (Rulers, UniIDs (consistent ids
  written to a folder), Compnts, Links, Save As), Ghosts (Tail, number, Lead),
  ID lists of the current / previous time point (tree glyphs, id colours,
  multi-selection, Copy / Delete menu, Delete / Backspace, Ctrl+C),
  Backward / Forward (VMovieView Down/UpFrame), Output text.
* Reachability as wx: the dialog exists hidden from start-up (wx: a hidden
  floating pane); every menu entry that would show it is commented out in
  wx (Analyze drop-down, Tools menu, the tree's "Components && Tracking..."),
  so the port adds none -- `MainWindow::ShowTraceDlg()` (wx ShowTraceDlg)
  shows it, nothing in the UI calls it, as wx. The f key (CellFull) and l key
  (CellLink(false)) and the brush hooks work hidden, as in wx. Making it
  reachable needs one menu entry (wx ID_Trace), left to a decision.
* View hooks: after every brush Segment, auto ID (selector mode 1 / 2 / 4:
  CellNewID(true), 3: CellEraseID) and manual assist (CellUpdate unless
  auto ID, CellLink(true)) (`BrushTool::segmented`, wx Segment end);
  GetTraces (`RenderCanvas::GetTraces`, `RenderCanvasTraces.cpp`); the trace
  group per view (Create / Load / Save); the dialog's GetSettings on volume
  selection (wx UpdateTree / OnSelChanged). The 4D script task
  selection_tracking now uses the view's trace group as wx (with a track map
  the new time point's selection is the mapped cells, UpdateCellList /
  FindCell, then TraceDlg::GetSettings).
* As wx (shared engine): DrawTraces' body is commented out in wx, so nothing
  is drawn (ghost settings only change the trace group);
  TrackMapProcessor::GetMappedRulers is commented out in Tracking/TrackMap.cpp,
  so "Convert to: Rulers" adds no rulers. Trace groups are not saved in
  projects (wx neither). Measure() reads 8 / 16-bit data only as wx (other
  types: value 0 instead of wx's uninitialized value).

### Tests

* `VVD_QT_TEST=b4_views` (volA.nrrd volB.nrrd mesh.obj): second view,
  volB dragged onto it, linked / unlinked rotations, different cameras and
  background, active view by click and by tree selection (panes, brush
  dialog, Measurement, tracking dialog, selector), a ruler in view 2 only,
  the Record/Export view combo, captures v0.tif / v1.tif, a 2-view project
  p2.vrp (/views/num, /views/1/...), "Close" (only on view 2). `b4_viewsrt`
  (p2.vrp on the command line, `VVD_QT_SAVE`): the views, names and rulers
  back, captures r0 / r1, saved again (fixed point after one round trip;
  the first round trip differs only in `translation` and the key order of
  the passthrough sections, as for one view).
* `VVD_QT_TEST=b4_track` (tk_T1.nrrd + VVD_QT_TIME_SEQ=1: 3 time points with
  3 moving blobs whose label ids change per time point): Generate (links
  1->11, 2->13, 3->12, 11->21, 13->22, 12->23), Refine All, Save As / Load /
  Save, UniIDs, Append / Replace / Clear, f key, size filter, selection_tracking
  to T 1 / T 2 (mapped cells, ID lists, T labels), Unlink / Link / l key /
  Excl. Link / Isolate, Rulers (none, as wx), Compnts, Links, Save As,
  Assign / Replace / Combine / Divide ID (.lbl written next to the data, as
  wx), auto ID after a Highlight stroke. "[b4] PASS/FAIL" lines; pictures of
  the dialog (`trace_*.png`) in `VVD_QT_TEST_DIR`.


## Batch 4b: plugins

### Plugin system (`QtGui/PluginApi.h`, `QtGui/QtPluginManager.*`, `QtGui/MainWindowPlugins.cpp`)

* **Interface.** `vvd::Plugin` is the toolkit-neutral part of wx
  `wxGuiPluginBase` / `wxNonGuiPluginBase` (no wx, no Qt): `GetName` (the id
  of RunPlugin / `-p` / PluginExists), `GetDisplayName` (menu, pane caption,
  project keys), `GetId`, `IsGui`, `OnInit` (application start), `OnDestroy`
  (close), `OnTreeUpdate`, `OnRun(options)`, `Work` (non-GUI plugins),
  `Load/SaveProjectSettingFile`. `vvd::QtGuiPlugin` adds the widget factory
  `QWidget* CreatePanel(QWidget*)`. `vvd::PluginHost` is what a plugin may call
  in the application (the wx plugins cast GetVVDMainFrame() to VRenderFrame*):
  data manager, current selection (wx GetCurSelType / Vol / Mesh), views,
  tree, brush, adjust / properties panes, AddVolume, ReplaceVolumeData,
  RemoveGroup, UpdateTree / UpdateTreeIcons / RefreshVRenderViews,
  OnSelection, StartupLoad(files, datasize, descs), LoadMeshes,
  UploadFileRemote, the plugin window calls and the interop message (wx
  `wxEVT_GUI_PLUGIN_INTEROP` -> `QtPluginManager::interop`, queued).
  `MainWindow` implements it.
* **Built in.** The three wx plugins call into the frame, the views and the
  engine all over (DataManager, readers / writers, VolumeData); as shared
  libraries they would need the executable's symbols at load time
  (`-bundle_loader` / exported executable symbols, an import library on
  Windows) and would duplicate engine statics otherwise. They are compiled
  into VVDViewerQt instead (`plugin_fiji/qt`, `plugin_nblast/qt`,
  `plugin_NA/qt`, globbed by `cmake/QtGui.cmake`) and registered in
  `QtPluginManager::LoadAllPlugins` in the order the wx loader lists them
  (file names in `PlugIns/gui`): Fiji Interface, NBLAST Search, Neuron
  Annotator Plugin. **Shared-library plugins** are still loaded, as wx does,
  from the wx plugins folder (macOS `<bundle>/Contents/PlugIns`, else
  `<exe dir>/plugins`) but its subfolder `qt` (wx: `gui` / `nongui`), so
  neither front end loads the other's libraries: every `*.dylib` / `.so` /
  `.dll` exporting `vvdCreateQtPlugin` / `vvdDeleteQtPlugin` (C linkage; may
  rely on the virtual interfaces and Qt only). Tested with a sample non-GUI
  library (`b4b_lib`). `VVD_QT_NO_PLUGINS=1` skips the plugin system
  (debugging aid).
* **UI (wx VRenderFrame 396-434, 7853-8013).** The toolbar's Plugins button
  pops up one entry per plugin (display names, GUI plugins first; wx
  OnPlugins / OnPluginMenuSelect): a GUI plugin's pane is shown (created on
  first use), a non-GUI plugin runs `Work()`. Panes are `QDockWidget`s
  (caption = display name, closable, movable, floatable; object name
  `PluginDock_<name>`). Deliberate difference: wx docks a pane Left().Layer(3),
  i.e. stacked under Workspace / Record; the Qt pane goes next to the
  Workspace column (split side by side), the plugins' panels being large;
  the pane starts at most a third of the window wide, scrolls below its
  panel's minimum size (PaneScrollArea), and a further plugin pane is tabbed
  with a docked one already shown, so plugin panes never widen the window.
  ToggleVisibilityPluginWindow(name, false) on a pane that does not exist does
  nothing (wx: the creation code in that branch is unreachable).
  RunPlugin(name, options, show), PluginExists, IsCreated/IsShownPluginWindow,
  CreatePluginWindow as wx. `-p "<name> <options>"` on the command line runs
  RunPlugin(name, options + " -d <desc>"..., true) after the start-up files
  (wx Main.cpp; the name is the text before the first blank, so only plugins
  without a blank in their name: NAPlugin).
* **OnTreeUpdate**: `TreePanel::treeUpdated` (end of UpdateTree,
  UpdateTreeIcons, UpdateTreeColors, where wx calls
  m_plugin_manager->OnTreeUpdate()).
* **Projects**: on save, for every GUI plugin *whose pane exists*:
  `/ui_layout/<display name>` (shown), `<display name>_float`, and
  `<display name>_setting` = `<project>_files/<display name>.set` when the
  plugin wrote that file (SaveProjectSettingFile; only the NA plugin does).
  On open: the pane is shown / hidden, floated / docked, the setting file
  loaded (wx OpenProject 7345-7366; at the end of OpenProject). Deliberate
  difference: wx writes the keys (and creates the `_files` folder) for every
  plugin; here only for the ones used, so projects saved without using a
  plugin are byte-identical to before (checked: `b4b_ui`; keys of a wx
  project are kept through the passthrough). A capture with and without the
  plugin system is byte-identical.
* **Settings files** (wx Load/SaveConfigFile): `fiji_interface_settings.dft`,
  `NBLAST_plugin_settings.dft`, `NBLAST_database_list.txt`,
  `NA_plugin_settings.dft`, same keys / format (IniFile = wxFileConfig) and
  place as wx (bundle Resources on macOS, else next to the executable or the
  user config dir); written when the panes are destroyed (wx: the window
  destructors) -- never in scripted test runs (TestRun.h).
* Shared engine fix found by the NA tests: `NrrdScaler::Run` waited for its
  worker threads with a counter the threads incremented themselves; when none
  had started yet the wait ended at once, `VolumeData::NrrdScale` returned an
  empty (unscaled) volume and the threads then used the destroyed scaler
  (`recursive_mutex lock failed`, abort). The counter is now incremented
  before each thread starts (wx and Qt).

### Fiji Interface (`plugin_fiji/qt/FijiInterfacePluginQt.*`, `WxIpcServer.*`)

* **Server = wxTCPServer on QTcpServer, byte for byte** (`WxIpcServer.h` has
  the full wire format, from wx 3.3 `src/common/sckipc.cpp`): little-endian
  int32, strings as int32 length + UTF-8, IPC_CONNECT(10) + topic ->
  IPC_CONNECT / IPC_FAIL(9); IPC_POKE(3) item, format byte, int32 size, data;
  IPC_EXECUTE / ADVISE / ADVISE_START (reply 4) / ADVISE_STOP (reply 7) /
  REQUEST (reply FAIL) / DISCONNECT(11); unknown codes and zero-size data
  -> IPC_FAIL. `Poke(item, wxString)` = format wxIPC_UTF8TEXT (17) with the
  terminating NUL (vvd_run.txt splits the argument on ",\0" because of it).
  Port 8002, topic "FijiVVDPlugin"; listens on the loopback interface (wx: all
  interfaces; vvd_listener connects to localhost).
* **Messages as wx OnPoke**: text "version" (Fiji plugin version -> the info
  line), "confirm" (echoed back as wxIPC_TEXT; ready; 128 MB buffers),
  "pid", "com_finish"; private "settimeout", "setrcvbufsize",
  "setsndbufsize", "volume". **SendCurrentVolume**: the current volume (its
  mask with "Process mask", else the volume), header int32 name length, name,
  nx, ny, nz, bits (8 / 16), r, g, b (colour x 255), double spacings, then
  the voxels; preceded by "setrcvbufsize" and 500 ms (wx). **SendCommand**:
  the volume, then Poke("com", command); the temporary voxel override
  (`;send_mask;override_vox` of RunPlugin, wx FI_RUN) and the group sync
  flags are restored at com_finish. **Received volume**: same name ->
  ReplaceVolumeData in view 0 (and the DataManager; the tree is rebuilt),
  else AddVolume; 16-bit: max value + scalar scale 65535 / max; a returned
  8-bit image with the name of the volume whose mask was sent (same size)
  becomes a copy of that volume with the image as its mask (wx); then the
  previous selection again (OnSelection(2, 0, 0, cur_vol)), and the window
  says "finished", posts the interop message "Fiji Interface,finished,<com>",
  closes the wait dialog, brings VVDViewer to the front.
* **StartFiji / CloseFiji** as wx: the macros vvd_listener.txt / vvd_run.txt
  / vvd_quit.txt and vvd_listener.jar (now bundled in Resources by
  `cmake/QtGui.cmake`; the wx bundle has none, so wx relied on a Fiji that
  already had them) are copied into Fiji's macros / plugins folders (an
  existing file is removed first, so a symbolic link is replaced, never
  written through), `defaults write <Fiji>/Contents/Info LSUIElement 1`,
  `ImageJ-macosx -macro vvd_listener.txt` started detached; CloseFiji: LSUIElement
  0, the process killed (SIGTERM), the macros removed. Window as wx: info
  line ("Connectiong to Fiji..." [sic] / versions / "finished"), Fiji path +
  Browse, "Launch Fiji at startup", command text ("Open..."), "Process
  mask", "Send event"; "Connecting to Fiji..." / "Waiting for Fiji..."
  progress dialogs with Cancel (closes Fiji). Deliberate differences: wx
  killed Fiji with "Time out" whenever the connection took 15 s or more
  (even when Fiji had connected by then); here only when it has not
  connected within the 35 s; between the volume and "com" the server waits
  until the volume is handed to the OS + 300 ms (vvd_listener reads a data
  block with read() calls of the block's full size, so a message already in
  its socket buffer could be read into the volume); the name is sent as
  UTF-8 bytes with their count (wx: the character count); osascript
  activation is started detached and skipped in scripted runs (it needs the
  accessibility permission, which this Mac does not grant osascript).
* **Tests with a real Fiji** (a local Fiji.app, used through a copy so the
  installed Fiji is never touched): note that a duplicated plugin jar in
  Fiji's `plugins/` folder makes Fiji show a modal "Plugin configuration
  error: Duplicate command" dialog that blocks every -macro start (and so the
  Fiji Interface in both apps); remove the duplicate.
  `b4b_fiji` (fiji8.nrrd 8-bit 64x48x20, fiji16.nrrd 16-bit): Fiji started by
  the plugin, connected in ~7 s; "Invert,stack" and "Gaussian
  Blur...,sigma=2 stack" on both volumes; the returned voxels are identical
  to the same commands in a headless Fiji on the bytes that were sent
  (4 / 4 comparisons); 16-bit fix-up checked; the command a second time via
  RunPlugin on the connected Fiji (Invert twice = the original);
  `VVD_QT_FIJI_MASK=1`: the mask sent, the inverted mask comes back as the
  mask of `fiji8.nrrd_1`; at exit Fiji is killed, the macros removed and
  LSUIElement reset. A logging proxy recorded the wire bytes (identical
  layout to the wx server's). `b4b_ipc` + a Python client: wrong topic ->
  IPC_FAIL, REQUEST -> FAIL, ADVISE_START / STOP -> 4 / 7, unknown code ->
  FAIL, zero-size poke -> FAIL, confirm echo, setrcvbufsize / volume / com
  ("com" format 17 with its NUL), a volume sent back in two pieces.

### NBLAST Search (`plugin_nblast/qt/NBLASTPluginQt.*`)

* Port of NBLASTGuiPlugin + window: toolbar Save / Import / Database /
  Setting, scoring method Forward / Mean, "Target Neurons" check boxes (5 per
  row, state saved in the list file), Run NBLAST, the results list (Name,
  Database, Skeleton and Volume Data thumbnails from MIP_thumb / swc_thumb,
  Score; the first hit selected after a search / import), the two preview
  panes (swc_prev and MIP of the selected hit, background bg.png where the
  image is black, the query projection `<result>.png` overlaid in red,
  "Overlay search query"; the MIP pane gets the overlay only with the same
  (integer) aspect ratio, as wx), Delete / Backspace, Ctrl+Z, Ctrl+Y /
  Ctrl+Shift+Z (300 steps), double click on Skeleton / Volume Data loads
  `swc/<name>.swc` / `volume/<name>.nrrd`, Save with the "Additional Options"
  (export skeletons / MIPs / skeleton previews / volumes, file name prefix
  score x 100000 / database) and the result list, Import, the database editor
  (add, in-place rename / path edit, drag to reorder, Delete key, the
  per-row Setting button for user / password; list file
  `name\tpath\ttrue|false\tuser\tpassword`, no final newline), the settings
  dialog (Rscript, temporary directory, Show Top N). runNBLAST: the selected
  volume's mask (else the volume) as vvdnbtmpv.nrrd or the selected mesh
  (SWC) as vvdnbtmpsk.swc, then Rscript nblast_search.R (bundled in
  Resources); a volume without "skeleton" in its name is first skeletonized
  in Fiji ("NBLAST Skeletonize;true;false" through the Fiji Interface; the
  search starts on the interop message). runNBLASTremote (upload with
  RNFR / RNTO, user / password) and LoadSWC ported (wx has no UI calling
  them). Rscript default `/Library/Frameworks/R.framework/.../Rscript`
  (Windows: the newest R-* under Program Files).
* Differences: R runs as a child process with its output in a progress
  dialog (Cancel kills it) and on stderr (wx on macOS: a Terminal window via
  term.sh); every selected database is checked for existence (wx skipped the
  last).
* **Tests with the real R** (R 4 + nat + nat.nblast are installed here): two
  synthetic databases (40 + 30 random SWC neurons, dotprops,
  `as.neuronlistfh` / `write.neuronlistfh`, thumbnails and previews written
  by a script; `nblast_search.R` needs at least as many neurons per database
  as worker threads). `b4b_nblast`: query = a jittered copy of n04 loaded as a
  mesh -> R exit 0, 70 hits, n04 first (score 0.914), list / thumbnails /
  previews, delete-undo-redo keys, the two double clicks, Save with all
  export options (212 exported files with "<score> <db> " prefixes), Import.
  `b4b_nbvol`: a volume query through Fiji (the clone with
  NBLAST_Skeletonize compiled from plugin_nblast/fiji and installed in the
  clone): Fiji skeleton back as the mask of fiji8.nrrd_1 (6 090 voxels),
  the search on that mask, 70 hits. `b4b_nbdlg`: the dialogs.
  `VVD_QT_NBLAST_DBLIST` / `_DBLIST_SAVE` / `_OUTDIR` / `_RSCRIPT` are the
  testing aids (list file, output folder, a replacement R script).

### Neuron Annotator (`plugin_NA/qt/NAPluginQt.*`)

* Port of NAGuiPlugin + window: runNALoader (label .v3dpbd with V3DPBDReader,
  volume through StartupLoad with 80 % of the graphics memory as the size
  budget -- `MainWindow::m_load_datasize`, DataManager::LoadVolumeData
  datasize --, chspec signals / reference, `-s XxYxZ` spacings, `-d`
  prefix; the label scaled to the signal size; shared label / mask; NA mode
  with every segment shown; previous NA groups removed; group sync; the
  reference moved out of the signal group; brush select-group, scale
  translate 0; masks hidden; bounds / centre / object translation reset),
  the MIP worker threads (wx MIPGeneratorThread partitioning; joined
  instead of the counter + detached threads), the reference MIP, the list
  (Reference, Background, Fragment n with check boxes and thumbnails;
  checking a fragment makes it active = segment mask 2, unchecking hides it,
  selecting / deselecting = active / shown through the 30 ms timer with one
  visibility-history step; double click toggles; thumbnails follow the list
  width; Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z on the list's deletions -- the Delete
  key is commented out in wx and stays off), the 100 ms idle refresh when the
  plugin is dirty (OnTreeUpdate while unlocked), SwitchPropPanel (the
  Properties pane follows the selected signal / reference), Import (label,
  then `<label>.h5j` or the volume dialog; reference hidden, all segments
  shown), export (SaveCombinedFragment: the shown fragments, maximum of the
  signals, gzip NRRD), OnRun (-l -v -c -s -d, quoted values allowed),
  project / default settings (Reference, Signal1-3, s_count, x/y/zspc,
  id_path, vol_path, prefix, seg_num, seg_<i> = "id,visibility"; LoadSettings
  rebuilds the segments of a loaded project from its volumes), LoadNrrd,
  runNALoaderRemote, LoadSWC. The view's NA context menu (MainWindow
  NAVolumes / ChangeFragments) and the list stay in step through OnTreeUpdate.
* Difference: the setting file stores each segment's actual mask state (wx
  wrote its own copy, which missed changes made with the view's menu).
* **Tests** (`b4b_na`, synthetic data: an 8-bit label 96x64x24 with 6
  fragments, labels 3 ... 25, and a 4-channel volume, both written as PBD8
  .v3dpbd by a script): `-p "NAPlugin -l ... -v ... -s 1x1x2"`: 7 segments
  in size order, 8 rows, shared label / mask, check / uncheck / double click
  / select, OnTreeUpdate after a change from the view menu, Show all,
  thumbnails resized with the pane, delete-undo-redo, export; the 7 segment
  MIPs, the reference MIP and the exported volume are identical to an
  independent numpy computation; `b4b_nart`: the project reopened, the
  segment visibility restored; `b4b_naimp`: Import through the file dialogs.
