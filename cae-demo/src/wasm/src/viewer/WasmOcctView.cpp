// Copyright (c) 2019 OPEN CASCADE SAS
//
// This file is part of the examples of the Open CASCADE Technology software
// library.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE

#include "WasmOcctView.h"

#include <emscripten/bind.h>
#include <spdlog/spdlog.h>

// ===================== OCCT ======================
#include <AIS_Shape.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_Handle.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Graphic3d_CubeMapPacked.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <Image_AlienPixMap.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <Message_ProgressIndicator.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Poly.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_ToolCylinder.hxx>
#include <Prs3d_ToolDisk.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_ArrayStreamBuffer.hxx>
#include <Standard_PrimitiveTypes.hxx>
#include <Standard_Version.hxx>
#include <StepData_StepModel.hxx>
#include <TColgp_Array1OfVec.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Solid.hxx>
#include <Wasm_Window.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_Location.hxx>
#include <XCAFDoc_ShapeTool.hxx>

// ==================== STD-CPP ======================
#include <array>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <vector>

#define THE_CANVAS_ID "canvas"

namespace {
//! Auxiliary wrapper for loading model.
struct ModelAsyncLoader {
  std::string Name;
  std::string Path;

  ModelAsyncLoader(const char* theName, const char* thePath)
      : Name(theName), Path(thePath) {}

  //! File data read event.
  static void onDataRead(void* theOpaque, void* theBuffer, int theDataLen) {
    const ModelAsyncLoader* aTask = (ModelAsyncLoader*)theOpaque;
    WasmOcctView::openFromMemory(
        aTask->Name, reinterpret_cast<uintptr_t>(theBuffer), theDataLen, false);
    delete aTask;
  }

  //! File read error event.
  static void onReadFailed(void* theOpaque) {
    const ModelAsyncLoader* aTask = (ModelAsyncLoader*)theOpaque;
    Message::DefaultMessenger()->Send(
        TCollection_AsciiString("Error: unable to load file ") +
            aTask->Path.c_str(),
        Message_Fail);
    delete aTask;
  }
};

//! Auxiliary wrapper for loading cubemap.
struct CubemapAsyncLoader {
  //! Image file read event.
  static void onImageRead(const char* theFilePath) {
    Handle(Graphic3d_CubeMapPacked) aCubemap;
    Handle(Image_AlienPixMap) anImage = new Image_AlienPixMap();
    if (anImage->Load(theFilePath)) {
      aCubemap = new Graphic3d_CubeMapPacked(anImage);
    }
    WasmOcctView::Instance().View()->SetBackgroundCubeMap(aCubemap, true,
                                                          false);
    WasmOcctView::Instance().UpdateView();
  }

  //! Image file failed read event.
  static void onImageFailed(const char* theFilePath) {
    Message::DefaultMessenger()->Send(
        TCollection_AsciiString("Error: unable to load image ") + theFilePath,
        Message_Fail);
  }
};
}  // namespace

// ================================================================
// Function : Instance
// Purpose  :
// ================================================================
WasmOcctView& WasmOcctView::Instance() {
  static WasmOcctView aViewer;
  return aViewer;
}

// ================================================================
// Function : WasmOcctView
// Purpose  :
// ================================================================
WasmOcctView::WasmOcctView() : myDevicePixelRatio(1.0f), myNbUpdateRequests(0) {
  addActionHotKeys(Aspect_VKey_NavForward, Aspect_VKey_W,
                   Aspect_VKey_W | Aspect_VKeyFlags_SHIFT);
  addActionHotKeys(Aspect_VKey_NavBackward, Aspect_VKey_S,
                   Aspect_VKey_S | Aspect_VKeyFlags_SHIFT);
  addActionHotKeys(Aspect_VKey_NavSlideLeft, Aspect_VKey_A,
                   Aspect_VKey_A | Aspect_VKeyFlags_SHIFT);
  addActionHotKeys(Aspect_VKey_NavSlideRight, Aspect_VKey_D,
                   Aspect_VKey_D | Aspect_VKeyFlags_SHIFT);
  addActionHotKeys(Aspect_VKey_NavRollCCW, Aspect_VKey_Q,
                   Aspect_VKey_Q | Aspect_VKeyFlags_SHIFT);
  addActionHotKeys(Aspect_VKey_NavRollCW, Aspect_VKey_E,
                   Aspect_VKey_E | Aspect_VKeyFlags_SHIFT);

  addActionHotKeys(Aspect_VKey_NavSpeedIncrease, Aspect_VKey_Plus,
                   Aspect_VKey_Plus | Aspect_VKeyFlags_SHIFT, Aspect_VKey_Equal,
                   Aspect_VKey_NumpadAdd,
                   Aspect_VKey_NumpadAdd | Aspect_VKeyFlags_SHIFT);
  addActionHotKeys(Aspect_VKey_NavSpeedDecrease, Aspect_VKey_Minus,
                   Aspect_VKey_Minus | Aspect_VKeyFlags_SHIFT,
                   Aspect_VKey_NumpadSubtract,
                   Aspect_VKey_NumpadSubtract | Aspect_VKeyFlags_SHIFT);

  // arrow keys conflict with browser page scrolling, so better be avoided in
  // non-fullscreen mode
  addActionHotKeys(Aspect_VKey_NavLookUp,
                   Aspect_VKey_Numpad8);  // Aspect_VKey_Up
  addActionHotKeys(Aspect_VKey_NavLookDown,
                   Aspect_VKey_Numpad2);  // Aspect_VKey_Down
  addActionHotKeys(Aspect_VKey_NavLookLeft,
                   Aspect_VKey_Numpad4);  // Aspect_VKey_Left
  addActionHotKeys(Aspect_VKey_NavLookRight,
                   Aspect_VKey_Numpad6);  // Aspect_VKey_Right
  addActionHotKeys(
      Aspect_VKey_NavSlideLeft,
      Aspect_VKey_Numpad1);  // Aspect_VKey_Left |Aspect_VKeyFlags_SHIFT
  addActionHotKeys(
      Aspect_VKey_NavSlideRight,
      Aspect_VKey_Numpad3);  // Aspect_VKey_Right|Aspect_VKeyFlags_SHIFT
  addActionHotKeys(
      Aspect_VKey_NavSlideUp,
      Aspect_VKey_Numpad9);  // Aspect_VKey_Up   |Aspect_VKeyFlags_SHIFT
  addActionHotKeys(
      Aspect_VKey_NavSlideDown,
      Aspect_VKey_Numpad7);  // Aspect_VKey_Down |Aspect_VKeyFlags_SHIFT
}

