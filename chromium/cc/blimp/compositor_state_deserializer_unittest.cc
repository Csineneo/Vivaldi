// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compositor_state_deserializer.h"

#include "base/run_loop.h"
#include "cc/animation/animation_host.h"
#include "cc/blimp/compositor_proto_state.h"
#include "cc/blimp/compositor_state_deserializer_client.h"
#include "cc/blimp/layer_tree_host_remote.h"
#include "cc/proto/compositor_message.pb.h"
#include "cc/test/fake_layer_tree_host.h"
#include "cc/test/fake_layer_tree_host_client.h"
#include "cc/test/fake_remote_compositor_bridge.h"
#include "cc/test/remote_client_layer_factory.h"
#include "cc/test/stub_layer_tree_host_client.h"
#include "cc/test/test_task_graph_runner.h"
#include "cc/trees/layer_tree_host_common.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cc {
namespace {

#define EXPECT_LAYERS_EQ(engine_layer_id, client_layer)                     \
  EXPECT_EQ(                                                                \
      compositor_state_deserializer_->GetLayerForEngineId(engine_layer_id), \
      client_layer);

class RemoteCompositorBridgeForTest : public FakeRemoteCompositorBridge {
 public:
  using ProtoFrameCallback = base::Callback<void(
      std::unique_ptr<CompositorProtoState> compositor_proto_state)>;

  RemoteCompositorBridgeForTest(
      scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
      ProtoFrameCallback proto_frame_callback)
      : FakeRemoteCompositorBridge(main_task_runner),
        proto_frame_callback_(proto_frame_callback) {}

  ~RemoteCompositorBridgeForTest() override = default;

  void ProcessCompositorStateUpdate(
      std::unique_ptr<CompositorProtoState> compositor_proto_state) override {
    proto_frame_callback_.Run(std::move(compositor_proto_state));
  }

