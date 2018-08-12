// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_BLIMP_LAYER_TREE_HOST_REMOTE_H_
#define CC_BLIMP_LAYER_TREE_HOST_REMOTE_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "cc/base/cc_export.h"
#include "cc/blimp/remote_compositor_bridge_client.h"
#include "cc/debug/layer_tree_debug_state.h"
#include "cc/surfaces/surface_sequence_generator.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/layer_tree_settings.h"
#include "cc/trees/swap_promise_manager.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace cc {
namespace proto {
class LayerTreeHost;
}  // namespace proto

class AnimationHost;
class RemoteCompositorBridge;
class LayerTreeHostClient;

class CC_EXPORT LayerTreeHostRemote : public LayerTreeHost,
                                      public RemoteCompositorBridgeClient {
 public:
  struct CC_EXPORT InitParams {
    LayerTreeHostClient* client = nullptr;
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner;
    std::unique_ptr<AnimationHost> animation_host;
    std::unique_ptr<RemoteCompositorBridge> remote_compositor_bridge;
    LayerTreeSettings const* settings = nullptr;

    InitParams();
    ~InitParams();
  };

  explicit LayerTreeHostRemote(InitParams* params);
  ~LayerTreeHostRemote() override;

  // LayerTreeHost implementation.
  int GetId() const override;
  int SourceFrameNumber() const override;
  LayerTree* GetLayerTree() override;
  const LayerTree* GetLayerTree() const override;
  UIResourceManager* GetUIResourceManager() const override;
  TaskRunnerProvider* GetTaskRunnerProvider() const override;
  const LayerTreeSettings& GetSettings() const override;
  void SetFrameSinkId(const FrameSinkId& frame_sink_id) override;
  void SetLayerTreeMutator(std::unique_ptr<LayerTreeMutator> mutator) override;
  void QueueSwapPromise(std::unique_ptr<SwapPromise> swap_promise) override;
  SwapPromiseManager* GetSwapPromiseManager() override;
  void SetHasGpuRasterizationTrigger(bool has_trigger) override;
  void SetVisible(bool visible) override;
  bool IsVisible() const override;
  void SetCompositorFrameSink(
      std::unique_ptr<CompositorFrameSink> compositor_frame_sink) override;
  std::unique_ptr<CompositorFrameSink> ReleaseCompositorFrameSink() override;
  void SetNeedsAnimate() override;
  void SetNeedsUpdateLayers() override;
  void SetNeedsCommit() override;
  void SetNeedsRecalculateRasterScales() override;
  bool BeginMainFrameRequested() const override;
  bool CommitRequested() const override;
  void SetDeferCommits(bool defer_commits) override;
  void LayoutAndUpdateLayers() override;
  void Composite(base::TimeTicks frame_begin_time) override;
  void SetNeedsRedraw() override;
  void SetNeedsRedrawRect(const gfx::Rect& damage_rect) override;
  void SetNextCommitForcesRedraw() override;
  void NotifyInputThrottledUntilCommit() override;
  void UpdateTopControlsState(TopControlsState constraints,
                              TopControlsState current,
                              bool animate) override;
  const base::WeakPtr<InputHandler>& GetInputHandler() const override;
  void DidStopFlinging() override;
  void SetDebugState(const LayerTreeDebugState& debug_state) override;
  const LayerTreeDebugState& GetDebugState() const override;
  int ScheduleMicroBenchmark(
      const std::string& benchmark_name,
      std::unique_ptr<base::Value> value,
      const MicroBenchmark::DoneCallback& callback) override;
  bool SendMessageToMicroBenchmark(int id,
                                   std::unique_ptr<base::Value> value) override;
  SurfaceSequenceGenerator* GetSurfaceSequenceGenerator() override;
  void SetNextCommitWaitsForActivation() override;
  void ResetGpuRasterizationTracking() override;

 protected:
  // Protected for testing. Allows tests to inject the LayerTree.
  LayerTreeHostRemote(InitParams* params,
                      std::unique_ptr<LayerTree> layer_tree);

 private:
  enum class FramePipelineStage { NONE, ANIMATE, UPDATE_LAYERS, COMMIT };

  // RemoteCompositorBridgeClient implementation.
  void BeginMainFrame() override;

  void MainFrameRequested(FramePipelineStage requested_pipeline_stage);
  void ScheduleMainFrameIfNecessary();
  void MainFrameComplete();
  void DispatchDrawAndSwapCallbacks();
  void SerializeCurrentState(proto::LayerTreeHost* layer_tree_host_proto);

  const int id_;
  int source_frame_number_ = 0;
  bool visible_ = false;
  bool defer_commits_ = false;

  // Set to true if a main frame request is pending on the
  // RemoteCompositorBridge.
  bool main_frame_requested_from_bridge_ = false;

  // Set to the pipeline stage we are currently at if we are inside a main frame
  // update.
  FramePipelineStage current_pipeline_stage_ = FramePipelineStage::NONE;

  // Set to the pipeline stage we need to go to for the current main frame
  // update, if we are inside a main frame update.
  FramePipelineStage max_pipeline_stage_for_current_frame_ =
      FramePipelineStage::NONE;

  // Set to the pipeline stage requested for the next BeginMainFrame.
  FramePipelineStage requested_pipeline_stage_for_next_frame_ =
      FramePipelineStage::NONE;

  LayerTreeHostClient* client_;
  std::unique_ptr<TaskRunnerProvider> task_runner_provider_;

  // The RemoteCompositorBridge used to submit frame updates to the client.
  std::unique_ptr<RemoteCompositorBridge> remote_compositor_bridge_;

  LayerTreeSettings settings_;
  LayerTreeDebugState debug_state_;

  // The LayerTree holds the root layer and other state on the engine.
  std::unique_ptr<LayerTree> layer_tree_;

  SwapPromiseManager swap_promise_manager_;
  SurfaceSequenceGenerator surface_sequence_generator_;

  base::WeakPtr<InputHandler> input_handler_weak_ptr_;

  base::WeakPtrFactory<LayerTreeHostRemote> weak_factory_;
};

}  // namespace cc

#endif  // CC_BLIMP_LAYER_TREE_HOST_REMOTE_H_
