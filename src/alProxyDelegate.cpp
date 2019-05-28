//
// Copyright 2019 Luma Pictures
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http:#www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "alProxyDelegate.h"

#include "debugCodes.h"

#include <hdmaya/delegates/delegateRegistry.h>

#include <pxr/base/tf/envSetting.h>
#include <pxr/base/tf/type.h>

#include <maya/MDGMessage.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MNodeClass.h>
#include <maya/MObject.h>
#include <maya/MTime.h>

#include <atomic>

#ifdef HD_MAYA_AL_OVERRIDE_PROXY_SELECTION
#include <AL/usdmaya/nodes/Engine.h>

#include <pxr/imaging/hdx/intersector.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/usdImaging/usdImagingGL/engine.h>

#include <maya/M3dView.h>
#endif // HD_MAYA_AL_OVERRIDE_PROXY_SELECTION

#if HDMAYA_UFE_BUILD
#include <ufe/rtid.h>
#include <ufe/runTimeMgr.h>
#endif // HDMAYA_UFE_BUILD

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(_tokens, (HdMayaALProxyDelegate));

TF_REGISTRY_FUNCTION(TfType) {
    TF_DEBUG(HDMAYA_AL_PLUGIN)
        .Msg("Calling TfType::Define for HdMayaALProxyDelegate\n");
    TfType::Define<HdMayaALProxyDelegate, TfType::Bases<HdMayaDelegate> >();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdMayaDelegateRegistry, HdMayaALProxyDelegate) {
    TF_DEBUG(HDMAYA_AL_PLUGIN)
        .Msg("Calling RegisterDelegate for HdMayaALProxyDelegate\n");
    HdMayaDelegateRegistry::RegisterDelegate(
        _tokens->HdMayaALProxyDelegate, HdMayaALProxyDelegate::Creator);
}

