/*
 * Copyright (C) 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/html/shadow/MediaControlElements.h"

#include "bindings/core/v8/ExceptionStatePlaceholder.h"
#include "core/InputTypeNames.h"
#include "core/dom/ClientRect.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/events/MouseEvent.h"
#include "core/frame/LocalFrame.h"
#include "core/html/HTMLMediaSource.h"
#include "core/html/HTMLVideoElement.h"
#include "core/html/TimeRanges.h"
#include "core/html/shadow/MediaControls.h"
#include "core/input/EventHandler.h"
#include "core/layout/LayoutTheme.h"
#include "core/layout/LayoutVideo.h"
#include "core/layout/api/LayoutSliderItem.h"
#include "platform/Histogram.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "public/platform/Platform.h"
#include "public/platform/UserMetricsAction.h"

namespace blink {

using namespace HTMLNames;

namespace {

// This is the duration from mediaControls.css
const double fadeOutDuration = 0.3;

bool isUserInteractionEvent(Event* event)
{
    const AtomicString& type = event->type();
    return type == EventTypeNames::mousedown
        || type == EventTypeNames::mouseup
        || type == EventTypeNames::click
        || type == EventTypeNames::dblclick
        || event->isKeyboardEvent()
        || event->isTouchEvent();
}

// Sliders (the volume control and timeline) need to capture some additional events used when dragging the thumb.
bool isUserInteractionEventForSlider(Event* event, LayoutObject* layoutObject)
{
    // It is unclear if this can be converted to isUserInteractionEvent(), since
    // mouse* events seem to be eaten during a drag anyway.  crbug.com/516416 .
    if (isUserInteractionEvent(event))
        return true;

    // Some events are only captured during a slider drag.
    LayoutSliderItem slider = LayoutSliderItem(toLayoutSlider(layoutObject));
    if (!slider.isNull() && !slider.inDragMode())
        return false;

    const AtomicString& type = event->type();
    return type == EventTypeNames::mouseover
        || type == EventTypeNames::mouseout
        || type == EventTypeNames::mousemove;
}

Element* elementFromCenter(Element& element)
{
    ClientRect* clientRect = element.getBoundingClientRect();
    int centerX = static_cast<int>((clientRect->left() + clientRect->right()) / 2);
    int centerY = static_cast<int>((clientRect->top() + clientRect->bottom()) / 2);

    return element.document().elementFromPoint(centerX , centerY);
}

} // anonymous namespace

MediaControlPanelElement::MediaControlPanelElement(MediaControls& mediaControls)
    : MediaControlDivElement(mediaControls, MediaControlsPanel)
    , m_isDisplayed(false)
    , m_opaque(true)
    , m_transitionTimer(this, &MediaControlPanelElement::transitionTimerFired)
{
}

RawPtr<MediaControlPanelElement> MediaControlPanelElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlPanelElement> panel = new MediaControlPanelElement(mediaControls);
    panel->setShadowPseudoId(AtomicString("-webkit-media-controls-panel"));
    return panel.release();
}

void MediaControlPanelElement::defaultEventHandler(Event* event)
{
    // Suppress the media element activation behavior (toggle play/pause) when
    // any part of the control panel is clicked.
    if (event->type() == EventTypeNames::click) {
        event->setDefaultHandled();
        return;
    }
    HTMLDivElement::defaultEventHandler(event);
}

void MediaControlPanelElement::startTimer()
{
    stopTimer();

    // The timer is required to set the property display:'none' on the panel,
    // such that captions are correctly displayed at the bottom of the video
    // at the end of the fadeout transition.
    // FIXME: Racing a transition with a setTimeout like this is wrong.
    m_transitionTimer.startOneShot(fadeOutDuration, BLINK_FROM_HERE);
}

void MediaControlPanelElement::stopTimer()
{
    if (m_transitionTimer.isActive())
        m_transitionTimer.stop();
}

void MediaControlPanelElement::transitionTimerFired(Timer<MediaControlPanelElement>*)
{
    if (!m_opaque)
        setIsWanted(false);

    stopTimer();
}

void MediaControlPanelElement::didBecomeVisible()
{
    ASSERT(m_isDisplayed && m_opaque);
    mediaElement().mediaControlsDidBecomeVisible();
}

void MediaControlPanelElement::makeOpaque()
{
    if (m_opaque)
        return;

    setInlineStyleProperty(CSSPropertyOpacity, 1.0, CSSPrimitiveValue::UnitType::Number);
    m_opaque = true;

    if (m_isDisplayed) {
        setIsWanted(true);
        didBecomeVisible();
    }
}

void MediaControlPanelElement::makeTransparent()
{
    if (!m_opaque)
        return;

    setInlineStyleProperty(CSSPropertyOpacity, 0.0, CSSPrimitiveValue::UnitType::Number);

    m_opaque = false;
    startTimer();
}

void MediaControlPanelElement::setIsDisplayed(bool isDisplayed)
{
    if (m_isDisplayed == isDisplayed)
        return;

    m_isDisplayed = isDisplayed;
    if (m_isDisplayed && m_opaque)
        didBecomeVisible();
}

bool MediaControlPanelElement::keepEventInNode(Event* event)
{
    return isUserInteractionEvent(event);
}

// ----------------------------

MediaControlPanelEnclosureElement::MediaControlPanelEnclosureElement(MediaControls& mediaControls)
    // Mapping onto same MediaControlElementType as panel element, since it has similar properties.
    : MediaControlDivElement(mediaControls, MediaControlsPanel)
{
}

RawPtr<MediaControlPanelEnclosureElement> MediaControlPanelEnclosureElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlPanelEnclosureElement> enclosure = new MediaControlPanelEnclosureElement(mediaControls);
    enclosure->setShadowPseudoId(AtomicString("-webkit-media-controls-enclosure"));
    return enclosure.release();
}

// ----------------------------

MediaControlOverlayEnclosureElement::MediaControlOverlayEnclosureElement(MediaControls& mediaControls)
    // Mapping onto same MediaControlElementType as panel element, since it has similar properties.
    : MediaControlDivElement(mediaControls, MediaControlsPanel)
{
}

RawPtr<MediaControlOverlayEnclosureElement> MediaControlOverlayEnclosureElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlOverlayEnclosureElement> enclosure = new MediaControlOverlayEnclosureElement(mediaControls);
    enclosure->setShadowPseudoId(AtomicString("-webkit-media-controls-overlay-enclosure"));
    return enclosure.release();
}

void* MediaControlOverlayEnclosureElement::preDispatchEventHandler(Event* event)
{
    // When the media element is clicked or touched we want to make the overlay cast button visible
    // (if the other requirements are right) even if JavaScript is doing its own handling of the event.
    // Doing it in preDispatchEventHandler prevents any interference from JavaScript.
    // Note that we can't simply test for click, since JS handling of touch events can prevent their translation to click events.
    if (event && (event->type() == EventTypeNames::click || event->type() == EventTypeNames::touchstart))
        mediaControls().showOverlayCastButtonIfNeeded();
    return MediaControlDivElement::preDispatchEventHandler(event);
}


// ----------------------------

MediaControlMuteButtonElement::MediaControlMuteButtonElement(MediaControls& mediaControls)
    : MediaControlInputElement(mediaControls, MediaMuteButton)
{
}

RawPtr<MediaControlMuteButtonElement> MediaControlMuteButtonElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlMuteButtonElement> button = new MediaControlMuteButtonElement(mediaControls);
    button->ensureUserAgentShadowRoot();
    button->setType(InputTypeNames::button);
    button->setShadowPseudoId(AtomicString("-webkit-media-controls-mute-button"));
    return button.release();
}

void MediaControlMuteButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == EventTypeNames::click) {
        if (mediaElement().muted())
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.Unmute"));
        else
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.Mute"));

        mediaElement().setMuted(!mediaElement().muted());
        event->setDefaultHandled();
    }

    HTMLInputElement::defaultEventHandler(event);
}

void MediaControlMuteButtonElement::updateDisplayType()
{
    setDisplayType(mediaElement().muted() ? MediaUnMuteButton : MediaMuteButton);
}

// ----------------------------

MediaControlPlayButtonElement::MediaControlPlayButtonElement(MediaControls& mediaControls)
    : MediaControlInputElement(mediaControls, MediaPlayButton)
{
}

RawPtr<MediaControlPlayButtonElement> MediaControlPlayButtonElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlPlayButtonElement> button = new MediaControlPlayButtonElement(mediaControls);
    button->ensureUserAgentShadowRoot();
    button->setType(InputTypeNames::button);
    button->setShadowPseudoId(AtomicString("-webkit-media-controls-play-button"));
    return button.release();
}

void MediaControlPlayButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == EventTypeNames::click) {
        if (mediaElement().paused())
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.Play"));
        else
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.Pause"));

        // Allow play attempts for plain src= media to force a reload in the error state. This allows potential
        // recovery for transient network and decoder resource issues.
        const String& url = mediaElement().currentSrc().getString();
        if (mediaElement().error() && !HTMLMediaElement::isMediaStreamURL(url) && !HTMLMediaSource::lookup(url))
            mediaElement().load();

        mediaElement().togglePlayState();
        updateDisplayType();
        event->setDefaultHandled();
    }
    HTMLInputElement::defaultEventHandler(event);
}

void MediaControlPlayButtonElement::updateDisplayType()
{
    setDisplayType(mediaElement().paused() ? MediaPlayButton : MediaPauseButton);
}

// ----------------------------

MediaControlOverlayPlayButtonElement::MediaControlOverlayPlayButtonElement(MediaControls& mediaControls)
    : MediaControlInputElement(mediaControls, MediaOverlayPlayButton)
{
}

RawPtr<MediaControlOverlayPlayButtonElement> MediaControlOverlayPlayButtonElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlOverlayPlayButtonElement> button = new MediaControlOverlayPlayButtonElement(mediaControls);
    button->ensureUserAgentShadowRoot();
    button->setType(InputTypeNames::button);
    button->setShadowPseudoId(AtomicString("-webkit-media-controls-overlay-play-button"));
    return button.release();
}

void MediaControlOverlayPlayButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == EventTypeNames::click && mediaElement().paused()) {
        Platform::current()->recordAction(UserMetricsAction("Media.Controls.PlayOverlay"));
        mediaElement().play();
        updateDisplayType();
        event->setDefaultHandled();
    }
}

void MediaControlOverlayPlayButtonElement::updateDisplayType()
{
    setIsWanted(mediaElement().shouldShowControls() && mediaElement().paused());
}

bool MediaControlOverlayPlayButtonElement::keepEventInNode(Event* event)
{
    return isUserInteractionEvent(event);
}


// ----------------------------

MediaControlToggleClosedCaptionsButtonElement::MediaControlToggleClosedCaptionsButtonElement(MediaControls& mediaControls)
    : MediaControlInputElement(mediaControls, MediaShowClosedCaptionsButton)
{
}

RawPtr<MediaControlToggleClosedCaptionsButtonElement> MediaControlToggleClosedCaptionsButtonElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlToggleClosedCaptionsButtonElement> button = new MediaControlToggleClosedCaptionsButtonElement(mediaControls);
    button->ensureUserAgentShadowRoot();
    button->setType(InputTypeNames::button);
    button->setShadowPseudoId(AtomicString("-webkit-media-controls-toggle-closed-captions-button"));
    button->setIsWanted(false);
    return button.release();
}

void MediaControlToggleClosedCaptionsButtonElement::updateDisplayType()
{
    bool captionsVisible = mediaElement().closedCaptionsVisible();
    setDisplayType(captionsVisible ? MediaHideClosedCaptionsButton : MediaShowClosedCaptionsButton);
    setChecked(captionsVisible);
}

void MediaControlToggleClosedCaptionsButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == EventTypeNames::click) {
        if (mediaElement().closedCaptionsVisible())
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.ClosedCaptionHide"));
        else
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.ClosedCaptionShow"));
        mediaElement().setClosedCaptionsVisible(!mediaElement().closedCaptionsVisible());
        setChecked(mediaElement().closedCaptionsVisible());
        updateDisplayType();
        event->setDefaultHandled();
    }

    HTMLInputElement::defaultEventHandler(event);
}

// ----------------------------

MediaControlTimelineElement::MediaControlTimelineElement(MediaControls& mediaControls)
    : MediaControlInputElement(mediaControls, MediaSlider)
{
}

RawPtr<MediaControlTimelineElement> MediaControlTimelineElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlTimelineElement> timeline = new MediaControlTimelineElement(mediaControls);
    timeline->ensureUserAgentShadowRoot();
    timeline->setType(InputTypeNames::range);
    timeline->setAttribute(stepAttr, "any");
    timeline->setShadowPseudoId(AtomicString("-webkit-media-controls-timeline"));
    return timeline.release();
}

void MediaControlTimelineElement::defaultEventHandler(Event* event)
{
    if (event->isMouseEvent() && toMouseEvent(event)->button() != LeftButton)
        return;

    if (!inShadowIncludingDocument() || !document().isActive())
        return;

    if (event->type() == EventTypeNames::mousedown) {
        Platform::current()->recordAction(UserMetricsAction("Media.Controls.ScrubbingBegin"));
        mediaControls().beginScrubbing();
    }

    if (event->type() == EventTypeNames::mouseup) {
        Platform::current()->recordAction(UserMetricsAction("Media.Controls.ScrubbingEnd"));
        mediaControls().endScrubbing();
    }

    MediaControlInputElement::defaultEventHandler(event);

    if (event->type() == EventTypeNames::mouseover || event->type() == EventTypeNames::mouseout || event->type() == EventTypeNames::mousemove)
        return;

    double time = value().toDouble();
    if (event->type() == EventTypeNames::input) {
        // FIXME: This will need to take the timeline offset into consideration
        // once that concept is supported, see https://crbug.com/312699
        if (mediaElement().seekable()->contain(time))
            mediaElement().setCurrentTime(time);
    }

    LayoutSliderItem slider = LayoutSliderItem(toLayoutSlider(layoutObject()));
    if (!slider.isNull() && slider.inDragMode())
        mediaControls().updateCurrentTimeDisplay();
}

bool MediaControlTimelineElement::willRespondToMouseClickEvents()
{
    return inShadowIncludingDocument() && document().isActive();
}

void MediaControlTimelineElement::setPosition(double currentTime)
{
    setValue(String::number(currentTime));

    if (LayoutObject* layoutObject = this->layoutObject())
        layoutObject->setShouldDoFullPaintInvalidation();
}

void MediaControlTimelineElement::setDuration(double duration)
{
    setFloatingPointAttribute(maxAttr, std::isfinite(duration) ? duration : 0);

    if (LayoutObject* layoutObject = this->layoutObject())
        layoutObject->setShouldDoFullPaintInvalidation();
}

bool MediaControlTimelineElement::keepEventInNode(Event* event)
{
    return isUserInteractionEventForSlider(event, layoutObject());
}

// ----------------------------

MediaControlVolumeSliderElement::MediaControlVolumeSliderElement(MediaControls& mediaControls)
    : MediaControlInputElement(mediaControls, MediaVolumeSlider)
{
}

RawPtr<MediaControlVolumeSliderElement> MediaControlVolumeSliderElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlVolumeSliderElement> slider = new MediaControlVolumeSliderElement(mediaControls);
    slider->ensureUserAgentShadowRoot();
    slider->setType(InputTypeNames::range);
    slider->setAttribute(stepAttr, "any");
    slider->setAttribute(maxAttr, "1");
    slider->setShadowPseudoId(AtomicString("-webkit-media-controls-volume-slider"));
    return slider.release();
}

void MediaControlVolumeSliderElement::defaultEventHandler(Event* event)
{
    if (event->isMouseEvent() && toMouseEvent(event)->button() != LeftButton)
        return;

    if (!inShadowIncludingDocument() || !document().isActive())
        return;

    MediaControlInputElement::defaultEventHandler(event);

    if (event->type() == EventTypeNames::mouseover || event->type() == EventTypeNames::mouseout || event->type() == EventTypeNames::mousemove)
        return;

    if (event->type() == EventTypeNames::mousedown)
        Platform::current()->recordAction(UserMetricsAction("Media.Controls.VolumeChangeBegin"));

    if (event->type() == EventTypeNames::mouseup)
        Platform::current()->recordAction(UserMetricsAction("Media.Controls.VolumeChangeEnd"));

    double volume = value().toDouble();
    mediaElement().setVolume(volume, ASSERT_NO_EXCEPTION);
    mediaElement().setMuted(false);
}

bool MediaControlVolumeSliderElement::willRespondToMouseMoveEvents()
{
    if (!inShadowIncludingDocument() || !document().isActive())
        return false;

    return MediaControlInputElement::willRespondToMouseMoveEvents();
}

bool MediaControlVolumeSliderElement::willRespondToMouseClickEvents()
{
    if (!inShadowIncludingDocument() || !document().isActive())
        return false;

    return MediaControlInputElement::willRespondToMouseClickEvents();
}

void MediaControlVolumeSliderElement::setVolume(double volume)
{
    if (value().toDouble() != volume)
        setValue(String::number(volume));
}

bool MediaControlVolumeSliderElement::keepEventInNode(Event* event)
{
    return isUserInteractionEventForSlider(event, layoutObject());
}

// ----------------------------

MediaControlFullscreenButtonElement::MediaControlFullscreenButtonElement(MediaControls& mediaControls)
    : MediaControlInputElement(mediaControls, MediaEnterFullscreenButton)
{
}

RawPtr<MediaControlFullscreenButtonElement> MediaControlFullscreenButtonElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlFullscreenButtonElement> button = new MediaControlFullscreenButtonElement(mediaControls);
    button->ensureUserAgentShadowRoot();
    button->setType(InputTypeNames::button);
    button->setShadowPseudoId(AtomicString("-webkit-media-controls-fullscreen-button"));
    button->setIsWanted(false);
    return button.release();
}

void MediaControlFullscreenButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == EventTypeNames::click) {
        if (mediaElement().isFullscreen()) {
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.ExitFullscreen"));
            mediaElement().exitFullscreen();
        } else {
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.EnterFullscreen"));
            mediaElement().enterFullscreen();
        }
        event->setDefaultHandled();
    }
    HTMLInputElement::defaultEventHandler(event);
}

void MediaControlFullscreenButtonElement::setIsFullscreen(bool isFullscreen)
{
    setDisplayType(isFullscreen ? MediaExitFullscreenButton : MediaEnterFullscreenButton);
}

// ----------------------------

MediaControlCastButtonElement::MediaControlCastButtonElement(MediaControls& mediaControls, bool isOverlayButton)
    : MediaControlInputElement(mediaControls, MediaCastOnButton), m_isOverlayButton(isOverlayButton)
{
    if (m_isOverlayButton)
        recordMetrics(CastOverlayMetrics::Created);
    setIsPlayingRemotely(false);
}

RawPtr<MediaControlCastButtonElement> MediaControlCastButtonElement::create(MediaControls& mediaControls, bool isOverlayButton)
{
    RawPtr<MediaControlCastButtonElement> button = new MediaControlCastButtonElement(mediaControls, isOverlayButton);
    button->ensureUserAgentShadowRoot();
    button->setType(InputTypeNames::button);
    return button.release();
}

void MediaControlCastButtonElement::defaultEventHandler(Event* event)
{
    if (event->type() == EventTypeNames::click) {
        if (m_isOverlayButton)
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.CastOverlay"));
        else
            Platform::current()->recordAction(UserMetricsAction("Media.Controls.Cast"));

        if (m_isOverlayButton && !m_clickUseCounted) {
            m_clickUseCounted = true;
            recordMetrics(CastOverlayMetrics::Clicked);
        }
        if (mediaElement().isPlayingRemotely()) {
            mediaElement().requestRemotePlaybackControl();
        } else {
            mediaElement().requestRemotePlayback();
        }
    }
    HTMLInputElement::defaultEventHandler(event);
}

const AtomicString& MediaControlCastButtonElement::shadowPseudoId() const
{
    DEFINE_STATIC_LOCAL(AtomicString, id_nonOverlay, ("-internal-media-controls-cast-button"));
    DEFINE_STATIC_LOCAL(AtomicString, id_overlay, ("-internal-media-controls-overlay-cast-button"));
    return m_isOverlayButton ? id_overlay : id_nonOverlay;
}

void MediaControlCastButtonElement::setIsPlayingRemotely(bool isPlayingRemotely)
{
    if (isPlayingRemotely) {
        if (m_isOverlayButton) {
            setDisplayType(MediaOverlayCastOnButton);
        } else {
            setDisplayType(MediaCastOnButton);
        }
    } else {
        if (m_isOverlayButton) {
            setDisplayType(MediaOverlayCastOffButton);
        } else {
            setDisplayType(MediaCastOffButton);
        }
    }
}

void MediaControlCastButtonElement::tryShowOverlay()
{
    ASSERT(m_isOverlayButton);

    setIsWanted(true);
    if (elementFromCenter(*this) != &mediaElement()) {
        setIsWanted(false);
        return;
    }

    ASSERT(isWanted());
    if (!m_showUseCounted) {
        m_showUseCounted = true;
        recordMetrics(CastOverlayMetrics::Shown);
    }
}

bool MediaControlCastButtonElement::keepEventInNode(Event* event)
{
    return isUserInteractionEvent(event);
}

void MediaControlCastButtonElement::recordMetrics(CastOverlayMetrics metric)
{
    ASSERT(m_isOverlayButton);
    DEFINE_STATIC_LOCAL(EnumerationHistogram, overlayHistogram, ("Cast.Sender.Overlay", static_cast<int>(CastOverlayMetrics::Count)));
    overlayHistogram.count(static_cast<int>(metric));
}

// ----------------------------

MediaControlTimeRemainingDisplayElement::MediaControlTimeRemainingDisplayElement(MediaControls& mediaControls)
    : MediaControlTimeDisplayElement(mediaControls, MediaTimeRemainingDisplay)
{
}

RawPtr<MediaControlTimeRemainingDisplayElement> MediaControlTimeRemainingDisplayElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlTimeRemainingDisplayElement> element = new MediaControlTimeRemainingDisplayElement(mediaControls);
    element->setShadowPseudoId(AtomicString("-webkit-media-controls-time-remaining-display"));
    return element.release();
}

// ----------------------------

MediaControlCurrentTimeDisplayElement::MediaControlCurrentTimeDisplayElement(MediaControls& mediaControls)
    : MediaControlTimeDisplayElement(mediaControls, MediaCurrentTimeDisplay)
{
}

RawPtr<MediaControlCurrentTimeDisplayElement> MediaControlCurrentTimeDisplayElement::create(MediaControls& mediaControls)
{
    RawPtr<MediaControlCurrentTimeDisplayElement> element = new MediaControlCurrentTimeDisplayElement(mediaControls);
    element->setShadowPseudoId(AtomicString("-webkit-media-controls-current-time-display"));
    return element.release();
}

} // namespace blink
