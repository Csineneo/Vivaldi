// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/canvas/HTMLCanvasElementModule.h"

#include "core/frame/FrameView.h"
#include "core/html/HTMLCanvasElement.h"
#include "core/html/HTMLDocument.h"
#include "core/loader/EmptyClients.h"
#include "core/testing/DummyPageHolder.h"
#include "modules/offscreencanvas/OffscreenCanvas.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class HTMLCanvasElementModuleTest : public ::testing::Test {
protected:
    virtual void SetUp()
    {
        Page::PageClients pageClients;
        fillWithEmptyClients(pageClients);
        OwnPtr<DummyPageHolder> m_dummyPageHolder = DummyPageHolder::create(IntSize(800, 600), &pageClients);
        Persistent<HTMLDocument> m_document = toHTMLDocument(&m_dummyPageHolder->document());
        m_document->documentElement()->setInnerHTML("<body><canvas id='c'></canvas></body>", ASSERT_NO_EXCEPTION);
        m_document->view()->updateAllLifecyclePhases();
        m_canvasElement = toHTMLCanvasElement(m_document->getElementById("c"));
    }

    HTMLCanvasElement& canvasElement() const { return *m_canvasElement; }
private:
    Persistent<HTMLCanvasElement> m_canvasElement;
};

TEST_F(HTMLCanvasElementModuleTest, TransferControlToOffscreen)
{
    NonThrowableExceptionState exceptionState;
    OffscreenCanvas* offscreenCanvas = HTMLCanvasElementModule::transferControlToOffscreen(canvasElement(), exceptionState);
    HTMLCanvasElement* canvas = offscreenCanvas->getAssociatedCanvas();
    EXPECT_EQ(canvas, canvasElement());
}

} // namespace blink
