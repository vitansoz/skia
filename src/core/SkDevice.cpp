/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkDevice.h"
#include "SkDeviceProperties.h"
#include "SkDraw.h"
#include "SkMetaData.h"
#include "SkPatchUtils.h"
#include "SkPathMeasure.h"
#include "SkRasterClip.h"
#include "SkShader.h"
#include "SkTextBlob.h"
#include "SkTextToPathIter.h"

SkBaseDevice::SkBaseDevice()
    : fLeakyProperties(SkNEW_ARGS(SkDeviceProperties, (SkDeviceProperties::kLegacyLCD_InitType)))
#ifdef SK_DEBUG
    , fAttachedToCanvas(false)
#endif
{
    fOrigin.setZero();
    fMetaData = NULL;
}

SkBaseDevice::SkBaseDevice(const SkDeviceProperties& dp)
    : fLeakyProperties(SkNEW_ARGS(SkDeviceProperties, (dp)))
#ifdef SK_DEBUG
    , fAttachedToCanvas(false)
#endif
{
    fOrigin.setZero();
    fMetaData = NULL;
}

SkBaseDevice::~SkBaseDevice() {
    SkDELETE(fLeakyProperties);
    SkDELETE(fMetaData);
}

SkMetaData& SkBaseDevice::getMetaData() {
    // metadata users are rare, so we lazily allocate it. If that changes we
    // can decide to just make it a field in the device (rather than a ptr)
    if (NULL == fMetaData) {
        fMetaData = new SkMetaData;
    }
    return *fMetaData;
}

SkImageInfo SkBaseDevice::imageInfo() const {
    return SkImageInfo::MakeUnknown();
}

const SkBitmap& SkBaseDevice::accessBitmap(bool changePixels) {
    const SkBitmap& bitmap = this->onAccessBitmap();
    if (changePixels) {
        bitmap.notifyPixelsChanged();
    }
    return bitmap;
}

SkPixelGeometry SkBaseDevice::CreateInfo::AdjustGeometry(const SkImageInfo& info,
                                                         Usage usage,
                                                         SkPixelGeometry geo) {
    switch (usage) {
        case kGeneral_Usage:
            break;
        case kSaveLayer_Usage:
            if (info.alphaType() != kOpaque_SkAlphaType) {
                geo = kUnknown_SkPixelGeometry;
            }
            break;
        case kImageFilter_Usage:
            geo = kUnknown_SkPixelGeometry;
            break;
    }
    return geo;
}

void SkBaseDevice::initForRootLayer(SkPixelGeometry geo) {
    // For now we don't expect to change the geometry for the root-layer, but we make the call
    // anyway to document logically what is going on.
    //
    fLeakyProperties->setPixelGeometry(CreateInfo::AdjustGeometry(this->imageInfo(),
                                                                  kGeneral_Usage,
                                                                  geo));
}

SkSurface* SkBaseDevice::newSurface(const SkImageInfo&, const SkSurfaceProps&) { return NULL; }

const void* SkBaseDevice::peekPixels(SkImageInfo*, size_t*) { return NULL; }

void SkBaseDevice::drawDRRect(const SkDraw& draw, const SkRRect& outer,
                              const SkRRect& inner, const SkPaint& paint) {
    SkPath path;
    path.addRRect(outer);
    path.addRRect(inner);
    path.setFillType(SkPath::kEvenOdd_FillType);

    const SkMatrix* preMatrix = NULL;
    const bool pathIsMutable = true;
    this->drawPath(draw, path, paint, preMatrix, pathIsMutable);
}

void SkBaseDevice::drawPatch(const SkDraw& draw, const SkPoint cubics[12], const SkColor colors[4],
                             const SkPoint texCoords[4], SkXfermode* xmode, const SkPaint& paint) {
    SkPatchUtils::VertexData data;
    
    SkISize lod = SkPatchUtils::GetLevelOfDetail(cubics, draw.fMatrix);

    // It automatically adjusts lodX and lodY in case it exceeds the number of indices.
    // If it fails to generate the vertices, then we do not draw. 
    if (SkPatchUtils::getVertexData(&data, cubics, colors, texCoords, lod.width(), lod.height())) {
        this->drawVertices(draw, SkCanvas::kTriangles_VertexMode, data.fVertexCount, data.fPoints,
                           data.fTexCoords, data.fColors, xmode, data.fIndices, data.fIndexCount,
                           paint);
    }
}

