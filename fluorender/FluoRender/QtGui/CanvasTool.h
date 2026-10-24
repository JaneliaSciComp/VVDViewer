#ifndef VVD_QT_CANVASTOOL_H
#define VVD_QT_CANVASTOOL_H

// Interaction/overlay plug-in for RenderCanvas (paint brush, rulers, ...).
//
// The wx view handles these through m_int_mode inside VRenderVulkanView. In
// the Qt port each mode is a CanvasTool registered on the canvas: while a
// tool is active it sees the mouse first (return true to consume the event,
// i.e. no camera rotation/pan), and every frame it may draw overlays.

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class RenderCanvas;

class CanvasTool
{
public:
	virtual ~CanvasTool() = default;

	// Only active tools receive events; DrawOverlay is called for every
	// registered tool (rulers stay visible while the brush is active).
	virtual bool IsActive() const { return false; }

	virtual bool MousePress(QMouseEvent*) { return false; }
	virtual bool MouseMove(QMouseEvent*) { return false; }
	virtual bool MouseRelease(QMouseEvent*) { return false; }
	// a right button release the tool uses itself (wx: the ruler mode's
	// RightUp finishes the ruler instead of opening the view's menu)
	virtual bool HandlesRightClick(QMouseEvent*) const { return false; }
	virtual bool Wheel(QWheelEvent*) { return false; }
	virtual bool KeyPress(QKeyEvent*) { return false; }
	// Every mouse move over the canvas, for every registered tool (active or
	// not), before the active tool / camera see it. Cannot consume the event
	// (e.g. the Info overlay follows the mouse).
	virtual void HoverMove(QMouseEvent*) {}

	// Inside the frame (between prepareFrame and submitFrame), after the
	// scene has been drawn into the swapchain image. GetMvMat()/GetProjMat()
	// are the object-space matrices of the frame. nx/ny are the swapchain
	// size. Tools draw with canvas->GetV2DRender() / the engine renderers,
	// chaining semaphores the way the wx overlay code does.
	virtual void DrawOverlay(RenderCanvas*, int /*nx*/, int /*ny*/) {}
	// After DrawOverlay and after the point where the "Capture" button reads
	// the frame back (wx PostDraw): what is drawn here is shown but not
	// captured (wx: frame border, Info text, brush cursor / strokes).
	virtual void DrawOverlayPost(RenderCanvas*, int /*nx*/, int /*ny*/) {}
	// Before the scene is drawn (e.g. per-frame state updates).
	virtual void PreDraw(RenderCanvas*, int /*nx*/, int /*ny*/) {}
};

#endif
