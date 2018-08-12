// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shell.h"
#include "components/exo/buffer.h"
#include "components/exo/pointer.h"
#include "components/exo/pointer_delegate.h"
#include "components/exo/shell_surface.h"
#include "components/exo/surface.h"
#include "components/exo/test/exo_test_base.h"
#include "components/exo/test/exo_test_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "ui/events/test/event_generator.h"

namespace exo {
namespace {

using PointerTest = test::ExoTestBase;

class MockPointerDelegate : public PointerDelegate {
 public:
  MockPointerDelegate() {}

  // Overridden from PointerDelegate:
  MOCK_METHOD1(OnPointerDestroying, void(Pointer*));
  MOCK_CONST_METHOD1(CanAcceptPointerEventsForSurface, bool(Surface*));
  MOCK_METHOD3(OnPointerEnter, void(Surface*, const gfx::Point&, int));
  MOCK_METHOD1(OnPointerLeave, void(Surface*));
  MOCK_METHOD2(OnPointerMotion, void(base::TimeDelta, const gfx::Point&));
  MOCK_METHOD3(OnPointerButton, void(base::TimeDelta, int, bool));
  MOCK_METHOD2(OnPointerWheel, void(base::TimeDelta, const gfx::Vector2d&));
};

TEST_F(PointerTest, OnPointerEnter) {
  scoped_ptr<Surface> surface(new Surface);
  scoped_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  shell_surface->Init();
  gfx::Size buffer_size(10, 10);
  scoped_ptr<Buffer> buffer(new Buffer(
      exo_test_helper()->CreateGpuMemoryBuffer(buffer_size), GL_TEXTURE_2D));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  scoped_ptr<Pointer> pointer(new Pointer(&delegate));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::Point(), 0));
  generator.MoveMouseTo(surface->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerLeave) {
  scoped_ptr<Surface> surface(new Surface);
  scoped_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  shell_surface->Init();
  gfx::Size buffer_size(10, 10);
  scoped_ptr<Buffer> buffer(new Buffer(
      exo_test_helper()->CreateGpuMemoryBuffer(buffer_size), GL_TEXTURE_2D));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  scoped_ptr<Pointer> pointer(new Pointer(&delegate));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::Point(), 0));
  generator.MoveMouseTo(surface->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerLeave(surface.get()));
  generator.MoveMouseTo(surface->GetBoundsInScreen().bottom_right());

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerMotion) {
  scoped_ptr<Surface> surface(new Surface);
  scoped_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  shell_surface->Init();
  gfx::Size buffer_size(10, 10);
  scoped_ptr<Buffer> buffer(new Buffer(
      exo_test_helper()->CreateGpuMemoryBuffer(buffer_size), GL_TEXTURE_2D));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  scoped_ptr<Pointer> pointer(new Pointer(&delegate));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::Point(), 0));
  generator.MoveMouseTo(surface->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerMotion(testing::_, gfx::Point(1, 1)));
  generator.MoveMouseTo(surface->GetBoundsInScreen().origin() +
                        gfx::Vector2d(1, 1));

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerButton) {
  scoped_ptr<Surface> surface(new Surface);
  scoped_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  shell_surface->Init();
  gfx::Size buffer_size(10, 10);
  scoped_ptr<Buffer> buffer(new Buffer(
      exo_test_helper()->CreateGpuMemoryBuffer(buffer_size), GL_TEXTURE_2D));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  scoped_ptr<Pointer> pointer(new Pointer(&delegate));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::Point(), 0));
  generator.MoveMouseTo(surface->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate,
              OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, true));
  EXPECT_CALL(delegate,
              OnPointerButton(testing::_, ui::EF_LEFT_MOUSE_BUTTON, false));
  generator.ClickLeftButton();

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

TEST_F(PointerTest, OnPointerWheel) {
  scoped_ptr<Surface> surface(new Surface);
  scoped_ptr<ShellSurface> shell_surface(new ShellSurface(surface.get()));
  shell_surface->Init();
  gfx::Size buffer_size(10, 10);
  scoped_ptr<Buffer> buffer(new Buffer(
      exo_test_helper()->CreateGpuMemoryBuffer(buffer_size), GL_TEXTURE_2D));
  surface->Attach(buffer.get());
  surface->Commit();

  MockPointerDelegate delegate;
  scoped_ptr<Pointer> pointer(new Pointer(&delegate));
  ui::test::EventGenerator generator(ash::Shell::GetPrimaryRootWindow());

  EXPECT_CALL(delegate, CanAcceptPointerEventsForSurface(surface.get()))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(delegate, OnPointerEnter(surface.get(), gfx::Point(), 0));
  generator.MoveMouseTo(surface->GetBoundsInScreen().origin());

  EXPECT_CALL(delegate, OnPointerWheel(testing::_, gfx::Vector2d(1, 1)));
  generator.MoveMouseWheel(1, 1);

  EXPECT_CALL(delegate, OnPointerDestroying(pointer.get()));
  pointer.reset();
}

}  // namespace
}  // namespace exo
