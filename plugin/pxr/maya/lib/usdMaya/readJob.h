//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#ifndef PXRUSDMAYA_READ_JOB_H
#define PXRUSDMAYA_READ_JOB_H

/// \file usdMaya/readJob.h

#include "usdMaya/jobArgs.h"
#include "usdMaya/primReaderContext.h"
#include "usdMaya/primReaderRegistry.h"

#include "pxr/pxr.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"

#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>

#include <map>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE



class UsdMaya_ReadJob
{
public:
    UsdMaya_ReadJob(
            const std::string& iFileName,
            const std::string& iPrimPath,
            const std::map<std::string, std::string>& iVariants,
            const UsdMayaJobImportArgs & iArgs);

    ~UsdMaya_ReadJob();

    /// Reads the USD stage specified by the job file name and prim path.
    /// Newly-created DAG paths are inserted into the vector \p addedDagPaths.
    bool Read(std::vector<MDagPath>* addedDagPaths);

    /// Redoes a previous Read() operation after Undo() has been called.
    /// If Undo() hasn't been called, does nothing.
    bool Redo();

    /// Undoes a previous Read() operation, removing all added nodes.
    bool Undo();

    // Getters/Setters
    void SetMayaRootDagPath(const MDagPath &mayaRootDagPath);

private:
    // Types
    using _PrimReaderMap =
        std::unordered_map<SdfPath, UsdMayaPrimReaderSharedPtr, SdfPath::Hash>;

    // XXX: Activating the 'Expanded' representation of a USD reference
    // assembly node is very much like performing a regular UsdMaya_ReadJob but with
    // a few key differences (e.g. creating proxy shapes at collapse points).
    // These private helper methods cover the functionality of a regular
    // usdImport, and an 'Expanded' representation-style import, respectively.
    // It would be great if we could combine these into a single traversal at
    // some point.
    bool _DoImport(UsdPrimRange& range, const UsdPrim& usdRootPrim,
        const UsdStageRefPtr& stage);
    bool _DoImportWithProxies(UsdPrimRange& range);
    void _DoImportPrimIt(
        UsdPrimRange::iterator& primIt, const UsdPrim& usdRootPrim,
        UsdMayaPrimReaderContext& readCtx, _PrimReaderMap& primReaders);

    // These are helper methods for the proxy import method.
    bool _ProcessProxyPrims(
            const std::vector<UsdPrim>& proxyPrims,
            const UsdPrim& pxrGeomRoot,
            const std::vector<std::string>& collapsePointPathStrings);
    bool _ProcessSubAssemblyPrims(const std::vector<UsdPrim>& subAssemblyPrims);
    bool _ProcessCameraPrims(const std::vector<UsdPrim>& cameraPrims);

    // Data
    UsdMayaJobImportArgs mArgs;
    std::string mFileName;
    std::string mPrimPath;
    std::map<std::string,std::string> mVariants;
    MDagModifier mDagModifierUndo;
    bool mDagModifierSeeded;
    UsdMayaPrimReaderContext::ObjectRegistry mNewNodeRegistry;
    MDagPath mMayaRootDagPath;
};


PXR_NAMESPACE_CLOSE_SCOPE


#endif
