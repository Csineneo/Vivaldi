// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/content_suggestions/cells/content_suggestions_article_item.h"

#include "testing/gtest/include/gtest/gtest.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#import "third_party/ocmock/gtest_support.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

// Tests that configureCell: set all the fields of the cell except the image and
// fetches the image through the delegate.
TEST(ContentSuggestionsArticleItemTest, CellIsConfiguredWithoutImage) {
  // Setup.
  NSString* title = @"testTitle";
  NSString* subtitle = @"testSubtitle";
  GURL url = GURL("http://chromium.org");
  id delegateMock =
      OCMProtocolMock(@protocol(ContentSuggestionsArticleItemDelegate));
  ContentSuggestionsArticleItem* item =
      [[ContentSuggestionsArticleItem alloc] initWithType:0
                                                    title:title
                                                 subtitle:subtitle
                                                 delegate:delegateMock
                                                      url:url];
  OCMExpect([delegateMock loadImageForArticleItem:item]);
  ContentSuggestionsArticleCell* cell = [[[item cellClass] alloc] init];
  ASSERT_EQ([ContentSuggestionsArticleCell class], [cell class]);
  ASSERT_EQ(url, item.articleURL);
  ASSERT_EQ(nil, item.image);
  id cellMock = OCMPartialMock(cell);
  OCMExpect([cellMock setContentImage:item.image]);

  // Action.
  [item configureCell:cell];

  // Tests.
  EXPECT_OCMOCK_VERIFY(cellMock);
  EXPECT_EQ(title, cell.titleLabel.text);
  EXPECT_EQ(subtitle, cell.subtitleLabel.text);
  EXPECT_OCMOCK_VERIFY(delegateMock);
}

// Tests that configureCell: does not call the delegate if it fetched the image
// once.
TEST(ContentSuggestionsArticleItemTest, DontFetchImageIsImageIsBeingFetched) {
  // Setup.
  NSString* title = @"testTitle";
  NSString* subtitle = @"testSubtitle";
  GURL url = GURL("http://chromium.org");
  id niceDelegateMock =
      OCMProtocolMock(@protocol(ContentSuggestionsArticleItemDelegate));
  ContentSuggestionsArticleItem* item =
      [[ContentSuggestionsArticleItem alloc] initWithType:0
                                                    title:title
                                                 subtitle:subtitle
                                                 delegate:niceDelegateMock
                                                      url:url];
  item.image = [[UIImage alloc] init];

  OCMExpect([niceDelegateMock loadImageForArticleItem:item]);
  ContentSuggestionsArticleCell* cell = [[[item cellClass] alloc] init];
  ASSERT_NE(nil, item.image);
  [item configureCell:cell];
  ASSERT_OCMOCK_VERIFY(niceDelegateMock);

  id strictDelegateMock =
      OCMStrictProtocolMock(@protocol(ContentSuggestionsArticleItemDelegate));
  item.delegate = strictDelegateMock;
  id cellMock = OCMPartialMock(cell);
  OCMExpect([cellMock setContentImage:item.image]);

  // Action.
  [item configureCell:cell];

  // Tests.
  EXPECT_OCMOCK_VERIFY(cellMock);
  EXPECT_EQ(title, cell.titleLabel.text);
  EXPECT_EQ(subtitle, cell.subtitleLabel.text);
}

}  // namespace
