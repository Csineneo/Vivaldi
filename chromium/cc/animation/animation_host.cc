// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/animation/animation_host.h"

#include <algorithm>

#include "base/macros.h"
#include "cc/animation/animation_delegate.h"
#include "cc/animation/animation_events.h"
#include "cc/animation/animation_id_provider.h"
#include "cc/animation/animation_player.h"
#include "cc/animation/animation_registrar.h"
#include "cc/animation/animation_timeline.h"
#include "cc/animation/element_animations.h"
#include "cc/animation/scroll_offset_animation_curve.h"
#include "cc/animation/timing_function.h"
#include "ui/gfx/geometry/box_f.h"
#include "ui/gfx/geometry/scroll_offset.h"

namespace cc {

class AnimationHost::ScrollOffsetAnimations : public AnimationDelegate {
 public:
  explicit ScrollOffsetAnimations(AnimationHost* animation_host)
      : animation_host_(animation_host),
        scroll_offset_timeline_(
            AnimationTimeline::Create(AnimationIdProvider::NextTimelineId())),
        scroll_offset_animation_player_(
            AnimationPlayer::Create(AnimationIdProvider::NextPlayerId())) {
    scroll_offset_timeline_->set_is_impl_only(true);
    scroll_offset_animation_player_->set_layer_animation_delegate(this);

    animation_host_->AddAnimationTimeline(scroll_offset_timeline_.get());
    scroll_offset_timeline_->AttachPlayer(
        scroll_offset_animation_player_.get());
  }

  ~ScrollOffsetAnimations() override {
    scroll_offset_timeline_->DetachPlayer(
        scroll_offset_animation_player_.get());
    animation_host_->RemoveAnimationTimeline(scroll_offset_timeline_.get());
  }

  void ScrollAnimationCreate(int layer_id,
                             const gfx::ScrollOffset& target_offset,
                             const gfx::ScrollOffset& current_offset) {
    scoped_ptr<ScrollOffsetAnimationCurve> curve =
        ScrollOffsetAnimationCurve::Create(
            target_offset, EaseInOutTimingFunction::Create(),
            ScrollOffsetAnimationCurve::DurationBehavior::INVERSE_DELTA);
    curve->SetInitialValue(current_offset);

    scoped_ptr<Animation> animation = Animation::Create(
        std::move(curve), AnimationIdProvider::NextAnimationId(),
        AnimationIdProvider::NextGroupId(), TargetProperty::SCROLL_OFFSET);
    animation->set_is_impl_only(true);

    DCHECK(scroll_offset_animation_player_);
    DCHECK(scroll_offset_animation_player_->animation_timeline());

    ReattachScrollOffsetPlayerIfNeeded(layer_id);

    scroll_offset_animation_player_->AddAnimation(std::move(animation));
  }

  bool ScrollAnimationUpdateTarget(int layer_id,
                                   const gfx::Vector2dF& scroll_delta,
                                   const gfx::ScrollOffset& max_scroll_offset,
                                   base::TimeTicks frame_monotonic_time) {
    DCHECK(scroll_offset_animation_player_);
    if (!scroll_offset_animation_player_->element_animations())
      return false;

    DCHECK_EQ(layer_id, scroll_offset_animation_player_->layer_id());

    Animation* animation = scroll_offset_animation_player_->element_animations()
                               ->layer_animation_controller()
                               ->GetAnimation(TargetProperty::SCROLL_OFFSET);
    if (!animation) {
      scroll_offset_animation_player_->DetachLayer();
      return false;
    }

    ScrollOffsetAnimationCurve* curve =
        animation->curve()->ToScrollOffsetAnimationCurve();

    gfx::ScrollOffset new_target =
        gfx::ScrollOffsetWithDelta(curve->target_value(), scroll_delta);
    new_target.SetToMax(gfx::ScrollOffset());
    new_target.SetToMin(max_scroll_offset);

    curve->UpdateTarget(animation->TrimTimeToCurrentIteration(
                                       frame_monotonic_time).InSecondsF(),
                        new_target);

    return true;
  }

