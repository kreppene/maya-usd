#include "viewportRenderer.h"

#include <maya/MRenderingInfo.h>
#include <pxr/base/gf/matrix4d.h>

#include <pxr/imaging/hdx/rendererPlugin.h>
#include <pxr/imaging/hdx/rendererPluginRegistry.h>
#include <pxr/imaging/hdx/tokens.h>

#include <memory>
#include <mutex>
#include <exception>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
    std::unique_ptr<HdMayaViewportRenderer> _viewportRenderer = nullptr;
    std::mutex _viewportRendererLock;

    TfToken _getDefaultRenderer() {
        const auto l = HdMayaViewportRenderer::getRendererPlugins();
        if (l.empty()) { return {}; }
        return l[0];
    }

    GfMatrix4d _getGfMatrixFromMaya(const MMatrix& mayaMat) {
        GfMatrix4d mat;
        memcpy(mat.GetArray(), mayaMat[0], sizeof(double) * 16);
        return mat;
    }
}

HdMayaViewportRenderer::HdMayaViewportRenderer() :
    MViewportRenderer("HdMayaViewportRenderer"),
    selectionTracker(new HdxSelectionTracker) {
    fUIName.set("Hydra Viewport Renderer");
    fRenderingOverride = MViewportRenderer::kOverrideAllDrawing;

    delegateID = SdfPath("/HdMayaViewportRenderer").AppendChild(TfToken(TfStringPrintf("_HdMaya_%p", this)));
    rendererName = _getDefaultRenderer();
    // This is a critical error, so we don't allow the construction
    // of the viewport renderer plugin if there is no renderer plugin
    // present.
    if (rendererName.IsEmpty()) {
        throw std::runtime_error("No default renderer is present!");
    }
}

HdMayaViewportRenderer::~HdMayaViewportRenderer() {

}

HdMayaViewportRenderer* HdMayaViewportRenderer::getInstance() {
    std::lock_guard<std::mutex> lg(_viewportRendererLock);
    if (_viewportRenderer == nullptr) {
        try {
            auto* vp = new HdMayaViewportRenderer();
            _viewportRenderer.reset(vp);
        } catch (std::runtime_error& e) {
            _viewportRenderer = nullptr;
            return nullptr;
        }
    }
    return _viewportRenderer.get();
}

void HdMayaViewportRenderer::cleanup() {
    _viewportRenderer = nullptr;
}

MStatus HdMayaViewportRenderer::initialize() {
    initializedViewport = true;
    GlfGlewInit();
    initHydraResources();
    return MStatus::kSuccess;
}

MStatus HdMayaViewportRenderer::uninitialize() {
    clearHydraResources();
    return MStatus::kSuccess;
}

void HdMayaViewportRenderer::initHydraResources() {
    rendererPlugin = HdxRendererPluginRegistry::GetInstance().GetRendererPlugin(rendererName);
    auto* renderDelegate = rendererPlugin->CreateRenderDelegate();
    renderIndex = HdRenderIndex::New(renderDelegate);
    delegate = new HdMayaDelegate(renderIndex, delegateID);
    taskController = new HdxTaskController(
        renderIndex,
        delegateID.AppendChild(TfToken(
            TfStringPrintf("_UsdImaging_%s_%p", TfMakeValidIdentifier(rendererName.GetText()).c_str(), this))));

    // delegate->SetRootVisibility(true);
    // delegate->SetRootTransform(GfMatrix4d(1.0));
}

void HdMayaViewportRenderer::clearHydraResources() {
    if (delegate != nullptr) {
        delete delegate;
        delegate = nullptr;
    }

    if (taskController != nullptr) {
        delete taskController;
        taskController = nullptr;
    }

    HdRenderDelegate* renderDelegate = nullptr;
    if (renderIndex != nullptr) {
        renderDelegate = renderIndex->GetRenderDelegate();
        delete renderIndex;
        renderIndex = nullptr;
    }

    if (rendererPlugin != nullptr) {
        if (renderDelegate != nullptr) {
            rendererPlugin->DeleteRenderDelegate(renderDelegate);
        }
        HdxRendererPluginRegistry::GetInstance().ReleasePlugin(rendererPlugin);
        rendererPlugin = nullptr;
    }
}

TfTokenVector HdMayaViewportRenderer::getRendererPlugins() {
    HfPluginDescVector pluginDescs;
    HdxRendererPluginRegistry::GetInstance().GetPluginDescs(&pluginDescs);

    TfTokenVector ret; ret.reserve(pluginDescs.size());
    for (const auto& desc: pluginDescs) {
        ret.emplace_back(desc.id);
    }
    return ret;
}

std::string HdMayaViewportRenderer::getRendererPluginDisplayName(const TfToken& id) {
    HfPluginDesc pluginDesc;
    if (!TF_VERIFY(HdxRendererPluginRegistry::GetInstance().GetPluginDesc(id, &pluginDesc))) {
        return {};
    }

    return pluginDesc.displayName;
}

void HdMayaViewportRenderer::changeRendererPlugin(const TfToken& id) {
    auto* instance = getInstance();
    if (instance->rendererName == id) { return; }
    const auto renderers = getRendererPlugins();
    if (std::find(renderers.begin(), renderers.end(), id) == renderers.end()) { return; }
    instance->rendererName = id;
    if (instance->initializedViewport) {
        instance->clearHydraResources();
        instance->initHydraResources();
    }
}

MStatus HdMayaViewportRenderer::render(const MRenderingInfo& renderInfo) {
    if (renderInfo.renderingAPI() != MViewportRenderer::kOpenGL) {
        return MS::kFailure;
    }

    float clearColor[4] = { 0.0f };
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    renderInfo.renderTarget().makeTargetCurrent();

    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const auto originX = renderInfo.originX();
    const auto originY = renderInfo.originY();
    const auto width = renderInfo.width();
    const auto height = renderInfo.height();

    GfVec4d viewport(originX, originY, width, height);
    taskController->SetCameraMatrices(
        _getGfMatrixFromMaya(renderInfo.viewMatrix()),
        _getGfMatrixFromMaya(renderInfo.projectionMatrix()));
    taskController->SetCameraViewport(viewport);
    taskController->SetEnableSelection(false);
    VtValue selectionTrackerValue(selectionTracker);
    engine.SetTaskContextData(HdxTokens->selectionState, selectionTrackerValue);

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    glEnable(GL_LIGHTING);

    engine.Execute(*renderIndex, taskController->GetTasks(HdxTaskSetTokens->colorRender));

    glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glPopAttrib(); // GL_ENABLE_BIT | GL_CURRENT_BIT

    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    return MStatus::kSuccess;
}

bool HdMayaViewportRenderer::nativelySupports(MViewportRenderer::RenderingAPI api, float /*version*/) {
    return MViewportRenderer::kOpenGL == api;
}

bool HdMayaViewportRenderer::override(MViewportRenderer::RenderingOverride override) {
    return fRenderingOverride == override;
}