namespace {

#if HDMAYA_UFE_BUILD
constexpr auto USD_UFE_RUNTIME_NAME = "USD";
static UFE_NS::Rtid usdUfeRtid = 0;
#endif // HDMAYA_UFE_BUILD

// Don't know if this variable would be accessed from multiple threads, but
// plugin load/unload is infrequent enough that performance isn't an issue, and
// I prefer to default to thread-safety for global variables.
std::atomic<bool> isALPluginLoaded(false);

void StageLoadedCallback(void* userData, AL::event::NodeEvents* node) {
    // Bunch of conversions + sanity checks...
    auto* delegate = static_cast<HdMayaALProxyDelegate*>(userData);
    if (!TF_VERIFY(
            delegate, "StageLoadedCallback called with null userData ptr")) {
        return;
    }
    auto* proxy = dynamic_cast<ProxyShape*>(node);
    if (!TF_VERIFY(
            proxy,
            "StageLoadedCallback called with null or non-ProxyShape* ptr")) {
        return;
    }

    // Real work done by delegate->createUsdImagingDelegate
    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg(
            "HdMayaALProxyDelegate - called StageLoadedCallback (ProxyShape: "
            "%s)\n",
            proxy->name().asChar());
    delegate->CreateUsdImagingDelegate(proxy);
}

void ProxyShapeDestroyedCallback(void* userData, AL::event::NodeEvents* node) {
    // Bunch of conversions + sanity checks...
    auto* delegate = static_cast<HdMayaALProxyDelegate*>(userData);
    if (!TF_VERIFY(
            delegate,
            "ProxyShapeDestroyedCallback called with null userData ptr")) {
        return;
    }
    auto* proxy = dynamic_cast<ProxyShape*>(node);
    if (!TF_VERIFY(
            proxy,
            "ProxyShapeDestroyedCallback called with null or non-ProxyShape* "
            "ptr")) {
        return;
    }

    // Real work done by delegate->removeProxy
    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg(
            "HdMayaALProxyDelegate - called ProxyShapeDestroyedCallback "
            "(ProxyShape: %s)\n",
            proxy->name().asChar());
    delegate->RemoveProxy(proxy);
}

void ProxyShapeAddedCallback(MObject& node, void* clientData) {
    // Bunch of conversions + sanity checks...
    auto delegate = static_cast<HdMayaALProxyDelegate*>(clientData);
    if (!TF_VERIFY(
            delegate,
            "ProxyShapeAddedCallback called with null HdMayaALProxyDelegate "
            "ptr")) {
        return;
    }

    MStatus status;
    MFnDependencyNode mfnNode(node, &status);
    if (!TF_VERIFY(status, "Error getting MFnDependencyNode")) { return; }

    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg(
            "HdMayaALProxyDelegate - called ProxyShapeAddedCallback "
            "(ProxyShape: %s)\n",
            mfnNode.name().asChar());

    if (!TF_VERIFY(
            mfnNode.typeId() == ProxyShape::kTypeId,
            "ProxyShapeAddedCallback called on non-%s node",
            ProxyShape::kTypeName.asChar())) {
        return;
    }

    auto* proxy = dynamic_cast<ProxyShape*>(mfnNode.userNode());
    if (!TF_VERIFY(
            proxy, "Error getting ProxyShape* for %s",
            mfnNode.name().asChar())) {
        return;
    }

    // Real work done by delegate->addProxy
    delegate->AddProxy(proxy);
}

void ProxyShapeRemovedCallback(MObject& node, void* clientData) {
    // Bunch of conversions + sanity checks...
    auto delegate = static_cast<HdMayaALProxyDelegate*>(clientData);
    if (!TF_VERIFY(
            delegate,
            "ProxyShapeRemovedCallback called with null HdMayaALProxyDelegate "
            "ptr")) {
        return;
    }

    MStatus status;
    MFnDependencyNode mfnNode(node, &status);
    if (!TF_VERIFY(status, "Error getting MFnDependencyNode")) { return; }

    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg(
            "HdMayaALProxyDelegate - called ProxyShapeRemovedCallback "
            "(ProxyShape: %s)\n",
            mfnNode.name().asChar());

    if (!TF_VERIFY(
            mfnNode.typeId() == ProxyShape::kTypeId,
            "ProxyShapeRemovedCallback called on non-%s node",
            ProxyShape::kTypeName.asChar())) {
        return;
    }

    auto* proxy = dynamic_cast<ProxyShape*>(mfnNode.userNode());
    if (!TF_VERIFY(
            proxy, "Error getting ProxyShape* for %s",
            mfnNode.name().asChar())) {
        return;
    }

    // Real work done by delegate->deleteUsdImagingDelegate
    delegate->DeleteUsdImagingDelegate(proxy);
}

bool IsALPluginLoaded() {
    auto nodeClass = MNodeClass(ProxyShape::kTypeId);
    // if not loaded yet, typeName() will be an empty string
    return nodeClass.typeName() == ProxyShape::kTypeName;
}

void PluginCallback(const MStringArray& strs, void* clientData) {
    // Considered having separate plugin loaded/unloaded callbacks, but that
    // would mean checking for the plugin "name", which seems somewhat
    // unreliable - it's just the name of the built library, which seems too
    // easy to alter.
    // Instead, we check if the node is registered.

    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg(
            "HdMayaALProxyDelegate - PluginCallback - %s - %s\n",
            strs.length() > 0 ? strs[0].asChar() : "<none>",
            strs.length() > 1 ? strs[1].asChar() : "<none");

    bool isCurrentlyLoaded = IsALPluginLoaded();
    bool wasLoaded = isALPluginLoaded.exchange(isCurrentlyLoaded);
    if (wasLoaded != isCurrentlyLoaded) {
        if (TfDebug::IsEnabled(HDMAYA_AL_CALLBACKS)) {
            if (isCurrentlyLoaded) {
                TfDebug::Helper().Msg("ALUSDMayaPlugin loaded!\n");
            } else {
                TfDebug::Helper().Msg("ALUSDMayaPlugin unloaded!\n");
            }
        }
        // AL plugin was either loaded or unloaded - either way, need to reset
        // the renderOverride to either add / remove our AL delegate
        HdMayaDelegateRegistry::SignalDelegatesChanged();
    }
}

void SetupPluginCallbacks() {
    MStatus status;

    isALPluginLoaded.store(IsALPluginLoaded());

    // Set up callback to notify of plugin load
    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg("HdMayaALProxyDelegate - creating PluginLoaded callback\n");
    MSceneMessage::addStringArrayCallback(
        MSceneMessage::kAfterPluginLoad, PluginCallback, nullptr, &status);
    TF_VERIFY(status, "Could not set pluginLoaded callback");

    // Set up callback to notify of plugin unload
    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg("HdMayaALProxyDelegate - creating PluginUnloaded callback\n");
    MSceneMessage::addStringArrayCallback(
        MSceneMessage::kAfterPluginUnload, PluginCallback, nullptr, &status);
    TF_VERIFY(status, "Could not set pluginUnloaded callback");
}

#ifdef HD_MAYA_AL_OVERRIDE_PROXY_SELECTION

ProxyShape::FindPickedPrimsRunner oldFindPickedPrimsRunner(nullptr, nullptr);
HdMayaALProxyDelegate* hdStAlProxyDelegate;

/// Alternate picking mechanism for the AL proxy shape, which
/// uses our own renderIndex
bool FindPickedPrimsMtoh(
    ProxyShape& proxy, const MDagPath& proxyDagPath,
    const GfMatrix4d& viewMatrix, const GfMatrix4d& projectionMatrix,
    const GfMatrix4d& worldToLocalSpace, const SdfPathVector& paths,
    const UsdImagingGLRenderParams& params, bool nearestOnly,
    unsigned int pickResolution, ProxyShape::HitBatch& outHit, void* userData) {
    TF_UNUSED(userData);

    TF_DEBUG(HDMAYA_AL_SELECTION).Msg("FindPickedPrimsMtoh\n");

    auto doOldFindPickedPrims = [&]() {
        if (!oldFindPickedPrimsRunner) {
            TF_WARN(
                "called FindPickedPrimsMtoh before oldFindPickedPrimsRunner "
                "set");
            return false;
        }
        return oldFindPickedPrimsRunner(
            proxy, proxyDagPath, viewMatrix, projectionMatrix,
            worldToLocalSpace, paths, params, nearestOnly, pickResolution,
            outHit);
    };

    // Unless the current viewport renderer is an mtoh HdStream one, use
    // the normal AL picking function.
    MStatus status;
    auto view = M3dView::active3dView(&status);
    if (!status) {
        TF_WARN("Error getting active3dView\n");
        return doOldFindPickedPrims();
    }
    auto rendererEnum = view.getRendererName(&status);
    if (!status) {
        TF_WARN("Error calling getRendererName\n");
        return doOldFindPickedPrims();
    }
    if (rendererEnum != M3dView::kViewport2Renderer) {
        return doOldFindPickedPrims();
    }
    MString overrideName = view.renderOverrideName(&status);
    if (!status) {
        TF_WARN("Error calling renderOverrideName\n");
        return doOldFindPickedPrims();
    }
    if (overrideName != "mtohRenderOverride_HdStreamRendererPlugin") {
        return doOldFindPickedPrims();
    }

    // Ok, we have an Mtoh StreamRenderer view - find the HdMayaALProxyDelegate
    // with the HdStream render delegate

    HdMayaALProxyDelegate* alProxyDelegate = hdStAlProxyDelegate;
    if (!TF_VERIFY(alProxyDelegate->IsHdSt())) { doOldFindPickedPrims(); }

    TF_DEBUG(HDMAYA_AL_SELECTION)
        .Msg("FindPickedPrimsMtoh - using mtoh's render index\n");

    auto* proxyData = alProxyDelegate->FindProxyData(&proxy);
    if (!TF_VERIFY(proxyData)) { return false; }
    const SdfPath& proxyDelegateID = proxyData->delegate->GetDelegateID();

    // We found the HdSt proxy delegate, use it's engine / renderIndex to do
    // selection!

    HdRprimCollection intersectCollect;
    TfTokenVector renderTags;
    HdxIntersector::HitVector hdxHits;
    auto& intersectionMode = nearestOnly
                                 ? HdxIntersectionModeTokens->nearestToCamera
                                 : HdxIntersectionModeTokens->unique;

    if (!AL::usdmaya::nodes::Engine::TestIntersectionBatch(
            viewMatrix, projectionMatrix, worldToLocalSpace, paths, params,
            intersectionMode, pickResolution, intersectCollect,
            *alProxyDelegate->GetTaskController(), alProxyDelegate->GetEngine(),
            hdxHits)) {
        return false;
    }

    bool foundHit = false;
    for (const auto& hit : hdxHits) {
        const SdfPath primPath = hit.objectId;

        // TODO: improve handling of multiple AL proxy shapes
        // if we have multiple proxy shapes, we will run a selection
        // query once for each shape, and then we throw any results that
        // aren't in our proxy - clearly, this is wasteful - we should run
        // the selection once, cache it, and use that for all shapes...
        if (!primPath.HasPrefix(proxyDelegateID)) { continue; }

        foundHit = true;

        const SdfPath instancerPath = hit.instancerId;
        const int instanceIndex = hit.instanceIndex;

        // auto instancePath = proxy.engine()->GetPrimPathFromInstanceIndex(
        auto usdPath = proxyData->delegate->GetPathForInstanceIndex(
            primPath, instanceIndex, nullptr);

        if (usdPath.IsEmpty()) {
            usdPath = primPath.StripAllVariantSelections();
        }
        usdPath = proxyData->delegate->GetPathForUsd(usdPath);

        TF_DEBUG(HDMAYA_AL_SELECTION)
            .Msg(
                "FindPickedPrimsMtoh - hit (usdPath): %s\n", usdPath.GetText());

        auto& worldSpaceHitPoint = outHit[usdPath];
        worldSpaceHitPoint = GfVec3d(
            hit.worldSpaceHitPoint[0], hit.worldSpaceHitPoint[1],
            hit.worldSpaceHitPoint[2]);
    }

    return foundHit;
}

#endif // HD_MAYA_AL_OVERRIDE_PROXY_SELECTION

} // namespace