// ================================================================
// Function : ~WasmOcctView
// Purpose  :
// ================================================================
WasmOcctView::~WasmOcctView() {}

// ================================================================
// Function : run
// Purpose  :
// ================================================================
void WasmOcctView::run() {
  initWindow();
  initViewer();
  initDemoScene();
  if (myView.IsNull()) {
    return;
  }

  myView->MustBeResized();
  myView->Redraw();

  // There is no infinite message loop, main() will return from here
  // immediately. Tell that our Module should be left loaded and handle events
  // through callbacks.
  // emscripten_set_main_loop (redrawView, 60, 1);
  // emscripten_set_main_loop (redrawView, -1, 1);
  EM_ASM(Module['noExitRuntime'] = true);
}

// ================================================================
// Function : initWindow
// Purpose  :
// ================================================================
void WasmOcctView::initWindow() {
  myDevicePixelRatio = emscripten_get_device_pixel_ratio();
  myCanvasId = THE_CANVAS_ID;
  const char* aTargetId = !myCanvasId.IsEmpty()
                              ? myCanvasId.ToCString()
                              : EMSCRIPTEN_EVENT_TARGET_WINDOW;
  const EM_BOOL toUseCapture = EM_TRUE;
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this,
                                 toUseCapture, onResizeCallback);

  emscripten_set_mousedown_callback(aTargetId, this, toUseCapture,
                                    onMouseCallback);
  // bind these events to window to track mouse movements outside of canvas
  // emscripten_set_mouseup_callback    (aTargetId, this, toUseCapture,
  // onMouseCallback); emscripten_set_mousemove_callback  (aTargetId, this,
  // toUseCapture, onMouseCallback); emscripten_set_mouseleave_callback
  // (aTargetId, this, toUseCapture, onMouseCallback);
  emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this,
                                  toUseCapture, onMouseCallback);
  emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this,
                                    toUseCapture, onMouseCallback);

  emscripten_set_dblclick_callback(aTargetId, this, toUseCapture,
                                   onMouseCallback);
  emscripten_set_click_callback(aTargetId, this, toUseCapture, onMouseCallback);
  emscripten_set_mouseenter_callback(aTargetId, this, toUseCapture,
                                     onMouseCallback);
  emscripten_set_wheel_callback(aTargetId, this, toUseCapture, onWheelCallback);

  emscripten_set_touchstart_callback(aTargetId, this, toUseCapture,
                                     onTouchCallback);
  emscripten_set_touchend_callback(aTargetId, this, toUseCapture,
                                   onTouchCallback);
  emscripten_set_touchmove_callback(aTargetId, this, toUseCapture,
                                    onTouchCallback);
  emscripten_set_touchcancel_callback(aTargetId, this, toUseCapture,
                                      onTouchCallback);

  // emscripten_set_keypress_callback   (aTargetId, this, toUseCapture,
  // onKeyCallback);
  emscripten_set_keydown_callback(aTargetId, this, toUseCapture,
                                  onKeyDownCallback);
  emscripten_set_keyup_callback(aTargetId, this, toUseCapture, onKeyUpCallback);
  // emscripten_set_focus_callback    (aTargetId, this, toUseCapture,
  // onFocusCallback); emscripten_set_focusin_callback  (aTargetId, this,
  // toUseCapture, onFocusCallback);
  emscripten_set_focusout_callback(aTargetId, this, toUseCapture,
                                   onFocusCallback);
}

// ================================================================
// Function : dumpGlInfo
// Purpose  :
// ================================================================
void WasmOcctView::dumpGlInfo(bool theIsBasic) {
  TColStd_IndexedDataMapOfStringString aGlCapsDict;
  myView->DiagnosticInformation(aGlCapsDict,
                                theIsBasic ? Graphic3d_DiagnosticInfo_Basic
                                           : Graphic3d_DiagnosticInfo_Complete);
  if (theIsBasic) {
    TCollection_AsciiString aViewport;
    aGlCapsDict.FindFromKey("Viewport", aViewport);
    aGlCapsDict.Clear();
    aGlCapsDict.Add("Viewport", aViewport);
  }
  aGlCapsDict.Add("Display scale", TCollection_AsciiString(myDevicePixelRatio));

  // beautify output
  {
    TCollection_AsciiString* aGlVer = aGlCapsDict.ChangeSeek("GLversion");
    TCollection_AsciiString* aGlslVer = aGlCapsDict.ChangeSeek("GLSLversion");
    if (aGlVer != NULL && aGlslVer != NULL) {
      *aGlVer = *aGlVer + " [GLSL: " + *aGlslVer + "]";
      aGlslVer->Clear();
    }
  }

  TCollection_AsciiString anInfo;
  for (TColStd_IndexedDataMapOfStringString::Iterator aValueIter(aGlCapsDict);
       aValueIter.More(); aValueIter.Next()) {
    if (!aValueIter.Value().IsEmpty()) {
      if (!anInfo.IsEmpty()) {
        anInfo += "\n";
      }
      anInfo += aValueIter.Key() + ": " + aValueIter.Value();
    }
  }

  ::Message::DefaultMessenger()->Send(anInfo, Message_Warning);
}