  void ScrollAnimationAbort(bool needs_completion) {
    DCHECK(scroll_offset_animation_player_);
    scroll_offset_animation_player_->AbortAnimations(
        TargetProperty::SCROLL_OFFSET, needs_completion);
  }

  // AnimationDelegate implementation.
  void NotifyAnimationStarted(base::TimeTicks monotonic_time,
                              TargetProperty::Type target_property,
                              int group) override {}
  void NotifyAnimationFinished(base::TimeTicks monotonic_time,
                               TargetProperty::Type target_property,
                               int group) override {
    DCHECK_EQ(target_property, TargetProperty::SCROLL_OFFSET);
    DCHECK(animation_host_->mutator_host_client());
    animation_host_->mutator_host_client()->ScrollOffsetAnimationFinished();
  }
  void NotifyAnimationAborted(base::TimeTicks monotonic_time,
                              TargetProperty::Type target_property,
                              int group) override {}
  void NotifyAnimationTakeover(base::TimeTicks monotonic_time,
                               TargetProperty::Type target_property,
                               double animation_start_time,
                               scoped_ptr<AnimationCurve> curve) override {}

 private:
  void ReattachScrollOffsetPlayerIfNeeded(int layer_id) {
    if (scroll_offset_animation_player_->layer_id() != layer_id) {
      if (scroll_offset_animation_player_->layer_id())
        scroll_offset_animation_player_->DetachLayer();
      if (layer_id)
        scroll_offset_animation_player_->AttachLayer(layer_id);
    }
  }

  AnimationHost* animation_host_;
  scoped_refptr<AnimationTimeline> scroll_offset_timeline_;

  // We have just one player for impl-only scroll offset animations.
  // I.e. only one layer can have an impl-only scroll offset animation at
  // any given time.
  scoped_refptr<AnimationPlayer> scroll_offset_animation_player_;