HdMayaALProxyDelegate::HdMayaALProxyDelegate(const InitData& initData)
    : HdMayaDelegate(initData), _renderIndex(initData.renderIndex) {
    TF_DEBUG(HDMAYA_AL_PROXY_DELEGATE)
        .Msg(
            "HdMayaALProxyDelegate - creating with delegateID %s\n",
            GetMayaDelegateID().GetText());

#ifdef HD_MAYA_AL_OVERRIDE_PROXY_SELECTION
    if (IsHdSt()) {
        TF_DEBUG(HDMAYA_AL_SELECTION)
            .Msg(
                "HdMayaALProxyDelegate - installing alt "
                "FindPickedPrimsFunction\n");
        if (TF_VERIFY(!oldFindPickedPrimsRunner)) {
            oldFindPickedPrimsRunner = ProxyShape::getFindPickedPrimsRunner();
            ProxyShape::setFindPickedPrimsFunction(FindPickedPrimsMtoh);
            hdStAlProxyDelegate = this;
        }
    }
#endif // HD_MAYA_AL_OVERRIDE_PROXY_SELECTION

    MStatus status;

    // Add all pre-existing proxy shapes
    MFnDependencyNode fn;
    MItDependencyNodes iter(MFn::kPluginShape);
    for (; !iter.isDone(); iter.next()) {
        MObject mobj = iter.thisNode();
        fn.setObject(mobj);
        if (fn.typeId() != ProxyShape::kTypeId) { continue; }

        auto proxyShape = static_cast<ProxyShape*>(fn.userNode());
        if (!TF_VERIFY(
                proxyShape, "ProxyShape had no mpx data: %s",
                fn.name().asChar())) {
            continue;
        }

        AddProxy(proxyShape);
    }

    // Set up callback to add any new ProxyShapes
    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg(
            "HdMayaALProxyDelegate - creating ProxyShapeAddedCallback callback "
            "for all ProxyShapes\n");
    _nodeAddedCBId = MDGMessage::addNodeAddedCallback(
        ProxyShapeAddedCallback, ProxyShape::kTypeName, this, &status);
    if (!TF_VERIFY(status, "Could not set nodeAdded callback")) {
        _nodeAddedCBId = 0;
    }

    // Set up callback to remove ProxyShapes from the index
    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg(
            "HdMayaALProxyDelegate - creating ProxyShapeRemovedCallback "
            "callback "
            "for all ProxyShapes\n");
    _nodeRemovedCBId = MDGMessage::addNodeRemovedCallback(
        ProxyShapeRemovedCallback, ProxyShape::kTypeName, this, &status);
    if (!TF_VERIFY(status, "Could not set nodeRemoved callback")) {
        _nodeRemovedCBId = 0;
    }

#if HDMAYA_UFE_BUILD
    if (usdUfeRtid == 0) {
        try {
            usdUfeRtid =
                UFE_NS::RunTimeMgr::instance().getId(USD_UFE_RUNTIME_NAME);
        }
        // This shoudl catch ufe's InvalidRunTimeName exception, but they don't
        // expose that!
        catch (...) {
            TF_WARN("USD UFE Runtime plugin not loaded!\n");
        }
    }
#endif // HDMAYA_UFE_BUILD
}