 private:
  ProtoFrameCallback proto_frame_callback_;
};

class CompositorStateDeserializerTest
    : public testing::Test,
      public CompositorStateDeserializerClient {
 public:
  void SetUp() override {
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner =
        base::ThreadTaskRunnerHandle::Get();

    // Engine side setup.
    LayerTreeHostRemote::InitParams params;
    params.client = &layer_tree_host_client_remote_;
    params.main_task_runner = main_task_runner;
    params.animation_host = AnimationHost::CreateMainInstance();
    params.remote_compositor_bridge =
        base::MakeUnique<RemoteCompositorBridgeForTest>(
            main_task_runner,
            base::Bind(
                &CompositorStateDeserializerTest::ProcessCompositorStateUpdate,
                base::Unretained(this)));
    LayerTreeSettings settings;
    params.settings = &settings;

    layer_tree_host_remote_ = base::MakeUnique<LayerTreeHostRemote>(&params);

    // Client side setup.
    layer_tree_host_in_process_ = FakeLayerTreeHost::Create(
        &layer_tree_host_client_client_, &task_graph_runner_);
    compositor_state_deserializer_ =
        base::MakeUnique<CompositorStateDeserializer>(
            layer_tree_host_in_process_.get(),
            base::Bind(&CompositorStateDeserializerTest::LayerScrolled,
                       base::Unretained(this)),
            this);
  }

  void TearDown() override {
    layer_tree_host_remote_ = nullptr;
    compositor_state_deserializer_ = nullptr;
    layer_tree_host_in_process_ = nullptr;
  }

  void ProcessCompositorStateUpdate(
      std::unique_ptr<CompositorProtoState> compositor_proto_state) {
    // Immediately deserialize the state update.
    compositor_state_deserializer_->DeserializeCompositorUpdate(
        compositor_proto_state->compositor_message->layer_tree_host());
  }

  // CompositorStateDeserializer implementation.
  bool ShouldRetainClientScroll(int engine_layer_id,
                                const gfx::ScrollOffset& new_offset) override {
    return should_retain_client_scroll_;
  }
  bool ShouldRetainClientPageScale(float new_page_scale) override {
    return should_retain_client_scale_;
  }

  void LayerScrolled(int engine_layer_id) {}

  void VerifyTreesAreIdentical() {
    LayerTree* engine_layer_tree = layer_tree_host_remote_->GetLayerTree();
    LayerTree* client_layer_tree = layer_tree_host_in_process_->GetLayerTree();

    if (engine_layer_tree->root_layer()) {
      LayerTreeHostCommon::CallFunctionForEveryLayer(
          engine_layer_tree, [this](Layer* engine_layer) {
            VerifyLayersAreIdentical(
                engine_layer,
                compositor_state_deserializer_->GetLayerForEngineId(
                    engine_layer->id()));
          });
    } else {
      EXPECT_EQ(layer_tree_host_in_process_->GetLayerTree()->root_layer(),
                nullptr);
    }

    // Viewport layers.
    // Overscroll Elasticty Layer.
    Layer* client_overscroll_elasticity_layer =
        client_layer_tree->overscroll_elasticity_layer();
    if (engine_layer_tree->overscroll_elasticity_layer()) {
      int engine_overscroll_elasticity_layer_id =
          engine_layer_tree->overscroll_elasticity_layer()->id();

      EXPECT_LAYERS_EQ(engine_overscroll_elasticity_layer_id,
                       client_overscroll_elasticity_layer);
    } else {
      EXPECT_EQ(client_overscroll_elasticity_layer, nullptr);
    }

    // PageScale Layer.
    Layer* client_page_scale_layer = client_layer_tree->page_scale_layer();
    if (engine_layer_tree->page_scale_layer()) {
      int engine_page_scale_layer_id =
          engine_layer_tree->page_scale_layer()->id();

      EXPECT_LAYERS_EQ(engine_page_scale_layer_id, client_page_scale_layer);
    } else {
      EXPECT_EQ(client_page_scale_layer, nullptr);
    }

    // InnerViewportScroll Layer.
    Layer* client_inner_viewport_layer =
        client_layer_tree->inner_viewport_scroll_layer();
    if (engine_layer_tree->inner_viewport_scroll_layer()) {
      int engine_inner_viewport_layer_id =
          engine_layer_tree->inner_viewport_scroll_layer()->id();

      EXPECT_LAYERS_EQ(engine_inner_viewport_layer_id,
                       client_inner_viewport_layer);
    } else {
      EXPECT_EQ(client_inner_viewport_layer, nullptr);
    }

    // OuterViewportScroll Layer.
    Layer* client_outer_viewport_layer =
        client_layer_tree->outer_viewport_scroll_layer();
    if (engine_layer_tree->outer_viewport_scroll_layer()) {
      int engine_outer_viewport_layer_id =
          engine_layer_tree->outer_viewport_scroll_layer()->id();

      EXPECT_LAYERS_EQ(engine_outer_viewport_layer_id,
                       client_outer_viewport_layer);
    } else {
      EXPECT_EQ(client_outer_viewport_layer, nullptr);
    }
  }

  void VerifyLayersAreIdentical(Layer* engine_layer, Layer* client_layer) {
    ASSERT_NE(client_layer, nullptr);

    LayerTree* client_layer_tree = layer_tree_host_in_process_->GetLayerTree();
    EXPECT_EQ(client_layer_tree, client_layer->GetLayerTree());

    // Parent.
    if (engine_layer->parent()) {
      int engine_parent_id = engine_layer->parent()->id();
      EXPECT_LAYERS_EQ(engine_parent_id, client_layer->parent());
    } else {
      EXPECT_EQ(client_layer->parent(), nullptr);
    }

    // Mask Layers.
    if (engine_layer->mask_layer()) {
      int engine_mask_layer_id = engine_layer->mask_layer()->id();
      EXPECT_LAYERS_EQ(engine_mask_layer_id, client_layer->mask_layer());
    } else {
      EXPECT_EQ(client_layer->mask_layer(), nullptr);
    }

    // Scroll parent.
    if (engine_layer->scroll_parent()) {
      int engine_scroll_parent_id = engine_layer->scroll_parent()->id();
      EXPECT_LAYERS_EQ(engine_scroll_parent_id, client_layer->scroll_parent());
    } else {
      EXPECT_EQ(client_layer->scroll_parent(), nullptr);
    }

    // Clip parent.
    if (engine_layer->clip_parent()) {
      int engine_clip_parent_id = engine_layer->clip_parent()->id();
      EXPECT_LAYERS_EQ(engine_clip_parent_id, client_layer->clip_parent());
    } else {
      EXPECT_EQ(client_layer->clip_parent(), nullptr);
    }

    // Scroll-clip layer.
    if (engine_layer->scroll_clip_layer()) {
      int scroll_clip_id = engine_layer->scroll_clip_layer()->id();
      EXPECT_LAYERS_EQ(scroll_clip_id, client_layer->scroll_clip_layer());
    } else {
      EXPECT_EQ(client_layer->scroll_clip_layer(), nullptr);
    }
  }

  // Engine setup.
  std::unique_ptr<LayerTreeHostRemote> layer_tree_host_remote_;
  StubLayerTreeHostClient layer_tree_host_client_remote_;

  // Client setup.
  std::unique_ptr<FakeLayerTreeHost> layer_tree_host_in_process_;
  std::unique_ptr<CompositorStateDeserializer> compositor_state_deserializer_;
  FakeLayerTreeHostClient layer_tree_host_client_client_;
  TestTaskGraphRunner task_graph_runner_;

  bool should_retain_client_scroll_ = false;
  bool should_retain_client_scale_ = false;
};

TEST_F(CompositorStateDeserializerTest, BasicSync) {
  // Set up a tree with a single node.
  scoped_refptr<Layer> root_layer = Layer::Create();
  layer_tree_host_remote_->GetLayerTree()->SetRootLayer(root_layer);

  // Synchronize State and verify.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();

  // Swap the root layer.
  scoped_refptr<Layer> new_root_layer = Layer::Create();
  new_root_layer->AddChild(Layer::Create());
  new_root_layer->AddChild(Layer::Create());
  layer_tree_host_remote_->GetLayerTree()->SetRootLayer(new_root_layer);

  // Synchronize State and verify.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();
  // Verify that we are no longer tracking the destroyed layer on the client.
  EXPECT_EQ(
      compositor_state_deserializer_->GetLayerForEngineId(root_layer->id()),
      nullptr);

  // Remove the root layer to change to a null tree.
  layer_tree_host_remote_->GetLayerTree()->SetRootLayer(nullptr);

  // Synchronize State and verify.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();
}

TEST_F(CompositorStateDeserializerTest, ViewportLayers) {
  scoped_refptr<Layer> root_layer = Layer::Create();
  layer_tree_host_remote_->GetLayerTree()->SetRootLayer(root_layer);

  scoped_refptr<Layer> overscroll_elasticity_layer = Layer::Create();
  scoped_refptr<Layer> inner_viewport_scroll_layer = Layer::Create();
  scoped_refptr<Layer> outer_viewport_scroll_layer = Layer::Create();
  scoped_refptr<Layer> page_scale_layer = Layer::Create();
  layer_tree_host_remote_->GetLayerTree()->RegisterViewportLayers(
      overscroll_elasticity_layer, page_scale_layer,
      inner_viewport_scroll_layer, outer_viewport_scroll_layer);

  root_layer->AddChild(overscroll_elasticity_layer);
  overscroll_elasticity_layer->AddChild(page_scale_layer);
  page_scale_layer->AddChild(inner_viewport_scroll_layer);
  inner_viewport_scroll_layer->AddChild(outer_viewport_scroll_layer);

  // Synchronize State and verify.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();
}

TEST_F(CompositorStateDeserializerTest, ScrollClipAndMaskLayers) {
  /*   root -- A---C---D
  /      |      \
  /      |       E(MaskLayer)
  /      ------B */
  scoped_refptr<Layer> root_layer = Layer::Create();
  layer_tree_host_remote_->GetLayerTree()->SetRootLayer(root_layer);

  scoped_refptr<Layer> layer_a = Layer::Create();
  scoped_refptr<Layer> layer_b = Layer::Create();
  scoped_refptr<Layer> layer_c = Layer::Create();
  scoped_refptr<Layer> layer_d = Layer::Create();
  scoped_refptr<Layer> layer_e = Layer::Create();

  root_layer->AddChild(layer_a);
  root_layer->AddChild(layer_b);
  layer_a->AddChild(layer_c);
  layer_c->AddChild(layer_d);

  layer_a->SetMaskLayer(layer_e.get());
  layer_c->SetScrollParent(layer_b.get());
  layer_c->SetScrollClipLayerId(root_layer->id());
  layer_d->SetClipParent(layer_a.get());

  // Synchronize State and verify.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();
}

TEST_F(CompositorStateDeserializerTest, ReconcileScrollAndScale) {
  scoped_refptr<Layer> root_layer = Layer::Create();
  layer_tree_host_remote_->GetLayerTree()->SetRootLayer(root_layer);

  // Set scroll offset.
  scoped_refptr<Layer> scroll_layer = Layer::Create();
  root_layer->AddChild(scroll_layer);
  gfx::ScrollOffset engine_offset(4, 3);
  scroll_layer->SetScrollOffset(engine_offset);

  // Set page scale.
  float engine_page_scale = 0.5f;
  layer_tree_host_remote_->GetLayerTree()->SetPageScaleFactorAndLimits(
      engine_page_scale, 1.0, 1.0);

  // Synchronize State and verify that the engine values are used.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();

  EXPECT_EQ(engine_page_scale,
            layer_tree_host_in_process_->GetLayerTree()->page_scale_factor());
  EXPECT_EQ(engine_offset, compositor_state_deserializer_
                               ->GetLayerForEngineId(scroll_layer->id())
                               ->scroll_offset());

  // Now reset the scroll offset and page scale and force-retain the client
  // values.
  gfx::ScrollOffset new_engine_offset(2, 2);
  scroll_layer->SetScrollOffset(new_engine_offset);
  float new_engine_page_scale = 0.8f;
  layer_tree_host_remote_->GetLayerTree()->SetPageScaleFactorAndLimits(
      new_engine_page_scale, 1.0, 1.0);
  should_retain_client_scroll_ = true;
  should_retain_client_scale_ = true;

  // Synchronize State and verify that the client values are retained.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();

  EXPECT_EQ(engine_page_scale,
            layer_tree_host_in_process_->GetLayerTree()->page_scale_factor());
  EXPECT_EQ(engine_offset, compositor_state_deserializer_
                               ->GetLayerForEngineId(scroll_layer->id())
                               ->scroll_offset());
}

TEST_F(CompositorStateDeserializerTest, PropertyTreesAreIdentical) {
  // Override the LayerFactory. This is necessary to ensure the layer ids
  // tracked in PropertyTrees on the engine and client are identical.
  compositor_state_deserializer_->SetLayerFactoryForTesting(
      base::MakeUnique<RemoteClientLayerFactory>());

  scoped_refptr<Layer> root_layer = Layer::Create();
  root_layer->SetBounds(gfx::Size(10, 10));
  layer_tree_host_remote_->GetLayerTree()->SetRootLayer(root_layer);

  scoped_refptr<Layer> child1 = Layer::Create();
  root_layer->AddChild(child1);
  gfx::Transform transform;
  transform.Translate(gfx::Vector2dF(5, 4));
  child1->SetTransform(transform);
  child1->SetMasksToBounds(true);

  scoped_refptr<Layer> child2 = Layer::Create();
  root_layer->AddChild(child2);
  child2->SetBounds(gfx::Size(5, 5));
  child2->SetScrollOffset(gfx::ScrollOffset(3, 4));
  child2->SetScrollParent(child1.get());
  child2->SetUserScrollable(true, true);

  scoped_refptr<Layer> grandchild11 = Layer::Create();
  child1->AddChild(grandchild11);
  grandchild11->SetClipParent(root_layer.get());

  scoped_refptr<Layer> grandchild21 = Layer::Create();
  child2->AddChild(grandchild21);
  grandchild21->SetScrollClipLayerId(child1->id());
  grandchild21->SetOpacity(0.5);

  // Synchronize State and verify.
  base::RunLoop().RunUntilIdle();
  VerifyTreesAreIdentical();
  EXPECT_EQ(root_layer->id(), layer_tree_host_in_process_->root_layer()->id());

  // Sanity test to ensure that the PropertyTrees generated from the Layers on
  // the client and engine are identical.
  layer_tree_host_remote_->GetLayerTree()->BuildPropertyTreesForTesting();
  PropertyTrees* engine_property_trees =
      layer_tree_host_remote_->GetLayerTree()->property_trees();

  layer_tree_host_in_process_->BuildPropertyTreesForTesting();
  PropertyTrees* client_property_trees =
      layer_tree_host_in_process_->property_trees();

  EXPECT_EQ(*engine_property_trees, *client_property_trees);
}

}  // namespace
}  // namespace cc