  DISALLOW_COPY_AND_ASSIGN(ScrollOffsetAnimations);
};

scoped_ptr<AnimationHost> AnimationHost::Create(
    ThreadInstance thread_instance) {
  return make_scoped_ptr(new AnimationHost(thread_instance));
}

AnimationHost::AnimationHost(ThreadInstance thread_instance)
    : animation_registrar_(AnimationRegistrar::Create()),
      mutator_host_client_(nullptr),
      thread_instance_(thread_instance) {
  if (thread_instance_ == ThreadInstance::IMPL)
    scroll_offset_animations_ =
        make_scoped_ptr(new ScrollOffsetAnimations(this));
}

AnimationHost::~AnimationHost() {
  scroll_offset_animations_ = nullptr;

  ClearTimelines();
  DCHECK(!mutator_host_client());
  DCHECK(layer_to_element_animations_map_.empty());
}

AnimationTimeline* AnimationHost::GetTimelineById(int timeline_id) const {
  auto f = id_to_timeline_map_.find(timeline_id);
  return f == id_to_timeline_map_.end() ? nullptr : f->second.get();
}

void AnimationHost::ClearTimelines() {
  for (auto& kv : id_to_timeline_map_)
    EraseTimeline(kv.second);
  id_to_timeline_map_.clear();
}

void AnimationHost::EraseTimeline(scoped_refptr<AnimationTimeline> timeline) {
  timeline->ClearPlayers();
  timeline->SetAnimationHost(nullptr);
}

void AnimationHost::AddAnimationTimeline(
    scoped_refptr<AnimationTimeline> timeline) {
  DCHECK(timeline->id());
  timeline->SetAnimationHost(this);
  id_to_timeline_map_.insert(
      std::make_pair(timeline->id(), std::move(timeline)));
}

void AnimationHost::RemoveAnimationTimeline(
    scoped_refptr<AnimationTimeline> timeline) {
  DCHECK(timeline->id());
  EraseTimeline(timeline);
  id_to_timeline_map_.erase(timeline->id());
}

void AnimationHost::RegisterLayer(int layer_id, LayerTreeType tree_type) {
  ElementAnimations* element_animations =
      GetElementAnimationsForLayerId(layer_id);
  if (element_animations)
    element_animations->LayerRegistered(layer_id, tree_type);
}

void AnimationHost::UnregisterLayer(int layer_id, LayerTreeType tree_type) {
  ElementAnimations* element_animations =
      GetElementAnimationsForLayerId(layer_id);
  if (element_animations)
    element_animations->LayerUnregistered(layer_id, tree_type);
}

void AnimationHost::RegisterPlayerForLayer(int layer_id,
                                           AnimationPlayer* player) {
  DCHECK(layer_id);
  DCHECK(player);

  ElementAnimations* element_animations =
      GetElementAnimationsForLayerId(layer_id);
  if (!element_animations) {
    auto new_element_animations = ElementAnimations::Create(this);
    element_animations = new_element_animations.get();

    layer_to_element_animations_map_[layer_id] =
        std::move(new_element_animations);
    element_animations->CreateLayerAnimationController(layer_id);
  }

  DCHECK(element_animations);
  element_animations->AddPlayer(player);
}

void AnimationHost::UnregisterPlayerForLayer(int layer_id,
                                             AnimationPlayer* player) {
  DCHECK(layer_id);
  DCHECK(player);

  ElementAnimations* element_animations =
      GetElementAnimationsForLayerId(layer_id);
  DCHECK(element_animations);
  element_animations->RemovePlayer(player);

  if (element_animations->IsEmpty()) {
    element_animations->DestroyLayerAnimationController();
    layer_to_element_animations_map_.erase(layer_id);
    element_animations = nullptr;
  }
}

void AnimationHost::SetMutatorHostClient(MutatorHostClient* client) {
  if (mutator_host_client_ == client)
    return;

  mutator_host_client_ = client;
}

void AnimationHost::SetNeedsCommit() {
  DCHECK(mutator_host_client_);
  mutator_host_client_->SetMutatorsNeedCommit();
}

void AnimationHost::SetNeedsRebuildPropertyTrees() {
  DCHECK(mutator_host_client_);
  mutator_host_client_->SetMutatorsNeedRebuildPropertyTrees();
}

void AnimationHost::PushPropertiesTo(AnimationHost* host_impl) {
  PushTimelinesToImplThread(host_impl);
  RemoveTimelinesFromImplThread(host_impl);
  PushPropertiesToImplThread(host_impl);
}

void AnimationHost::PushTimelinesToImplThread(AnimationHost* host_impl) const {
  for (auto& kv : id_to_timeline_map_) {
    auto& timeline = kv.second;
    AnimationTimeline* timeline_impl =
        host_impl->GetTimelineById(timeline->id());
    if (timeline_impl)
      continue;

    scoped_refptr<AnimationTimeline> to_add = timeline->CreateImplInstance();
    host_impl->AddAnimationTimeline(to_add.get());
  }
}

void AnimationHost::RemoveTimelinesFromImplThread(
    AnimationHost* host_impl) const {
  IdToTimelineMap& timelines_impl = host_impl->id_to_timeline_map_;

  // Erase all the impl timelines which |this| doesn't have.
  for (auto it = timelines_impl.begin(); it != timelines_impl.end();) {
    auto& timeline_impl = it->second;
    if (timeline_impl->is_impl_only() || GetTimelineById(timeline_impl->id())) {
      ++it;
    } else {
      host_impl->EraseTimeline(it->second);
      it = timelines_impl.erase(it);
    }
  }
}

void AnimationHost::PushPropertiesToImplThread(AnimationHost* host_impl) {
  // Firstly, sync all players with impl thread to create ElementAnimations and
  // layer animation controllers.
  for (auto& kv : id_to_timeline_map_) {
    AnimationTimeline* timeline = kv.second.get();
    AnimationTimeline* timeline_impl =
        host_impl->GetTimelineById(timeline->id());
    if (timeline_impl)
      timeline->PushPropertiesTo(timeline_impl);
  }

  // Secondly, sync properties for created layer animation controllers.
  for (auto& kv : layer_to_element_animations_map_) {
    ElementAnimations* element_animations = kv.second.get();
    ElementAnimations* element_animations_impl =
        host_impl->GetElementAnimationsForLayerId(kv.first);
    if (element_animations_impl)
      element_animations->PushPropertiesTo(element_animations_impl);
  }
}

LayerAnimationController* AnimationHost::GetControllerForLayerId(
    int layer_id) const {
  const ElementAnimations* element_animations =
      GetElementAnimationsForLayerId(layer_id);
  if (!element_animations)
    return nullptr;

  return element_animations->layer_animation_controller();
}

ElementAnimations* AnimationHost::GetElementAnimationsForLayerId(
    int layer_id) const {
  DCHECK(layer_id);
  auto iter = layer_to_element_animations_map_.find(layer_id);
  return iter == layer_to_element_animations_map_.end() ? nullptr
                                                        : iter->second.get();
}

void AnimationHost::SetSupportsScrollAnimations(
    bool supports_scroll_animations) {
  animation_registrar_->set_supports_scroll_animations(
      supports_scroll_animations);
}

bool AnimationHost::SupportsScrollAnimations() const {
  return animation_registrar_->supports_scroll_animations();
}

bool AnimationHost::NeedsAnimateLayers() const {
  return animation_registrar_->needs_animate_layers();
}

bool AnimationHost::ActivateAnimations() {
  return animation_registrar_->ActivateAnimations();
}

bool AnimationHost::AnimateLayers(base::TimeTicks monotonic_time) {
  return animation_registrar_->AnimateLayers(monotonic_time);
}

bool AnimationHost::UpdateAnimationState(bool start_ready_animations,
                                         AnimationEvents* events) {
  return animation_registrar_->UpdateAnimationState(start_ready_animations,
                                                    events);
}

scoped_ptr<AnimationEvents> AnimationHost::CreateEvents() {
  return animation_registrar_->CreateEvents();
}

void AnimationHost::SetAnimationEvents(scoped_ptr<AnimationEvents> events) {
  return animation_registrar_->SetAnimationEvents(std::move(events));
}

bool AnimationHost::ScrollOffsetAnimationWasInterrupted(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->scroll_offset_animation_was_interrupted()
                    : false;
}

static LayerAnimationController::ObserverType ObserverTypeFromTreeType(
    LayerTreeType tree_type) {
  return tree_type == LayerTreeType::ACTIVE
             ? LayerAnimationController::ObserverType::ACTIVE
             : LayerAnimationController::ObserverType::PENDING;
}

bool AnimationHost::IsAnimatingFilterProperty(int layer_id,
                                              LayerTreeType tree_type) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->IsCurrentlyAnimatingProperty(
                   TargetProperty::FILTER, ObserverTypeFromTreeType(tree_type))
             : false;
}