// ================================================================
// Function : initPixelScaleRatio
// Purpose  :
// ================================================================
void WasmOcctView::initPixelScaleRatio() {
  SetTouchToleranceScale(myDevicePixelRatio);
  if (!myView.IsNull()) {
    myView->ChangeRenderingParams().Resolution =
        (unsigned int)(96.0 * myDevicePixelRatio + 0.5);
  }
  if (!myContext.IsNull()) {
    myContext->SetPixelTolerance(int(myDevicePixelRatio * 6.0));
    if (!myViewCube.IsNull()) {
      static const double THE_CUBE_SIZE = 60.0;
      myViewCube->SetSize(myDevicePixelRatio * THE_CUBE_SIZE, false);
      myViewCube->SetBoxFacetExtension(myViewCube->Size() * 0.15);
      myViewCube->SetAxesPadding(myViewCube->Size() * 0.10);
      myViewCube->SetFontHeight(THE_CUBE_SIZE * 0.16);
      if (myViewCube->HasInteractiveContext()) {
        myContext->Redisplay(myViewCube, false);
      }
    }
  }
}

// ================================================================
// Function : initViewer
// Purpose  :
// ================================================================
bool WasmOcctView::initViewer() {
  // Build with "--preload-file MyFontFile.ttf" option
  // and register font in Font Manager to use custom font(s).
  /*const char* aFontPath = "MyFontFile.ttf";
  if (Handle(Font_SystemFont) aFont = Font_FontMgr::GetInstance()->CheckFont
  (aFontPath))
  {
    Font_FontMgr::GetInstance()->RegisterFont (aFont, true);
  }
  else
  {
    Message::DefaultMessenger()->Send (TCollection_AsciiString ("Error: font '")
  + aFontPath + "' is not found", Message_Fail);
  }*/

  Handle(Aspect_DisplayConnection) aDisp;
  Handle(OpenGl_GraphicDriver) aDriver = new OpenGl_GraphicDriver(aDisp, false);
  aDriver->ChangeOptions().buffersNoSwap = true;  // swap has no effect in WebGL
  aDriver->ChangeOptions().buffersOpaqueAlpha =
      true;  // avoid unexpected blending of canvas with page background
  if (!aDriver->InitContext()) {
    Message::DefaultMessenger()->Send(
        TCollection_AsciiString("Error: EGL initialization failed"),
        Message_Fail);
    return false;
  }

  Handle(V3d_Viewer) aViewer = new V3d_Viewer(aDriver);
  aViewer->SetComputedMode(false);
  aViewer->SetDefaultShadingModel(Graphic3d_TypeOfShadingModel_Phong);
  aViewer->SetDefaultLights();
  aViewer->SetLightOn();
  for (V3d_ListOfLight::Iterator aLightIter(aViewer->ActiveLights());
       aLightIter.More(); aLightIter.Next()) {
    const Handle(V3d_Light)& aLight = aLightIter.Value();
    if (aLight->Type() == Graphic3d_TypeOfLightSource_Directional) {
      aLight->SetCastShadows(true);
    }
  }

  Handle(Wasm_Window) aWindow = new Wasm_Window(THE_CANVAS_ID);
  aWindow->Size(myWinSizeOld.x(), myWinSizeOld.y());

  myTextStyle = new Prs3d_TextAspect();
  myTextStyle->SetFont(Font_NOF_ASCII_MONO);
  myTextStyle->SetHeight(12);
  myTextStyle->Aspect()->SetColor(Quantity_NOC_GRAY95);
  myTextStyle->Aspect()->SetColorSubTitle(Quantity_NOC_BLACK);
  myTextStyle->Aspect()->SetDisplayType(Aspect_TODT_SHADOW);
  myTextStyle->Aspect()->SetTextFontAspect(Font_FA_Bold);
  myTextStyle->Aspect()->SetTextZoomable(false);
  myTextStyle->SetHorizontalJustification(Graphic3d_HTA_LEFT);
  myTextStyle->SetVerticalJustification(Graphic3d_VTA_BOTTOM);

  myView = new V3d_View(aViewer);
  myView->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Perspective);
  myView->SetImmediateUpdate(false);
  myView->ChangeRenderingParams().IsShadowEnabled = false;
  myView->ChangeRenderingParams().Resolution =
      (unsigned int)(96.0 * myDevicePixelRatio + 0.5);
  myView->ChangeRenderingParams().ToShowStats = true;
  myView->ChangeRenderingParams().StatsTextAspect = myTextStyle->Aspect();
  myView->ChangeRenderingParams().StatsTextHeight = (int)myTextStyle->Height();
  myView->SetWindow(aWindow);
  dumpGlInfo(false);

  myContext = new AIS_InteractiveContext(aViewer);
  initPixelScaleRatio();
  return true;
}