void SkBaseDevice::drawTextBlob(const SkDraw& draw, const SkTextBlob* blob, SkScalar x, SkScalar y,
                                const SkPaint &paint) {

    SkPaint runPaint = paint;

    SkTextBlob::RunIterator it(blob);
    while (!it.done()) {
        size_t textLen = it.glyphCount() * sizeof(uint16_t);
        const SkPoint& offset = it.offset();
        // applyFontToPaint() always overwrites the exact same attributes,
        // so it is safe to not re-seed the paint.
        it.applyFontToPaint(&runPaint);
        runPaint.setFlags(this->filterTextFlags(runPaint));

        switch (it.positioning()) {
        case SkTextBlob::kDefault_Positioning:
            this->drawText(draw, it.glyphs(), textLen, x + offset.x(), y + offset.y(), runPaint);
            break;
        case SkTextBlob::kHorizontal_Positioning:
            this->drawPosText(draw, it.glyphs(), textLen, it.pos(), 1,
                              SkPoint::Make(x, y + offset.y()), runPaint);
            break;
        case SkTextBlob::kFull_Positioning:
            this->drawPosText(draw, it.glyphs(), textLen, it.pos(), 2,
                              SkPoint::Make(x, y), runPaint);
            break;
        default:
            SkFAIL("unhandled positioning mode");
        }

        it.next();
    }
}

bool SkBaseDevice::readPixels(const SkImageInfo& info, void* dstP, size_t rowBytes, int x, int y) {
#ifdef SK_DEBUG
    SkASSERT(info.width() > 0 && info.height() > 0);
    SkASSERT(dstP);
    SkASSERT(rowBytes >= info.minRowBytes());
    SkASSERT(x >= 0 && y >= 0);

    const SkImageInfo& srcInfo = this->imageInfo();
    SkASSERT(x + info.width() <= srcInfo.width());
    SkASSERT(y + info.height() <= srcInfo.height());
#endif
    return this->onReadPixels(info, dstP, rowBytes, x, y);
}

bool SkBaseDevice::writePixels(const SkImageInfo& info, const void* pixels, size_t rowBytes,
                               int x, int y) {
#ifdef SK_DEBUG
    SkASSERT(info.width() > 0 && info.height() > 0);
    SkASSERT(pixels);
    SkASSERT(rowBytes >= info.minRowBytes());
    SkASSERT(x >= 0 && y >= 0);

    const SkImageInfo& dstInfo = this->imageInfo();
    SkASSERT(x + info.width() <= dstInfo.width());
    SkASSERT(y + info.height() <= dstInfo.height());
#endif
    return this->onWritePixels(info, pixels, rowBytes, x, y);
}

bool SkBaseDevice::onWritePixels(const SkImageInfo&, const void*, size_t, int, int) {
    return false;
}

bool SkBaseDevice::onReadPixels(const SkImageInfo&, void*, size_t, int x, int y) {
    return false;
}

void* SkBaseDevice::accessPixels(SkImageInfo* info, size_t* rowBytes) {
    SkImageInfo tmpInfo;
    size_t tmpRowBytes;
    if (NULL == info) {
        info = &tmpInfo;
    }
    if (NULL == rowBytes) {
        rowBytes = &tmpRowBytes;
    }
    return this->onAccessPixels(info, rowBytes);
}

void* SkBaseDevice::onAccessPixels(SkImageInfo* info, size_t* rowBytes) {
    return NULL;
}