HdMayaALProxyDelegate::~HdMayaALProxyDelegate() {
    TF_DEBUG(HDMAYA_AL_PROXY_DELEGATE)
        .Msg(
            "HdMayaALProxyDelegate - destroying with delegateID %s\n",
            GetMayaDelegateID().GetText());

    TF_DEBUG(HDMAYA_AL_CALLBACKS)
        .Msg("~HdMayaALProxyDelegate - removing all callbacks\n");
    if (_nodeAddedCBId) { MMessage::removeCallback(_nodeAddedCBId); }
    if (_nodeRemovedCBId) { MMessage::removeCallback(_nodeRemovedCBId); }

    // If the delegate is destroyed before the proxy shapes, clean up their
    // callbacks
    for (auto& proxyAndData : _proxiesData) {
        auto& proxy = proxyAndData.first;
        auto& proxyData = proxyAndData.second;
        for (auto& callbackId : proxyData.proxyShapeCallbacks) {
            proxy->scheduler()->unregisterCallback(callbackId);
        }
    }

#ifdef HD_MAYA_AL_OVERRIDE_PROXY_SELECTION
    if (IsHdSt()) {
        TF_DEBUG(HDMAYA_AL_SELECTION)
            .Msg(
                "~HdMayaALProxyDelegate - uninstalling alt "
                "FindPickedPrimsFunction\n");
        TF_VERIFY(oldFindPickedPrimsRunner);
        ProxyShape::setFindPickedPrimsRunner(oldFindPickedPrimsRunner);
        oldFindPickedPrimsRunner.func = nullptr;
        oldFindPickedPrimsRunner.userData = nullptr;
        hdStAlProxyDelegate = nullptr;
    }
#endif // HD_MAYA_AL_OVERRIDE_PROXY_SELECTION
}