// ================================================================
// Function : initDemoScene
// Purpose  :
// ================================================================
void WasmOcctView::initDemoScene() {
  if (myContext.IsNull()) {
    return;
  }

  // myView->TriedronDisplay (Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GOLD, 0.08,
  // V3d_WIREFRAME);

  myViewCube = new AIS_ViewCube();
  // presentation parameters
  initPixelScaleRatio();
  myViewCube->SetTransformPersistence(new Graphic3d_TransformPers(
      Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_LOWER,
      Graphic3d_Vec2i(100, 100)));
  myViewCube->Attributes()->SetDatumAspect(new Prs3d_DatumAspect());
  myViewCube->Attributes()->DatumAspect()->SetTextAspect(myTextStyle);
  // animation parameters
  myViewCube->SetViewAnimation(myViewAnimation);
  myViewCube->SetFixedAnimationLoop(false);
  myViewCube->SetAutoStartAnimation(true);
  myContext->Display(myViewCube, false);

  // Build with "--preload-file MySampleFile.brep" option to load some shapes
  // here.
}

// ================================================================
// Function : ProcessInput
// Purpose  :
// ================================================================
void WasmOcctView::ProcessInput() {
  if (!myView.IsNull()) {
    // Queue onRedrawView()/redrawView callback to redraw canvas after all user
    // input is flushed by browser. Redrawing viewer on every single message
    // would be a pointless waste of resources, as user will see only the last
    // drawn frame due to WebGL implementation details. -1 in
    // emscripten_async_call() redirects to requestAnimationFrame();
    // requestPostAnimationFrame() is a better under development alternative.
    if (++myNbUpdateRequests == 1) {
      emscripten_async_call(onRedrawView, this, -1);
    }
  }
}

// ================================================================
// Function : UpdateView
// Purpose  :
// ================================================================
void WasmOcctView::UpdateView() {
  if (!myView.IsNull()) {
    myView->Invalidate();
    // queue next onRedrawView()/redrawView()
    ProcessInput();
  }
}

// ================================================================
// Function : redrawView
// Purpose  :
// ================================================================
void WasmOcctView::redrawView() {
  if (!myView.IsNull()) {
    myNbUpdateRequests = 0;
    FlushViewEvents(myContext, myView, true);
  }
}

// ================================================================
// Function : handleViewRedraw
// Purpose  :
// ================================================================
void WasmOcctView::handleViewRedraw(const Handle(AIS_InteractiveContext) &
                                        theCtx,
                                    const Handle(V3d_View) & theView) {
  AIS_ViewController::handleViewRedraw(theCtx, theView);
  if (myToAskNextFrame) {
    // ask more frames
    if (++myNbUpdateRequests == 1) {
      emscripten_async_call(onRedrawView, this, -1);
    }
  }
}

// ================================================================
// Function : onResizeEvent
// Purpose  :
// ================================================================
EM_BOOL WasmOcctView::onResizeEvent(int theEventType,
                                    const EmscriptenUiEvent* theEvent) {
  (void)theEventType;  // EMSCRIPTEN_EVENT_RESIZE or
                       // EMSCRIPTEN_EVENT_CANVASRESIZED
  (void)theEvent;
  if (myView.IsNull()) {
    return EM_FALSE;
  }

  Handle(Wasm_Window) aWindow = Handle(Wasm_Window)::DownCast(myView->Window());
  Graphic3d_Vec2i aWinSizeNew;
  aWindow->DoResize();
  aWindow->Size(aWinSizeNew.x(), aWinSizeNew.y());
  const float aPixelRatio = emscripten_get_device_pixel_ratio();
  if (aWinSizeNew != myWinSizeOld || aPixelRatio != myDevicePixelRatio) {
    myWinSizeOld = aWinSizeNew;
    if (myDevicePixelRatio != aPixelRatio) {
      myDevicePixelRatio = aPixelRatio;
      initPixelScaleRatio();
    }

    myView->MustBeResized();
    myView->Invalidate();
    myView->Redraw();
    dumpGlInfo(true);
  }
  return EM_TRUE;
}

//! Update canvas bounding rectangle.
EM_JS(void, jsUpdateBoundingClientRect, (),
      { Module._myCanvasRect = Module.canvas.getBoundingClientRect(); });

//! Get canvas bounding top.
EM_JS(int, jsGetBoundingClientTop, (),
      { return Math.round(Module._myCanvasRect.top); });

//! Get canvas bounding left.
EM_JS(int, jsGetBoundingClientLeft, (),
      { return Math.round(Module._myCanvasRect.left); });

// ================================================================
// Function : onMouseEvent
// Purpose  :
// ================================================================
EM_BOOL WasmOcctView::onMouseEvent(int theEventType,
                                   const EmscriptenMouseEvent* theEvent) {
  if (myView.IsNull()) {
    return EM_FALSE;
  }

  Handle(Wasm_Window) aWindow = Handle(Wasm_Window)::DownCast(myView->Window());
  if (theEventType == EMSCRIPTEN_EVENT_MOUSEMOVE ||
      theEventType == EMSCRIPTEN_EVENT_MOUSEUP) {
    // these events are bound to EMSCRIPTEN_EVENT_TARGET_WINDOW, and coordinates
    // should be converted
    jsUpdateBoundingClientRect();
    EmscriptenMouseEvent anEvent = *theEvent;
    anEvent.targetX -= jsGetBoundingClientLeft();
    anEvent.targetY -= jsGetBoundingClientTop();
    aWindow->ProcessMouseEvent(*this, theEventType, &anEvent);
    return EM_FALSE;
  }

  return aWindow->ProcessMouseEvent(*this, theEventType, theEvent) ? EM_TRUE
                                                                   : EM_FALSE;
}