bool AnimationHost::IsAnimatingOpacityProperty(int layer_id,
                                               LayerTreeType tree_type) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->IsCurrentlyAnimatingProperty(
                   TargetProperty::OPACITY, ObserverTypeFromTreeType(tree_type))
             : false;
}

bool AnimationHost::IsAnimatingTransformProperty(
    int layer_id,
    LayerTreeType tree_type) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->IsCurrentlyAnimatingProperty(
                   TargetProperty::TRANSFORM,
                   ObserverTypeFromTreeType(tree_type))
             : false;
}

bool AnimationHost::HasPotentiallyRunningFilterAnimation(
    int layer_id,
    LayerTreeType tree_type) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->IsPotentiallyAnimatingProperty(
                   TargetProperty::FILTER, ObserverTypeFromTreeType(tree_type))
             : false;
}

bool AnimationHost::HasPotentiallyRunningOpacityAnimation(
    int layer_id,
    LayerTreeType tree_type) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->IsPotentiallyAnimatingProperty(
                   TargetProperty::OPACITY, ObserverTypeFromTreeType(tree_type))
             : false;
}

bool AnimationHost::HasPotentiallyRunningTransformAnimation(
    int layer_id,
    LayerTreeType tree_type) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->IsPotentiallyAnimatingProperty(
                   TargetProperty::TRANSFORM,
                   ObserverTypeFromTreeType(tree_type))
             : false;
}

bool AnimationHost::HasAnyAnimationTargetingProperty(
    int layer_id,
    TargetProperty::Type property) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  if (!controller)
    return false;

  return !!controller->GetAnimation(property);
}