HdMayaDelegatePtr HdMayaALProxyDelegate::Creator(const InitData& initData) {
    static std::once_flag setupPluginCallbacksOnce;
    std::call_once(setupPluginCallbacksOnce, SetupPluginCallbacks);

    if (!isALPluginLoaded.load()) { return nullptr; }
    return std::static_pointer_cast<HdMayaDelegate>(
        std::make_shared<HdMayaALProxyDelegate>(initData));
}

void HdMayaALProxyDelegate::Populate() {
    TF_DEBUG(HDMAYA_AL_POPULATE).Msg("HdMayaALProxyDelegate::Populate\n");
    for (auto& proxyAndData : _proxiesData) {
        PopulateSingleProxy(proxyAndData.first, proxyAndData.second);
    }
}

bool HdMayaALProxyDelegate::PopulateSingleProxy(
    ProxyShape* proxy, HdMayaALProxyData& proxyData) {
    if (!proxyData.delegate) { return false; }

    proxyData.delegate->SetRootTransform(
        GfMatrix4d(proxy->parentTransform().inclusiveMatrix().matrix));

    if (!proxyData.populated) {
        TF_DEBUG(HDMAYA_AL_POPULATE)
            .Msg(
                "HdMayaALProxyDelegate::Populating %s\n",
                proxy->name().asChar());

        auto stage = proxy->getUsdStage();
        if (!stage) {
            MGlobal::displayError(
                MString("Could not get stage for proxyShape: ") +
                proxy->name());
            return false;
        }
        proxyData.delegate->Populate(stage->GetPseudoRoot());
        proxyData.populated = true;
    }
    return true;
}

void HdMayaALProxyDelegate::PreFrame(const MHWRender::MDrawContext& context) {
    for (auto& proxyAndData : _proxiesData) {
        auto& proxy = proxyAndData.first;
        auto& proxyData = proxyAndData.second;
        if (PopulateSingleProxy(proxy, proxyData)) {
            proxyData.delegate->ApplyPendingUpdates();
            proxyData.delegate->SetTime(UsdTimeCode(
                proxy->outTimePlug().asMTime().as(MTime::uiUnit())));
            proxyData.delegate->PostSyncCleanup();
        }
    }
}