// ================================================================
// Function : onWheelEvent
// Purpose  :
// ================================================================
EM_BOOL WasmOcctView::onWheelEvent(int theEventType,
                                   const EmscriptenWheelEvent* theEvent) {
  if (myView.IsNull() || theEventType != EMSCRIPTEN_EVENT_WHEEL) {
    return EM_FALSE;
  }

  Handle(Wasm_Window) aWindow = Handle(Wasm_Window)::DownCast(myView->Window());
  return aWindow->ProcessWheelEvent(*this, theEventType, theEvent) ? EM_TRUE
                                                                   : EM_FALSE;
}

// ================================================================
// Function : onTouchEvent
// Purpose  :
// ================================================================
EM_BOOL WasmOcctView::onTouchEvent(int theEventType,
                                   const EmscriptenTouchEvent* theEvent) {
  if (myView.IsNull()) {
    return EM_FALSE;
  }

  Handle(Wasm_Window) aWindow = Handle(Wasm_Window)::DownCast(myView->Window());
  return aWindow->ProcessTouchEvent(*this, theEventType, theEvent) ? EM_TRUE
                                                                   : EM_FALSE;
}

// ================================================================
// Function : navigationKeyModifierSwitch
// Purpose  :
// ================================================================
bool WasmOcctView::navigationKeyModifierSwitch(unsigned int theModifOld,
                                               unsigned int theModifNew,
                                               double theTimeStamp) {
  bool hasActions = false;
  for (unsigned int aKeyIter = 0; aKeyIter < Aspect_VKey_ModifiersLower;
       ++aKeyIter) {
    if (!myKeys.IsKeyDown(aKeyIter)) {
      continue;
    }

    Aspect_VKey anActionOld = Aspect_VKey_UNKNOWN,
                anActionNew = Aspect_VKey_UNKNOWN;
    myNavKeyMap.Find(aKeyIter | theModifOld, anActionOld);
    myNavKeyMap.Find(aKeyIter | theModifNew, anActionNew);
    if (anActionOld == anActionNew) {
      continue;
    }

    if (anActionOld != Aspect_VKey_UNKNOWN) {
      myKeys.KeyUp(anActionOld, theTimeStamp);
    }
    if (anActionNew != Aspect_VKey_UNKNOWN) {
      hasActions = true;
      myKeys.KeyDown(anActionNew, theTimeStamp);
    }
  }
  return hasActions;
}

// ================================================================
// Function : onFocusEvent
// Purpose  :
// ================================================================
EM_BOOL WasmOcctView::onFocusEvent(int theEventType,
                                   const EmscriptenFocusEvent* theEvent) {
  if (myView.IsNull() ||
      (theEventType != EMSCRIPTEN_EVENT_FOCUS &&
       theEventType != EMSCRIPTEN_EVENT_FOCUSIN  // about to receive focus
       && theEventType != EMSCRIPTEN_EVENT_FOCUSOUT)) {
    return EM_FALSE;
  }

  Handle(Wasm_Window) aWindow = Handle(Wasm_Window)::DownCast(myView->Window());
  return aWindow->ProcessFocusEvent(*this, theEventType, theEvent) ? EM_TRUE
                                                                   : EM_FALSE;
}

// ================================================================
// Function : onKeyDownEvent
// Purpose  :
// ================================================================
EM_BOOL WasmOcctView::onKeyDownEvent(int theEventType,
                                     const EmscriptenKeyboardEvent* theEvent) {
  if (myView.IsNull() ||
      theEventType != EMSCRIPTEN_EVENT_KEYDOWN)  // EMSCRIPTEN_EVENT_KEYPRESS
  {
    return EM_FALSE;
  }

  Handle(Wasm_Window) aWindow = Handle(Wasm_Window)::DownCast(myView->Window());
  return aWindow->ProcessKeyEvent(*this, theEventType, theEvent) ? EM_TRUE
                                                                 : EM_FALSE;
}

//=======================================================================
// function : KeyDown
// purpose  :
//=======================================================================
void WasmOcctView::KeyDown(Aspect_VKey theKey, double theTime,
                           double thePressure) {
  const unsigned int aModifOld = myKeys.Modifiers();
  AIS_ViewController::KeyDown(theKey, theTime, thePressure);

  const unsigned int aModifNew = myKeys.Modifiers();
  if (aModifNew != aModifOld &&
      navigationKeyModifierSwitch(aModifOld, aModifNew, theTime)) {
    // modifier key just pressed
  }

  Aspect_VKey anAction = Aspect_VKey_UNKNOWN;
  if (myNavKeyMap.Find(theKey | myKeys.Modifiers(), anAction) &&
      anAction != Aspect_VKey_UNKNOWN) {
    AIS_ViewController::KeyDown(anAction, theTime, thePressure);
  }
}

