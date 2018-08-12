// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "core/fetch/LinkFetchResource.h"

#include "core/fetch/FetchRequest.h"
#include "core/fetch/ResourceFetcher.h"

namespace blink {

PassRefPtrWillBeRawPtr<Resource> LinkFetchResource::fetch(Resource::Type type, FetchRequest& request, ResourceFetcher* fetcher)
{
    ASSERT(type == LinkPrefetch);
    ASSERT(request.resourceRequest().frameType() == WebURLRequest::FrameTypeNone);
    fetcher->determineRequestContext(request.mutableResourceRequest(), type);
    return fetcher->requestResource(request, LinkResourceFactory(type));
}

LinkFetchResource::LinkFetchResource(const ResourceRequest& request, Type type)
    : Resource(request, type)
{
}

LinkFetchResource::~LinkFetchResource()
{
}

} // namespace blink
