// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>
#include <psapi.h>
#include <stddef.h>

#include "base/debug/gdi_debug_util_win.h"
#include "base/logging.h"
#include "base/win/win_util.h"
#include "skia/ext/bitmap_platform_device_win.h"
#include "skia/ext/platform_canvas.h"
#include "skia/ext/skia_utils_win.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkRegion.h"

namespace {

HBITMAP CreateHBitmap(int width, int height, bool is_opaque,
                             HANDLE shared_section, void** data) {
  // CreateDIBSection appears to get unhappy if we create an empty bitmap, so
  // just create a minimal bitmap
  if ((width == 0) || (height == 0)) {
    width = 1;
    height = 1;
  }

  BITMAPINFOHEADER hdr = {0};
  hdr.biSize = sizeof(BITMAPINFOHEADER);
  hdr.biWidth = width;
  hdr.biHeight = -height;  // minus means top-down bitmap
  hdr.biPlanes = 1;
  hdr.biBitCount = 32;
  hdr.biCompression = BI_RGB;  // no compression
  hdr.biSizeImage = 0;
  hdr.biXPelsPerMeter = 1;
  hdr.biYPelsPerMeter = 1;
  hdr.biClrUsed = 0;
  hdr.biClrImportant = 0;

  HBITMAP hbitmap = CreateDIBSection(NULL, reinterpret_cast<BITMAPINFO*>(&hdr),
                                     0, data, shared_section, 0);

#if !defined(_WIN64)
  // If this call fails, we're gonna crash hard. Try to get some useful
  // information out before we crash for post-mortem analysis.
  if (!hbitmap)
    base::debug::GDIBitmapAllocFailure(&hdr, shared_section);
#endif

  return hbitmap;
}

struct CubicPoints {
  SkPoint p[4];
};
typedef std::vector<CubicPoints> CubicPath;
typedef std::vector<CubicPath> CubicPaths;

bool SkPathToCubicPaths(CubicPaths* paths, const SkPath& skpath) {
  paths->clear();
  CubicPath* current_path = NULL;
  SkPoint current_points[4];
  CubicPoints points_to_add;
  SkPath::Iter iter(skpath, false);
  for (SkPath::Verb verb = iter.next(current_points);
       verb != SkPath::kDone_Verb;
       verb = iter.next(current_points)) {
    switch (verb) {
      case SkPath::kMove_Verb: {  // iter.next returns 1 point
        // Ignores it since the point is copied in the next operation. See
        // SkPath::Iter::next() for reference.
        paths->push_back(CubicPath());
        current_path = &paths->back();
        // Skip point addition.
        continue;
      }
      case SkPath::kLine_Verb: {  // iter.next returns 2 points
        points_to_add.p[0] = current_points[0];
        points_to_add.p[1] = current_points[0];
        points_to_add.p[2] = current_points[1];
        points_to_add.p[3] = current_points[1];
        break;
      }
      case SkPath::kQuad_Verb: {  // iter.next returns 3 points
        points_to_add.p[0] = current_points[0];
        points_to_add.p[1] = current_points[1];
        points_to_add.p[2] = current_points[2];
        points_to_add.p[3] = current_points[2];
        break;
      }
      case SkPath::kCubic_Verb: {  // iter.next returns 4 points
        points_to_add.p[0] = current_points[0];
        points_to_add.p[1] = current_points[1];
        points_to_add.p[2] = current_points[2];
        points_to_add.p[3] = current_points[3];
        break;
      }
      case SkPath::kClose_Verb: {  // iter.next returns 1 point (the last point)
        paths->push_back(CubicPath());
        current_path = &paths->back();
        continue;
      }
      default: {
        current_path = NULL;
        // Will return false.
        break;
      }
    }
    SkASSERT(current_path);
    if (!current_path) {
      paths->clear();
      return false;
    }
    current_path->push_back(points_to_add);
  }
  return true;
}

bool LoadPathToDC(HDC context, const SkPath& path) {
  switch (path.getFillType()) {
    case SkPath::kWinding_FillType: {
      int res = SetPolyFillMode(context, WINDING);
      SkASSERT(res != 0);
      break;
    }
    case SkPath::kEvenOdd_FillType: {
      int res = SetPolyFillMode(context, ALTERNATE);
      SkASSERT(res != 0);
      break;
    }
    default: {
      SkASSERT(false);
      break;
    }
  }
  BOOL res = BeginPath(context);
  if (!res) {
      return false;
  }

  CubicPaths paths;
  if (!SkPathToCubicPaths(&paths, path))
    return false;

  std::vector<POINT> points;
  for (CubicPaths::const_iterator path(paths.begin()); path != paths.end();
       ++path) {
    if (!path->size())
      continue;
    points.resize(0);
    points.reserve(path->size() * 3 / 4 + 1);
    points.push_back(skia::SkPointToPOINT(path->front().p[0]));
    for (CubicPath::const_iterator point(path->begin()); point != path->end();
       ++point) {
      // Never add point->p[0]
      points.push_back(skia::SkPointToPOINT(point->p[1]));
      points.push_back(skia::SkPointToPOINT(point->p[2]));
      points.push_back(skia::SkPointToPOINT(point->p[3]));
    }
    SkASSERT((points.size() - 1) % 3 == 0);
    // This is slightly inefficient since all straight line and quadratic lines
    // are "upgraded" to a cubic line.
    // TODO(maruel):  http://b/1147346 We should use
    // PolyDraw/PolyBezier/Polyline whenever possible.
    res = PolyBezier(context, &points.front(),
                     static_cast<DWORD>(points.size()));
    SkASSERT(res != 0);
    if (res == 0)
      break;
  }
  if (res == 0) {
    // Make sure the path is discarded.
    AbortPath(context);
  } else {
    res = EndPath(context);
    SkASSERT(res != 0);
  }
  return true;
}

void LoadTransformToDC(HDC dc, const SkMatrix& matrix) {
  XFORM xf;
  xf.eM11 = matrix[SkMatrix::kMScaleX];
  xf.eM21 = matrix[SkMatrix::kMSkewX];
  xf.eDx = matrix[SkMatrix::kMTransX];
  xf.eM12 = matrix[SkMatrix::kMSkewY];
  xf.eM22 = matrix[SkMatrix::kMScaleY];
  xf.eDy = matrix[SkMatrix::kMTransY];
  SetWorldTransform(dc, &xf);
}

void LoadClippingRegionToDC(HDC context,
                            const SkRegion& region,
                            const SkMatrix& transformation) {
  HRGN hrgn;
  if (region.isEmpty()) {
    // region can be empty, in which case everything will be clipped.
    hrgn = CreateRectRgn(0, 0, 0, 0);
  } else if (region.isRect()) {
    // We don't apply transformation, because the translation is already applied
    // to the region.
    hrgn = CreateRectRgnIndirect(&skia::SkIRectToRECT(region.getBounds()));
  } else {
    // It is complex.
    SkPath path;
    region.getBoundaryPath(&path);
    // Clip. Note that windows clipping regions are not affected by the
    // transform so apply it manually.
    // Since the transform is given as the original translation of canvas, we
    // should apply it in reverse.
    SkMatrix t(transformation);
    t.setTranslateX(-t.getTranslateX());
    t.setTranslateY(-t.getTranslateY());
    path.transform(t);
    LoadPathToDC(context, path);
    hrgn = PathToRegion(context);
  }
  int result = SelectClipRgn(context, hrgn);
  SkASSERT(result != ERROR);
  result = DeleteObject(hrgn);
  SkASSERT(result != 0);
}

}  // namespace