// ================================================================
// Function : onKeyUpEvent
// Purpose  :
// ================================================================
EM_BOOL WasmOcctView::onKeyUpEvent(int theEventType,
                                   const EmscriptenKeyboardEvent* theEvent) {
  if (myView.IsNull() || theEventType != EMSCRIPTEN_EVENT_KEYUP) {
    return EM_FALSE;
  }

  Handle(Wasm_Window) aWindow = Handle(Wasm_Window)::DownCast(myView->Window());
  return aWindow->ProcessKeyEvent(*this, theEventType, theEvent) ? EM_TRUE
                                                                 : EM_FALSE;
}

//=======================================================================
// function : KeyUp
// purpose  :
//=======================================================================
void WasmOcctView::KeyUp(Aspect_VKey theKey, double theTime) {
  const unsigned int aModifOld = myKeys.Modifiers();
  AIS_ViewController::KeyUp(theKey, theTime);

  Aspect_VKey anAction = Aspect_VKey_UNKNOWN;
  if (myNavKeyMap.Find(theKey | myKeys.Modifiers(), anAction) &&
      anAction != Aspect_VKey_UNKNOWN) {
    AIS_ViewController::KeyUp(anAction, theTime);
    processKeyPress(anAction);
  }

  const unsigned int aModifNew = myKeys.Modifiers();
  if (aModifNew != aModifOld &&
      navigationKeyModifierSwitch(aModifOld, aModifNew, theTime)) {
    // modifier key released
  }

  processKeyPress(theKey | aModifNew);
}

//==============================================================================
// function : processKeyPress
// purpose  :
//==============================================================================
bool WasmOcctView::processKeyPress(Aspect_VKey theKey) {
  switch (theKey) {
    case Aspect_VKey_F: {
      myView->FitAll(0.01, false);
      UpdateView();
      return true;
    }
  }
  return false;
}

// ================================================================
// Function : setCubemapBackground
// Purpose  :
// ================================================================
void WasmOcctView::setCubemapBackground(const std::string& theImagePath) {
  if (!theImagePath.empty()) {
    emscripten_async_wget(theImagePath.c_str(), "/emulated/cubemap.jpg",
                          CubemapAsyncLoader::onImageRead,
                          CubemapAsyncLoader::onImageFailed);
  } else {
    WasmOcctView::Instance().View()->SetBackgroundCubeMap(
        Handle(Graphic3d_CubeMapPacked)(), true, false);
    WasmOcctView::Instance().UpdateView();
  }
}

// ================================================================
// Function : fitAllObjects
// Purpose  :
// ================================================================
void WasmOcctView::fitAllObjects(bool theAuto) {
  WasmOcctView& aViewer = Instance();
  if (theAuto) {
    aViewer.FitAllAuto(aViewer.Context(), aViewer.View());
  } else {
    aViewer.View()->FitAll(0.01, false);
  }
  aViewer.UpdateView();
}

// ================================================================
// Function : removeAllObjects
// Purpose  :
// ================================================================
void WasmOcctView::removeAllObjects() {
  WasmOcctView& aViewer = Instance();
  for (NCollection_IndexedDataMap<TCollection_AsciiString,
                                  Handle(AIS_InteractiveObject)>::Iterator
           anObjIter(aViewer.myObjects);
       anObjIter.More(); anObjIter.Next()) {
    aViewer.Context()->Remove(anObjIter.Value(), false);
  }
  aViewer.myObjects.Clear();
  aViewer.UpdateView();
}

// ================================================================
// Function : removeObject
// Purpose  :
// ================================================================
bool WasmOcctView::removeObject(const std::string& theName) {
  spdlog::debug(__func__);

  WasmOcctView& aViewer = Instance();
  spdlog::debug("objects : {}", aViewer.myObjects.Size());
  Handle(AIS_InteractiveObject) anObj;
  if (!theName.empty() &&
      !aViewer.myObjects.FindFromKey(theName.c_str(), anObj)) {
    return false;
  }

  aViewer.Context()->Remove(anObj, false);
  aViewer.myObjects.RemoveKey(theName.c_str());
  aViewer.UpdateView();
  spdlog::debug("{} done.", __func__);
  return true;
}

// ================================================================
// Function : eraseObject
// Purpose  :
// ================================================================
bool WasmOcctView::eraseObject(const std::string& theName) {
  WasmOcctView& aViewer = Instance();
  Handle(AIS_InteractiveObject) anObj;
  if (!theName.empty() &&
      !aViewer.myObjects.FindFromKey(theName.c_str(), anObj)) {
    return false;
  }

  aViewer.Context()->Erase(anObj, false);
  aViewer.UpdateView();
  return true;
}

// ================================================================
// Function : displayObject
// Purpose  :
// ================================================================
bool WasmOcctView::displayObject(const std::string& theName) {
  WasmOcctView& aViewer = Instance();
  Handle(AIS_InteractiveObject) anObj;
  if (!theName.empty() &&
      !aViewer.myObjects.FindFromKey(theName.c_str(), anObj)) {
    return false;
  }

  aViewer.Context()->Display(anObj, false);
  aViewer.UpdateView();
  return true;
}

// ================================================================
// Function : openFromUrl
// Purpose  :
// ================================================================
void WasmOcctView::openFromUrl(const std::string& theName,
                               const std::string& theModelPath) {
  ModelAsyncLoader* aTask =
      new ModelAsyncLoader(theName.c_str(), theModelPath.c_str());
  emscripten_async_wget_data(theModelPath.c_str(), (void*)aTask,
                             ModelAsyncLoader::onDataRead,
                             ModelAsyncLoader::onReadFailed);
}