void HdMayaALProxyDelegate::PopulateSelectedPaths(
    const MSelectionList& mayaSelection, SdfPathVector& selectedSdfPaths,
    const HdSelectionSharedPtr& selection) {
    MStatus status;
    MObject proxyMObj;
    MFnDagNode proxyMFnDag;
    MDagPathArray proxyDagPaths;

    for (auto& proxyAndData : _proxiesData) {
        auto& proxy = proxyAndData.first;
        auto& proxyData = proxyAndData.second;

        // First, we check to see if the entire proxy shape is selected
        proxyDagPaths.clear();
        proxyMObj = proxy->thisMObject();
        if (!TF_VERIFY(!proxyMObj.isNull())) { continue; }
        if (!TF_VERIFY(proxyMFnDag.setObject(proxyMObj))) { continue; }
        if (!TF_VERIFY(proxyMFnDag.getAllPaths(proxyDagPaths))) { continue; }
        if (!TF_VERIFY(proxyDagPaths.length())) { continue; }

        bool wholeProxySelected = false;
        // Loop over all instances
        for (unsigned int i = 0, n = proxyDagPaths.length(); i < n; ++i) {
            MDagPath& dagPath = proxyDagPaths[i];
            // Loop over all parents
            while (dagPath.length()) {
                if (mayaSelection.hasItem(dagPath)) {
                    TF_DEBUG(HDMAYA_AL_SELECTION)
                        .Msg(
                            "proxy node %s was selected\n",
                            dagPath.fullPathName().asChar());
                    wholeProxySelected = true;
                    selectedSdfPaths.push_back(
                        proxyData.delegate->GetDelegateID());
                    selection->AddRprim(
                        HdSelection::HighlightModeSelect,
                        selectedSdfPaths.back());
                    break;
                }
                dagPath.pop();
            }
            if (wholeProxySelected) { break; }
        }
        if (wholeProxySelected) { continue; }

        // Ok, we didn't have the entire proxy selected - instead, add in
        // any "subpaths" of the proxy which may be selected

        // Not sure why we need both paths1 and paths2, or what the difference
        // is... but AL's own selection drawing code (in ProxyDrawOverride)
        // makes a combined list from both of these, so we do the same
        const auto& paths1 = proxy->selectedPaths();
        const auto& paths2 = proxy->selectionList().paths();
        selectedSdfPaths.reserve(
            selectedSdfPaths.size() + paths1.size() + paths2.size());

        if (TfDebug::IsEnabled(HDMAYA_AL_SELECTION)) {
            TfDebug::Helper().Msg(
                "proxy %s has %lu selectedPaths",
                proxyDagPaths[0].fullPathName().asChar(), paths1.size());
            if (!paths1.empty()) {
                TfDebug::Helper().Msg(" (1st: %s)", paths1.begin()->GetText());
            }
            TfDebug::Helper().Msg(
                ", and %lu selectionList paths", paths2.size());
            if (!paths2.empty()) {
                TfDebug::Helper().Msg(" (1st: %s)", paths2.begin()->GetText());
            }
            TfDebug::Helper().Msg("\n");
        }

        for (auto& usdPath : paths1) {
            selectedSdfPaths.push_back(
                proxyData.delegate->GetPathForIndex(usdPath));
            selection->AddRprim(
                HdSelection::HighlightModeSelect, selectedSdfPaths.back());
        }

        for (auto& usdPath : paths2) {
            selectedSdfPaths.push_back(
                proxyData.delegate->GetPathForIndex(usdPath));
            selection->AddRprim(
                HdSelection::HighlightModeSelect, selectedSdfPaths.back());
        }
    }
}

#if HDMAYA_UFE_BUILD