bool SkBaseDevice::EXPERIMENTAL_drawPicture(SkCanvas*, const SkPicture*, const SkMatrix*,
                                            const SkPaint*) {
    // The base class doesn't perform any accelerated picture rendering
    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////

static void morphpoints(SkPoint dst[], const SkPoint src[], int count,
                        SkPathMeasure& meas, const SkMatrix& matrix) {
    SkMatrix::MapXYProc proc = matrix.getMapXYProc();
    
    for (int i = 0; i < count; i++) {
        SkPoint pos;
        SkVector tangent;
        
        proc(matrix, src[i].fX, src[i].fY, &pos);
        SkScalar sx = pos.fX;
        SkScalar sy = pos.fY;
        
        if (!meas.getPosTan(sx, &pos, &tangent)) {
            // set to 0 if the measure failed, so that we just set dst == pos
            tangent.set(0, 0);
        }
        
        /*  This is the old way (that explains our approach but is way too slow
         SkMatrix    matrix;
         SkPoint     pt;
         
         pt.set(sx, sy);
         matrix.setSinCos(tangent.fY, tangent.fX);
         matrix.preTranslate(-sx, 0);
         matrix.postTranslate(pos.fX, pos.fY);
         matrix.mapPoints(&dst[i], &pt, 1);
         */
        dst[i].set(pos.fX - SkScalarMul(tangent.fY, sy),
                   pos.fY + SkScalarMul(tangent.fX, sy));
    }
}

/*  TODO
 
 Need differentially more subdivisions when the follow-path is curvy. Not sure how to
 determine that, but we need it. I guess a cheap answer is let the caller tell us,
 but that seems like a cop-out. Another answer is to get Rob Johnson to figure it out.
 */
static void morphpath(SkPath* dst, const SkPath& src, SkPathMeasure& meas,
                      const SkMatrix& matrix) {
    SkPath::Iter    iter(src, false);
    SkPoint         srcP[4], dstP[3];
    SkPath::Verb    verb;
    
    while ((verb = iter.next(srcP)) != SkPath::kDone_Verb) {
        switch (verb) {
            case SkPath::kMove_Verb:
                morphpoints(dstP, srcP, 1, meas, matrix);
                dst->moveTo(dstP[0]);
                break;
            case SkPath::kLine_Verb:
                // turn lines into quads to look bendy
                srcP[0].fX = SkScalarAve(srcP[0].fX, srcP[1].fX);
                srcP[0].fY = SkScalarAve(srcP[0].fY, srcP[1].fY);
                morphpoints(dstP, srcP, 2, meas, matrix);
                dst->quadTo(dstP[0], dstP[1]);
                break;
            case SkPath::kQuad_Verb:
                morphpoints(dstP, &srcP[1], 2, meas, matrix);
                dst->quadTo(dstP[0], dstP[1]);
                break;
            case SkPath::kCubic_Verb:
                morphpoints(dstP, &srcP[1], 3, meas, matrix);
                dst->cubicTo(dstP[0], dstP[1], dstP[2]);
                break;
            case SkPath::kClose_Verb:
                dst->close();
                break;
            default:
                SkDEBUGFAIL("unknown verb");
                break;
        }
    }
}

void SkBaseDevice::drawTextOnPath(const SkDraw& draw, const void* text, size_t byteLength,
                                  const SkPath& follow, const SkMatrix* matrix,
                                  const SkPaint& paint) {
    SkASSERT(byteLength == 0 || text != NULL);
    
    // nothing to draw
    if (text == NULL || byteLength == 0 || draw.fRC->isEmpty()) {
        return;
    }
    
    SkTextToPathIter    iter((const char*)text, byteLength, paint, true);
    SkPathMeasure       meas(follow, false);
    SkScalar            hOffset = 0;
    
    // need to measure first
    if (paint.getTextAlign() != SkPaint::kLeft_Align) {
        SkScalar pathLen = meas.getLength();
        if (paint.getTextAlign() == SkPaint::kCenter_Align) {
            pathLen = SkScalarHalf(pathLen);
        }
        hOffset += pathLen;
    }
    
    const SkPath*   iterPath;
    SkScalar        xpos;
    SkMatrix        scaledMatrix;
    SkScalar        scale = iter.getPathScale();
    
    scaledMatrix.setScale(scale, scale);
    
    while (iter.next(&iterPath, &xpos)) {
        if (iterPath) {
            SkPath      tmp;
            SkMatrix    m(scaledMatrix);
            
            tmp.setIsVolatile(true);
            m.postTranslate(xpos + hOffset, 0);
            if (matrix) {
                m.postConcat(*matrix);
            }
            morphpath(&tmp, *iterPath, meas, m);
            this->drawPath(draw, tmp, iter.getPaint(), NULL, true);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////

uint32_t SkBaseDevice::filterTextFlags(const SkPaint& paint) const {
    uint32_t flags = paint.getFlags();

    if (!paint.isLCDRenderText() || !paint.isAntiAlias()) {
        return flags;
    }

    if (kUnknown_SkPixelGeometry == fLeakyProperties->pixelGeometry()
        || this->onShouldDisableLCD(paint)) {

        flags &= ~SkPaint::kLCDRenderText_Flag;
        flags |= SkPaint::kGenA8FromLCD_Flag;
    }

    return flags;
}