// ================================================================
// Function : openFromMemory
// Purpose  :
// ================================================================
bool WasmOcctView::openFromMemory(const std::string& theName,
                                  uintptr_t theBuffer, int theDataLen,
                                  bool theToFree) {
  removeAllObjects();
  removeObject(theName);
  char* aBytes = reinterpret_cast<char*>(theBuffer);
  if (aBytes == nullptr || theDataLen <= 0) {
    return false;
  }

  auto ext = std::filesystem::path(theName).extension();

// Function to check if specified data stream starts with specified header.
#define dataStartsWithHeader(theData, theHeader) \
  (::strncmp(theData, theHeader, sizeof(theHeader) - 1) == 0)

  if (dataStartsWithHeader(aBytes, "DBRep_DrawableShape")) {
    return openBRepFromMemory(theName, theBuffer, theDataLen, theToFree);
  } else if (dataStartsWithHeader(aBytes, "glTF")) {
    // return openGltfFromMemory (theName, theBuffer, theDataLen, theToFree);
  } else if (dataStartsWithHeader(aBytes, "ISO-10303-21") | ext == ".iges") {
    return openSTEPAndIGESFromMemory(theName, theBuffer, theDataLen, theToFree);
  }
  if (theToFree) {
    free(aBytes);
  }

  Message::SendFail() << "Error: file '" << theName.c_str()
                      << "' has unsupported format";
  return false;
}

// ================================================================
// Function : openBRepFromMemory
// Purpose  :
// ================================================================
bool WasmOcctView::openBRepFromMemory(const std::string& theName,
                                      uintptr_t theBuffer, int theDataLen,
                                      bool theToFree) {
  Message::SendTrace() << "starting reading : " << theName;
  removeObject(theName);

  WasmOcctView& aViewer = Instance();
  TopoDS_Shape aShape;
  BRep_Builder aBuilder;
  bool isLoaded = false;
  {
    char* aRawData = reinterpret_cast<char*>(theBuffer);
    Standard_ArrayStreamBuffer aStreamBuffer(aRawData, theDataLen);
    std::istream aStream(&aStreamBuffer);
    BRepTools::Read(aShape, aStream, aBuilder);
    if (theToFree) {
      free(aRawData);
    }
    isLoaded = true;
  }
  if (!isLoaded) {
    return false;
  }

  Handle(AIS_Shape) aShapePrs = new AIS_Shape(aShape);
  if (!theName.empty()) {
    aViewer.myObjects.Add(theName.c_str(), aShapePrs);
  }
  aShapePrs->SetMaterial(Graphic3d_NameOfMaterial_Silver);
  aViewer.Context()->Display(aShapePrs, AIS_Shaded, 0, false);
  aViewer.View()->FitAll(0.01, false);
  aViewer.UpdateView();

  Message::DefaultMessenger()->Send(
      TCollection_AsciiString("Loaded file ") + theName.c_str(), Message_Info);
  Message::DefaultMessenger()->Send(OSD_MemInfo::PrintInfo(), Message_Trace);
  return true;
}

bool WasmOcctView::openFromString(const std::string& theName,
                                  const std::string& buffer) {
  Message::SendTrace() << __func__;
  return openFromMemory(theName, reinterpret_cast<uintptr_t>(buffer.data()),
                        buffer.length(), false);
}

bool WasmOcctView::openSTEPAndIGESFromMemory(const std::string& theName,
                                             uintptr_t theBuffer,
                                             int theDataLen, bool theToFree) {
  Message::SendTrace() << "open step from memory : " << theName;
  auto ext = std::filesystem::path(theName).extension();

  removeObject(theName);

  WasmOcctView& aViewer = Instance();
  TopoDS_Shape aShape;
  bool isLoaded = false;

  {
    char* aRawData = reinterpret_cast<char*>(theBuffer);
    Standard_ArrayStreamBuffer aStreamBuffer(aRawData, theDataLen);
    std::istream aStream(&aStreamBuffer);
    std::filesystem::path wDir("/working");
    std::filesystem::create_directories(wDir);
    std::filesystem::path fpath = wDir.append(theName);

    std::fstream fi{fpath, fi.binary | fi.out};
    fi.write(aRawData, theDataLen);
    fi.close();

    {
      Handle(TDocStd_Document) doc;
      XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", doc);
      bool canRead = false;
      if (ext == ".step" || ext == ".stp") {
        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(true);
        // reader.
        if (reader.ReadFile(fpath.c_str()) == IFSelect_RetDone) {
          canRead = true;
          // Message_ProgressIndicator pi();
          reader.Transfer(doc);
        }
      } else {
        IGESCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(true);
        // reader.
        if (reader.ReadFile(fpath.c_str()) == IFSelect_RetDone) {
          canRead = true;
          reader.Transfer(doc);
        }
      }

      if (!canRead) {
        Message::DefaultMessenger()->SendFail()
            << "Failed opening file : " << theName;
        return false;
      }

      Handle(XCAFDoc_ShapeTool) shapeTool =
          XCAFDoc_DocumentTool::ShapeTool(doc->Main());
      Handle(XCAFDoc_ColorTool) colorTool =
          XCAFDoc_DocumentTool::ColorTool(doc->Main());
      TDF_LabelSequence topLevelShapes;
      shapeTool->GetShapes(topLevelShapes);
      spdlog::debug("shapes : {}", topLevelShapes.Length());
      for (Standard_Integer iLabel = 1; iLabel <= topLevelShapes.Length();
           ++iLabel) {
        TDF_Label label = topLevelShapes.Value(iLabel);
        TopoDS_Shape shape;
        shapeTool->GetShape(label, shape);
        Handle(AIS_Shape) shapePrs = new AIS_Shape(shape);
        if (!theName.empty()) {
          aViewer.myObjects.Add(theName.c_str(), shapePrs);
        }
        shapePrs->SetMaterial(Graphic3d_NameOfMaterial_Silver);
        aViewer.Context()->Display(shapePrs, AIS_Shaded, 0, false);
        aViewer.View()->FitAll(0.01, false);
        aViewer.UpdateView();
      }

      isLoaded = true;
    }

    if (theToFree) {
      free(aRawData);
    }
  }

  if (!isLoaded) {
    return false;
  }
  // Handle(AIS_Shape) aShapePrs = new AIS_Shape(aShape);
  // if (!theName.empty()) {
  //   aViewer.myObjects.Add(theName.c_str(), aShapePrs);
  // }
  // aShapePrs->SetMaterial(Graphic3d_NameOfMaterial_Silver);
  // aViewer.Context()->Display(aShapePrs, AIS_Shaded, 0, false);
  aViewer.View()->FitAll(0.01, false);
  aViewer.UpdateView();

  Message::DefaultMessenger()->Send(
      TCollection_AsciiString("Loaded file ") + theName.c_str(), Message_Info);
  Message::DefaultMessenger()->Send(OSD_MemInfo::PrintInfo(), Message_Trace);

  return true;
}