void HdMayaALProxyDelegate::PopulateSelectedPaths(
    const UFE_NS::Selection& ufeSelection, SdfPathVector& selectedSdfPaths,
    const HdSelectionSharedPtr& selection) {
    MStatus status;
    MObject proxyMObj;
    MFnDagNode proxyMFnDag;
    MDagPathArray proxyDagPaths;

    TF_DEBUG(HDMAYA_AL_SELECTION)
        .Msg(
            "HdMayaALProxyDelegate::PopulateSelectedPaths (ufe version) - ufe "
            "sel size: %lu\n",
            ufeSelection.size());

    // We get the maya selection for the whole-proxy-selected check, since
    // it is a subset of the ufe selection
    MSelectionList mayaSel;
    MGlobal::getActiveSelectionList(mayaSel);

    std::unordered_map<std::string, HdMayaALProxyData*> proxyPathToData;

    for (auto& proxyAndData : _proxiesData) {
        auto& proxy = proxyAndData.first;
        auto& proxyData = proxyAndData.second;

        // First, we check to see if the entire proxy shape is selected
        proxyDagPaths.clear();
        proxyMObj = proxy->thisMObject();
        if (!TF_VERIFY(!proxyMObj.isNull())) { continue; }
        if (!TF_VERIFY(proxyMFnDag.setObject(proxyMObj))) { continue; }
        if (!TF_VERIFY(proxyMFnDag.getAllPaths(proxyDagPaths))) { continue; }
        if (!TF_VERIFY(proxyDagPaths.length())) { continue; }

        bool wholeProxySelected = false;
        // Loop over all instances
        for (unsigned int i = 0, n = proxyDagPaths.length(); i < n; ++i) {
            // Make a copy because we're modifying it
            MDagPath dagPath = proxyDagPaths[i];
            // Loop over all parents
            while (dagPath.length()) {
                if (mayaSel.hasItem(dagPath)) {
                    TF_DEBUG(HDMAYA_AL_SELECTION)
                        .Msg(
                            "proxy node %s was selected\n",
                            dagPath.fullPathName().asChar());
                    wholeProxySelected = true;
                    selectedSdfPaths.push_back(
                        proxyData.delegate->GetDelegateID());
                    selection->AddRprim(
                        HdSelection::HighlightModeSelect,
                        selectedSdfPaths.back());
                    break;
                }
                dagPath.pop();
            }
            if (wholeProxySelected) { break; }
        }
        if (!wholeProxySelected) {
            for (unsigned int i = 0, n = proxyDagPaths.length(); i < n; ++i) {
                MDagPath& dagPath = proxyDagPaths[i];

                TF_DEBUG(HDMAYA_AL_SELECTION)
                    .Msg(
                        "HdMayaALProxyDelegate::PopulateSelectedPaths - adding "
                        "proxy to lookup: %s\n",
                        dagPath.fullPathName().asChar());
                proxyPathToData.emplace(
                    dagPath.fullPathName().asChar(), &proxyData);
            }
        }
    }

    for (auto item : ufeSelection) {
        if (item->runTimeId() != usdUfeRtid) { continue; }
        auto& pathSegments = item->path().getSegments();
        if (pathSegments.size() != 2) {
            TF_WARN(
                "Found invalid usd-ufe path (had %lu segments - should have "
                "2): %s\n",
                item->path().size(), item->path().string().c_str());
            continue;
        }
        // We popHead for maya pathSegment because it always starts with
        // "|world", which makes it non-standard...
        auto mayaPathSegment = pathSegments[0].popHead();
        auto& usdPathSegment = pathSegments[1];

        auto findResult = proxyPathToData.find(mayaPathSegment.string());

        TF_DEBUG(HDMAYA_AL_SELECTION)
            .Msg(
                "HdMayaALProxyDelegate::PopulateSelectedPaths - looking up "
                "proxy: %s\n",
                mayaPathSegment.string().c_str());

        if (findResult == proxyPathToData.cend()) { continue; }

        auto& proxyData = *(findResult->second);

        selectedSdfPaths.push_back(proxyData.delegate->GetPathForIndex(
            SdfPath(usdPathSegment.string())));
        selection->AddRprim(
            HdSelection::HighlightModeSelect, selectedSdfPaths.back());
        TF_DEBUG(HDMAYA_AL_SELECTION)
            .Msg(
                "HdMayaALProxyDelegate::PopulateSelectedPaths - selecting %s\n",
                selectedSdfPaths.back().GetText());
    }
}

bool HdMayaALProxyDelegate::SupportsUfeSelection() { return usdUfeRtid != 0; }

#endif // HDMAYA_UFE_BUILD

