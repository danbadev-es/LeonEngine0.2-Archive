#pragma once

#include <string>
#include <vector>

namespace Leon {

    /**
     * The engine's primitive meshes (cube, sphere, cylinder, plane) are authored as FBX next to the
     * .lmesh the runtime loads. This imports the ones whose FBX is newer than their .lmesh, or all of
     * them, and writes the .lmesh files. A development step: the runtime only loads.
     */
    struct FEnginePrimitiveImport {
        struct FResult {
            /** .lmesh files written. */
            std::vector<std::string> Imported;
            /** Primitives whose .lmesh was already newer than its FBX. */
            std::vector<std::string> UpToDate;
            /** One line per primitive that could not be imported. */
            std::vector<std::string> Errors;
        };

        static FResult Run(bool bInForce = false);
    };

} // namespace Leon