// ================================================================
// Function : displayGround
// Purpose  :
// ================================================================
void WasmOcctView::displayGround(bool theToShow) {
  static Handle(AIS_Shape) aGroundPrs = new AIS_Shape(TopoDS_Shape());

  WasmOcctView& aViewer = Instance();
  Bnd_Box aBox;
  if (theToShow) {
    aViewer.Context()->Remove(aGroundPrs, false);
    aBox = aViewer.View()->View()->MinMaxValues();
  }
  if (aBox.IsVoid() || aBox.IsZThin(Precision::Confusion())) {
    if (!aGroundPrs.IsNull() && aGroundPrs->HasInteractiveContext()) {
      aViewer.Context()->Remove(aGroundPrs, false);
      aViewer.UpdateView();
    }
    return;
  }

  const gp_XYZ aSize = aBox.CornerMax().XYZ() - aBox.CornerMin().XYZ();
  const double aRadius = Max(aSize.X(), aSize.Y());
  const double aZValue = aBox.CornerMin().Z() - Min(10.0, aSize.Z() * 0.01);
  const double aZSize = aRadius * 0.01;
  gp_XYZ aGroundCenter((aBox.CornerMin().X() + aBox.CornerMax().X()) * 0.5,
                       (aBox.CornerMin().Y() + aBox.CornerMax().Y()) * 0.5,
                       aZValue);

  TopoDS_Compound aGround;
  gp_Trsf aTrsf1, aTrsf2;
  aTrsf1.SetTranslation(gp_Vec(aGroundCenter - gp_XYZ(0.0, 0.0, aZSize)));
  aTrsf2.SetTranslation(gp_Vec(aGroundCenter));
  Prs3d_ToolCylinder aCylTool(aRadius, aRadius, aZSize, 50, 1);
  Prs3d_ToolDisk aDiskTool(0.0, aRadius, 50, 1);
  TopoDS_Face aCylFace, aDiskFace1, aDiskFace2;
  BRep_Builder().MakeFace(aCylFace, aCylTool.CreatePolyTriangulation(aTrsf1));
  BRep_Builder().MakeFace(aDiskFace1,
                          aDiskTool.CreatePolyTriangulation(aTrsf1));
  BRep_Builder().MakeFace(aDiskFace2,
                          aDiskTool.CreatePolyTriangulation(aTrsf2));

  BRep_Builder().MakeCompound(aGround);
  BRep_Builder().Add(aGround, aCylFace);
  BRep_Builder().Add(aGround, aDiskFace1);
  BRep_Builder().Add(aGround, aDiskFace2);

  aGroundPrs->SetShape(aGround);
  aGroundPrs->SetToUpdate();
  aGroundPrs->SetMaterial(Graphic3d_NameOfMaterial_Stone);
  aGroundPrs->SetInfiniteState(false);
  aViewer.Context()->Display(aGroundPrs, AIS_Shaded, -1, false);
  aGroundPrs->SetInfiniteState(true);
  aViewer.UpdateView();
}

// Module exports
EMSCRIPTEN_BINDINGS(OccViewerModule) {
  emscripten::function("setCubemapBackground",
                       &WasmOcctView::setCubemapBackground);
  emscripten::function("fitAllObjects", &WasmOcctView::fitAllObjects);
  emscripten::function("removeAllObjects", &WasmOcctView::removeAllObjects);
  emscripten::function("removeObject", &WasmOcctView::removeObject);
  emscripten::function("eraseObject", &WasmOcctView::eraseObject);
  emscripten::function("displayObject", &WasmOcctView::displayObject);
  emscripten::function("displayGround", &WasmOcctView::displayGround);
  emscripten::function("openFromUrl", &WasmOcctView::openFromUrl);
  emscripten::function("openFromMemory", &WasmOcctView::openFromMemory,
                       emscripten::allow_raw_pointers());
  emscripten::function("openFromString", &WasmOcctView::openFromString);
  emscripten::function("openBRepFromMemory", &WasmOcctView::openBRepFromMemory,
                       emscripten::allow_raw_pointers());
}