HdMayaALProxyData& HdMayaALProxyDelegate::AddProxy(ProxyShape* proxy) {
    // Our ProxyShapeAdded callback is triggered every time the node is added
    // to the DG, NOT when the C++ ProxyShape object is created; due to the
    // undo queue, it's possible for the same ProxyShape to be added (and
    // removed) from the DG several times throughout it's lifetime. However,
    // we only call removeProxy() when the C++ ProxyShape object is actually
    // destroyed - so it's possible that the given proxy has already been
    // added to this delegate!
    auto emplaceResult = _proxiesData.emplace(
        std::piecewise_construct, std::forward_as_tuple(proxy),
        std::forward_as_tuple());
    auto& proxyData = emplaceResult.first->second;
    if (emplaceResult.second) {
        // Okay, we have an actual insertion! Set up callbacks...
        auto* scheduler = proxy->scheduler();
        if (!TF_VERIFY(
                scheduler, "Error getting scheduler for %s",
                proxy->name().asChar())) {
            return proxyData;
        }

        TF_DEBUG(HDMAYA_AL_CALLBACKS)
            .Msg(
                "HdMayaALProxyDelegate - creating PreStageLoaded callback "
                "for %s\n",
                proxy->name().asChar());
        proxyData.proxyShapeCallbacks.push_back(scheduler->registerCallback(
            proxy->getId("PreStageLoaded"),      // eventId
            "HdMayaALProxyDelegate_onStageLoad", // tag
            StageLoadedCallback,                 // functionPointer
            10000,                               // weight
            this                                 // userData
            ));

        TF_DEBUG(HDMAYA_AL_CALLBACKS)
            .Msg(
                "HdMayaALProxyDelegate - creating PreDestroyProxyShape "
                "callback for %s\n",
                proxy->name().asChar());
        proxyData.proxyShapeCallbacks.push_back(scheduler->registerCallback(
            proxy->getId("PreDestroyProxyShape"),   // eventId
            "HdMayaALProxyDelegate_onProxyDestroy", // tag
            ProxyShapeDestroyedCallback,            // functionPointer
            10000,                                  // weight
            this                                    // userData
            ));
    }
    CreateUsdImagingDelegate(proxy, proxyData);
    return proxyData;
}

void HdMayaALProxyDelegate::RemoveProxy(ProxyShape* proxy) {
    // Note that we don't bother unregistering the proxyShapeCallbacks,
    // as this is called when the ProxyShape is about to be destroyed anyway
    _proxiesData.erase(proxy);
}

HdMayaALProxyData* HdMayaALProxyDelegate::FindProxyData(ProxyShape* proxy) {
    auto findResult = _proxiesData.find(proxy);
    if (findResult == _proxiesData.end()) { return nullptr; }
    return &(findResult->second);
}

void HdMayaALProxyDelegate::CreateUsdImagingDelegate(ProxyShape* proxy) {
    auto proxyDataIter = _proxiesData.find(proxy);
    if (!TF_VERIFY(
            proxyDataIter != _proxiesData.end(),
            "Proxy not found in delegate: %s", proxy->name().asChar())) {
        return;
    }
    CreateUsdImagingDelegate(proxy, proxyDataIter->second);
}

void HdMayaALProxyDelegate::CreateUsdImagingDelegate(
    ProxyShape* proxy, HdMayaALProxyData& proxyData) {
    // Why do this release when we do a reset right below? Because we want
    // to make sure we delete the old delegate before creating a new one
    // (the reset statement below will first create a new one, THEN delete
    // the old one). Why do we care? In case they have the same _renderIndex
    // - if so, the delete may clear out items from the renderIndex that the
    // constructor potentially adds
    proxyData.delegate.release();
    proxyData.delegate.reset(new UsdImagingDelegate(
        _renderIndex,
        GetMayaDelegateID().AppendChild(TfToken(TfStringPrintf(
            "ALProxyDelegate_%s_%p", proxy->name().asChar(), proxy)))));
    proxyData.populated = false;
}

void HdMayaALProxyDelegate::DeleteUsdImagingDelegate(ProxyShape* proxy) {
    auto proxyDataIter = _proxiesData.find(proxy);
    if (!TF_VERIFY(
            proxyDataIter != _proxiesData.end(),
            "Proxy not found in delegate: %s", proxy->name().asChar())) {
        return;
    }
    auto& proxyData = proxyDataIter->second;
    proxyData.delegate.reset(nullptr);
    proxyData.populated = false;
}

PXR_NAMESPACE_CLOSE_SCOPE