bool AnimationHost::FilterIsAnimatingOnImplOnly(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  if (!controller)
    return false;

  Animation* animation = controller->GetAnimation(TargetProperty::FILTER);
  return animation && animation->is_impl_only();
}

bool AnimationHost::OpacityIsAnimatingOnImplOnly(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  if (!controller)
    return false;

  Animation* animation = controller->GetAnimation(TargetProperty::OPACITY);
  return animation && animation->is_impl_only();
}

bool AnimationHost::ScrollOffsetIsAnimatingOnImplOnly(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  if (!controller)
    return false;

  Animation* animation =
      controller->GetAnimation(TargetProperty::SCROLL_OFFSET);
  return animation && animation->is_impl_only();
}

bool AnimationHost::TransformIsAnimatingOnImplOnly(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  if (!controller)
    return false;

  Animation* animation = controller->GetAnimation(TargetProperty::TRANSFORM);
  return animation && animation->is_impl_only();
}

bool AnimationHost::HasFilterAnimationThatInflatesBounds(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->HasFilterAnimationThatInflatesBounds()
                    : false;
}

bool AnimationHost::HasTransformAnimationThatInflatesBounds(
    int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->HasTransformAnimationThatInflatesBounds()
                    : false;
}

bool AnimationHost::HasAnimationThatInflatesBounds(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->HasAnimationThatInflatesBounds() : false;
}

bool AnimationHost::FilterAnimationBoundsForBox(int layer_id,
                                                const gfx::BoxF& box,
                                                gfx::BoxF* bounds) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->FilterAnimationBoundsForBox(box, bounds)
                    : false;
}

bool AnimationHost::TransformAnimationBoundsForBox(int layer_id,
                                                   const gfx::BoxF& box,
                                                   gfx::BoxF* bounds) const {
  *bounds = gfx::BoxF();
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->TransformAnimationBoundsForBox(box, bounds)
                    : true;
}

bool AnimationHost::HasOnlyTranslationTransforms(
    int layer_id,
    LayerTreeType tree_type) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->HasOnlyTranslationTransforms(
                   ObserverTypeFromTreeType(tree_type))
             : true;
}

bool AnimationHost::AnimationsPreserveAxisAlignment(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->AnimationsPreserveAxisAlignment() : true;
}

bool AnimationHost::MaximumTargetScale(int layer_id,
                                       LayerTreeType tree_type,
                                       float* max_scale) const {
  *max_scale = 0.f;
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->MaximumTargetScale(
                   ObserverTypeFromTreeType(tree_type), max_scale)
             : true;
}

bool AnimationHost::AnimationStartScale(int layer_id,
                                        LayerTreeType tree_type,
                                        float* start_scale) const {
  *start_scale = 0.f;
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller
             ? controller->AnimationStartScale(
                   ObserverTypeFromTreeType(tree_type), start_scale)
             : true;
}

bool AnimationHost::HasAnyAnimation(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->has_any_animation() : false;
}

bool AnimationHost::HasActiveAnimationForTesting(int layer_id) const {
  LayerAnimationController* controller = GetControllerForLayerId(layer_id);
  return controller ? controller->HasActiveAnimation() : false;
}

void AnimationHost::ImplOnlyScrollAnimationCreate(
    int layer_id,
    const gfx::ScrollOffset& target_offset,
    const gfx::ScrollOffset& current_offset) {
  DCHECK(scroll_offset_animations_);
  scroll_offset_animations_->ScrollAnimationCreate(layer_id, target_offset,
                                                   current_offset);
}

bool AnimationHost::ImplOnlyScrollAnimationUpdateTarget(
    int layer_id,
    const gfx::Vector2dF& scroll_delta,
    const gfx::ScrollOffset& max_scroll_offset,
    base::TimeTicks frame_monotonic_time) {
  DCHECK(scroll_offset_animations_);
  return scroll_offset_animations_->ScrollAnimationUpdateTarget(
      layer_id, scroll_delta, max_scroll_offset, frame_monotonic_time);
}

void AnimationHost::ScrollAnimationAbort(bool needs_completion) {
  DCHECK(scroll_offset_animations_);
  return scroll_offset_animations_->ScrollAnimationAbort(needs_completion);
}

}  // namespace cc