namespace skia {

void DrawToNativeContext(SkCanvas* canvas, HDC hdc, int x, int y,
                         const RECT* src_rect) {
  PlatformDevice* platform_device = GetPlatformDevice(GetTopDevice(*canvas));
  if (platform_device)
    platform_device->DrawToHDC(hdc, x, y, src_rect);
}

void PlatformDevice::DrawToHDC(HDC, int x, int y, const RECT* src_rect) {}

HDC BitmapPlatformDevice::GetBitmapDC() {
  if (!hdc_) {
    hdc_ = CreateCompatibleDC(NULL);
    InitializeDC(hdc_);
    old_hbitmap_ = static_cast<HBITMAP>(SelectObject(hdc_, hbitmap_));
  }

  LoadConfig();
  return hdc_;
}

void BitmapPlatformDevice::ReleaseBitmapDC() {
  SkASSERT(hdc_);
  SelectObject(hdc_, old_hbitmap_);
  DeleteDC(hdc_);
  hdc_ = NULL;
  old_hbitmap_ = NULL;
}

bool BitmapPlatformDevice::IsBitmapDCCreated()
    const {
  return hdc_ != NULL;
}


void BitmapPlatformDevice::SetMatrixClip(
    const SkMatrix& transform,
    const SkRegion& region) {
  transform_ = transform;
  clip_region_ = region;
  config_dirty_ = true;
}

void BitmapPlatformDevice::LoadConfig() {
  if (!config_dirty_ || !hdc_)
    return;  // Nothing to do.
  config_dirty_ = false;

  // Transform.
  LoadTransformToDC(hdc_, transform_);
  LoadClippingRegionToDC(hdc_, clip_region_, transform_);
}

static void DeleteHBitmapCallback(void* addr, void* context) {
  // If context is not NULL then it's a valid HBITMAP to delete.
  // Otherwise we just unmap the pixel memory.
  if (context)
    DeleteObject(static_cast<HBITMAP>(context));
  else
    UnmapViewOfFile(addr);
}

static bool InstallHBitmapPixels(SkBitmap* bitmap, int width, int height,
                                 bool is_opaque, void* data, HBITMAP hbitmap) {
  const SkAlphaType at = is_opaque ? kOpaque_SkAlphaType : kPremul_SkAlphaType;
  const SkImageInfo info = SkImageInfo::MakeN32(width, height, at);
  const size_t rowBytes = info.minRowBytes();
  SkColorTable* color_table = NULL;
  return bitmap->installPixels(info, data, rowBytes, color_table,
                               DeleteHBitmapCallback, hbitmap);
}

// We use this static factory function instead of the regular constructor so
// that we can create the pixel data before calling the constructor. This is
// required so that we can call the base class' constructor with the pixel
// data.
BitmapPlatformDevice* BitmapPlatformDevice::Create(
    int width,
    int height,
    bool is_opaque,
    HANDLE shared_section,
    bool do_clear) {

  void* data;
  HBITMAP hbitmap = NULL;

  // This function contains an implementation of a Skia platform bitmap for
  // drawing and compositing graphics. The original implementation uses Windows
  // GDI to create the backing bitmap memory, however it's possible for a
  // process to not have access to GDI which will cause this code to fail. It's
  // possible to detect when GDI is unavailable and instead directly map the
  // shared memory as the bitmap.
  if (base::win::IsUser32AndGdi32Available()) {
    hbitmap = CreateHBitmap(width, height, is_opaque, shared_section, &data);
    if (!hbitmap)
      return NULL;
  } else {
    DCHECK(shared_section != NULL);
    data = MapViewOfFile(shared_section, FILE_MAP_WRITE, 0, 0,
                         PlatformCanvasStrideForWidth(width) * height);
    if (!data)
      return NULL;
  }

  SkBitmap bitmap;
  if (!InstallHBitmapPixels(&bitmap, width, height, is_opaque, data, hbitmap))
    return NULL;

  if (do_clear)
    bitmap.eraseColor(0);

#ifndef NDEBUG
  // If we were given data, then don't clobber it!
  if (!shared_section && is_opaque)
    // To aid in finding bugs, we set the background color to something
    // obviously wrong so it will be noticable when it is not cleared
    bitmap.eraseARGB(255, 0, 255, 128);  // bright bluish green
#endif

  // The device object will take ownership of the HBITMAP. The initial refcount
  // of the data object will be 1, which is what the constructor expects.
  return new BitmapPlatformDevice(hbitmap, bitmap);
}

// static
BitmapPlatformDevice* BitmapPlatformDevice::Create(int width, int height,
                                                   bool is_opaque) {
  const HANDLE shared_section = NULL;
  const bool do_clear = false;
  return Create(width, height, is_opaque, shared_section, do_clear);
}

// The device will own the HBITMAP, which corresponds to also owning the pixel
// data. Therefore, we do not transfer ownership to the SkBitmapDevice's bitmap.
BitmapPlatformDevice::BitmapPlatformDevice(
    HBITMAP hbitmap,
    const SkBitmap& bitmap)
    : SkBitmapDevice(bitmap),
      hbitmap_(hbitmap),
      old_hbitmap_(NULL),
      hdc_(NULL),
      config_dirty_(true),  // Want to load the config next time.
      transform_(SkMatrix::I()) {
  // The data object is already ref'ed for us by create().
  if (hbitmap) {
    SetPlatformDevice(this, this);
    // Initialize the clip region to the entire bitmap.
    BITMAP bitmap_data;
    if (GetObject(hbitmap_, sizeof(BITMAP), &bitmap_data)) {
      SkIRect rect;
      rect.set(0, 0, bitmap_data.bmWidth, bitmap_data.bmHeight);
      clip_region_ = SkRegion(rect);
    }
  }
}

BitmapPlatformDevice::~BitmapPlatformDevice() {
  if (hdc_)
    ReleaseBitmapDC();
}

HDC BitmapPlatformDevice::BeginPlatformPaint() {
  return GetBitmapDC();
}

void BitmapPlatformDevice::setMatrixClip(const SkMatrix& transform,
                                         const SkRegion& region,
                                         const SkClipStack&) {
  SetMatrixClip(transform, region);
}

void BitmapPlatformDevice::DrawToHDC(HDC dc, int x, int y,
                                     const RECT* src_rect) {
  bool created_dc = !IsBitmapDCCreated();
  HDC source_dc = BeginPlatformPaint();

  RECT temp_rect;
  if (!src_rect) {
    temp_rect.left = 0;
    temp_rect.right = width();
    temp_rect.top = 0;
    temp_rect.bottom = height();
    src_rect = &temp_rect;
  }

  int copy_width = src_rect->right - src_rect->left;
  int copy_height = src_rect->bottom - src_rect->top;

  // We need to reset the translation for our bitmap or (0,0) won't be in the
  // upper left anymore
  SkMatrix identity;
  identity.reset();

  LoadTransformToDC(source_dc, identity);
  if (isOpaque()) {
    BitBlt(dc,
           x,
           y,
           copy_width,
           copy_height,
           source_dc,
           src_rect->left,
           src_rect->top,
           SRCCOPY);
  } else {
    SkASSERT(copy_width != 0 && copy_height != 0);
    BLENDFUNCTION blend_function = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dc,
                  x,
                  y,
                  copy_width,
                  copy_height,
                  source_dc,
                  src_rect->left,
                  src_rect->top,
                  copy_width,
                  copy_height,
                  blend_function);
  }
  LoadTransformToDC(source_dc, transform_);

  if (created_dc)
    ReleaseBitmapDC();
}

const SkBitmap& BitmapPlatformDevice::onAccessBitmap() {
  // FIXME(brettw) OPTIMIZATION: We should only flush if we know a GDI
  // operation has occurred on our DC.
  if (IsBitmapDCCreated())
    GdiFlush();
  return SkBitmapDevice::onAccessBitmap();
}

SkBaseDevice* BitmapPlatformDevice::onCreateDevice(const CreateInfo& cinfo,
                                                   const SkPaint*) {
  const SkImageInfo& info = cinfo.fInfo;
  const bool do_clear = !info.isOpaque();
  SkASSERT(info.colorType() == kN32_SkColorType);
  return Create(info.width(), info.height(), info.isOpaque(), NULL, do_clear);
}

// PlatformCanvas impl

SkCanvas* CreatePlatformCanvas(int width,
                               int height,
                               bool is_opaque,
                               HANDLE shared_section,
                               OnFailureType failureType) {
  sk_sp<SkBaseDevice> dev(
      BitmapPlatformDevice::Create(width, height, is_opaque, shared_section));
  return CreateCanvas(dev, failureType);
}

}  // namespace skia
